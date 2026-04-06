#pragma once

#include <QObject>
#include <QBluetoothUuid>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyService>
#include <QLowEnergyController>
#include <QBluetoothDeviceDiscoveryAgent>

class QLowEnergyService;
class QLowEnergyCharacteristic;
class QTimer;

class BleBarcodeClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString scannerStatus READ scannerStatus NOTIFY scannerStatusChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY scannerStatusChanged)

public:
    explicit BleBarcodeClient(QObject *parent = nullptr);
    ~BleBarcodeClient() override;

    Q_INVOKABLE void start();

    QString scannerStatus() const;
    bool connected() const;

signals:
    void barcodeReceived(const QString &barcode);
    void connectionChanged(bool connected, const QString &status);
    void scannerStatusChanged();

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);

    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerErrorOccurred(QLowEnergyController::Error error);

    void onServiceDiscovered(const QBluetoothUuid &service);
    void onServiceScanDone();

    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                 const QByteArray &value);

private:
    void startScan();
    void connectToDevice(const QBluetoothDeviceInfo &info);
    void setupService();
    void cleanupController();
    void cleanupService();
    void scheduleReconnect();
    void setStatus(const QString &status, bool connected);

    static const QBluetoothUuid kServiceUuid;
    static const QBluetoothUuid kCharacteristicUuid;
    static constexpr const char *kDeviceName = "ScanGenie-ESP32";

    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;

    QBluetoothDeviceInfo m_targetDevice;
    QString m_scannerStatus;
    bool m_connected = false;
    bool m_discoveryRunning = false;
    bool m_isConnecting = false;
    bool m_stopping = false;
    QTimer *m_reconnectTimer = nullptr;
};
