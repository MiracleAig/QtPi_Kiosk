#include "inventorymanager.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>
#include <QStringList>

namespace {
QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

QVariant normalizedNumber(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) return QVariant();

    bool ok = false;
    const double n = value.toDouble(&ok);
    if (!ok) return QVariant();

    return n;
}
}

InventoryManager::InventoryManager(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_LINUX
    // Pi kiosk path
    m_dbPath = "/home/fridgesense/inventory.db";
#else
    // Windows/macOS dev path
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    m_dbPath = base + "/inventory.db";
#endif

    m_ready = ensureDb() && ensureSchema();
}

bool InventoryManager::ensureDb()
{
    if (QSqlDatabase::contains(m_connName)) {
        auto db = QSqlDatabase::database(m_connName);
        if (db.isOpen()) return true;
        if (!db.open()) {
            emit errorOccurred(db.lastError().text());
            return false;
        }
        return true;
    }

    auto db = QSqlDatabase::addDatabase("QSQLITE", m_connName);

    if (!db.isValid()) {
        emit errorOccurred("QSQLITE driver not available. Check Qt SQL plugins / deployment.");
        return false;
    }

    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        emit errorOccurred(QString("Failed to open DB at '%1': %2")
                               .arg(m_dbPath, db.lastError().text()));
        return false;
    }
    return true;
}

bool InventoryManager::ensureSchema()
{
    auto db = QSqlDatabase::database(m_connName);
    if (!db.isOpen()) return false;

    QSqlQuery q(db);

    // Inventory table (multiple rows allowed)
    const char *inventorySql = R"SQL(
        CREATE TABLE IF NOT EXISTS inventory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            barcode TEXT NOT NULL,
            name TEXT,
            brand TEXT,
            size TEXT,
            image TEXT,
            calories REAL,
            protein REAL,
            carbs REAL,
            fat REAL,
            sugar REAL,
            sodium REAL,
            created_at INTEGER NOT NULL
        );
    )SQL";

    if (!q.exec(QString::fromUtf8(inventorySql))) {
        emit errorOccurred(q.lastError().text());
        return false;
    }

    q.exec("CREATE INDEX IF NOT EXISTS idx_inventory_barcode ON inventory(barcode);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_inventory_created_at ON inventory(created_at);");

    // Cache table (ONE row per barcode; offline-first)
    const char *cacheSql = R"SQL(
        CREATE TABLE IF NOT EXISTS product_cache (
            barcode TEXT PRIMARY KEY,
            name TEXT,
            brand TEXT,
            size TEXT,
            image TEXT,
            image_local TEXT,
            calories REAL,
            protein REAL,
            carbs REAL,
            fat REAL,
            sugar REAL,
            sodium REAL,
            updated_at INTEGER NOT NULL
        );
    )SQL";

    if (!q.exec(QString::fromUtf8(cacheSql))) {
        emit errorOccurred(q.lastError().text());
        return false;
    }

    // Backward-compat migrations. Duplicate-column errors are safe to ignore.
    q.exec("ALTER TABLE product_cache ADD COLUMN image_local TEXT;");
    q.exec("ALTER TABLE product_cache ADD COLUMN calories REAL;");
    q.exec("ALTER TABLE product_cache ADD COLUMN protein REAL;");
    q.exec("ALTER TABLE product_cache ADD COLUMN carbs REAL;");
    q.exec("ALTER TABLE product_cache ADD COLUMN fat REAL;");
    q.exec("ALTER TABLE product_cache ADD COLUMN sugar REAL;");
    q.exec("ALTER TABLE product_cache ADD COLUMN sodium REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN calories REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN protein REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN carbs REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN fat REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN sugar REAL;");
    q.exec("ALTER TABLE inventory ADD COLUMN sodium REAL;");

    q.exec("CREATE INDEX IF NOT EXISTS idx_cache_updated_at ON product_cache(updated_at);");

    return true;
}

