#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>

class ProductLookup : public QObject {
    Q_OBJECT
public:
    explicit ProductLookup(QObject *parent = nullptr);

    Q_INVOKABLE void lookup(const QString &barcode);

signals:
    void productReady(const QVariantMap &product); // empty map => not found
    void error(const QString &message);

private:
    QNetworkAccessManager m_net;
};
