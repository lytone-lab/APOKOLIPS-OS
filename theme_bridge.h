// theme_bridge.h - external-app theming bridge (GTK + Qt5/Qt6 via qt5ct/qt6ct).
//
//   writeThemeConfigs()      call once at shell startup; safe to repeat
//   themeEnvFor(appId)       "KEY=VALUE" pairs to apply to an external app launch
//   applyThemeEnv(env, id)   applies themeEnvFor() to a QProcessEnvironment
//   themeChromiumFlags()     dark-mode flags for the Chromium/Electron family
//
// Qt config files written here start with the marker line below. A file
// WITHOUT the marker (hand-edited, or saved by qt6ct's own GUI) is never
// overwritten.
#ifndef APOKOLIPS_THEME_BRIDGE_H
#define APOKOLIPS_THEME_BRIDGE_H

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPalette>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QString>
#include <QStringList>

namespace ThemeBridgeDetail {

// ';' marks a comment in the INI files Qt reads.
inline QString marker() {
    return QStringLiteral("; apokolips-managed: delete this line to take over this file");
}

inline bool writeAtomic(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return f.commit();
}

// Write only if the file is missing or still carries our marker.
inline bool writeManaged(const QString &path, const QString &body) {
    QFile old(path);
    if (old.exists()) {
        if (!old.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        const QString cur = QString::fromUtf8(old.readAll());
        old.close();
        if (!cur.contains(marker())) return false;   // user-owned, leave alone
        if (cur == body) return true;                // already up to date
    }
    return writeAtomic(path, body);
}

// Add key=value under [section] only if that key is absent.
// Never changes a key that already exists.
inline bool ensureIniKey(const QString &path, const QString &section,
                         const QString &key, const QString &value) {
    QString text;
    QFile in(path);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        text = QString::fromUtf8(in.readAll());
        in.close();
    }
    QStringList lines = text.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();

    const QString header = "[" + section + "]";
    int secStart = -1, secEnd = int(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (secStart < 0) {
            if (t == header) secStart = i;
        } else if (t.startsWith('[')) {
            secEnd = i;
            break;
        }
    }
    if (secStart < 0) {
        lines << header << (key + "=" + value);
    } else {
        for (int i = secStart + 1; i < secEnd; ++i) {
            const QString t = lines[i].trimmed();
            if (t.startsWith(key + "=") || t.startsWith(key + " =")) return true;
        }
        lines.insert(secEnd, key + "=" + value);
    }
    return writeAtomic(path, lines.join('\n') + "\n");
}

inline QPalette sigilPalette() {
    QPalette p;
    const QColor win(0x14, 0x14, 0x16), fg(0xf0, 0xed, 0xe8),
                 base(0x1e, 0x1e, 0x20), alt(0x18, 0x18, 0x1a),
                 btn(0x26, 0x26, 0x2a), accent(0xa8, 0x32, 0x32),
                 dim(0x7a, 0x78, 0x73);
    p.setColor(QPalette::Window, win);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::BrightText, QColor(Qt::white));
    p.setColor(QPalette::Light, QColor(0x3a, 0x3a, 0x3f));
    p.setColor(QPalette::Midlight, QColor(0x30, 0x30, 0x35));
    p.setColor(QPalette::Dark, QColor(0x0c, 0x0c, 0x0e));
    p.setColor(QPalette::Mid, QColor(0x1c, 0x1c, 0x1f));
    p.setColor(QPalette::Shadow, QColor(Qt::black));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::Link, QColor(0xe0, 0x7a, 0x6f));
    p.setColor(QPalette::LinkVisited, QColor(0xb0, 0x8f, 0xb8));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, fg);
    QColor ph = fg;
    ph.setAlpha(110);
    p.setColor(QPalette::PlaceholderText, ph);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, accent);
#endif
    for (QPalette::ColorRole r : { QPalette::WindowText, QPalette::Text,
                                   QPalette::ButtonText })
        p.setColor(QPalette::Disabled, r, dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x4a, 0x2a, 0x2a));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, dim);
    return p;
}

inline QString paletteLine(const QPalette &p, QPalette::ColorGroup g, int count) {
    QStringList c;
    for (int r = 0; r < count; ++r)
        c << p.color(g, QPalette::ColorRole(r)).name(QColor::HexArgb);
    return c.join(", ");
}

