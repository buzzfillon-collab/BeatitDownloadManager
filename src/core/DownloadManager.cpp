#include "DownloadManager.h"
#include "HttpDownloader.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QCryptographicHash>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {
    database_.open();

    for (const auto &stored : database_.loadHistory()) {
        bool ok = false;
        const int n = stored.id.startsWith(QStringLiteral("download-"))
            ? stored.id.mid(QStringLiteral("download-").size()).toInt(&ok) : 0;
        if (ok) nextId_ = qMax(nextId_, n + 1);
    }

    for (const auto &stored : database_.loadActive()) {
        auto d = stored;
        if (d.status == QStringLiteral("Downloading") || d.status == QStringLiteral("Starting"))
            d.status = QStringLiteral("Queued");
        queued_.insert(d.id, d);
    }

    QTimer::singleShot(0, this, [this] {
        for (const auto &d : database_.loadHistory()) {
            if (d.type == QStringLiteral("http"))
                emit taskRestored(d.id, d.source, d.filename, d.status, d.downloadedBytes, d.totalBytes, d.updatedAt);
        }
        startNextQueued();
    });
}

DownloadManager::~DownloadManager() {
    const auto ids = active_.keys();
    for (const QString &id : ids) cancel(id);
    const auto tasks = active_;
    for (auto it = tasks.cbegin(); it != tasks.cend(); ++it)
        if (it->thread) it->thread->wait(5000);
}

void DownloadManager::persist(const QString &id, const QString &status, qint64 downloaded,
                              qint64 total, qint64 speed, const QString &error) {
    auto it = queued_.find(id);
    if (it == queued_.end()) return;
    it->status = status;
    if (downloaded >= 0) it->downloadedBytes = downloaded;
    if (total >= 0) it->totalBytes = total;
    it->speed = speed;
    it->error = error;
    database_.save(it.value());
}

void DownloadManager::setMaxActive(int count) {
    maxActive_ = qMax(1, count);
    updateActiveBandwidthLimits();
    startNextQueued();
}

void DownloadManager::setHttpConnections(int count) {
    httpConnections_ = qBound(1, count, 8);
}

void DownloadManager::setBandwidthLimit(qint64 bytesPerSecond) {
    bandwidthLimit_ = qMax<qint64>(0, bytesPerSecond);
    updateActiveBandwidthLimits();
}

void DownloadManager::updateActiveBandwidthLimits() {
    const qint64 perDownload = bandwidthLimit_ > 0
        ? qMax<qint64>(1, bandwidthLimit_ / qMax(1, active_.size())) : 0;
    for (auto it = active_.begin(); it != active_.end(); ++it)
        it->downloader->setBandwidthLimit(perDownload);
}

void DownloadManager::setSchedulerAllowed(bool allowed) {
    if (schedulerAllowed_ == allowed) return;
    schedulerAllowed_ = allowed;
    if (!allowed) {
        const auto ids = active_.keys();
        for (const QString &id : ids) {
            schedulerPaused_[id] = true;
            pause(id);
        }
    } else {
        const auto ids = schedulerPaused_.keys();
        schedulerPaused_.clear();
        for (const QString &id : ids) {
            if (queued_.contains(id) && queued_[id].status == QStringLiteral("Paused")) {
                queued_[id].status = QStringLiteral("Queued");
                database_.save(queued_[id]);
            }
        }
        startNextQueued();
    }
}

void DownloadManager::setExpectedSha256(const QString &id, const QString &sha256) {
    const QString normalized = sha256.trimmed().toLower();
    if (normalized.size() != 64 || normalized.contains(QRegularExpression(QStringLiteral("[^0-9a-f]"))))
        return;
    auto it = queued_.find(id);
    if (it == queued_.end()) return;
    it->sha256 = normalized;
    it->verification.clear();
    database_.save(it.value());
    if (auto active = active_.find(id); active != active_.end())
        active->downloader->setExpectedSha256(normalized);
}

bool DownloadManager::verifyChecksum(const QString &id, QString *message) {
    auto it = queued_.find(id);
    if (it == queued_.end() || it->sha256.isEmpty()) {
        if (message) *message = QStringLiteral("No SHA-256 checksum is configured.");
        return false;
    }
    const QString path = QDir(it->destination).filePath(it->filename);
    QString actual;
    const bool ok = HttpDownloader::verifySha256(path, it->sha256, &actual);
    it->verification = ok ? QStringLiteral("Verified") : QStringLiteral("Checksum mismatch");
    database_.save(it.value());
    if (message) *message = ok
        ? QStringLiteral("SHA-256 verified: %1").arg(actual)
        : QStringLiteral("SHA-256 mismatch. Expected %1, got %2.").arg(it->sha256, actual);
    return ok;
}

QString DownloadManager::addUrl(const QString &url, const QString &destination) {
    const QString id = QStringLiteral("download-%1").arg(nextId_++);
    PersistedDownload d;
    d.id = id;
    d.type = QStringLiteral("http");
    d.source = url;
    d.destination = destination;
    d.filename = QUrl(url).fileName();
    if (d.filename.isEmpty()) d.filename = QStringLiteral("download");
    d.status = QStringLiteral("Queued");
    queued_.insert(id, d);
    database_.save(d);
    emit taskAdded(id, url);
    startNextQueued();
    return id;
}

