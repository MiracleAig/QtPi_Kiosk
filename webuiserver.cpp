#include "webuiserver.h"
#include "inventorymanager.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

namespace {

// -------------------------
// Request model
// -------------------------
struct HttpRequest {
    QByteArray method;
    QByteArray target;   // "/path?x=y"
    QByteArray path;     // "/path"
    QHash<QByteArray, QByteArray> headers; // lowercase header -> value
    QByteArray body;
    bool headOnly = false;
};

static QByteArray normalizePath(QByteArray rawTarget)
{
    const int q = rawTarget.indexOf('?');
    if (q >= 0) rawTarget = rawTarget.left(q);
    if (rawTarget.isEmpty()) return "/";
    return rawTarget;
}

static QHash<QByteArray, QByteArray> parseHeaders(const QByteArray& headerBlock)
{
    QHash<QByteArray, QByteArray> out;
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        QByteArray key = line.left(colon).trimmed().toLower();
        QByteArray val = line.mid(colon + 1).trimmed();
        out.insert(key, val);
    }
    return out;
}

static bool parseRequest(const QByteArray& raw, HttpRequest& req)
{
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;

    const QByteArray headerPart = raw.left(headerEnd);
    req.body = raw.mid(headerEnd + 4);

    const QList<QByteArray> lines = headerPart.split('\n');
    if (lines.isEmpty()) return false;

    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2) return false;

    req.method = first.at(0).trimmed();
    req.target = first.at(1).trimmed();
    req.path = normalizePath(req.target);
    req.headOnly = (req.method == "HEAD");

    QByteArray headersOnly;
    for (int i = 1; i < lines.size(); ++i) {
        headersOnly += lines.at(i);
        if (!headersOnly.endsWith('\n')) headersOnly += '\n';
    }
    req.headers = parseHeaders(headersOnly);

    return true;
}

static int contentLength(const HttpRequest& req)
{
    const QByteArray v = req.headers.value("content-length");
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok ? n : 0;
}

static QByteArray queryValue(const QByteArray& rawTarget, const QByteArray& key)
{
    const int q = rawTarget.indexOf('?');
    if (q < 0) return {};
    const QByteArray query = rawTarget.mid(q + 1);
    const QList<QByteArray> pairs = query.split('&');

    for (const QByteArray& pair : pairs) {
        const int eq = pair.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray k = pair.left(eq).trimmed();
        if (k != key) continue;
        return pair.mid(eq + 1).trimmed();
    }
    return {};
}

