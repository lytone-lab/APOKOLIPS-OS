#ifndef APOKOLIPS_RECENTS_H
#define APOKOLIPS_RECENTS_H

// Recents — shared recent-files store, read/written by every app.
// Type-aware: each entry carries the MIME, so apps can ask for
// "recent images only", "recent notes", etc.
//
// Storage: ~/.local/share/apokolips/recents.json
// Format:  {"entries": [{"path","mime","app","ts"}...]}
// Cap:     200 entries on disk (older dropped).
//
// NOTE: read-modify-write without a lock. Two apps opening files
// in the same millisecond could lose one entry. Acceptable for
// v1; upgrade path is a QLocalServer or flock when it matters.

#include <QString>
#include <QStringList>
#include <QList>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QStandardPaths>
#include <QMimeDatabase>
#include <QProcess>

namespace Recents {

struct Entry {
    QString path;
    QString mime;
    QString app;   // "apokolips-files", "apokolips-notes", etc.
    qint64  ts = 0;   // ms since epoch
};

inline QString storePath() {
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::GenericDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.local/share";
    QDir().mkpath(dir + "/apokolips");
    return dir + "/apokolips/recents.json";
}

// Bucket each MIME belongs to. Apps filter on the bucket name.
inline QString bucketForMime(const QString &mime) {
    if (mime.startsWith("image/"))  return "image";
    if (mime.startsWith("video/"))  return "video";
    if (mime.startsWith("audio/"))  return "audio";
    if (mime == "inode/directory")  return "folder";
    if (mime.startsWith("text/"))   return "document";
    if (mime == "application/pdf")  return "document";
    if (mime.startsWith("application/")
        && (mime.contains("zip") || mime.contains("tar")
            || mime.contains("compressed")
            || mime.contains("rar") || mime.contains("7z")))
        return "archive";
    return "other";
}

inline QList<Entry> loadAll() {
    QList<Entry> out;
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!d.isObject()) return out;
    QJsonArray arr = d.object().value("entries").toArray();
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        Entry e;
        e.path = o.value("path").toString();
        e.mime = o.value("mime").toString();
        e.app  = o.value("app").toString();
        e.ts   = (qint64)o.value("ts").toDouble();
        if (!e.path.isEmpty()) out.append(e);
    }
    return out;
}

inline void saveAll(const QList<Entry> &entries) {
    QJsonArray arr;
    for (const Entry &e : entries) {
        QJsonObject o;
        o["path"] = e.path;
        o["mime"] = e.mime;
        o["app"]  = e.app;
        o["ts"]   = double(e.ts);
        arr.append(o);
    }
    QJsonObject root;
    root["entries"] = arr;
    QFile f(storePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson());
}

// Add or refresh an entry. Same path + app moves to front.
inline void add(const QString &path, const QString &mime,
                const QString &app) {
    if (path.isEmpty()) return;
    QList<Entry> all = loadAll();
    for (int i = all.size() - 1; i >= 0; --i) {
        if (all[i].path == path && all[i].app == app)
            all.removeAt(i);
    }
    Entry e;
    e.path = path;
    e.mime = mime;
    e.app  = app;
    e.ts   = QDateTime::currentMSecsSinceEpoch();
    all.prepend(e);
    while (all.size() > 200) all.removeLast();
    saveAll(all);
}

inline QList<Entry> list(int max = 15) {
    QList<Entry> all = loadAll();
    if (all.size() > max) all = all.mid(0, max);
    return all;
}

inline QList<Entry> listByBucket(const QString &bucket,
                                 int max = 10) {
    QList<Entry> out;
    for (const Entry &e : loadAll()) {
        if (bucketForMime(e.mime) == bucket) {
            out.append(e);
            if (out.size() >= max) break;
        }
    }
    return out;
}

inline void clear() {
    QFile::remove(storePath());
}

// Convenience: log the open, then xdg-open. Every app should use
// this instead of a raw QProcess::startDetached("xdg-open", ...)
// so the recents list stays in sync. Pass the app's sway app_id.
inline void open(const QString &path, const QString &app) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;
    QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    add(path, mime, app);
    QProcess::startDetached("xdg-open", { path });
}

}

#endif
