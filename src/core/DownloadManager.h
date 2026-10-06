#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include "DownloadDatabase.h"

class HttpDownloader;
class QThread;

class DownloadManager final : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    QString addUrl(const QString &url, const QString &destination);
    void pause(const QString &id);
    void cancel(const QString &id);
    void resume(const QString &id);
    void setMaxActive(int count);
    int maxActive() const noexcept { return maxActive_; }

signals:
    void taskAdded(const QString &id, const QString &url);
    void taskRestored(const QString &id, const QString &url, const QString &filename,
                      const QString &status, qint64 downloaded, qint64 total);
    void taskStarted(const QString &id, const QString &filename, qint64 totalBytes);
    void taskProgress(const QString &id, qint64 downloaded, qint64 total, qint64 speed);
    void taskPaused(const QString &id, qint64 downloaded);
    void taskCompleted(const QString &id, const QString &path);
    void taskFailed(const QString &id, const QString &error);
    void taskCancelled(const QString &id);

private:
    struct ActiveTask {
        QString url;
        QString destination;
        HttpDownloader *downloader{};
        QThread *thread{};
    };

    void persist(const QString &id, const QString &status, qint64 downloaded = 0,
                 qint64 total = 0, qint64 speed = 0, const QString &error = {});
    void startNextQueued();

    int nextId_ = 1;
    int maxActive_ = 3;
    QHash<QString, ActiveTask> active_;
    QHash<QString, PersistedDownload> queued_;
    DownloadDatabase database_;
};