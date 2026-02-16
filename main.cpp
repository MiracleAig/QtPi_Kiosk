#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>

#include "productlookup.h"
#include "inventorymanager.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // This makes: import Kiosk 1.0  work
    qmlRegisterType<ProductLookup>("KioskBackend", 1, 0, "ProductLookup");

    QQmlApplicationEngine engine;

    InventoryManager inventoryManager;
    engine.rootContext()->setContextProperty("inventoryManager", &inventoryManager);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kiosk/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
