#include "YtDlpManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

YtDlpManager::YtDlpManager(QObject *parent)
    : QObject(parent) {}

QString YtDlpManager::locateExecutable() const
{
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("tools/yt-dlp.exe"));

    if (QFileInfo::exists(bundled))
        return bundled;

    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

QString YtDlpManager::executablePath() const
{
    return locateExecutable();
}

YtDlpManager::Channel YtDlpManager::channel() const
{
    QSettings settings(QStringLiteral("Beatit"),
                       QStringLiteral("Beatit"));

    const QString value =
        settings.value(QStringLiteral("ytdlp/channel"),
                       QStringLiteral("nightly"))
            .toString();

    return value.compare(QStringLiteral("stable"),
                         Qt::CaseInsensitive) == 0
        ? Channel::Stable
        : Channel::Nightly;
}

void YtDlpManager::setChannel(Channel value)
{
    QSettings settings(QStringLiteral("Beatit"),
                       QStringLiteral("Beatit"));

    settings.setValue(
        QStringLiteral("ytdlp/channel"),
        value == Channel::Stable
            ? QStringLiteral("stable")
            : QStringLiteral("nightly"));
}

QString YtDlpManager::channelName() const
{
    return channel() == Channel::Stable
        ? QStringLiteral("stable")
        : QStringLiteral("nightly");
}

void YtDlpManager::updateIfDue()
{
    QSettings settings(QStringLiteral("Beatit"),
                       QStringLiteral("Beatit"));

    const qint64 lastCheck =
        settings.value(QStringLiteral("ytdlp/lastUpdateCheck"), 0)
            .toLongLong();

    constexpr qint64 checkInterval = 24 * 60 * 60;

    if (lastCheck > 0 &&
        QDateTime::currentSecsSinceEpoch() - lastCheck < checkInterval)
        return;

    startUpdate();
}

void YtDlpManager::updateNow()
{
    startUpdate();
}

void YtDlpManager::updateAfterFailure()
{
    startUpdate();
}

void YtDlpManager::startUpdate()
{
    if (process_)
        return;

    const QString executable = locateExecutable();

    if (executable.isEmpty()) {
        emit updateFinished(
            false,
            QStringLiteral("yt-dlp.exe was not found"));
        return;
    }

    QSettings settings(QStringLiteral("Beatit"),
                       QStringLiteral("Beatit"));

    settings.setValue(
        QStringLiteral("ytdlp/lastUpdateCheck"),
        QDateTime::currentSecsSinceEpoch());

    process_ = new QProcess(this);

    process_->setProgram(executable);
    process_->setArguments({
        QStringLiteral("--update-to"),
        channelName()
    });

    process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(
        process_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus status) {

            const QString output =
                QString::fromLocal8Bit(process_->readAll()).trimmed();

            const bool success =
                status == QProcess::NormalExit &&
                exitCode == 0;

            QString message =
                success
                    ? QStringLiteral("yt-dlp is up to date")
                    : QStringLiteral("yt-dlp update failed");

            if (!output.isEmpty()) {
                const QStringList lines =
                    output.split(
                        QRegularExpression(
                            QStringLiteral("[\\r\\n]+")),
                        Qt::SkipEmptyParts);

                if (!lines.isEmpty())
                    message = lines.constLast();
            }

            process_->deleteLater();
            process_ = nullptr;

            emit updateFinished(success, message);
        });

    connect(
        process_,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError) {

            const QString message =
                process_
                    ? process_->errorString()
                    : QStringLiteral("yt-dlp update failed");

            if (process_) {
                process_->deleteLater();
                process_ = nullptr;
            }

            emit updateFinished(false, message);
        });

    emit updateStarted();

    process_->start();
}