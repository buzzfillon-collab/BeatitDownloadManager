#include "app/MainWindow.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>

namespace {
constexpr auto kInstanceServer = "BeatitSingleInstance";

QByteArray commandPayload(const QStringList &arguments) {
    QJsonArray args;
    for (const QString &argument : arguments) args.append(argument);
    return QJsonDocument(QJsonObject{{QStringLiteral("arguments"), args}})
        .toJson(QJsonDocument::Compact);
}

bool forwardToExistingInstance(const QStringList &arguments) {
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstanceServer));
    if (!socket.waitForConnected(400)) return false;
    const QByteArray payload = commandPayload(arguments);
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(500);
    return true;
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Beatit"));
    QApplication::setApplicationDisplayName(QStringLiteral("Beatit Download Manager"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0-beta1"));

    if (app.arguments().contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "Beatit Download Manager "
                            << QApplication::applicationVersion() << Qt::endl;
        return 0;
    }

    const QStringList externalArguments = app.arguments().mid(1);
    if (forwardToExistingInstance(externalArguments))
        return 0;

    QLocalServer instanceServer;
    QLocalServer::removeServer(QString::fromLatin1(kInstanceServer));
    if (!instanceServer.listen(QString::fromLatin1(kInstanceServer))) {
        if (forwardToExistingInstance(externalArguments)) return 0;
        return 1;
    }

    app.setStyle(QStringLiteral("Fusion"));
    MainWindow window;

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &app, [socket, &window] {
                const QByteArray payload = socket->readAll();
                const auto document = QJsonDocument::fromJson(payload);
                if (!document.isObject()) return;
                const auto array = document.object().value(QStringLiteral("arguments")).toArray();
                QStringList arguments;
                for (const auto &value : array)
                    if (value.isString()) arguments.append(value.toString());
                if (arguments.isEmpty())
                    window.showFromTray();
                else
                    window.handleExternalCommand(arguments);
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });

    if (app.arguments().contains(QStringLiteral("--hidden")))
        window.hide();
    else if (!externalArguments.isEmpty())
        window.handleExternalCommand(externalArguments);
    else
        window.show();

    return app.exec();
}
