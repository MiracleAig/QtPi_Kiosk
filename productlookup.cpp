#include "productlookup.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QStringList>

ProductLookup::ProductLookup(QObject *parent) : QObject(parent) {}

void ProductLookup::lookup(const QString &barcode)
{
    // OFF v2 endpoint; limit fields to what we need
    const QString code = barcode.trimmed();



    QUrl url(QString("https://world.openfoodfacts.org/api/v2/product/%1").arg(code));
    QUrlQuery q;
    q.addQueryItem("fields", "code,product_name,brands,quantity,image_front_small_url,nutriments");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "FridgeSensesUI/2.0 (Qt)");

    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {


        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit error(reply->errorString());
            emit productReady(QVariantMap{});
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit error("Invalid JSON");
            emit productReady(QVariantMap{});
            return;
        }

        const auto obj = doc.object();

        // OFF uses "status": 1 when found, 0 when not found (common pattern)
        const int status = obj.value("status").toInt(0);
        if (status != 1) {
            emit productReady(QVariantMap{});
            return;
        }

        const auto productObj = obj.value("product").toObject();

        QString outCode = productObj.value("code").toString();
        if (outCode.size() == 12)
            outCode.prepend('0');

        QVariantMap out;
        out["barcode"] = outCode;
        out["name"]    = productObj.value("product_name").toString();
        out["brand"]   = productObj.value("brands").toString();
        out["size"]    = productObj.value("quantity").toString();
        out["image"]   = productObj.value("image_front_small_url").toString();

        const QJsonObject nutriments = productObj.value("nutriments").toObject();
        const auto readNumber = [&nutriments](const QStringList &keys) -> QVariant {
            for (const QString &key : keys) {
                const QJsonValue value = nutriments.value(key);
                if (value.isDouble()) return value.toDouble();
                if (value.isString()) {
                    bool ok = false;
                    const double n = value.toString().toDouble(&ok);
                    if (ok) return n;
                }
            }
            return QVariant();
        };

        out["calories"] = readNumber({"energy-kcal_100g", "energy-kcal", "energy_kcal"});
        out["protein"]  = readNumber({"proteins_100g", "protein_100g", "proteins", "protein"});
        out["carbs"]    = readNumber({"carbohydrates_100g", "carbs_100g", "carbohydrates", "carbs"});
        out["fat"]      = readNumber({"fat_100g", "fat"});
        out["sugar"]    = readNumber({"sugars_100g", "sugar_100g", "sugars", "sugar"});
        out["sodium"]   = readNumber({"sodium_100g", "sodium", "salt_100g", "salt"});

        emit productReady(out);
    });
}
