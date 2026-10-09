#include "DownloadManager.h"
#include "HttpDownloader.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <climits>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {
    database_.open();
    for (const auto &queue : database_.loadQueues()) queues_.insert(queue.id, queue);
    if (!queues_.contains(QStringLiteral("main"))) { DownloadQueue q; q.id="main"; q.name="Main"; q.maxActive=maxActive_; database_.saveQueue(q); queues_.insert(q.id,q); }

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
    if (queues_.contains(QStringLiteral("main"))) { queues_["main"].maxActive=maxActive_; database_.saveQueue(queues_.value("main")); }
    updateActiveBandwidthLimits();
    startNextQueued();
}


QVector<DownloadQueue> DownloadManager::queues() const {
    QVector<DownloadQueue> v; for (const auto &q : queues_) v.push_back(q);
    std::sort(v.begin(),v.end(),[](const DownloadQueue&a,const DownloadQueue&b){return a.sortOrder==b.sortOrder?a.id<b.id:a.sortOrder<b.sortOrder;}); return v;
}
bool DownloadManager::createQueue(const QString &name) {
    const QString n=name.trimmed(); if(n.isEmpty())return false;
    for(const auto&q:queues_)if(q.name.compare(n,Qt::CaseInsensitive)==0)return false;
    DownloadQueue q; q.id="queue-"+QUuid::createUuid().toString(QUuid::WithoutBraces); q.name=n; q.maxActive=2; q.sortOrder=queues_.size();
    if(!database_.saveQueue(q))return false;queues_.insert(q.id,q);startNextQueued();return true;
}
bool DownloadManager::renameQueue(const QString&id,const QString&name) {
    const QString n=name.trimmed();if(!queues_.contains(id)||n.isEmpty())return false;
    for(const auto&q:queues_)if(q.id!=id&&q.name.compare(n,Qt::CaseInsensitive)==0)return false;
    queues_[id].name=n;return database_.saveQueue(queues_.value(id));
}
bool DownloadManager::setQueueConcurrency(const QString&id,int count) {
    if(!queues_.contains(id))return false;queues_[id].maxActive=qBound(1,count,32);
    if(!database_.saveQueue(queues_.value(id)))return false;startNextQueued();return true;
}
bool DownloadManager::setQueuePaused(const QString&id,bool paused) {
    if(!queues_.contains(id))return false;queues_[id].paused=paused;
    if(!database_.saveQueue(queues_.value(id)))return false;
    // Stopping a queue prevents new tasks from starting; already-running tasks finish normally.
    // Do not resume manually paused tasks when the queue is started again.
    startNextQueued();
    return true;
}
bool DownloadManager::moveToQueue(const QString&downloadId,const QString&queueId) {
    if(!queues_.contains(queueId)||active_.contains(downloadId)||!queued_.contains(downloadId))return false;
    auto&d=queued_[downloadId];d.queueId=queueId; // Preserve manually paused state when moving queues.
    const bool ok=database_.save(d);startNextQueued();return ok;
}
void DownloadManager::retryFailedInQueue(const QString&queueId) {
    for(auto it=queued_.begin();it!=queued_.end();++it)if((queueId.isEmpty()||it->queueId==queueId)&&it->status=="Failed"){it->status="Queued";it->error.clear();database_.save(it.value());}
    startNextQueued();
}
int DownloadManager::activeCountForQueue(const QString&id) const {
    int n=0;for(auto it=active_.cbegin();it!=active_.cend();++it)if(queued_.value(it.key()).queueId==id)++n;return n;
}
void DownloadManager::setHttpConnections(int count) {
    httpConnections_ = qBound(1, count, 8);
}

