#include "app/MainWindow.h"

#include <QApplication>
#include <QTextStream>

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

    app.setStyle(QStringLiteral("Fusion"));
    MainWindow window;
    if (app.arguments().contains(QStringLiteral("--hidden")))
        window.hide();
    else
        window.show();
    return app.exec();
}
