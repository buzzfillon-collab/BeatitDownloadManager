#include "DownloadTask.h"
#include <utility>
DownloadTask::DownloadTask(QString id, QString url, QObject *parent)
 : QObject(parent), id_(std::move(id)), url_(std::move(url)) {}
