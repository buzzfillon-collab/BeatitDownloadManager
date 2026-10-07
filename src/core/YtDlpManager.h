#pragma once

#include <QObject>
#include <QString>

class QProcess;

class YtDlpManager final : public QObject {
    Q_OBJECT

public:
    enum class Channel { Nightly, Stable };

    explicit YtDlpManager(QObject *parent = nullptr);

    QString executablePath() const;
    Channel channel() const;
    void setChannel(Channel channel);
    QString channelName() const;

    void updateIfDue();
    void updateNow();

signals:
    void updateStarted();
    void updateFinished(bool success, const QString &message);

private:
    QString locateExecutable() const;
    void startUpdate();

    QProcess *process_ = nullptr;
};