void DownloadManager::startNextQueued() {
    if (!schedulerAllowed_) return;
    while (active_.size() < maxActive_) {
        QString id;
        for (auto it = queued_.cbegin(); it != queued_.cend(); ++it) {
            if (it->status == QStringLiteral("Queued")) {
                id = it.key();
                break;
            }
        }
        if (id.isEmpty()) return;

        const auto d = queued_.value(id);
        queued_[id].status = QStringLiteral("Starting");
        database_.save(queued_[id]);

        auto *thread = new QThread(this);
        auto *downloader = new HttpDownloader();
        downloader->moveToThread(thread);
        active_.insert(id, ActiveTask{d.source, d.destination, downloader, thread});
        downloader->setExpectedSha256(d.sha256);
        updateActiveBandwidthLimits();

        connect(thread, &QThread::started, downloader, [downloader, d, this] {
            downloader->setSegments(httpConnections_);
            downloader->setExpectedSha256(d.sha256);
            downloader->start(d.source, d.destination);
        });

        connect(downloader, &HttpDownloader::started, this,
            [this, id](const QString &filename, qint64 total, bool) {
                queued_[id].filename = filename;
                queued_[id].status = QStringLiteral("Downloading");
                queued_[id].totalBytes = total;
                database_.save(queued_[id]);
                emit taskStarted(id, filename, total);
            });

        connect(downloader, &HttpDownloader::progress, this,
            [this, id](qint64 done, qint64 total, qint64 speed) {
                persist(id, QStringLiteral("Downloading"), done, total, speed);
                emit taskProgress(id, done, total, speed);
            });

        auto stopThread = [this, id] {
            if (auto it = active_.find(id); it != active_.end())
                it->thread->quit();
        };

        connect(downloader, &HttpDownloader::paused, this,
            [this, id, stopThread](qint64 done) {
                persist(id, QStringLiteral("Paused"), done, queued_.value(id).totalBytes);
                emit taskPaused(id, done);
                stopThread();
            });

        connect(downloader, &HttpDownloader::completed, this,
            [this, id, stopThread](const QString &path) {
                const qint64 size = QFileInfo(path).size();
                auto &entry = queued_[id];
                persist(id, entry.sha256.isEmpty() ? QStringLiteral("Completed") : QStringLiteral("Completed (verified)"), size, size);
                entry.verification = entry.sha256.isEmpty() ? QString() : QStringLiteral("Verified");
                database_.save(entry);
                emit taskCompleted(id, path);
                stopThread();
            });

        connect(downloader, &HttpDownloader::failed, this,
            [this, id, stopThread](const QString &error) {
                auto &entry = queued_[id];
                if (error.startsWith(QStringLiteral("SHA-256 checksum mismatch"))) entry.verification = QStringLiteral("Checksum mismatch");
                database_.save(entry);
                persist(id, QStringLiteral("Failed"), queued_.value(id).downloadedBytes,
                        queued_.value(id).totalBytes, 0, error);
                emit taskFailed(id, error);
                stopThread();
            });

        connect(downloader, &HttpDownloader::cancelled, this,
            [this, id, stopThread] {
                if (pendingRemoval_.contains(id)) {
                    const bool deleteFile = pendingDeleteFile_.value(id);
                    const auto d = queued_.value(id);
                    const QString path = QDir(d.destination).filePath(d.filename);
                    if (deleteFile) {
                        QFile::remove(path);
                        QFile::remove(path + QStringLiteral(".part"));
                        for (int i = 0; i < 32; ++i)
                            QFile::remove(path + QStringLiteral(".part.%1").arg(i));
                    }
                    database_.remove(id);
                    queued_.remove(id);
                    pendingRemoval_.remove(id);
                    pendingDeleteFile_.remove(id);
                    emit taskRemoved(id);
                } else {
                    persist(id, QStringLiteral("Cancelled"), queued_.value(id).downloadedBytes,
                            queued_.value(id).totalBytes);
                    emit taskCancelled(id);
                }
                stopThread();
            });

        connect(thread, &QThread::finished, downloader, &QObject::deleteLater);
        connect(thread, &QThread::finished, this, [this, id] {
            if (auto it = active_.find(id); it != active_.end()) {
                it->thread->deleteLater();
                active_.erase(it);
            }
            updateActiveBandwidthLimits();
            startNextQueued();
        });

        thread->start();
    }
}

void DownloadManager::pause(const QString &id) {
    if (auto it = active_.find(id); it != active_.end())
        QMetaObject::invokeMethod(it->downloader, "pause", Qt::DirectConnection);
}

void DownloadManager::resume(const QString &id) {
    if (auto it = queued_.find(id); it != queued_.end()) {
        schedulerPaused_.remove(id);
        it->status = QStringLiteral("Queued");
        database_.save(it.value());
        startNextQueued();
    }
}

void DownloadManager::cancel(const QString &id) {
    if (auto it = active_.find(id); it != active_.end()) {
        QMetaObject::invokeMethod(it->downloader, "cancel", Qt::DirectConnection);
        return;
    }
    if (queued_.contains(id)) {
        queued_[id].status = QStringLiteral("Cancelled");
        database_.save(queued_[id]);
        emit taskCancelled(id);
    }
}

void DownloadManager::remove(const QString &id, bool deleteFile) {
    if (active_.contains(id)) {
        pendingRemoval_[id] = true;
        pendingDeleteFile_[id] = deleteFile;
        QMetaObject::invokeMethod(active_.value(id).downloader, "cancel", Qt::DirectConnection);
        return;
    }

    auto it = queued_.find(id);
    if (it == queued_.end()) return;

    const auto d = it.value();
    if (deleteFile) {
        const QString path = QDir(d.destination).filePath(d.filename);
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".part"));
        for (int i = 0; i < 16; ++i)
            QFile::remove(path + QStringLiteral(".part.%1").arg(i));
    }
    database_.remove(id);
    queued_.erase(it);
    emit taskRemoved(id);
}
