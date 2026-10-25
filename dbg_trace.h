// dbg_trace.h - tiny file logger, so separate app processes (Finder etc.)
// can be traced. Log: $XDG_RUNTIME_DIR/apokolips-trace.log
#ifndef APOKOLIPS_DBG_TRACE_H
#define APOKOLIPS_DBG_TRACE_H

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

inline void apoTrace(const QString &msg) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = "/tmp";
    const QString path = dir + "/apokolips-trace.log";
    if (QFileInfo(path).size() > 200000) QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f) << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
                    << " pid=" << QCoreApplication::applicationPid()
                    << " " << msg << "\n";
}

#endif // APOKOLIPS_DBG_TRACE_H
