#include "DownloadManager.h"
#include "DownloadTask.h"
DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}
QString DownloadManager::addUrl(const QString &url) {
 const QString id=QStringLiteral("download-%1").arg(nextId_++);
 new DownloadTask(id,url,this);
 emit taskAdded(id,url);
 return id;
}
