#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHostAddress>

class QTcpSocket;
class InventoryManager;

class WebUiServer : public QObject
{
    Q_OBJECT
public:
    explicit WebUiServer(InventoryManager *inventoryManager, QObject *parent = nullptr);

    bool start(const QHostAddress &address = QHostAddress::Any, quint16 port = 8080);

private:
    void handleConnection();
    void respond(QTcpSocket *socket, int statusCode, const QByteArray &statusText,
                 const QByteArray &contentType, const QByteArray &body,
                 const QByteArray &extraHeaders = QByteArray());

    InventoryManager *m_inventoryManager;
    QTcpServer m_server;
};
