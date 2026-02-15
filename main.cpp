#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "productlookup.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    ProductLookup lookup;
    engine.rootContext()->setContextProperty("ProductLookup", &lookup);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [](){ QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Kiosk", "Main");
    return app.exec();
}
