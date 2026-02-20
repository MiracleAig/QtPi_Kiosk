#include "webuiserver.h"

#include "inventorymanager.h"

#include <QTcpSocket>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QByteArray normalizePath(QByteArray rawPath)
{
    const int queryStart = rawPath.indexOf('?');
    if (queryStart >= 0) {
        rawPath = rawPath.left(queryStart);
    }

    if (rawPath.isEmpty()) {
        return "/";
    }
    return rawPath;
}

QByteArray tokenFromPath(const QByteArray &rawPath)
{
    const int queryStart = rawPath.indexOf('?');
    if (queryStart < 0) {
        return QByteArray();
    }

    const QByteArray query = rawPath.mid(queryStart + 1);
    const QList<QByteArray> pairs = query.split('&');
    for (const QByteArray &pair : pairs) {
        const int equalsIndex = pair.indexOf('=');
        if (equalsIndex <= 0) {
            continue;
        }

        const QByteArray key = pair.left(equalsIndex).trimmed();
        if (key != "token") {
            continue;
        }

        return pair.mid(equalsIndex + 1).trimmed();
    }

    return QByteArray();
}
}

WebUiServer::WebUiServer(InventoryManager *inventoryManager, QObject *parent)
    : QObject(parent),
    m_inventoryManager(inventoryManager),
    m_authToken(qgetenv("KIOSK_WEBUI_TOKEN").trimmed())
{
    connect(&m_server, &QTcpServer::newConnection, this, &WebUiServer::handleConnection);
}

bool WebUiServer::isAuthorized(const QByteArray &request, const QByteArray &path) const
{
    if (m_authToken.isEmpty()) {
        return true;
    }

    const QByteArray queryToken = tokenFromPath(path);
    if (!queryToken.isEmpty() && queryToken == m_authToken) {
        return true;
    }

    const QList<QByteArray> lines = request.split('\n');
    for (const QByteArray &lineRaw : lines) {
        const QByteArray line = lineRaw.trimmed();
        if (!line.toLower().startsWith("authorization:")) {
            continue;
        }

        const int separator = line.indexOf(':');
        if (separator < 0) {
            continue;
        }

        const QByteArray value = line.mid(separator + 1).trimmed();
        if (value == "Bearer " + m_authToken) {
            return true;
        }
    }

    return false;
}

QByteArray WebUiServer::unauthorizedBody() const
{
    return "Unauthorized. Provide Authorization: Bearer <token> header or ?token=<token> query parameter.\n";
}

bool WebUiServer::start(const QHostAddress &address, quint16 port)
{
    if (m_server.isListening()) {
        return true;
    }

    const bool ok = m_server.listen(address, port);
    if (ok) {
        const QString host = (address == QHostAddress::Any || address == QHostAddress::AnyIPv6)
                                 ? QStringLiteral("0.0.0.0")
                                 : address.toString();
        qInfo() << "Web UI available at http://" << host << ":" << port;
    } else {
        qWarning() << "Failed to start Web UI server:" << m_server.errorString();
    }
    return ok;
}

void WebUiServer::handleConnection()
{
    QTcpSocket *socket = m_server.nextPendingConnection();
    if (!socket) return;

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    QByteArray request;
    while (!request.contains("\r\n\r\n") && request.size() < 8192) {
        if (!socket->waitForReadyRead(2000)) {
            break;
        }
        request += socket->readAll();
    }

    if (request.isEmpty()) {
        respond(socket, 400, "Bad Request", "text/plain", "No request data\n");
        return;
    }

    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty()) {
        respond(socket, 400, "Bad Request", "text/plain", "Malformed request\n");
        return;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        respond(socket, 400, "Bad Request", "text/plain", "Malformed request line\n");
        return;
    }

    const QByteArray method = requestLine.at(0);
    const QByteArray rawPath = requestLine.at(1);
    const QByteArray path = normalizePath(rawPath);

    if (method != "GET" && method != "HEAD") {
        respond(socket, 405, "Method Not Allowed", "text/plain", "Only GET and HEAD are supported\n");
        return;
    }

    const bool headOnly = (method == "HEAD");

    if (!isAuthorized(request, rawPath)) {
        const QByteArray headers = "WWW-Authenticate: Bearer realm=\"QtPiKiosk\"\r\n";
        respond(socket, 401, "Unauthorized", "text/plain", headOnly ? QByteArray() : unauthorizedBody(), headers);
        return;
    }

    if (path == "/" || path == "/index.html") {
        const QString body = QStringLiteral(
                                 "<!doctype html><html><head><meta charset='utf-8'>"
                                 "<title>QtPi Kiosk Export</title>"
                                 "<style>body{font-family:sans-serif;margin:2rem;max-width:700px;}"
                                 "a{display:inline-block;padding:.8rem 1.2rem;background:#0b5fff;color:#fff;"
                                 "text-decoration:none;border-radius:8px;}"
                                 "small{display:block;margin-top:1rem;color:#666;}</style></head><body>"
                                 "<h1>QtPi Kiosk Data Export</h1>"
                                 "<p>Download the inventory database contents as CSV or JSON.</p>"
                                 "<a href='/export.csv'>Download export.csv</a>"
                                 " <a href='/api/inventory.json'>View inventory.json</a>"
                                 "<small>Generated endpoint time: %1</small>"
                                 "</body></html>")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODate));

        respond(socket, 200, "OK", "text/html; charset=utf-8", headOnly ? QByteArray() : body.toUtf8());
        return;
    }

    if (path == "/export.csv") {
        if (!m_inventoryManager) {
            respond(socket, 500, "Internal Server Error", "text/plain", "Inventory manager unavailable\n");
            return;
        }

        const QByteArray csv = headOnly ? QByteArray() : m_inventoryManager->exportInventoryCsv().toUtf8();
        const QByteArray headers = "Content-Disposition: attachment; filename=\"export.csv\"\r\n";
        respond(socket, 200, "OK", "text/csv; charset=utf-8", csv, headers);
        return;
    }

    if (path == "/api/inventory.json") {
        if (!m_inventoryManager) {
            respond(socket, 500, "Internal Server Error", "text/plain", "Inventory manager unavailable\n");
            return;
        }

        QByteArray body;
        if (!headOnly) {
            const QVariantList rows = m_inventoryManager->loadInventory(0);
            QJsonArray items;
            for (const QVariant &row : rows) {
                items.append(QJsonObject::fromVariantMap(row.toMap()));
            }

            QJsonObject root;
            root.insert("count", items.size());
            root.insert("inventory", items);
            body = QJsonDocument(root).toJson(QJsonDocument::Indented);
        }

        respond(socket, 200, "OK", "application/json; charset=utf-8", body);
        return;
    }

    respond(socket, 404, "Not Found", "text/plain", "Not found\n");
}

void WebUiServer::respond(QTcpSocket *socket, int statusCode, const QByteArray &statusText,
                          const QByteArray &contentType, const QByteArray &body,
                          const QByteArray &extraHeaders)
{
    if (!socket) return;

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += extraHeaders;
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}
