#ifndef APOKOLIPS_OPENWITH_H
#define APOKOLIPS_OPENWITH_H

// OpenWith — a shared "Open With…" picker for every app.
// Scans .desktop files, filters by the target file's MIME, shows
// the matched apps in a Sigil-styled menu. Falls back to xdg-open
// if nothing matches.
//
// Also logs every launch into the Recents store so the omega
// menu's Recent Items fills from any open path.

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QStringList>
#include <QRegularExpression>
#include <QMimeDatabase>
#include <QProcess>
#include <QWidget>
#include <QCursor>
#include "recents.h"

namespace OpenWith {

struct AppEntry {
    QString desktopFile;   // "apokolips-aperture.desktop"
    QString fullPath;      // absolute path
    QString label;         // "Aperture"
    QString exec;          // "apokolips-shell --image %U"
};

inline QList<AppEntry> candidates(const QString &mime) {
    QList<AppEntry> out;
    const QStringList dirs = {
        QDir::homePath() + "/.local/share/applications",
        "/usr/local/share/applications",
        "/usr/share/applications"
    };
    QSet<QString> seen;
    for (const QString &d : dirs) {
        QDir dir(d);
        if (!dir.exists()) continue;
        for (const QString &f :
             dir.entryList({ "*.desktop" }, QDir::Files)) {
            if (seen.contains(f)) continue;
            seen.insert(f);

            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly)) continue;
            QString text = QString::fromUtf8(file.readAll());
            file.close();

            QString label, exec, mimes;
            bool nodisplay = false;
            for (const QString &line : text.split('\n')) {
                QString t = line.trimmed();
                if (t.startsWith("Name=") && label.isEmpty())
                    label = t.mid(5).trimmed();
                else if (t.startsWith("Exec=") && exec.isEmpty())
                    exec  = t.mid(5).trimmed();
                else if (t.startsWith("MimeType="))
                    mimes = t.mid(9).trimmed();
                else if (t == "NoDisplay=true" || t == "Hidden=true")
                    nodisplay = true;
            }
            if (nodisplay || label.isEmpty() || exec.isEmpty())
                continue;

            const QStringList mimeList =
                mimes.split(';', Qt::SkipEmptyParts);
            if (!mimeList.contains(mime)) continue;

            AppEntry e;
            e.desktopFile = f;
            e.fullPath    = dir.filePath(f);
            e.label       = label;
            e.exec        = exec;
            out.append(e);
        }
    }
    return out;
}

// Strip field codes (%U %u %F %f %d %D %n %N %i %c %k) then
// append the file path as an argument. Good enough for single
// files — multi-file %F launches drop the extras.
inline void launchExec(const QString &exec, const QString &path) {
    QString cmd = exec;
    cmd.replace(QRegularExpression("%[UuFfDdNnIicCk]"), " ");
    cmd = cmd.simplified();
    QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    QString prog = parts.takeFirst();
    parts << path;
    QProcess::startDetached(prog, parts);
}

inline QString currentDefault(const QString &mime) {
    QProcess p;
    p.start("xdg-mime", { "query", "default", mime });
    if (!p.waitForFinished(500)) return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// Show the picker next to the cursor. Logs the launch into
// Recents so the omega menu stays fresh regardless of which app
// picks up the file.
inline void show(QWidget *parent, const QString &path,
                 const QString &recentsApp = QString()) {
    QFileInfo fi(path);
    if (!fi.exists()) return;

    const QString mime = QMimeDatabase()
                             .mimeTypeForFile(path).name();
    QList<AppEntry> apps = candidates(mime);
    const QString current = currentDefault(mime);

    ApoMenu m(parent);

    QLabel *hdr = new QLabel(fi.fileName());
    hdr->setStyleSheet(
        "QLabel { color: rgb(150, 150, 155);"
        "  font-size: 11px; padding: 6px 12px;"
        "  background: transparent; }");
    QWidgetAction *hdrAct = new QWidgetAction(&m);
    hdrAct->setDefaultWidget(hdr);
    m.addAction(hdrAct);
    m.addSeparator();

    if (apps.isEmpty()) {
        QAction *a = m.addAction("(no apps registered)");
        a->setEnabled(false);
    } else {
        for (const AppEntry &e : apps) {
            QString label = e.label;
            if (e.desktopFile == current)
                label += QString::fromUtf8("  \xE2\x9C\x93");
            QAction *a = m.addAction(label);
            QString exec = e.exec;
            QObject::connect(a, &QAction::triggered,
                [exec, path, recentsApp]() {
                    OpenWith::launchExec(exec, path);
                    if (!recentsApp.isEmpty())
                        Recents::add(path,
                            QMimeDatabase().mimeTypeForFile(path).name(),
                            recentsApp);
            });
        }
    }

    m.addSeparator();
    QAction *aOpenDefault = m.addAction("Open with System Default");
    QObject::connect(aOpenDefault, &QAction::triggered,
        [path, recentsApp]() {
            QProcess::startDetached("xdg-open", { path });
            if (!recentsApp.isEmpty())
                Recents::add(path,
                    QMimeDatabase().mimeTypeForFile(path).name(),
                    recentsApp);
    });

    m.exec(QCursor::pos());
}

}

#endif
