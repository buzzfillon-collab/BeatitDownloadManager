#pragma once
#include <QString>
#include <QVector>

struct PersistedDownload {
    QString id;
    QString type;
    QString source;
    QString destination;
    QString filename;
    QString status;
    qint64 totalBytes = 0;
    qint64 downloadedBytes = 0;
    qint64 speed = 0;
    QString error;
    qint64 updatedAt = 0;
    QString sha256;
    QString verification;
    QString category;
    QString description;
    QString userAgent;
    QString queueId;
    int connectionCount = 0;
    int proxyType = -1;
    QString proxyHost;
    int proxyPort = 0;
};

class DownloadDatabase final {
public:
    DownloadDatabase();
    ~DownloadDatabase();
    bool open();
    bool save(const PersistedDownload &download);
    bool remove(const QString &id);
    QVector<PersistedDownload> loadActive() const;
    QVector<PersistedDownload> loadHistory() const;
    QString path() const;
private:
    bool initialize();
    void close();
    void *db_ = nullptr;
    QString path_;
};