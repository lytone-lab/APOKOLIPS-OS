#ifndef APOKOLIPS_THEME_H
#define APOKOLIPS_THEME_H

// SysStats, Theme, ThemeManager — moved out of main.cpp (Step 1)

#include "shell.h"

struct Theme {
    QColor chromeBg;      // dark glass background for bars/panels/dock
    QColor chromeBorder;  // subtle border around chrome
    QColor accent;        // main accent color (icons, underlines, glow)
    QColor accentSoft;    // dimmer accent for hover fills
    QColor accentStrong;  // bright accent for pressed states
    QColor textPrimary;
    QColor textSecondary;
    QColor textDim;
    QColor panelBg;       // launcher/notification panels
};

static QString rgba(const QColor &c) {
    return QString("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

class SysStats {
public:
    static double cpuPercent() {
        static quint64 lastIdle = 0, lastTotal = 0;
        QFile f("/proc/stat");
        if (!f.open(QIODevice::ReadOnly)) return 0.0;
        QByteArray line = f.readLine();
        f.close();
        auto parts = line.split(' ');
        quint64 vals[10] = {0};
        int n = 0;
        for (int i = 1; i < parts.size() && n < 10; ++i) {
            if (parts[i].isEmpty()) continue;
            vals[n++] = parts[i].toULongLong();
        }
        quint64 idle  = vals[3] + vals[4];
        quint64 total = 0;
        for (int i = 0; i < 10; ++i) total += vals[i];
        quint64 dIdle  = idle  - lastIdle;
        quint64 dTotal = total - lastTotal;
        lastIdle = idle; lastTotal = total;
        if (dTotal == 0) return 0.0;
        return 100.0 * (1.0 - double(dIdle) / double(dTotal));
    }

    static double ramPercent() {
        QFile f("/proc/meminfo");
        if (!f.open(QIODevice::ReadOnly)) return 0.0;
        QByteArray all = f.readAll();   // works on /proc where atEnd() lies
        f.close();
        quint64 total = 0, avail = 0;
        for (const QByteArray &raw : all.split('\n')) {
            QByteArray line = raw.simplified();
            if (line.isEmpty()) continue;
            QList<QByteArray> toks = line.split(' ');
            if (toks.size() < 2) continue;
            if (toks[0] == "MemTotal:")          total = toks[1].toULongLong();
            else if (toks[0] == "MemAvailable:") avail = toks[1].toULongLong();
        }
        if (total == 0) return 0.0;
        return 100.0 * (1.0 - double(avail) / double(total));
    }
};

class ThemeManager {
public:
    using Callback = std::function<void(const Theme &)>;

    static ThemeManager &instance() {
        static ThemeManager tm;
        return tm;
    }

    const Theme &current() const { return m_theme; }

    void setTheme(const Theme &t) {
        m_theme = t;
        neutralize();
        for (auto &cb : m_callbacks) cb(m_theme);
    }

    // Force every accent-related color to neutral white/grey.
    // Wallpapers may set colored themes; we strip them here so nothing
    // in the UI carries color except traffic lights and app icons.
    void neutralize() {
        // Derive neutral accents from text color so they stay visible on
        // both dark themes (light text) and light themes (dark text).
        QColor b = m_theme.textPrimary;
        auto mk = [&b](int a) {
            return QColor(b.red(), b.green(), b.blue(), a);
        };
        m_theme.chromeBorder = mk(22);
        m_theme.accent       = mk(210);
        m_theme.accentSoft   = mk(40);
        m_theme.accentStrong = mk(80);
    }

    void subscribe(Callback cb) {
        m_callbacks.append(cb);
        cb(m_theme);
    }

    void setBlurredBg(const QPixmap &p) { m_blurredBg = p; }
    const QPixmap &blurredBg() const { return m_blurredBg; }

private:
    Theme m_theme;
    QList<Callback> m_callbacks;
    QPixmap m_blurredBg;
};


// --- Cleanup pass ---
namespace SysState {

inline QString networkKind() {
    // "wifi" | "ethernet" | "offline"
    QProcess p;
    p.start("nmcli", { "-t", "-f", "DEVICE,TYPE,STATE", "device" });
    p.waitForFinished(500);
    QString out = QString::fromUtf8(p.readAllStandardOutput());
    bool ethernet = false, wifi = false, wifiConnected = false;
    for (const QString &line : out.split('\n')) {
        QStringList parts = line.trimmed().split(':');
        if (parts.size() < 3) continue;
        if (parts[1] == "ethernet" && parts[2].startsWith("connected"))
            ethernet = true;
        if (parts[1] == "wifi") {
            wifi = true;
            if (parts[2].startsWith("connected")) wifiConnected = true;
        }
    }
    if (wifi && wifiConnected) return "wifi";
    if (ethernet)              return "ethernet";
    if (wifi)                  return "wifi";
    return "offline";
}

inline bool bluetoothPresent() {
    QDir d("/sys/class/bluetooth");
    if (!d.exists()) return false;
    return !d.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

inline int batteryPercent() {
    QDir d("/sys/class/power_supply");
    if (!d.exists()) return -1;
    for (const QString &entry : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("BAT")) continue;
        QFile cap("/sys/class/power_supply/" + entry + "/capacity");
        if (!cap.open(QIODevice::ReadOnly)) continue;
        return QString(cap.readAll()).trimmed().toInt();
    }
    return -1;
}

inline bool batteryCharging() {
    QDir d("/sys/class/power_supply");
    if (!d.exists()) return false;
    for (const QString &entry : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("BAT")) continue;
        QFile s("/sys/class/power_supply/" + entry + "/status");
        if (!s.open(QIODevice::ReadOnly)) continue;
        QString v = QString(s.readAll()).trimmed();
        return v.compare("Charging", Qt::CaseInsensitive) == 0 ||
               v.compare("Full", Qt::CaseInsensitive) == 0;
    }
    return false;
}

}

#endif // APOKOLIPS_THEME_H
