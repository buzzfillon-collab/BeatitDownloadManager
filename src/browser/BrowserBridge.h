#pragma once
#include <QObject>
#include <QJsonObject>
class QLocalServer;
class QLocalSocket;
class BrowserBridge final : public QObject {
    Q_OBJECT
public:
    explicit BrowserBridge(QObject *parent = nullptr);
    bool start();
signals:
    void captureRequested(const QString &url, const QString &title, const QString &kind);
private slots:
    void acceptConnection();
    void readSocket();
private:
    void processMessage(QLocalSocket *socket, const QJsonObject &message);
    QLocalServer *server_{};
};