// roleCount must equal the Qt build's QPalette::NColorRoles: 21 for Qt5,
// NColorRoles for Qt6. Otherwise qt5ct/qt6ct silently ignore the palette.
inline QString schemeBody(int roleCount) {
    const QPalette p = sigilPalette();
    return marker() + "\n[ColorScheme]\n"
         + "active_colors="   + paletteLine(p, QPalette::Active,   roleCount) + "\n"
         + "disabled_colors=" + paletteLine(p, QPalette::Disabled, roleCount) + "\n"
         + "inactive_colors=" + paletteLine(p, QPalette::Inactive, roleCount) + "\n";
}

inline QString confBody(const QString &schemePath) {
    return marker() + "\n[Appearance]\n"
         + "color_scheme_path=" + schemePath + "\n"
         + "custom_palette=true\n"
         + "icon_theme=Adwaita\n"
         + "standard_dialogs=default\n"
         + "style=Fusion\n";
}

// VLC: only flip an EXISTING qt-dark-palette line. VLC rewrites vlcrc itself,
// so we never create the file or append lines outside its [qt] section.
inline void ensureVlcDarkPalette() {
    const QString path = QDir::homePath() + "/.config/vlc/vlcrc";
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    static const QRegularExpression re(
        QStringLiteral("^#?\\s*qt-dark-palette=.*$"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch() || m.captured(0) == "qt-dark-palette=1") return;
    QString out = text;
    out.replace(m.capturedStart(0), m.capturedLength(0), "qt-dark-palette=1");
    writeAtomic(path, out);
}

} // namespace ThemeBridgeDetail

// Call once at shell startup. Never overwrites a file you edited.
inline void writeThemeConfigs() {
    using namespace ThemeBridgeDetail;
    const QString cfg = QDir::homePath() + "/.config";

    // GTK: gtk.css files are left alone; just make sure the dark keys exist.
    ensureIniKey(cfg + "/gtk-3.0/settings.ini", "Settings",
                 "gtk-application-prefer-dark-theme", "1");
    ensureIniKey(cfg + "/gtk-4.0/settings.ini", "Settings",
                 "gtk-application-prefer-dark-theme", "1");
    ensureIniKey(cfg + "/gtk-3.0/settings.ini", "Settings",
                 "gtk-theme-name", "Adwaita-dark");

    // Qt5 / Qt6 through qt5ct / qt6ct: Fusion + Sigil palette.
    const QString s5 = cfg + "/qt5ct/colors/sigil.conf";
    const QString s6 = cfg + "/qt6ct/colors/sigil.conf";
    writeManaged(s5, schemeBody(21));
    writeManaged(s6, schemeBody(int(QPalette::NColorRoles)));
    writeManaged(cfg + "/qt5ct/qt5ct.conf", confBody(s5));
    writeManaged(cfg + "/qt6ct/qt6ct.conf", confBody(s6));

    ensureVlcDarkPalette();

    // Same-value writes are no-ops for dconf, so this is cheap on every start.
    QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface",
                                           "color-scheme", "prefer-dark" });
    QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface",
                                           "gtk-theme", "Adwaita-dark" });
}

// "KEY=VALUE" pairs for any external app launch.
inline QStringList themeEnvFor(const QString &appId) {
    // VLC: its dark palette comes from vlcrc and already works, so it
    // inherits the session environment untouched.
    if (appId.toLower().contains("vlc")) return {};
    return {
        "GTK_THEME=Adwaita:dark",
        "QT_QPA_PLATFORMTHEME=qt6ct:qt5ct",
        "QT_STYLE_OVERRIDE=Fusion",
        "QT_AUTO_SCREEN_SCALE_FACTOR=0"
    };
}

inline void applyThemeEnv(QProcessEnvironment &env, const QString &appId) {
    for (const QString &kv : themeEnvFor(appId)) {
        const int i = kv.indexOf('=');
        if (i > 0) env.insert(kv.left(i), kv.mid(i + 1));
    }
}

// Chromium / Electron family: dark mode, merged into any existing
// --enable-features (Chromium honours only the LAST such flag).
inline void themeChromiumFlags(const QString &program, QStringList &args) {
    const QString base = QFileInfo(program).fileName().toLower();
    bool hit = false;
    for (const char *n : { "chromium", "chrome", "brave", "vivaldi",
                           "thorium", "helium", "electron" })
        if (base.contains(QLatin1String(n))) { hit = true; break; }
    if (!hit) return;
    if (!args.contains("--force-dark-mode")) args << "--force-dark-mode";
    int idx = -1;
    for (int i = 0; i < args.size(); ++i)
        if (args[i].startsWith("--enable-features=")) idx = i;
    if (idx < 0) args << "--enable-features=WebUIDarkMode";
    else if (!args[idx].contains("WebUIDarkMode")) args[idx] += ",WebUIDarkMode";
}

#endif // APOKOLIPS_THEME_BRIDGE_H