bool InventoryManager::insertProduct(const QVariantMap &product)
{
    if (!m_ready && !(ensureDb() && ensureSchema())) return false;

    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    q.prepare(R"SQL(
        INSERT INTO inventory (
            barcode, name, brand, size, image,
            calories, protein, carbs, fat, sugar, sodium,
            created_at
        )
        VALUES (
            :barcode, :name, :brand, :size, :image,
            :calories, :protein, :carbs, :fat, :sugar, :sodium,
            :created_at
        )
    )SQL");

    q.bindValue(":barcode", product.value("barcode").toString());
    q.bindValue(":name",    product.value("name").toString());
    q.bindValue(":brand",   product.value("brand").toString());
    q.bindValue(":size",    product.value("size").toString());
    q.bindValue(":image",   product.value("image").toString());
    q.bindValue(":calories", normalizedNumber(product.value("calories")));
    q.bindValue(":protein", normalizedNumber(product.value("protein")));
    q.bindValue(":carbs", normalizedNumber(product.value("carbs")));
    q.bindValue(":fat", normalizedNumber(product.value("fat")));
    q.bindValue(":sugar", normalizedNumber(product.value("sugar")));
    q.bindValue(":sodium", normalizedNumber(product.value("sodium")));
    q.bindValue(":created_at", QDateTime::currentSecsSinceEpoch());

    if (!q.exec()) {
        emit errorOccurred(q.lastError().text());
        return false;
    }

    emit inventoryChanged();
    return true;
}

QVariantList InventoryManager::loadInventory(int limit)
{
    QVariantList out;
    if (!m_ready && !(ensureDb() && ensureSchema())) return out;

    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    QString sql =
        "SELECT id, barcode, name, brand, size, image, "
        "calories, protein, carbs, fat, sugar, sodium, created_at "
        "FROM inventory ORDER BY created_at DESC";
    if (limit > 0) sql += " LIMIT " + QString::number(limit);

    if (!q.exec(sql)) {
        emit errorOccurred(q.lastError().text());
        return out;
    }

    while (q.next()) {
        QVariantMap row;
        row["id"] = q.value(0);
        row["barcode"] = q.value(1);
        row["name"] = q.value(2);
        row["brand"] = q.value(3);
        row["size"] = q.value(4);
        row["image"] = q.value(5);
        row["calories"] = q.value(6);
        row["protein"] = q.value(7);
        row["carbs"] = q.value(8);
        row["fat"] = q.value(9);
        row["sugar"] = q.value(10);
        row["sodium"] = q.value(11);
        row["created_at"] = q.value(12);
        out.append(row);
    }
    return out;
}

QString InventoryManager::exportInventoryCsv()
{
    const QVariantList rows = loadInventory(0);

    QStringList lines;
    lines << "id,barcode,name,brand,size,image,calories,protein,carbs,fat,sugar,sodium,created_at";

    for (const QVariant &item : rows) {
        const QVariantMap row = item.toMap();

        QStringList cols;
        cols << csvEscape(row.value("id").toString())
             << csvEscape(row.value("barcode").toString())
             << csvEscape(row.value("name").toString())
             << csvEscape(row.value("brand").toString())
             << csvEscape(row.value("size").toString())
             << csvEscape(row.value("image").toString())
             << csvEscape(row.value("calories").toString())
             << csvEscape(row.value("protein").toString())
             << csvEscape(row.value("carbs").toString())
             << csvEscape(row.value("fat").toString())
             << csvEscape(row.value("sugar").toString())
             << csvEscape(row.value("sodium").toString())
             << csvEscape(row.value("created_at").toString());

        lines << cols.join(',');
    }

    return lines.join("\n") + "\n";
}

// --------------------
// OFFLINE-FIRST CACHE
// --------------------

