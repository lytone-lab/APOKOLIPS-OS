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
    QString sunrise     = "06:29";    // "HH:mm"
    QString sunset      = "19:54";    // "HH:mm"
    QList<double> hourlyTemps;        // next 12 hours
    QList<int>    hourlyCodes;        // parallel weather codes

    // Multi-day forecast — first entry is today.
    struct Day {
        QString name;   // "Mon", "Tue"
        int     code = -1;
        double  maxC = 21.0;
        double  minC = 15.0;
    };
    QList<Day> daily;

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
            "&daily=temperature_2m_max,temperature_2m_min,"
            "sunrise,sunset,weather_code"
            "&hourly=temperature_2m,weather_code"
            "&forecast_days=6"
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

                    // Multi-day forecast — build the week list.
                    {
                        QJsonArray dTime = day.value("time").toArray();
                        QJsonArray dCode = day.value("weather_code").toArray();
                        int n = qMin(qMin(dTime.size(), mx.size()),
                                     qMin(mn.size(), dCode.size()));
                        for (int i = 0; i < n; ++i) {
                            Snapshot::Day d;
                            QDate dt = QDate::fromString(
                                dTime[i].toString(), "yyyy-MM-dd");
                            d.name = dt.isValid()
                                ? dt.toString("ddd") : QString("---");
                            d.maxC = mx[i].toDouble(21.0);
                            d.minC = mn[i].toDouble(15.0);
                            d.code = dCode[i].toInt(-1);
                            s.daily.append(d);
                        }
                    }
                    // sunrise / sunset come as ISO strings "2026-10-15T06:29"
                    auto tailTime = [](const QString &iso) -> QString {
                        int t = iso.indexOf('T');
                        if (t >= 0 && t + 6 <= iso.size())
                            return iso.mid(t + 1, 5);
                        return iso;
                    };
                    QJsonArray sr = day.value("sunrise").toArray();
                    QJsonArray ss = day.value("sunset").toArray();
                    if (!sr.isEmpty())
                        s.sunrise = tailTime(sr.first().toString());
                    if (!ss.isEmpty())
                        s.sunset  = tailTime(ss.first().toString());
                    // hourly — keep the first 12 slots from now
                    QJsonObject hr = root.value("hourly").toObject();
                    QJsonArray hTemps = hr.value("temperature_2m").toArray();
                    QJsonArray hCodes = hr.value("weather_code").toArray();
                    QJsonArray hTimes = hr.value("time").toArray();
                    // find the index closest to now
                    QString nowIso = QDateTime::currentDateTime()
                        .toString("yyyy-MM-ddTHH:00");
                    int start = 0;
                    for (int i = 0; i < hTimes.size(); ++i) {
                        if (hTimes[i].toString() >= nowIso) {
                            start = i; break;
                        }
                    }
                    for (int i = start; i < hTimes.size()
                                      && s.hourlyTemps.size() < 12; ++i) {
                        s.hourlyTemps.append(
                            hTemps[i].toDouble(21.0));
                        s.hourlyCodes.append(
                            hCodes[i].toInt(-1));
                    }
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
