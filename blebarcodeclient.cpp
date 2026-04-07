#include "blebarcodeclient.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QTimer>
#include <QDebug>

namespace {
enum class ClientState {
    Idle,
    Scanning,
    Connecting,
    DiscoveringServices,
    DiscoveringDetails,
    Connected,
    ReconnectScheduled
};

QString stateToString(ClientState state)
{
    switch (state) {
    case ClientState::Idle: return QStringLiteral("Idle");
    case ClientState::Scanning: return QStringLiteral("Scanning");
    case ClientState::Connecting: return QStringLiteral("Connecting");
    case ClientState::DiscoveringServices: return QStringLiteral("DiscoveringServices");
    case ClientState::DiscoveringDetails: return QStringLiteral("DiscoveringDetails");
    case ClientState::Connected: return QStringLiteral("Connected");
    case ClientState::ReconnectScheduled: return QStringLiteral("ReconnectScheduled");
    }

    return QStringLiteral("Unknown");
}

ClientState clientState(const BleBarcodeClient *client)
{
    return static_cast<ClientState>(client->property("ble_client_state").toInt());
}

void transitionState(BleBarcodeClient *client, ClientState next, const QString &reason)
{
    const ClientState current = clientState(client);
    if (current == next)
        return;

    qDebug().noquote() << "[BLE]" << stateToString(current) << "->" << stateToString(next) << ":" << reason;
    client->setProperty("ble_client_state", static_cast<int>(next));
}

bool isBusyState(ClientState state)
{
    return state == ClientState::Connecting
        || state == ClientState::DiscoveringServices
        || state == ClientState::DiscoveringDetails;
}

QTimer *reconnectTimer(BleBarcodeClient *client)
{
    auto *timer = client->findChild<QTimer *>(QStringLiteral("bleReconnectTimer"));
    if (!timer) {
        timer = new QTimer(client);
        timer->setObjectName(QStringLiteral("bleReconnectTimer"));
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, client, [client]() {
            transitionState(client, ClientState::Idle, QStringLiteral("Reconnect timer fired"));
            client->start();
        });
    }
    return timer;
}
} // namespace

const QBluetoothUuid BleBarcodeClient::kServiceUuid(
    QStringLiteral("12345678-1234-1234-1234-1234567890ab"));
const QBluetoothUuid BleBarcodeClient::kCharacteristicUuid(
    QStringLiteral("abcd1234-1234-1234-1234-abcdefabcdef"));

BleBarcodeClient::BleBarcodeClient(QObject *parent)
    : QObject(parent)
{
    setProperty("ble_client_state", static_cast<int>(ClientState::Idle));

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

    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
}

BleBarcodeClient::~BleBarcodeClient()
{
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
    if (clientState(this) != ClientState::Scanning) {
        qDebug().noquote() << "[BLE] Ignoring discovered device while state is" << stateToString(clientState(this));
        return;
    }

    const bool nameMatch = info.name() == QString::fromUtf8(kDeviceName);
    const bool serviceMatch = info.serviceUuids().contains(kServiceUuid);

    if (!nameMatch && !serviceMatch)
        return;

    m_targetDevice = info;

    if (m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
}

void BleBarcodeClient::onScanFinished()
{
    m_discoveryRunning = false;

    if (clientState(this) == ClientState::Scanning) {
        transitionState(this, ClientState::Idle, QStringLiteral("Scan completed"));
    }

    if (m_targetDevice.isValid()) {
        connectToDevice(m_targetDevice);
        return;
    }

    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
}

void BleBarcodeClient::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    qDebug() << "[BLE] Scan error:" << error;
    m_discoveryRunning = false;
    transitionState(this, ClientState::Idle, QStringLiteral("Scan error"));
    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
    scheduleReconnect();
}

void BleBarcodeClient::onControllerConnected()
{
    transitionState(this, ClientState::DiscoveringServices, QStringLiteral("Controller connected"));
    setStatus(QStringLiteral("Scanner: Connecting..."), false);
    m_controller->discoverServices();
}

