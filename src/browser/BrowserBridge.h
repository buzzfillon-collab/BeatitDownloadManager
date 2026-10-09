#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
class QLocalServer;
class QLocalSocket;
class BrowserBridge final : public QObject {
    Q_OBJECT
public:
    explicit BrowserBridge(QObject *parent = nullptr);
    bool start();
signals:
    void captureRequested(const QString &url, const QString &title, const QString &kind, const QString &pageUrl, const QString &userAgent);
private slots:
    void acceptConnection();
    void readSocket();
private:
    void processMessage(QLocalSocket *socket, const QJsonObject &message);
    QLocalServer *server_{};
    QHash<QString, qint64> recentCaptures_;
};