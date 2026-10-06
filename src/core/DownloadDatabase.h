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
    void initialize();
    void close();
    void *db_ = nullptr;
    QString path_;
};