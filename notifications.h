#ifndef APOKOLIPS_NOTIFICATIONS_H
#define APOKOLIPS_NOTIFICATIONS_H

// ApoNotify — a system-wide notification service.
// Any shell app can call ApoNotify::send(title, body, app)
// to push a notification into the shell's NotificationCenter.
// The shell gates each incoming notification through the
// user's Focus mode (mute / priority / all) — so a single
// setting silences every app at once.

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

namespace ApoNotify {

inline void send(const QString &app,
                 const QString &title,
                 const QString &body)
{
    SoundManager::playNotification();
    const QString dir = QDir::tempPath() + "/apokolips-notify";
    QDir().mkpath(dir);
    const QString path = dir + "/" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) +
        "-" + QString::number(QCoreApplication::applicationPid()) +
        ".json";
    QJsonObject o;
    o["app"]   = app;
    o["title"] = title;
    o["body"]  = body;
    o["ts"]    = double(QDateTime::currentMSecsSinceEpoch());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(o).toJson());
    f.close();
}

}

#endif
