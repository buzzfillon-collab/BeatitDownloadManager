#pragma once
#include <QObject>
#include <QString>
class DownloadManager final : public QObject {
 Q_OBJECT
public:
 explicit DownloadManager(QObject *parent=nullptr);
 QString addUrl(const QString &url);
signals:
 void taskAdded(const QString &id, const QString &url);
private:
 int nextId_=1;
};