void BleBarcodeClient::onControllerDisconnected()
{
    m_isConnecting = false;
    cleanupService();
    cleanupController();
    transitionState(this, ClientState::Idle, QStringLiteral("Controller disconnected"));
    setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
    scheduleReconnect();
}

void BleBarcodeClient::onControllerErrorOccurred(QLowEnergyController::Error error)
{
    qDebug() << "[BLE] Controller error:" << error;
    m_isConnecting = false;
    cleanupService();
    cleanupController();
    transitionState(this, ClientState::Idle, QStringLiteral("Controller error"));
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
        setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
        if (m_controller) {
            qDebug() << "[BLE] Service not found, disconnecting controller";
            m_controller->disconnectFromDevice();
        }
    }
}

void BleBarcodeClient::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    qDebug() << "[BLE] Service state changed:" << state;

    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    const QLowEnergyCharacteristic characteristic = m_service->characteristic(kCharacteristicUuid);
    if (!characteristic.isValid()) {
        setStatus(QStringLiteral("Scanner: Disconnected - reconnecting..."), false);
        cleanupService();
        if (m_controller) {
            qDebug() << "[BLE] Characteristic missing, disconnecting controller";
            m_controller->disconnectFromDevice();
        }
        return;
    }

    const QLowEnergyDescriptor cccd = characteristic.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (cccd.isValid()) {
        m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    }

    m_isConnecting = false;
    transitionState(this, ClientState::Connected, QStringLiteral("Service discovered and notifications enabled"));
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
    const ClientState state = clientState(this);
    if (state != ClientState::Idle) {
        qDebug().noquote() << "[BLE] startScan skipped in state" << stateToString(state);
        return;
    }

    if (m_discoveryAgent->isActive() || m_controller || m_discoveryRunning || m_isConnecting)
        return;

    m_targetDevice = QBluetoothDeviceInfo();
    m_discoveryRunning = true;
    transitionState(this, ClientState::Scanning, QStringLiteral("Starting LE scan"));
    setStatus(QStringLiteral("Scanner: Scanning..."), false);
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleBarcodeClient::connectToDevice(const QBluetoothDeviceInfo &info)
{
    const ClientState state = clientState(this);
    if (state != ClientState::Idle) {
        qDebug().noquote() << "[BLE] connectToDevice skipped in state" << stateToString(state);
        return;
    }

    if (!info.isValid()) {
        qDebug() << "[BLE] connectToDevice skipped: invalid target device";
        return;
    }

    if (m_discoveryAgent->isActive()) {
        qDebug() << "[BLE] Stopping active scan before connectToDevice";
        m_discoveryAgent->stop();
        return;
    }

    if (m_controller || m_isConnecting)
        return;

    m_isConnecting = true;
    transitionState(this, ClientState::Connecting, QStringLiteral("Creating controller and connecting"));
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
    if (!m_controller || m_service || clientState(this) != ClientState::DiscoveringServices)
        return;

    m_service = m_controller->createServiceObject(kServiceUuid, this);
    if (!m_service) {
        qDebug() << "[BLE] Failed to create service object, disconnecting controller";
        m_controller->disconnectFromDevice();
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &BleBarcodeClient::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &BleBarcodeClient::onCharacteristicChanged);

    transitionState(this, ClientState::DiscoveringDetails, QStringLiteral("Discovering service details"));
    m_service->discoverDetails();
}

void BleBarcodeClient::cleanupController()
{
    if (!m_controller)
        return;

    m_controller->disconnectFromDevice();
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
    const ClientState state = clientState(this);
    if (isBusyState(state)) {
        qDebug().noquote() << "[BLE] scheduleReconnect skipped in busy state" << stateToString(state);
        return;
    }

    auto *timer = reconnectTimer(this);
    if (timer->isActive()) {
        qDebug() << "[BLE] Reconnect timer already active; not scheduling another";
        return;
    }

    transitionState(this, ClientState::ReconnectScheduled, QStringLiteral("Scheduling reconnect in 3 seconds"));
    timer->start(3000);
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
