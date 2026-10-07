#include "BrowserBridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>
namespace { constexpr auto kServerName = "BeatitBrowserBridge"; constexpr auto kMaxMessageBytes = 1024 * 1024; }
BrowserBridge::BrowserBridge(QObject *parent) : QObject(parent), server_(new QLocalServer(this)) {
    connect(server_, &QLocalServer::newConnection, this, &BrowserBridge::acceptConnection);
}
bool BrowserBridge::start() {
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    return server_->listen(QString::fromLatin1(kServerName));
}
void BrowserBridge::acceptConnection() {
    while (auto *socket = server_->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, &BrowserBridge::readSocket);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}
void BrowserBridge::readSocket() {
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) return;
    if (socket->bytesAvailable() > kMaxMessageBytes) { socket->disconnectFromServer(); return; }
    const auto document = QJsonDocument::fromJson(socket->readAll());
    if (!document.isObject()) {
        socket->write(QJsonDocument(QJsonObject{{"ok", false}, {"error", "invalid-json"}}).toJson(QJsonDocument::Compact));
        socket->disconnectFromServer(); return;
    }
    processMessage(socket, document.object());
}
void BrowserBridge::processMessage(QLocalSocket *socket, const QJsonObject &message) {
    const QString url = message.value(QStringLiteral("url")).toString().trimmed();
    const QString title = message.value(QStringLiteral("title")).toString().trimmed();
    const QString kind = message.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        socket->write(QJsonDocument(QJsonObject{{"ok", false}, {"error", "invalid-url"}}).toJson(QJsonDocument::Compact));
        socket->disconnectFromServer(); return;
    }
    emit captureRequested(url, title, kind);
    socket->write(QJsonDocument(QJsonObject{{"ok", true}}).toJson(QJsonDocument::Compact));
    socket->disconnectFromServer();
}