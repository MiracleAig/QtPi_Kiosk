#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>
#include <QHostAddress>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QDebug>

#include "productlookup.h"
#include "inventorymanager.h"
#include "webuiserver.h"
#include "blebarcodeclient.h"

class SerialMonitorEmulator : public QObject
{
public:
    explicit SerialMonitorEmulator(QObject *parent = nullptr)
        : QObject(parent)
    {
        timer.setInterval(1000);

        QObject::connect(&timer, &QTimer::timeout, [this]() {
            checkPorts();
        });

        timer.start();
        checkPorts();
    }

private:
    void checkPorts()
    {
        QString detectedPort;

        const auto ports = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &info : ports) {
            QString name = info.portName();

            if (name.startsWith("ttyUSB") || name.startsWith("ttyACM")) {
                detectedPort = info.systemLocation();
                break;
            }
        }

        if (detectedPort.isEmpty())
            return;

        if (currentPort == detectedPort)
            return;

        openAndEmulate(detectedPort);
    }

    void openAndEmulate(const QString &portName)
    {
        serial = new QSerialPort(this);

        serial->setPortName(portName);
        serial->setBaudRate(QSerialPort::Baud115200);

        if (!serial->open(QIODevice::ReadWrite)) {
            qWarning() << "Failed to open scanner port:" << portName;
            delete serial;
            serial = nullptr;
            return;
        }

        currentPort = portName;

        qDebug() << "Scanner connected:" << portName;

        // emulate serial monitor reset
        serial->setDataTerminalReady(false);
        serial->setRequestToSend(false);

        QTimer::singleShot(100, [this]() {
            if (!serial) return;

            serial->setDataTerminalReady(true);
            serial->setRequestToSend(true);

            qDebug() << "Serial monitor emulation applied";
        });

        QObject::connect(serial, &QSerialPort::readyRead, [this]() {
            QByteArray data = serial->readAll();
            if (!data.isEmpty())
                qDebug().noquote() << QString::fromUtf8(data);
        });
    }

private:
    QTimer timer;
    QSerialPort *serial = nullptr;
    QString currentPort;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    SerialMonitorEmulator serialMonitor(&app);

    qmlRegisterType<ProductLookup>("KioskBackend", 1, 0, "ProductLookup");

    QQmlApplicationEngine engine;

    InventoryManager inventoryManager;
    engine.rootContext()->setContextProperty("inventoryManager", &inventoryManager);

    BleBarcodeClient bleBarcodeClient;
    engine.rootContext()->setContextProperty("bleBarcodeClient", &bleBarcodeClient);

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

    bleBarcodeClient.start();

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kiosk/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
