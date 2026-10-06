#pragma once
#include <QObject>
#include <QString>
class DownloadTask final : public QObject {
 Q_OBJECT
public:
 explicit DownloadTask(QString id, QString url, QObject *parent=nullptr);
 const QString &id() const noexcept { return id_; }
 const QString &url() const noexcept { return url_; }
private:
 QString id_;
 QString url_;
};
