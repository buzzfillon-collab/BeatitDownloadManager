#pragma once

#include <QObject>
#include <QHash>
#include <QString>

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

signals:
    void taskAdded(const QString &id, const QString &url);
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

    int nextId_ = 1;
    QHash<QString, ActiveTask> active_;
};
