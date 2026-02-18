#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>
#include <QHostAddress>

#include "productlookup.h"
#include "inventorymanager.h"
#include "webuiserver.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // This makes: import Kiosk 1.0  work
    qmlRegisterType<ProductLookup>("KioskBackend", 1, 0, "ProductLookup");

    QQmlApplicationEngine engine;

    InventoryManager inventoryManager;
    engine.rootContext()->setContextProperty("inventoryManager", &inventoryManager);

    WebUiServer webUiServer(&inventoryManager);
    const QByteArray bindEnv = qgetenv("KIOSK_WEBUI_BIND");
    const QByteArray portEnv = qgetenv("KIOSK_WEBUI_PORT");

#ifdef Q_OS_WINDOWS
    const QHostAddress bindAddress = bindEnv.isEmpty()
                                         ? QHostAddress::LocalHost
                                         : QHostAddress(QString::fromUtf8(bindEnv));
#else
    const QHostAddress bindAddress = bindEnv.isEmpty()
                                         ? QHostAddress::Any
                                         : QHostAddress(QString::fromUtf8(bindEnv));
#endif
    const quint16 bindPort = portEnv.isEmpty() ? 8080 : portEnv.toUShort();
    webUiServer.start(bindAddress, bindPort);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kiosk/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
