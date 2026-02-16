#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QNetworkAccessManager>

class InventoryManager : public QObject
{
    Q_OBJECT
public:
    explicit InventoryManager(QObject *parent = nullptr);

    Q_INVOKABLE bool insertProduct(const QVariantMap &product);
    Q_INVOKABLE QVariantList loadInventory(int limit = 0);

    // OFFLINE-FIRST CACHE
    Q_INVOKABLE QVariantMap findCachedProduct(const QString &barcode);
    Q_INVOKABLE bool cacheProduct(const QVariantMap &product);

signals:
    void inventoryChanged();
    void errorOccurred(const QString &message);

private:
    bool ensureDb();
    bool ensureSchema();

    QString ensureLocalImage(const QString &barcode, const QString &imageUrl);

    QString m_dbPath;
    QString m_connName = "inventory_conn";
    bool m_ready = false;

    QNetworkAccessManager m_nam;
};
