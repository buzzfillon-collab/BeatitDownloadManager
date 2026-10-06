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
};

class DownloadDatabase final {
public:
    DownloadDatabase();
    ~DownloadDatabase();
    bool open();
    bool save(const PersistedDownload &download);
    QVector<PersistedDownload> loadActive() const;
    QString path() const;
private:
    void initialize();
    void close();
    void *db_ = nullptr;
    QString path_;
};