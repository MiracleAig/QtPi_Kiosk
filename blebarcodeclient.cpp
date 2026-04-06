#include "blebarcodeclient.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QTimer>
#include <QDebug>

const QBluetoothUuid BleBarcodeClient::kServiceUuid(
    QStringLiteral("12345678-1234-1234-1234-1234567890ab"));
const QBluetoothUuid BleBarcodeClient::kCharacteristicUuid(
    QStringLiteral("abcd1234-1234-1234-1234-abcdefabcdef"));

BleBarcodeClient::BleBarcodeClient(QObject *parent)
    : QObject(parent)
{
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(5000);

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleBarcodeClient::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleBarcodeClient::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BleBarcodeClient::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BleBarcodeClient::onScanError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        startScan();
    });

    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
}

BleBarcodeClient::~BleBarcodeClient()
{
    m_stopping = true;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    cleanupService();
    cleanupController();
}

void BleBarcodeClient::start()
{
    startScan();
}

QString BleBarcodeClient::scannerStatus() const
{
    return m_scannerStatus;
}

bool BleBarcodeClient::connected() const
{
    return m_connected;
}

void BleBarcodeClient::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    const bool nameMatch = info.name() == QString::fromUtf8(kDeviceName);
    const bool serviceMatch = info.serviceUuids().contains(kServiceUuid);

    if (!nameMatch && !serviceMatch)
        return;

    m_targetDevice = info;

    if (m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }

    connectToDevice(info);
}

void BleBarcodeClient::onScanFinished()
{
    m_discoveryRunning = false;

    if (!m_targetDevice.isValid() && !m_controller) {
        setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
        scheduleReconnect();
    }
}

void BleBarcodeClient::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error);
    m_discoveryRunning = false;
    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
    scheduleReconnect();
}

void BleBarcodeClient::onControllerConnected()
{
    setStatus(QStringLiteral("Scanner: Connecting..."), false);
    m_controller->discoverServices();
}

void BleBarcodeClient::onControllerDisconnected()
{
    if (m_stopping)
        return;

    m_isConnecting = false;
    cleanupService();
    cleanupController();
    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
    scheduleReconnect();
}

void BleBarcodeClient::onControllerErrorOccurred(QLowEnergyController::Error error)
{
    Q_UNUSED(error);
    if (m_stopping)
        return;

    m_isConnecting = false;
    cleanupService();
    cleanupController();
    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
    scheduleReconnect();
}

void BleBarcodeClient::onServiceDiscovered(const QBluetoothUuid &service)
{
    if (service == kServiceUuid) {
        setupService();
    }
}

void BleBarcodeClient::onServiceScanDone()
{
    if (!m_service) {
        m_isConnecting = false;
        setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
        cleanupController();
        scheduleReconnect();
    }
}

void BleBarcodeClient::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    const QLowEnergyCharacteristic characteristic = m_service->characteristic(kCharacteristicUuid);
    if (!characteristic.isValid()) {
        setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
        cleanupService();
        cleanupController();
        scheduleReconnect();
        return;
    }

    const QLowEnergyDescriptor cccd = characteristic.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (cccd.isValid()) {
        m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    }

    m_isConnecting = false;
    setStatus(QStringLiteral("Scanner: Connected"), true);
}

void BleBarcodeClient::onCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                               const QByteArray &value)
{
    if (characteristic.uuid() != kCharacteristicUuid)
        return;

    const QString barcode = QString::fromUtf8(value).trimmed();
    if (barcode.isEmpty())
        return;

    emit barcodeReceived(barcode);
}

void BleBarcodeClient::startScan()
{
    if (m_discoveryAgent->isActive() || m_controller || m_discoveryRunning || m_isConnecting)
        return;

    m_targetDevice = QBluetoothDeviceInfo();
    m_discoveryRunning = true;
    setStatus(QStringLiteral("Scanner: Scanning..."), false);
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleBarcodeClient::connectToDevice(const QBluetoothDeviceInfo &info)
{
    if (m_controller || m_isConnecting)
        return;

    m_isConnecting = true;
    setStatus(QStringLiteral("Scanner: Connecting..."), false);

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected,
            this, &BleBarcodeClient::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleBarcodeClient::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleBarcodeClient::onControllerErrorOccurred);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleBarcodeClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleBarcodeClient::onServiceScanDone);

    m_controller->connectToDevice();
}

void BleBarcodeClient::setupService()
{
    if (!m_controller || m_service)
        return;

    m_service = m_controller->createServiceObject(kServiceUuid, this);
    if (!m_service)
        return;

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &BleBarcodeClient::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &BleBarcodeClient::onCharacteristicChanged);

    m_service->discoverDetails();
}

void BleBarcodeClient::cleanupController()
{
    if (!m_controller)
        return;

    m_controller->disconnect(this);
    m_controller->deleteLater();
    m_controller = nullptr;
}

void BleBarcodeClient::cleanupService()
{
    if (!m_service)
        return;

    m_service->deleteLater();
    m_service = nullptr;
}

void BleBarcodeClient::scheduleReconnect()
{
    if (m_stopping || m_reconnectTimer->isActive())
        return;

    if (m_discoveryAgent->isActive() || m_discoveryRunning || m_controller || m_isConnecting)
        return;

    m_reconnectTimer->start();
}

void BleBarcodeClient::setStatus(const QString &status, bool connectedNow)
{
    if (m_scannerStatus == status && m_connected == connectedNow)
        return;

    m_scannerStatus = status;
    m_connected = connectedNow;
    emit scannerStatusChanged();
    emit connectionChanged(m_connected, m_scannerStatus);
}
