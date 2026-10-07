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
    void remove(const QString &id, bool deleteFile = false);
    void resume(const QString &id);
    void setMaxActive(int count);
    void setHttpConnections(int count);
    void setBandwidthLimit(qint64 bytesPerSecond);
    qint64 bandwidthLimit() const noexcept { return bandwidthLimit_; }
    void setSchedulerAllowed(bool allowed);
    void setExpectedSha256(const QString &id, const QString &sha256);
    bool verifyChecksum(const QString &id, QString *message = nullptr);
    int maxActive() const noexcept { return maxActive_; }
    int httpConnections() const noexcept { return httpConnections_; }

signals:
    void taskAdded(const QString &id, const QString &url);
    void taskRestored(const QString &id, const QString &url, const QString &filename,
                      const QString &status, qint64 downloaded, qint64 total, qint64 updatedAt);
    void taskStarted(const QString &id, const QString &filename, qint64 totalBytes);
    void taskProgress(const QString &id, qint64 downloaded, qint64 total, qint64 speed);
    void taskPaused(const QString &id, qint64 downloaded);
    void taskCompleted(const QString &id, const QString &path);
    void taskFailed(const QString &id, const QString &error);
    void taskCancelled(const QString &id);
    void taskRemoved(const QString &id);

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
    void updateActiveBandwidthLimits();

    int nextId_ = 1;
    int maxActive_ = 3;
    int httpConnections_ = 8;
    qint64 bandwidthLimit_ = 0;
    bool schedulerAllowed_ = true;
    QHash<QString, bool> schedulerPaused_;
    QHash<QString, ActiveTask> active_;
    QHash<QString, PersistedDownload> queued_;
    QHash<QString, bool> pendingRemoval_;
    QHash<QString, bool> pendingDeleteFile_;
    DownloadDatabase database_;
};