QVariantMap InventoryManager::findCachedProduct(const QString &barcode)
{
    QVariantMap out;
    if (!m_ready && !(ensureDb() && ensureSchema())) return out;

    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    // 1) Try cache table first
    q.prepare(R"SQL(
        SELECT barcode, name, brand, size, image, image_local,
               calories, protein, carbs, fat, sugar, sodium,
               updated_at
        FROM product_cache
        WHERE barcode = :barcode
        LIMIT 1
    )SQL");
    q.bindValue(":barcode", barcode);

    if (!q.exec()) {
        emit errorOccurred(q.lastError().text());
        return out;
    }

    if (q.next()) {
        out["barcode"] = q.value(0).toString();
        out["name"] = q.value(1).toString();
        out["brand"] = q.value(2).toString();
        out["size"] = q.value(3).toString();
        out["image"] = q.value(4).toString();
        out["image_local"] = q.value(5).toString();
        out["calories"] = q.value(6);
        out["protein"] = q.value(7);
        out["carbs"] = q.value(8);
        out["fat"] = q.value(9);
        out["sugar"] = q.value(10);
        out["sodium"] = q.value(11);
        out["updated_at"] = q.value(12).toLongLong();
        return out;
    }

    // 2) Fallback: if already saved in inventory before cache existed
    q.prepare(R"SQL(
        SELECT barcode, name, brand, size, image,
               calories, protein, carbs, fat, sugar, sodium,
               created_at
        FROM inventory
        WHERE barcode = :barcode
        ORDER BY created_at DESC
        LIMIT 1
    )SQL");
    q.bindValue(":barcode", barcode);

    if (!q.exec()) {
        emit errorOccurred(q.lastError().text());
        return out;
    }

    if (q.next()) {
        out["barcode"] = q.value(0).toString();
        out["name"] = q.value(1).toString();
        out["brand"] = q.value(2).toString();
        out["size"] = q.value(3).toString();
        out["image"] = q.value(4).toString();
        out["calories"] = q.value(5);
        out["protein"] = q.value(6);
        out["carbs"] = q.value(7);
        out["fat"] = q.value(8);
        out["sugar"] = q.value(9);
        out["sodium"] = q.value(10);
        out["updated_at"] = q.value(11).toLongLong();
        // image_local not known from inventory fallback
        out["image_local"] = QString();
        return out;
    }

    return out;
}

QString InventoryManager::ensureLocalImage(const QString &barcode, const QString &imageUrl)
{
    if (imageUrl.isEmpty()) return QString();

    const QUrl url(imageUrl);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
        return QString();

    QFileInfo dbInfo(m_dbPath);
    const QString imgDir = dbInfo.dir().absolutePath() + "/images";
    QDir().mkpath(imgDir);

    QString ext = QFileInfo(url.path()).suffix().toLower();
    if (ext.isEmpty()) ext = "jpg";

    const QString localPath = imgDir + "/" + barcode + "." + ext;

    // If already downloaded, reuse
    if (QFile::exists(localPath))
        return localPath;

    // Download (blocking). Simple + reliable for kiosk use.
    QNetworkRequest req(url);
    QNetworkReply *reply = m_nam.get(req);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QString();
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QFile f(localPath);
    if (!f.open(QIODevice::WriteOnly))
        return QString();

    f.write(data);
    f.close();

    return localPath;
}

bool InventoryManager::cacheProduct(const QVariantMap &product)
{
    if (!m_ready && !(ensureDb() && ensureSchema())) return false;

    const QString barcode = product.value("barcode").toString();
    if (barcode.isEmpty()) {
        emit errorOccurred("cacheProduct: missing barcode");
        return false;
    }

    const QString imageUrl = product.value("image").toString();
    const QString imageLocal = ensureLocalImage(barcode, imageUrl);

    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    // Upsert into cache
    q.prepare(R"SQL(
        INSERT INTO product_cache (
            barcode, name, brand, size, image, image_local,
            calories, protein, carbs, fat, sugar, sodium,
            updated_at
        )
        VALUES (
            :barcode, :name, :brand, :size, :image, :image_local,
            :calories, :protein, :carbs, :fat, :sugar, :sodium,
            :updated_at
        )
        ON CONFLICT(barcode) DO UPDATE SET
            name = excluded.name,
            brand = excluded.brand,
            size = excluded.size,
            image = excluded.image,
            image_local = excluded.image_local,
            calories = excluded.calories,
            protein = excluded.protein,
            carbs = excluded.carbs,
            fat = excluded.fat,
            sugar = excluded.sugar,
            sodium = excluded.sodium,
            updated_at = excluded.updated_at
    )SQL");

    q.bindValue(":barcode", barcode);
    q.bindValue(":name", product.value("name").toString());
    q.bindValue(":brand", product.value("brand").toString());
    q.bindValue(":size", product.value("size").toString());
    q.bindValue(":image", imageUrl);
    q.bindValue(":image_local", imageLocal);
    q.bindValue(":calories", normalizedNumber(product.value("calories")));
    q.bindValue(":protein", normalizedNumber(product.value("protein")));
    q.bindValue(":carbs", normalizedNumber(product.value("carbs")));
    q.bindValue(":fat", normalizedNumber(product.value("fat")));
    q.bindValue(":sugar", normalizedNumber(product.value("sugar")));
    q.bindValue(":sodium", normalizedNumber(product.value("sodium")));
    q.bindValue(":updated_at", QDateTime::currentSecsSinceEpoch());

    if (!q.exec()) {
        emit errorOccurred(q.lastError().text());
        return false;
    }

    return true;
}
