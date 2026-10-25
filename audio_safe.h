// audio_safe.h - bounded, leak-free sound playback.
// Includes ONLY Qt headers (no project headers), so it can be included from
// any file without creating a circular include.
//   - at most ONE player process at a time: a new sound kills the previous one
//   - every player is killed after 2 seconds, no matter what
//   - player output goes to the null device, so nothing can block on a full pipe
#ifndef APOKOLIPS_AUDIO_SAFE_H
#define APOKOLIPS_AUDIO_SAFE_H

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTimer>

inline void playSoundSafe(const QString &path, double volume = 1.0, int minGapMs = 0) {
    static QPointer<QProcess> s_cur;
    static qint64 s_lastMs = 0;
    if (!QCoreApplication::instance()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (minGapMs > 0 && now - s_lastMs < minGapMs) return;
    s_lastMs = now;
    if (!QFile::exists(path)) return;
    if (volume < 0.0) volume = 0.0;
    if (volume > 1.0) volume = 1.0;

    // One player at a time: stop the previous one first.
    if (s_cur) { s_cur->kill(); s_cur.clear(); }

    QString prog;
    QStringList args;
    if (!(prog = QStandardPaths::findExecutable("pw-play")).isEmpty())
        args << QString("--volume=%1").arg(volume, 0, 'f', 2) << path;
    else if (!(prog = QStandardPaths::findExecutable("paplay")).isEmpty())
        args << QString("--volume=%1").arg(int(volume * 65536)) << path;
    else if (!(prog = QStandardPaths::findExecutable("aplay")).isEmpty())
        args << "-q" << path;
    else return;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.value("XDG_RUNTIME_DIR").isEmpty())
        env.insert("XDG_RUNTIME_DIR", "/run/user/1000");

    QProcess *p = new QProcess(QCoreApplication::instance());
    p->setProcessEnvironment(env);
    p->setStandardOutputFile(QProcess::nullDevice());
    p->setStandardErrorFile(QProcess::nullDevice());
    QObject::connect(p, &QProcess::finished, p, &QObject::deleteLater);
    QObject::connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) p->deleteLater();
    });
    QTimer::singleShot(2000, p, [p]() {
        if (p->state() != QProcess::NotRunning) p->kill();
    });
    s_cur = p;
    p->start(prog, args);
}

#endif // APOKOLIPS_AUDIO_SAFE_H
