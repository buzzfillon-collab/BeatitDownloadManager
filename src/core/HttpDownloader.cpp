#include "HttpDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <chrono>
#include <curl/curl.h>

namespace {
struct CurlContext {
    HttpDownloader *owner{};
    QFile *file{};
    qint64 written{};
    qint64 total{-1};
    qint64 base{};
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    qint64 lastBytes{};
};
size_t writeCallback(char *ptr,size_t size,size_t nmemb,void *userdata) {
    auto *ctx=static_cast<CurlContext*>(userdata);
    const qint64 bytes=static_cast<qint64>(size*nmemb);
    if(!ctx->file||!ctx->file->isOpen()) return 0;
    const qint64 n=ctx->file->write(ptr,bytes);
    if(n<=0) return 0;
    ctx->written+=n;
    const auto now=std::chrono::steady_clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-ctx->lastReport).count();
    if(elapsed>=500) {
        const qint64 delta=ctx->written-ctx->lastBytes;
        const qint64 speed=elapsed?(delta*1000)/elapsed:0;
        emit ctx->owner->progress(ctx->base+ctx->written,ctx->total,speed);
        ctx->lastReport=now; ctx->lastBytes=ctx->written;
    }
    return static_cast<size_t>(n);
}
int progressCallback(void *clientp,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<CurlContext*>(clientp)->owner->isCancelRequested()?1:0;
}
struct HeaderContext { qint64 contentRangeTotal=-1; };
size_t headerCallback(char *buffer,size_t size,size_t nitems,void *userdata) {
    auto *ctx=static_cast<HeaderContext*>(userdata);
    const QString line=QString::fromUtf8(buffer,static_cast<int>(size*nitems)).trimmed();
    static const QRegularExpression rx(QStringLiteral(R"(^Content-Range:\s*bytes\s+\d+-\d+/(\d+|\*))"),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto m=rx.match(line);
    if(m.hasMatch()&&m.captured(1)!=QStringLiteral("*")) ctx->contentRangeTotal=m.captured(1).toLongLong();
    return size*nitems;
}
}

HttpDownloader::HttpDownloader(QObject *parent):QObject(parent){curl_global_init(CURL_GLOBAL_DEFAULT);}
HttpDownloader::~HttpDownloader(){curl_global_cleanup();}
QString HttpDownloader::filenameFromUrl(const QString &url){
    QString name=QUrl(url).fileName();
    if(name.isEmpty()) name=QStringLiteral("download");
    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")),QStringLiteral("_"));
    return name;
}
QString HttpDownloader::humanCurlError(int code){return QString::fromUtf8(curl_easy_strerror(static_cast<CURLcode>(code)));}
void HttpDownloader::start(const QString &url,const QString &destination){
    pauseRequested_=false; cancelRequested_=false; run(url,destination);
}
void HttpDownloader::pause(){pauseRequested_=true;}
void HttpDownloader::cancel(){cancelRequested_=true;}
bool HttpDownloader::isCancelRequested() const noexcept{return cancelRequested_.load()||pauseRequested_.load();}

void HttpDownloader::run(const QString &url,const QString &destination){
    emit probing();
    QDir dir(destination);
    if(!dir.exists()&&!dir.mkpath(QStringLiteral("."))){emit failed(QStringLiteral("Unable to create download directory."));return;}
    const QString finalPath=dir.filePath(filenameFromUrl(url));
    const QString partPath=finalPath+QStringLiteral(".part");
    qint64 existing=QFileInfo::exists(partPath)?QFileInfo(partPath).size():0;

    auto perform=[&](bool resume,qint64 offset)->CURLcode{
        CURL *curl=curl_easy_init(); if(!curl) return CURLE_FAILED_INIT;
        QFile file(partPath);
        if(!file.open(resume?(QIODevice::WriteOnly|QIODevice::Append):(QIODevice::WriteOnly|QIODevice::Truncate))){
            curl_easy_cleanup(curl); return CURLE_WRITE_ERROR;
        }
        HeaderContext headers;
        CurlContext ctx{this,&file,0,-1,offset};
        curl_easy_setopt(curl,CURLOPT_URL,url.toUtf8().constData());
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,20L);
        curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
        curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
        curl_easy_setopt(curl,CURLOPT_USERAGENT,"BeatitDownloadManager/0.1 beta");
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCallback);
        curl_easy_setopt(curl,CURLOPT_WRITEDATA,&ctx);
        curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,headerCallback);
        curl_easy_setopt(curl,CURLOPT_HEADERDATA,&headers);
        curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progressCallback);
        curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&ctx);
        curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
        if(resume&&offset>0){
            const QByteArray range=QStringLiteral("%1-").arg(offset).toUtf8();
            curl_easy_setopt(curl,CURLOPT_RANGE,range.constData());
        }
        const CURLcode result=curl_easy_perform(curl);
        long response=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&response);
        curl_off_t length=-1; curl_easy_getinfo(curl,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&length);
        if(response==206&&headers.contentRangeTotal>=0) ctx.total=headers.contentRangeTotal;
        else if(!resume&&length>=0) ctx.total=length;
        else if(resume&&response==206&&length>=0) ctx.total=offset+length;
        file.flush(); file.close();

        if(resume&&response==200&&result==CURLE_OK){
            curl_easy_cleanup(curl);
            QFile::remove(partPath);
            return CURLE_RANGE_ERROR;
        }
        if(result==CURLE_OK&&response>=400){curl_easy_cleanup(curl);return CURLE_HTTP_RETURNED_ERROR;}
        if(ctx.total>=0) emit progress(QFileInfo(partPath).size(),ctx.total,0);
        curl_easy_cleanup(curl);
        return result;
    };

    emit started(QFileInfo(finalPath).fileName(),existing,existing>0);
    CURLcode result=perform(existing>0,existing);
    if(result==CURLE_RANGE_ERROR){existing=0;result=perform(false,0);}

    if(cancelRequested_){emit cancelled();return;}
    if(pauseRequested_){emit paused(QFileInfo(partPath).size());return;}
    if(result!=CURLE_OK){emit failed(QStringLiteral("Download failed: %1").arg(humanCurlError(result)));return;}

    const qint64 actual=QFileInfo(partPath).size();
    if(QFileInfo::exists(finalPath)&&!QFile::remove(finalPath)){
        emit failed(QStringLiteral("A file with the same name already exists and could not be replaced."));return;
    }
    if(!QFile::rename(partPath,finalPath)){emit failed(QStringLiteral("Unable to finalize the downloaded file."));return;}
    emit progress(actual,actual,0);
    emit completed(finalPath);
}