// -------------------------
// Resources
// -------------------------
static QByteArray loadResource(const QString& resPath)
{
    qInfo() << "exists :/web/login.html =" << QFile(":/web/login.html").exists();
    qInfo() << "exists :/web/dashboard.html =" << QFile(":/web/dashboard.html").exists();
    qInfo() << "dir :/web =" << QDir(":/web").entryList(QDir::Files);

    QFile f(resPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// -------------------------
// Session auth (cookie)
// -------------------------
static QByteArray adminPassword()
{
    QByteArray p = qgetenv("FRIDGESENSE_ADMIN_PASSWORD").trimmed();
    if (p.isEmpty()) p = "admin";
    return p;
}

static QString newSessionId()
{
    QByteArray r(32, Qt::Uninitialized);
    for (int i = 0; i < r.size(); ++i)
        r[i] = char(QRandomGenerator::global()->generate() & 0xFF);

    return QString::fromLatin1(QCryptographicHash::hash(r, QCryptographicHash::Sha256).toHex());
}

// sessionId -> expiry UTC
static QHash<QString, QDateTime> g_sessions;

static void sweepSessions()
{
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto it = g_sessions.begin(); it != g_sessions.end();) {
        if (it.value() < now) it = g_sessions.erase(it);
        else ++it;
    }
}

static QString cookieValue(const HttpRequest& req, const QByteArray& name)
{
    const QByteArray cookieHdr = req.headers.value("cookie");
    if (cookieHdr.isEmpty()) return {};

    const QList<QByteArray> parts = cookieHdr.split(';');
    for (QByteArray p : parts) {
        p = p.trimmed();
        if (!p.startsWith(name + "=")) continue;
        return QString::fromLatin1(p.mid(name.size() + 1));
    }
    return {};
}

static bool isAuthed(const HttpRequest& req)
{
    sweepSessions();
    const QString sid = cookieValue(req, "FSSESSID");
    if (sid.isEmpty()) return false;

    auto it = g_sessions.find(sid);
    if (it == g_sessions.end()) return false;

    it.value() = QDateTime::currentDateTimeUtc().addSecs(30 * 60);
    return true;
}

// -------------------------
// Response helpers
// -------------------------
static QByteArray statusTextFor(int code)
{
    switch (code) {
    case 200: return "OK";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

static void writeResponse(QTcpSocket* socket,
                          int statusCode,
                          const QByteArray& contentType,
                          const QByteArray& body,
                          const QByteArray& extraHeaders = {})
{
    if (!socket) return;

    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusTextFor(statusCode) + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += extraHeaders;
    resp += "\r\n";
    resp += body;

    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

static void redirect(QTcpSocket* socket, const QByteArray& location, const QByteArray& extraHeaders = {})
{
    QByteArray headers = "Location: " + location + "\r\n";
    headers += extraHeaders;
    writeResponse(socket, 302, "text/plain; charset=utf-8", QByteArray(), headers);
}

static void respondNotFound(QTcpSocket* socket)
{
    writeResponse(socket, 404, "text/plain; charset=utf-8", "Not found\n");
}

static void respondMethodNotAllowed(QTcpSocket* socket)
{
    writeResponse(socket, 405, "text/plain; charset=utf-8", "Method Not Allowed\n");
}

} // namespace

// -------------------------
// WebUiServer
// -------------------------
WebUiServer::WebUiServer(InventoryManager *inventoryManager, QObject *parent)
    : QObject(parent)
    , m_inventoryManager(inventoryManager)
{
    connect(&m_server, &QTcpServer::newConnection, this, &WebUiServer::handleConnection);
}

bool WebUiServer::start(const QHostAddress &address, quint16 port)
{
    if (m_server.isListening()) return true;

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

    QByteArray raw;
    const int maxHeader = 16 * 1024;
    while (!raw.contains("\r\n\r\n") && raw.size() < maxHeader) {
        if (!socket->waitForReadyRead(2000)) break;
        raw += socket->readAll();
    }

    if (raw.isEmpty()) {
        writeResponse(socket, 400, "text/plain; charset=utf-8", "No request data\n");
        return;
    }

    HttpRequest req;
    if (!parseRequest(raw, req)) {
        writeResponse(socket, 400, "text/plain; charset=utf-8", "Malformed request\n");
        return;
    }

    if (req.method == "POST") {
        const int cl = contentLength(req);
        if (cl > 64 * 1024) {
            writeResponse(socket, 413, "text/plain; charset=utf-8", "Payload too large\n");
            return;
        }
        while (req.body.size() < cl) {
            if (!socket->waitForReadyRead(2000)) break;
            req.body += socket->readAll();
        }
        if (req.body.size() < cl) {
            writeResponse(socket, 400, "text/plain; charset=utf-8", "Incomplete request body\n");
            return;
        }
    }

    const bool isGetOrHead = (req.method == "GET" || req.method == "HEAD");
    const bool isPost = (req.method == "POST");

    const auto requireAuth = [&]() -> bool {
        if (isAuthed(req)) return true;
        redirect(socket, "/login");
        return false;
    };

    qInfo() << "HTTP" << req.method << req.target;

    // Root -> login
    if (req.path == "/" || req.path == "/index.html") {
        redirect(socket, "/login");
        return;
    }

    // Login page
    if (req.path == "/login") {
        if (isGetOrHead) {
            const QByteArray html = loadResource(":/web/web/login.html");
            if (html.isEmpty()) {
                writeResponse(socket, 500, "text/plain; charset=utf-8", "Missing resource :/web/login.html\n");
                return;
            }
            writeResponse(socket, 200, "text/html; charset=utf-8", req.headOnly ? QByteArray() : html);
            return;
        }

        if (isPost) {
            QUrlQuery q(QString::fromUtf8(req.body));
            const QString pass = q.queryItemValue("password");

            if (pass.toUtf8() == adminPassword()) {
                const QString sid = newSessionId();
                g_sessions.insert(sid, QDateTime::currentDateTimeUtc().addSecs(30 * 60));

                const QByteArray cookie =
                    "Set-Cookie: FSSESSID=" + sid.toUtf8() + "; Path=/; HttpOnly; SameSite=Lax\r\n";
                redirect(socket, "/dashboard", cookie);
            } else {
                redirect(socket, "/login?err=1");
            }
            return;
        }

        respondMethodNotAllowed(socket);
        return;
    }

    if (req.path == "/logout") {
        const QString sid = cookieValue(req, "FSSESSID");
        if (!sid.isEmpty()) g_sessions.remove(sid);

        const QByteArray cookie =
            "Set-Cookie: FSSESSID=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n";
        redirect(socket, "/login", cookie);
        return;
    }

    // Public static assets needed by login/dashboard pages
    if (req.path == "/styles.css") {
        if (!isGetOrHead) { respondMethodNotAllowed(socket); return; }

        const QByteArray css = loadResource(":/web/web/styles.css");
        if (css.isEmpty()) {
            writeResponse(socket, 500, "text/plain; charset=utf-8", "Missing resource :/web/styles.css\n");
            return;
        }
        writeResponse(socket, 200, "text/css; charset=utf-8", req.headOnly ? QByteArray() : css);
        return;
    }

    if (req.path == "/app.js") {
        if (!isGetOrHead) { respondMethodNotAllowed(socket); return; }

        const QByteArray js = loadResource(":/web/app.js");
        if (js.isEmpty()) {
            writeResponse(socket, 500, "text/plain; charset=utf-8", "Missing resource :/web/app.js\n");
            return;
        }
        writeResponse(socket, 200, "application/javascript; charset=utf-8", req.headOnly ? QByteArray() : js);
        return;
    }

    // Protected dashboard page
    if (req.path == "/dashboard" || req.path == "/dashboard.html") {
        if (!isGetOrHead) { respondMethodNotAllowed(socket); return; }
        if (!requireAuth()) return;

        const QByteArray html = loadResource(":/web/web/dashboard.html");
        if (html.isEmpty()) {
            writeResponse(socket, 500, "text/plain; charset=utf-8", "Missing resource :/web/dashboard.html\n");
            return;
        }
        writeResponse(socket, 200, "text/html; charset=utf-8", req.headOnly ? QByteArray() : html);
        return;
    }

    // Protected export endpoint
    if (req.path == "/export.csv") {
        if (!isGetOrHead) { respondMethodNotAllowed(socket); return; }
        if (!requireAuth()) return;

        if (!m_inventoryManager) {
            writeResponse(socket, 500, "text/plain; charset=utf-8", "Inventory manager unavailable\n");
            return;
        }

        const QByteArray csv = req.headOnly ? QByteArray() : m_inventoryManager->exportInventoryCsv().toUtf8();
        const QByteArray headers = "Content-Disposition: attachment; filename=\"export.csv\"\r\n";
        writeResponse(socket, 200, "text/csv; charset=utf-8", csv, headers);
        return;
    }

    // Protected inventory JSON endpoint
    if (req.path == "/api/inventory.json") {
        if (!isGetOrHead) { respondMethodNotAllowed(socket); return; }
        if (!requireAuth()) return;

        if (!m_inventoryManager) {
            writeResponse(socket, 500, "text/plain; charset=utf-8", "Inventory manager unavailable\n");
            return;
        }

        QByteArray out;
        if (!req.headOnly) {
            const QVariantList rows = m_inventoryManager->loadInventory(0);
            QJsonArray items;
            for (const QVariant &row : rows)
                items.append(QJsonObject::fromVariantMap(row.toMap()));

            QJsonObject root;
            root.insert("count", items.size());
            root.insert("inventory", items);
            out = QJsonDocument(root).toJson(QJsonDocument::Indented);
        }

        writeResponse(socket, 200, "application/json; charset=utf-8", out);
        return;
    }

    // Protected add-food endpoint
    if (req.path == "/api/add-food") {
        if (!isPost) { respondMethodNotAllowed(socket); return; }
        if (!requireAuth()) return;

        if (!m_inventoryManager) {
            writeResponse(socket, 500, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Inventory manager unavailable"})");
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            writeResponse(socket, 400, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Invalid JSON payload"})");
            return;
        }

        QVariantMap product = doc.object().toVariantMap();
        const QString barcode = product.value("barcode").toString().trimmed();
        if (barcode.isEmpty()) {
            writeResponse(socket, 400, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"barcode is required"})");
            return;
        }
        product["barcode"] = barcode;

        if (!m_inventoryManager->insertProduct(product)) {
            writeResponse(socket, 500, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Failed to insert product"})");
            return;
        }

        writeResponse(socket, 200, "application/json; charset=utf-8", R"({"ok":true})");
        return;
    }

    // Protected delete-food endpoint
    if (req.path == "/api/delete-food") {
        if (!isPost) { respondMethodNotAllowed(socket); return; }
        if (!requireAuth()) return;

        if (!m_inventoryManager) {
            writeResponse(socket, 500, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Inventory manager unavailable"})");
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            writeResponse(socket, 400, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Invalid JSON payload"})");
            return;
        }

        QVariantMap criteria = doc.object().toVariantMap();
        const int id = criteria.value("id").toInt();
        const QString barcode = criteria.value("barcode").toString().trimmed();
        if (id <= 0 && barcode.isEmpty()) {
            writeResponse(socket, 400, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"id or barcode is required"})");
            return;
        }

        criteria["id"] = id;
        criteria["barcode"] = barcode;

        if (!m_inventoryManager->deleteProduct(criteria)) {
            writeResponse(socket, 404, "application/json; charset=utf-8",
                          R"({"ok":false,"error":"Food item not found"})");
            return;
        }

        writeResponse(socket, 200, "application/json; charset=utf-8", R"({"ok":true})");
        return;
    }

    respondNotFound(socket);
}
