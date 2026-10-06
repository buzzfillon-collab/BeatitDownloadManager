#include "DownloadManager.h"
#include "HttpDownloader.h"

#include <QMetaObject>
#include <QThread>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}

DownloadManager::~DownloadManager() {
    const auto ids = active_.keys();
    for (const QString &id : ids) cancel(id);
}

QString DownloadManager::addUrl(const QString &url, const QString &destination) {
    const QString id = QStringLiteral("download-%1").arg(nextId_++);

    auto *thread = new QThread(this);
    auto *downloader = new HttpDownloader();
    downloader->moveToThread(thread);

    active_.insert(id, ActiveTask{url, destination, downloader, thread});

    connect(thread, &QThread::started, downloader, [downloader, url, destination] {
        downloader->start(url, destination);
    });

    connect(downloader, &HttpDownloader::started, this,
        [this, id](const QString &filename, qint64 total, bool) {
            emit taskStarted(id, filename, total);
        });

    connect(downloader, &HttpDownloader::progress, this,
        [this, id](qint64 done, qint64 total, qint64 speed) {
            emit taskProgress(id, done, total, speed);
        });

    auto stopThread = [this, id] {
        if (auto it = active_.find(id); it != active_.end())
            it->thread->quit();
    };

    connect(downloader, &HttpDownloader::paused, this,
        [this, id, stopThread](qint64 done) {
            emit taskPaused(id, done);
            stopThread();
        });

    connect(downloader, &HttpDownloader::completed, this,
        [this, id, stopThread](const QString &path) {
            emit taskCompleted(id, path);
            stopThread();
        });

    connect(downloader, &HttpDownloader::failed, this,
        [this, id, stopThread](const QString &error) {
            emit taskFailed(id, error);
            stopThread();
        });

    connect(downloader, &HttpDownloader::cancelled, this,
        [this, id, stopThread] {
            emit taskCancelled(id);
            stopThread();
        });

    connect(thread, &QThread::finished, downloader, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, id] {
        if (auto it = active_.find(id); it != active_.end()) {
            it->thread->deleteLater();
            active_.erase(it);
        }
    });

    emit taskAdded(id, url);
    thread->start();
    return id;
}

void DownloadManager::pause(const QString &id) {
    if (auto it = active_.find(id); it != active_.end())
        QMetaObject::invokeMethod(it->downloader, "pause", Qt::DirectConnection);
}

void DownloadManager::cancel(const QString &id) {
    if (auto it = active_.find(id); it != active_.end())
        QMetaObject::invokeMethod(it->downloader, "cancel", Qt::DirectConnection);
}
