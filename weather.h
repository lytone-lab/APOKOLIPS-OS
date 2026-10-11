#ifndef APOKOLIPS_WEATHER_H
#define APOKOLIPS_WEATHER_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"

namespace Weather {

struct Snapshot {
    double  tempC       = 21.0;
    double  tempMax     = 27.0;
    double  tempMin     = 17.0;
    double  windKmh     = 7.0;
    int     humidityPct = 68;
    QString condition   = "Partly cloudy";
    QString city        = "Kampala";
    bool    valid       = false;
};

inline QString fromCode(int code) {
    if (code == 0)                return "Clear sky";
    if (code == 1)                return "Mainly clear";
    if (code == 2)                return "Partly cloudy";
    if (code == 3)                return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Rain showers";
    if (code >= 85 && code <= 86) return "Snow showers";
    if (code >= 95)               return "Thunderstorm";
    return "-";
}

class Fetcher : public QObject {
public:
    using Callback = std::function<void(const Snapshot &)>;

    explicit Fetcher(QObject *parent = nullptr) : QObject(parent) {
        m_nam = new QNetworkAccessManager(this);
    }

    void fetch(Callback cb) {
        const double lat = 0.347;
        const double lon = 32.582;
        QString url = QString(
            "https://api.open-meteo.com/v1/forecast"
            "?latitude=%1&longitude=%2"
            "&current=temperature_2m,weather_code,"
            "wind_speed_10m,relative_humidity_2m"
            "&daily=temperature_2m_max,temperature_2m_min"
            "&timezone=auto").arg(lat).arg(lon);

        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_nam->get(req);

        QObject::connect(reply, &QNetworkReply::finished,
                         [reply, cb]() {
            Snapshot s;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (doc.isObject()) {
                    QJsonObject root = doc.object();
                    QJsonObject cur = root.value("current").toObject();
                    QJsonObject day = root.value("daily").toObject();
                    s.tempC       = cur.value("temperature_2m").toDouble(21.0);
                    s.windKmh     = cur.value("wind_speed_10m").toDouble(7.0);
                    s.humidityPct = cur.value("relative_humidity_2m").toInt(68);
                    s.condition   = fromCode(cur.value("weather_code").toInt(-1));
                    QJsonArray mx = day.value("temperature_2m_max").toArray();
                    QJsonArray mn = day.value("temperature_2m_min").toArray();
                    if (!mx.isEmpty()) s.tempMax = mx.first().toDouble(27.0);
                    if (!mn.isEmpty()) s.tempMin = mn.first().toDouble(17.0);
                    s.valid = true;
                }
            }
            reply->deleteLater();
            cb(s);
        });
    }

private:
    QNetworkAccessManager *m_nam = nullptr;
};

}

#endif // APOKOLIPS_WEATHER_H