void DownloadManager::setProxy(const QString &host, int port, int type) {
    proxyHost_ = host.trimmed();
    proxyPort_ = qBound(0, port, 65535);
    proxyType_ = qBound(0, type, 2);
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

bool DownloadManager::setExpectedSha256(const QString &id, const QString &sha256) {
    const QString normalized = sha256.trimmed().toLower();
    if (normalized.size() != 64 || normalized.contains(QRegularExpression(QStringLiteral("[^0-9a-f]"))))
        return false;
    auto it = queued_.find(id);
    if (it != queued_.end()) {
        it->sha256 = normalized;
        it->verification.clear();
        database_.save(it.value());
        // Active transfers keep the checksum captured when they started; the new value applies on the next start.
        return true;
    }
    for (const auto &stored : database_.loadHistory()) {
        if (stored.id != id || stored.type != QStringLiteral("http")) continue;
        auto copy = stored;
        copy.sha256 = normalized;
        copy.verification.clear();
        database_.save(copy);
        return true;
    }
    return false;
}

bool DownloadManager::verifyChecksum(const QString &id, QString *message) {
    PersistedDownload d;
    bool found = false;
    auto it = queued_.find(id);
    if (it != queued_.end()) {
        d = it.value();
        found = true;
    } else {
        for (const auto &stored : database_.loadHistory()) {
            if (stored.id == id && stored.type == QStringLiteral("http")) {
                d = stored;
                found = true;
                break;
            }
        }
    }
    if (!found || d.sha256.isEmpty()) {
        if (message) *message = QStringLiteral("No SHA-256 checksum is configured.");
        return false;
    }
    const QString path = QDir(d.destination).filePath(d.filename);
    QString actual;
    const bool ok = HttpDownloader::verifySha256(path, d.sha256, &actual);
    d.verification = ok ? QStringLiteral("Verified") : QStringLiteral("Checksum mismatch");
    if (it != queued_.end()) it.value().verification = d.verification;
    database_.save(d);
    if (message) *message = ok
        ? QStringLiteral("SHA-256 verified: %1").arg(actual)
        : QStringLiteral("SHA-256 mismatch. Expected %1, got %2.").arg(d.sha256, actual);
    return ok;
}

PersistedDownload DownloadManager::downloadInfo(const QString &id) const {
    const auto it = queued_.constFind(id);
    if (it != queued_.cend()) return it.value();
    for (const auto &stored : database_.loadHistory())
        if (stored.id == id) return stored;
    return {};
}

bool DownloadManager::updateProperties(const PersistedDownload &properties) {
    if (properties.id.isEmpty()) return false;
    PersistedDownload updated;
    auto it = queued_.find(properties.id);
    if (it != queued_.end()) updated = it.value();
    else {
        bool found = false;
        for (const auto &stored : database_.loadHistory()) {
            if (stored.id == properties.id) { updated = stored; found = true; break; }
        }
        if (!found) return false;
    }
    if (active_.contains(properties.id)) {
        if (properties.destination != updated.destination || properties.filename != updated.filename) return false;
    } else {
        const QString filename = QFileInfo(properties.filename.trimmed()).fileName();
        if (filename.isEmpty() || filename == QStringLiteral(".") || filename == QStringLiteral("..") ||
            properties.destination.trimmed().isEmpty()) return false;
        updated.filename = filename;
        updated.destination = QDir::cleanPath(properties.destination.trimmed());
    }
    updated.category = properties.category.trimmed().isEmpty() ? QStringLiteral("Other") : properties.category.trimmed();
    updated.description = properties.description.trimmed();
    updated.connectionCount = qBound(1, properties.connectionCount, 8);
    updated.sha256 = properties.sha256.trimmed().toLower();
    if (!updated.sha256.isEmpty() &&
        (updated.sha256.size() != 64 || updated.sha256.contains(QRegularExpression(QStringLiteral("[^0-9a-f]")))))
        return false;
    updated.verification.clear();
    if (!database_.save(updated)) return false;
    if (it != queued_.end()) it.value() = updated;
    return true;
}

QString DownloadManager::addUrl(const QString &url, const QString &destination, const QString &category) {
    const QString id = QStringLiteral("download-%1").arg(nextId_++);
    PersistedDownload d;
    d.id = id;
    d.type = QStringLiteral("http");
    d.source = url;
    d.destination = destination;
    const QString folderName = QFileInfo(destination).fileName();
    static const QSet<QString> knownCategories{QStringLiteral("Video"), QStringLiteral("Music"),
        QStringLiteral("Documents"), QStringLiteral("Programs"), QStringLiteral("Other")};
    d.category = !category.trimmed().isEmpty() ? category.trimmed()
        : (knownCategories.contains(folderName) ? folderName : QStringLiteral("Other"));
    d.connectionCount = 0; // Zero means inherit the global HTTP connection setting.
    d.proxyType = -1;       // Negative means inherit the global proxy setting.
    d.queueId = QStringLiteral("main");
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
        QString id; QStringList ids=queued_.keys();
        std::sort(ids.begin(),ids.end(),[](const QString&a,const QString&b){
            auto number=[](const QString&s){bool ok=false;int n=s.startsWith("download-")?s.mid(9).toInt(&ok):0;return ok?n:INT_MAX;};
            const int na=number(a),nb=number(b);return na==nb?a<b:na<nb;
        });
        for(const QString&candidate:ids){const auto task=queued_.value(candidate);const auto q=queues_.value(task.queueId,queues_.value("main"));
            if(task.status=="Queued"&&!q.paused&&activeCountForQueue(q.id)<q.maxActive){id=candidate;break;}}
        if(id.isEmpty())return;
        const auto d=queued_.value(id);
        queued_[id].status = QStringLiteral("Starting");
        database_.save(queued_[id]);

        auto *thread = new QThread(this);
        auto *downloader = new HttpDownloader();
        downloader->moveToThread(thread);
        active_.insert(id, ActiveTask{d.source, d.destination, downloader, thread});
        downloader->setExpectedSha256(d.sha256);
        downloader->setProxy(proxyHost_, proxyPort_, proxyType_);
        updateActiveBandwidthLimits();

        connect(thread, &QThread::started, downloader, [downloader, d, this] {
            downloader->setSegments(d.connectionCount > 0 ? qBound(1, d.connectionCount, 8) : httpConnections_);
            downloader->setExpectedSha256(d.sha256);
            downloader->setProxy(proxyHost_, proxyPort_, proxyType_);
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
