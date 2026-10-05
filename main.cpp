#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QScreen>
#include <QPixmap>
#include <QProcess>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWindow>
#include <QMenu>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QListView>
#include <QFileSystemModel>
#include <QStackedWidget>
#include <unistd.h>
#include <QShortcut>
#include <QInputDialog>
#include <QRegularExpression>
#include <QListWidget>
#include <csignal>
#include <QUrl>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <QDialog>
#include <QWidgetAction>
#include <QDate>
#include <QCalendarWidget>
#include <QList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFileSystemWatcher>
#include <functional>
#include <cmath>

// =========================================================
// Theme — colors that chrome widgets adapt to
// =========================================================
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

// =========================================================
// System stats (reads /proc on Linux; returns 0 elsewhere)
// =========================================================
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

// =========================================================
// VisualStyle — glass / neu / skeuo / clay morphism
// =========================================================
enum class VisualStyle {
    Glass        = 0,
    Neumorphism  = 1,
    Skeuomorphism= 2,
    Claymorphism = 3
};

namespace StyleManager {

inline VisualStyle &current() {
    static VisualStyle s = VisualStyle::Glass;
    return s;
}

inline QList<std::function<void(VisualStyle)>> &callbacks() {
    static QList<std::function<void(VisualStyle)>> cbs;
    return cbs;
}

inline QString path() {
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::ConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
    return dir + "/apokolips/style.json";
}

inline void load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    int n = doc.object().value("style").toInt(0);
    if (n < 0 || n > 3) n = 0;
    current() = static_cast<VisualStyle>(n);
}

inline void save() {
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QJsonObject o;
    o["style"] = static_cast<int>(current());
    QFile f(path());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson());
}

inline void set(VisualStyle s) {
    current() = s;
    save();
    for (auto &cb : callbacks()) cb(s);
}

inline void subscribe(std::function<void(VisualStyle)> cb) {
    callbacks().append(cb);
    cb(current());
}

inline QTimer *startPolling(QObject *parent, std::function<void()> onChange) {
    QTimer *timer = new QTimer(parent);
    timer->setInterval(400);
    int *last = new int(static_cast<int>(current()));
    QObject::connect(timer, &QTimer::timeout, [last, onChange]() {
        QFile f(path());
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return;
        int n = doc.object().value("style").toInt(*last);
        if (n < 0 || n > 3) n = 0;
        if (n == *last) return;
        *last = n;
        current() = static_cast<VisualStyle>(n);
        onChange();
    });
    timer->start();
    return timer;
}

inline QString name(VisualStyle s) {
    switch (s) {
        case VisualStyle::Glass:         return "Glass";
        case VisualStyle::Neumorphism:   return "Neumorphism";
        case VisualStyle::Skeuomorphism: return "Skeuomorphism";
        case VisualStyle::Claymorphism:  return "Claymorphism";
    }
    return "Glass";
}

} // namespace StyleManager

// =========================================================
// StyleRenderer — one place that knows how each style paints
// =========================================================
namespace StyleRenderer {

// Draw the background of a card / panel / window.
// 'alphaHint' is the widget opacity preference; each style interprets it.
inline void drawPanel(QPainter &p, const QRectF &r, qreal radius,
                      const Theme &t, int alphaHint)
{
    VisualStyle s = StyleManager::current();
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);

    switch (s) {
    case VisualStyle::Glass: {
        // Translucent + faint border
        QColor top = t.panelBg.lighter(118);
        QColor bot = t.panelBg.darker(135);
        top.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bot);
        p.fillPath(path, g);

        QColor edge = t.chromeBorder;
        p.setPen(QPen(edge, 1));
        p.drawPath(path);
        break;
    }

    case VisualStyle::Neumorphism: {
        // Same surface color as background, extruded via paired shadows.
        QColor base(50, 50, 60, alphaHint);
        p.fillPath(path, base);

        // Light from top-left, shadow bottom-right
        p.setPen(Qt::NoPen);
        for (int i = 4; i >= 1; --i) {
            QColor light(255, 255, 255, 10);
            p.setBrush(light);
            p.drawRoundedRect(r.adjusted(-i, -i, -i + 1, -i + 1),
                              radius + i, radius + i);
            QColor dark(0, 0, 0, 14);
            p.setBrush(dark);
            p.drawRoundedRect(r.adjusted(i - 1, i - 1, i, i),
                              radius + i, radius + i);
        }
        break;
    }

    case VisualStyle::Skeuomorphism: {
        // Vertical gradient that reads as polished metal/plastic.
        QColor top = t.panelBg.lighter(145);
        QColor mid = t.panelBg;
        QColor bot = t.panelBg.darker(160);
        top.setAlpha(alphaHint);
        mid.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, top);
        g.setColorAt(0.5, mid);
        g.setColorAt(1.0, bot);
        p.fillPath(path, g);

        // Glossy highlight — confined to top ~22%, softer
        QColor glossTop(255, 255, 255, 55);
        QColor glossBot(255, 255, 255, 0);
        qreal glossH = r.height() * 0.22;
        QLinearGradient glossGrad(r.topLeft() + QPointF(0, 2),
                                  r.topLeft() + QPointF(0, glossH));
        glossGrad.setColorAt(0.0, glossTop);
        glossGrad.setColorAt(1.0, glossBot);
        QPainterPath gl;
        gl.addRoundedRect(QRectF(r.left() + 2, r.top() + 2,
                                 r.width() - 4, glossH),
                          radius - 1, radius - 1);
        p.fillPath(gl, glossGrad);

        // Metallic bezel
        QColor bezel = t.textPrimary;
        bezel.setAlpha(90);
        p.setPen(QPen(bezel, 2));
        p.drawPath(path);

        // Inner thin light line for a beveled look
        QColor inner(255, 255, 255, 40);
        p.setPen(QPen(inner, 1));
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2),
                          radius - 1, radius - 1);
        break;
    }

    case VisualStyle::Claymorphism: {
        // Puffy pastel surface, big radius, soft everything.
        QColor top = t.panelBg.lighter(160);
        QColor bot = t.panelBg.lighter(115);
        top.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bot);
        QPainterPath clay;
        clay.addRoundedRect(r, radius + 6, radius + 6);
        p.fillPath(clay, g);

        // Double outer shadow — light top-left, dark bottom-right, bigger
        p.setPen(Qt::NoPen);
        for (int i = 6; i >= 1; --i) {
            QColor dark(0, 0, 0, 8);
            p.setBrush(dark);
            p.drawRoundedRect(r.adjusted(i, i + 2, i + 2, i + 3),
                              radius + 6 + i, radius + 6 + i);
        }
        break;
    }
    }
}

} // namespace StyleRenderer

// =========================================================
// VisualConfig — every chrome tuning knob. Settings app targets this.
// =========================================================
struct VisualConfig {
    double blurStrength       = 0.25;   // 0=none, 1=heavy
    int    topBarAlpha        = 110;
    int    dockAlpha          = 130;
    int    launcherAlpha      = 165;
    int    controlCenterAlpha = 165;
    int    widgetCardAlpha    = 140;
    int    finderAlpha        = 110;
    bool   widgetsVisible     = true;
    int    blurRefreshMs      = 240;
    double dockMagnifyMax     = 0.42;
    double dockSigma          = 55.0;
};

class VisualConfigManager {
public:
    static VisualConfigManager &instance() {
        static VisualConfigManager v;
        v.load();
        return v;
    }
    using Callback = std::function<void(const VisualConfig &)>;

    VisualConfig &cfg() { return m_cfg; }
    const VisualConfig &cfg() const { return m_cfg; }

    void subscribe(Callback cb) {
        m_callbacks.append(cb);
        cb(m_cfg);
    }

    template<typename F>
    void setAndSave(F mutator) {
        mutator(m_cfg);
        save();
        for (auto &cb : m_callbacks) cb(m_cfg);
    }

    void load() {
        QFile f(path());
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) return;
        QJsonObject o = doc.object();
        auto getD = [&](const char *k, double d) { return o.contains(k) ? o[k].toDouble() : d; };
        auto getI = [&](const char *k, int d)    { return o.contains(k) ? o[k].toInt()    : d; };
        m_cfg.blurStrength       = getD("blurStrength",       m_cfg.blurStrength);
        m_cfg.topBarAlpha        = getI("topBarAlpha",        m_cfg.topBarAlpha);
        m_cfg.dockAlpha          = getI("dockAlpha",          m_cfg.dockAlpha);
        m_cfg.launcherAlpha      = getI("launcherAlpha",      m_cfg.launcherAlpha);
        m_cfg.controlCenterAlpha = getI("controlCenterAlpha", m_cfg.controlCenterAlpha);
        m_cfg.widgetCardAlpha    = getI("widgetCardAlpha",    m_cfg.widgetCardAlpha);
        m_cfg.finderAlpha        = getI("finderAlpha",        m_cfg.finderAlpha);
        m_cfg.widgetsVisible     = o.contains("widgetsVisible")
                                   ? o["widgetsVisible"].toBool(true)
                                   : m_cfg.widgetsVisible;
        m_cfg.blurRefreshMs      = getI("blurRefreshMs",      m_cfg.blurRefreshMs);
        m_cfg.dockMagnifyMax     = getD("dockMagnifyMax",     m_cfg.dockMagnifyMax);
        m_cfg.dockSigma          = getD("dockSigma",          m_cfg.dockSigma);
        clampToSafeRanges();
    }

    void clampToSafeRanges() {
        if (m_cfg.topBarAlpha        < 30)  m_cfg.topBarAlpha        = 30;
        if (m_cfg.dockAlpha          < 30)  m_cfg.dockAlpha          = 30;
        if (m_cfg.launcherAlpha      < 60)  m_cfg.launcherAlpha      = 60;
        if (m_cfg.controlCenterAlpha < 60)  m_cfg.controlCenterAlpha = 60;
        if (m_cfg.widgetCardAlpha    < 40)  m_cfg.widgetCardAlpha    = 40;
        if (m_cfg.finderAlpha        < 30)  m_cfg.finderAlpha        = 30;
        if (m_cfg.dockMagnifyMax     < 0.05) m_cfg.dockMagnifyMax    = 0.05;
        if (m_cfg.dockSigma          < 15)  m_cfg.dockSigma          = 15;
    }

    void resetToDefaults() {
        m_cfg = VisualConfig();
        save();
    }

    void save() {
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QJsonObject o;
        o["blurStrength"]       = m_cfg.blurStrength;
        o["topBarAlpha"]        = m_cfg.topBarAlpha;
        o["dockAlpha"]          = m_cfg.dockAlpha;
        o["launcherAlpha"]      = m_cfg.launcherAlpha;
        o["controlCenterAlpha"] = m_cfg.controlCenterAlpha;
        o["widgetCardAlpha"]    = m_cfg.widgetCardAlpha;
        o["finderAlpha"]        = m_cfg.finderAlpha;
        o["widgetsVisible"]     = m_cfg.widgetsVisible;
        o["blurRefreshMs"]      = m_cfg.blurRefreshMs;
        o["dockMagnifyMax"]     = m_cfg.dockMagnifyMax;
        o["dockSigma"]          = m_cfg.dockSigma;
        QFile f(path());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
        f.close();
    }

    qint64 lastModified() const {
        return QFileInfo(path()).lastModified().toMSecsSinceEpoch();
    }

private:
    static QString path() {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::ConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
        return dir + "/apokolips/visual.json";
    }
    VisualConfig m_cfg;
    QList<Callback> m_callbacks;
};

// =========================================================
// ThemeManager — notifies chrome widgets on theme change
// =========================================================
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

// =========================================================
// Wallpaper base
// =========================================================
class Wallpaper {
public:
    virtual ~Wallpaper() {}
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Theme theme() const = 0;
    virtual void paint(QPainter &p, const QRect &r) = 0;
    virtual void tick() {}
    virtual bool animated() const { return false; }
};

// =========================================================
// Ruby
// =========================================================
class RubyWallpaper : public Wallpaper {
public:
    QString id() const override { return "ruby"; }
    QString displayName() const override { return "Ruby"; }

    Theme theme() const override {
        Theme t;
        t.chromeBg      = QColor(18, 6, 14, 205);
        t.chromeBorder  = QColor(230, 70, 110, 170);
        t.accent        = QColor(255, 90, 130);
        t.accentSoft    = QColor(255, 90, 130, 90);
        t.accentStrong  = QColor(255, 60, 110, 160);
        t.textPrimary   = QColor(255, 225, 232);
        t.textSecondary = QColor(232, 180, 195);
        t.textDim       = QColor(176, 112, 128);
        t.panelBg       = QColor(14, 6, 12, 240);
        return t;
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient base(0, 0, 0, r.height());
        base.setColorAt(0.0, QColor(26, 6, 14));
        base.setColorAt(0.55, QColor(58, 12, 28));
        base.setColorAt(1.0, QColor(12, 2, 6));
        p.fillRect(r, base);

        QRadialGradient glow(r.width() * 0.78, r.height() * 0.14,
                             r.width() * 0.55);
        glow.setColorAt(0.0, QColor(210, 40, 80, 130));
        glow.setColorAt(0.4, QColor(150, 20, 60, 60));
        glow.setColorAt(1.0, QColor(80, 10, 30, 0));
        p.fillRect(r, glow);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 220, 230, 60));
        for (int i = 0; i < 60; ++i) {
            int sx = (i * 137) % r.width();
            int sy = (i * 89) % (r.height() / 2);
            p.drawEllipse(QPoint(sx, sy), 1, 1);
        }
    }
};

// =========================================================
// Starfield
// =========================================================
class StarfieldWallpaper : public Wallpaper {
public:
    StarfieldWallpaper() {
        for (int i = 0; i < 120; ++i) {
            Star s;
            s.x  = (i * 137 + (i * i * 31) % 500) % 1000 / 1000.0;
            s.y  = (i * 89  + (i * i * 17) % 500) % 1000 / 1000.0;
            s.depth = ((i * 53) % 100) / 100.0;
            s.size  = 0.5 + s.depth * 1.8;
            s.alpha = 40 + int(s.depth * 180);
            m_stars.append(s);
        }
    }

    QString id() const override { return "starfield"; }
    QString displayName() const override { return "Starfield"; }
    bool animated() const override { return true; }

    Theme theme() const override {
        Theme t;
        t.chromeBg      = QColor(10, 14, 30, 205);
        t.chromeBorder  = QColor(100, 150, 230, 160);
        t.accent        = QColor(120, 180, 255);
        t.accentSoft    = QColor(120, 180, 255, 90);
        t.accentStrong  = QColor(120, 180, 255, 160);
        t.textPrimary   = QColor(220, 230, 245);
        t.textSecondary = QColor(160, 184, 216);
        t.textDim       = QColor(96, 128, 168);
        t.panelBg       = QColor(8, 12, 26, 240);
        return t;
    }

    void tick() override {
        for (Star &s : m_stars) {
            s.x -= 0.00025 + s.depth * 0.0009;
            if (s.x < 0.0) s.x += 1.0;
        }
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::Antialiasing);

        QLinearGradient base(0, 0, 0, r.height());
        base.setColorAt(0.0, QColor(8, 6, 20));
        base.setColorAt(0.5, QColor(14, 10, 30));
        base.setColorAt(1.0, QColor(4, 2, 10));
        p.fillRect(r, base);

        QRadialGradient glow(r.width() * 0.25, r.height() * 0.7,
                             r.width() * 0.6);
        glow.setColorAt(0.0, QColor(60, 40, 120, 60));
        glow.setColorAt(0.5, QColor(40, 30, 90, 30));
        glow.setColorAt(1.0, QColor(20, 8, 40, 0));
        p.fillRect(r, glow);

        p.setPen(Qt::NoPen);
        for (const Star &s : m_stars) {
            int sx = int(s.x * r.width());
            int sy = int(s.y * r.height());
            QColor c = (s.depth > 0.7) ? QColor(200, 220, 255, s.alpha)
                                       : QColor(230, 230, 255, s.alpha);
            p.setBrush(c);
            p.drawEllipse(QPoint(sx, sy), int(s.size), int(s.size));
        }
    }

private:
    struct Star { double x, y, depth, size; int alpha; };
    QList<Star> m_stars;
};

// =========================================================
// Aurora
// =========================================================
class AuroraWallpaper : public Wallpaper {
public:
    QString id() const override { return "aurora"; }
    QString displayName() const override { return "Aurora"; }
    bool animated() const override { return true; }

    Theme theme() const override {
        Theme t;
        t.chromeBg      = QColor(8, 20, 24, 205);
        t.chromeBorder  = QColor(80, 200, 180, 160);
        t.accent        = QColor(100, 230, 200);
        t.accentSoft    = QColor(100, 230, 200, 90);
        t.accentStrong  = QColor(100, 230, 200, 160);
        t.textPrimary   = QColor(216, 240, 232);
        t.textSecondary = QColor(150, 200, 188);
        t.textDim       = QColor(90, 136, 120);
        t.panelBg       = QColor(6, 16, 20, 240);
        return t;
    }

    void tick() override {
        m_t += 0.006;
        if (m_t > 6.2831853) m_t -= 6.2831853;
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::Antialiasing);

        QLinearGradient base(0, 0, 0, r.height());
        base.setColorAt(0.0, QColor(6, 8, 20));
        base.setColorAt(0.5, QColor(10, 14, 32));
        base.setColorAt(1.0, QColor(4, 6, 16));
        p.fillRect(r, base);

        const struct {
            double phaseOff, speed, yCenter, amplitude;
            QColor top, bot;
        } bands[] = {
            { 0.0,  1.0, 0.45, 0.10, QColor( 40, 220, 180, 130),
                                     QColor( 40, 220, 180,   0) },
            { 2.1, -0.7, 0.55, 0.13, QColor(130, 100, 240, 110),
                                     QColor(130, 100, 240,   0) },
            { 4.4,  0.5, 0.62, 0.16, QColor( 40, 200, 255, 100),
                                     QColor( 40, 200, 255,   0) }
        };

        for (const auto &b : bands) {
            double phase = m_t * b.speed + b.phaseOff;

            QPainterPath path;
            const int N = 64;
            for (int i = 0; i <= N; ++i) {
                double t = double(i) / N;
                double x = t * r.width();
                double wave = std::sin(t * 6.0 + phase) * 0.6
                            + std::sin(t * 3.0 - phase * 0.7) * 0.4;
                double yTop = r.height() * (b.yCenter + wave * b.amplitude);
                double yBot = yTop + r.height() * 0.09;
                if (i == 0) path.moveTo(x, yTop);
                else        path.lineTo(x, yTop);
                Q_UNUSED(yBot);
            }
            for (int i = N; i >= 0; --i) {
                double t = double(i) / N;
                double x = t * r.width();
                double wave = std::sin(t * 6.0 + phase) * 0.6
                            + std::sin(t * 3.0 - phase * 0.7) * 0.4;
                double yTop = r.height() * (b.yCenter + wave * b.amplitude);
                double yBot = yTop + r.height() * 0.09;
                path.lineTo(x, yBot);
            }
            path.closeSubpath();

            QLinearGradient g(0, 0, 0, r.height());
            g.setColorAt(qMax(0.0, b.yCenter - 0.15), b.top);
            g.setColorAt(qMin(1.0, b.yCenter + 0.25), b.bot);
            p.setBrush(g);
            p.setPen(Qt::NoPen);
            p.drawPath(path);
        }

        p.setBrush(QColor(255, 255, 255, 90));
        for (int i = 0; i < 50; ++i) {
            int sx = (i * 197) % r.width();
            int sy = (i * 113) % (r.height() / 3);
            p.drawEllipse(QPoint(sx, sy), 1, 1);
        }
    }

private:
    double m_t = 0.0;
};

// =========================================================
// Golden Gate — dark navy with sweeping light-blue arcs
// =========================================================
class GoldenGateWallpaper : public Wallpaper {
public:
    GoldenGateWallpaper() {
        // Load once — scaled to a reasonable cache size.
        QPixmap raw;
        QStringList candidates = {
            QDir::homePath() + "/apokolips-shell/macos-golden-gate-dark.jpg",
            "/home/lytone/apokolips-shell/macos-golden-gate-dark.jpg",
            "macos-golden-gate-dark.jpg",
            "/usr/share/apokolips/wallpapers/macos-golden-gate-dark.jpg"
        };
        for (const QString &p : candidates) {
            if (QFile::exists(p) && raw.load(p)) break;
        }
        m_source = raw;
    }

    QString id() const override { return "goldengate"; }
    QString displayName() const override { return "Golden Gate"; }

    Theme theme() const override {
        // Tuned for glass over a photo — very translucent chrome,
        // cool cyan accents matching the wallpaper arcs.
        Theme t;
        t.chromeBg      = QColor(28, 28, 34);
        t.chromeBorder  = QColor(140, 190, 255, 130);
        t.accent        = QColor(140, 200, 255);
        t.accentSoft    = QColor(140, 200, 255, 70);
        t.accentStrong  = QColor(140, 200, 255, 140);
        t.textPrimary   = QColor(230, 238, 248);
        t.textSecondary = QColor(180, 198, 222);
        t.textDim       = QColor(120, 145, 178);
        t.panelBg       = QColor(38, 38, 44);
        return t;
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);

        if (m_source.isNull()) {
            // Fallback: simple navy gradient
            QLinearGradient g(0, 0, 0, r.height());
            g.setColorAt(0.0, QColor(20, 30, 52));
            g.setColorAt(1.0, QColor(6, 10, 20));
            p.fillRect(r, g);
            return;
        }

        // Cache scaled pixmap — only re-scale when target size changes
        if (m_cached.isNull() || m_cachedSize != r.size()) {
            // Scale: fill (cover) preserving aspect ratio, then crop
            QPixmap scaled = m_source.scaled(r.size(),
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation);
            int sx = (scaled.width()  - r.width())  / 2;
            int sy = (scaled.height() - r.height()) / 2;
            m_cached = scaled.copy(sx, sy, r.width(), r.height());
            m_cachedSize = r.size();
        }

        p.drawPixmap(r.topLeft(), m_cached);

        // Subtle darkening for text legibility
        p.fillRect(r, QColor(0, 0, 0, 30));

        // Soft blue glow top-right (matches the wallpaper arcs)
        QRadialGradient glow(r.width() * 0.85, r.height() * 0.12,
                             r.width() * 0.55);
        glow.setColorAt(0.0, QColor(120, 180, 255, 40));
        glow.setColorAt(1.0, QColor(80, 40, 130, 0));
        p.fillRect(r, glow);
    }

private:
    QPixmap m_source;
    QPixmap m_cached;
    QSize   m_cachedSize;
};

// =========================================================
// Everest — animated dawn mountain scene (light wallpaper)
// =========================================================
class EverestWallpaper : public Wallpaper {
public:
    EverestWallpaper() { m_phase = 0.0; }

    QString id() const override { return "everest"; }
    QString displayName() const override { return "Everest"; }
    bool animated() const override { return true; }

    Theme theme() const override {
        // Light theme — dark text, warm white glass, neutral dark accents
        Theme t;
        t.chromeBg      = QColor(252, 250, 246, 135);
        t.chromeBorder  = QColor(80, 84, 96, 40);
        t.accent        = QColor(60, 68, 82);
        t.accentSoft    = QColor(60, 68, 82, 55);
        t.accentStrong  = QColor(60, 68, 82, 120);
        t.textPrimary   = QColor(30, 34, 44);
        t.textSecondary = QColor(72, 80, 96);
        t.textDim       = QColor(130, 138, 152);
        t.panelBg       = QColor(250, 248, 246);
        return t;
    }

    void tick() override {
        m_phase += 0.0035;
        if (m_phase > 6.2831853) m_phase -= 6.2831853;
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // ---- Dawn sky ----
        QLinearGradient sky(0, 0, 0, r.height());
        sky.setColorAt(0.00, QColor(196, 214, 232));
        sky.setColorAt(0.30, QColor(232, 218, 202));
        sky.setColorAt(0.52, QColor(246, 206, 172));
        sky.setColorAt(0.68, QColor(238, 180, 142));
        sky.setColorAt(1.00, QColor(198, 148, 118));
        p.fillRect(r, sky);

        // ---- Sun + halo ----
        double sunX = r.width()  * (0.70 + std::sin(m_phase) * 0.008);
        double sunY = r.height() * (0.28 + std::sin(m_phase * 1.3) * 0.006);

        QRadialGradient halo(sunX, sunY, r.width() * 0.32);
        halo.setColorAt(0.00, QColor(255, 242, 205, 145));
        halo.setColorAt(0.45, QColor(255, 224, 178, 55));
        halo.setColorAt(1.00, QColor(255, 210, 160, 0));
        p.fillRect(r, halo);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 246, 224, 235));
        p.drawEllipse(QPointF(sunX, sunY),
                      r.width() * 0.036, r.width() * 0.036);
        p.setBrush(QColor(255, 253, 242, 255));
        p.drawEllipse(QPointF(sunX, sunY),
                      r.width() * 0.022, r.width() * 0.022);

        // ---- Mountain layers (far → near) ----
        struct Layer { double baseY; QColor col; QColor snow; double haze; };
        const Layer layers[] = {
            { 0.68, QColor(198, 208, 224), QColor(248, 250, 253), 0.55 },
            { 0.75, QColor(158, 172, 194), QColor(244, 248, 252), 0.32 },
            { 0.82, QColor(118, 136, 164), QColor(238, 244, 250), 0.14 },
            { 0.90, QColor( 72,  92, 128), QColor(230, 236, 246), 0.00 },
        };
        for (int i = 0; i < 4; ++i)
            drawRidge(p, r, layers[i].baseY, layers[i].col,
                      layers[i].snow, layers[i].haze, i);

        // Gentle warm wash from sun position
        QRadialGradient wash(sunX, sunY, r.width());
        wash.setColorAt(0.0, QColor(255, 232, 195, 32));
        wash.setColorAt(1.0, QColor(255, 210, 160, 0));
        p.fillRect(r, wash);
    }

private:
    void drawRidge(QPainter &p, const QRect &r, double baseY,
                   const QColor &col, const QColor &snowCol,
                   double haze, int layerIdx)
    {
        const int N = 26;
        QList<QPointF> pts;
        pts.reserve(N);
        for (int i = 0; i < N; ++i) {
            double t = double(i) / (N - 1);
            double x = t * r.width();

            double wave =
                std::sin(t * 9.0  + layerIdx * 1.7) * 0.045
              + std::sin(t * 3.5  + layerIdx * 2.3) * 0.075;
            double central =
                std::exp(-std::pow((t - 0.55) * 4.0, 2.0)) * 0.24;
            double secondary =
                std::exp(-std::pow((t - 0.28) * 6.5, 2.0)) * 0.11;

            double h = baseY - wave - central - secondary - layerIdx * 0.025;
            pts.append(QPointF(x, r.height() * h));
        }

        QPainterPath path;
        path.moveTo(pts[0]);
        for (int i = 1; i < pts.size(); ++i)
            path.lineTo(pts[i]);
        path.lineTo(r.width(), r.height());
        path.lineTo(0, r.height());
        path.closeSubpath();

        QColor top = col;
        QColor bot = col.darker(125);
        QLinearGradient g(0, r.height() * baseY, 0, r.height());
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bot);
        p.fillPath(path, g);

        // Snow caps at local maxima
        p.setPen(Qt::NoPen);
        for (int i = 1; i < pts.size() - 1; ++i) {
            if (pts[i].y() < pts[i-1].y() && pts[i].y() < pts[i+1].y()) {
                double capH = 12 + (1.0 - haze) * 22;
                double capW = 10 + (1.0 - haze) * 10;
                QPolygonF cap;
                cap << pts[i]
                    << QPointF(pts[i].x() - capW, pts[i].y() + capH)
                    << QPointF(pts[i].x(),        pts[i].y() + capH * 0.55)
                    << QPointF(pts[i].x() + capW, pts[i].y() + capH);
                QColor s = snowCol;
                s.setAlpha(int(200 * (1.0 - haze * 0.55)));
                p.setBrush(s);
                p.drawPolygon(cap);
            }
        }

        if (haze > 0.0) {
            QColor hz = QColor(232, 226, 218);
            hz.setAlpha(int(haze * 95));
            p.fillPath(path, hz);
        }
    }

    double m_phase = 0.0;
};

// =========================================================
// WallpaperConfig
// =========================================================
class WallpaperConfig {
public:
    static QString configPath() {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::ConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
        return dir + "/apokolips/wallpaper.json";
    }

    static QString loadId() {
        QFile f(configPath());
        if (!f.open(QIODevice::ReadOnly)) return "ruby";
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return "ruby";
        return doc.object().value("wallpaper").toString("ruby");
    }

    static void saveId(const QString &id) {
        QString path = configPath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonObject obj;
        obj["wallpaper"] = id;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(obj).toJson());
    }

    static Wallpaper *makeById(const QString &id) {
        if (id == "starfield")  return new StarfieldWallpaper;
        if (id == "aurora")     return new AuroraWallpaper;
        if (id == "goldengate") return new GoldenGateWallpaper;
        if (id == "everest")    return new EverestWallpaper;
        return new RubyWallpaper;
    }
};

// =========================================================
// Demo window
// =========================================================
class DemoWindow : public QWidget {
public:
    DemoWindow(const QString &appName, int offset, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(appName);
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        resize(480, 320);
        if (QScreen *s = QApplication::primaryScreen()) {
            QRect a = s->availableGeometry();
            move(a.x() + 120 + offset * 30, a.y() + 100 + offset * 30);
        }

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Header strip (transparent — paintEvent draws background)
        QWidget *header = new QWidget;
        header->setFixedHeight(40);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        header->setAutoFillBackground(false);

        QLabel *dot = new QLabel(QString::fromUtf8("\xE2\x97\x86"));
        QLabel *title = new QLabel(appName);

        QPushButton *close = new QPushButton;
        close->setFixedSize(13, 13);
        close->setCursor(Qt::PointingHandCursor);
        QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);

        QHBoxLayout *hLayout = new QHBoxLayout(header);
        hLayout->setContentsMargins(14, 0, 14, 0);
        hLayout->setSpacing(10);
        hLayout->addWidget(dot);
        hLayout->addWidget(title);
        hLayout->addStretch();
        hLayout->addWidget(close);

        QWidget *body = new QWidget;
        body->setAttribute(Qt::WA_NoSystemBackground, true);
        body->setAutoFillBackground(false);

        QLabel *big = new QLabel(appName);
        big->setAlignment(Qt::AlignCenter);
        QLabel *sub = new QLabel("running under Apokolips Shell");
        sub->setAlignment(Qt::AlignCenter);

        QVBoxLayout *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setAlignment(Qt::AlignCenter);
        bodyLayout->addWidget(big);
        bodyLayout->addWidget(sub);

        layout->addWidget(header);
        layout->addWidget(body, 1);

        StyleManager::startPolling(this, [this]() { update(); });
        ThemeManager::instance().subscribe(
            [this, dot, title, close, big, sub](const Theme &t) {
            m_theme = t;

            dot->setStyleSheet(QString(
                "color: %1; font-size: 14px; background: transparent;")
                .arg(t.textPrimary.name()));

            title->setStyleSheet(QString(
                "color: %1; font-size: 14px; font-weight: 600;"
                " background: transparent;").arg(t.textPrimary.name()));

            close->setStyleSheet(
                "QPushButton { background: #7a2b25;"
                "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
                "QPushButton:hover { background: #ff5f57; }");

            big->setStyleSheet(QString(
                "color: %1; font-size: 28px; font-weight: 300;"
                " letter-spacing: 4px; background: transparent;")
                .arg(t.textPrimary.name()));

            sub->setStyleSheet(QString(
                "color: %1; font-size: 11px; letter-spacing: 3px;"
                " margin-top: 8px; background: transparent;")
                .arg(t.textSecondary.name()));

            update();
        });
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Soft outer shadow
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = m_theme.textPrimary;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(rect().adjusted(-i, -i + 2, i - 1, i + 1),
                              14 + i, 14 + i);
        }

        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);

        // Style-aware body
        StyleRenderer::drawPanel(p, QRectF(rect().adjusted(0, 0, -1, -1)),
                                 14, m_theme, 235);

        // Header strip on top
        QPainterPath hdrPath;
        hdrPath.addRoundedRect(QRect(0, 0, width(), 40), 14, 14);
        QPainterPath square;
        square.addRect(QRect(0, 14, width(), 40 - 14));
        QPainterPath hdrFinal = hdrPath.united(square);

        QColor hdrTop = m_theme.chromeBg.lighter(125);
        QColor hdrBot = m_theme.chromeBg.lighter(105);
        QLinearGradient hg(0, 0, 0, 40);
        hg.setColorAt(0.0, hdrTop);
        hg.setColorAt(1.0, hdrBot);
        p.fillPath(hdrFinal, hg);

        // Separator line under header
        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.drawLine(0, 40, width(), 40);
    }

private:
    Theme m_theme;
};

// =========================================================
// Notification center
// =========================================================
class NotificationCenter : public QWidget {
public:
    explicit NotificationCenter(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFixedWidth(360);
        buildUi();

        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(16);
        QObject::connect(m_animTimer, &QTimer::timeout,
                         this, &NotificationCenter::tick);
        hide();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            repaint();
            // rebuild cards with new colors
            for (QWidget *c : m_cards) c->deleteLater();
            m_cards.clear();
            buildCards();
        });
    }

    void toggle() { isVisible() ? hideCenter() : showCenter(); }

    void showCenter() { raise(); show(); m_target = 1.0; m_animTimer->start(); }
    void hideCenter() { m_target = 0.0; m_animTimer->start(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Soft left-side shadow — many thin rings
        p.setPen(Qt::NoPen);
        for (int i = 12; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.015 * m_slide);
            p.setBrush(sh);
            p.drawRoundedRect(rect().adjusted(-i - 2, -i, 0, i),
                              14 + i, 14 + i);
        }

        QRectF ncRect(rect());
        StyleRenderer::drawPanel(p, ncRect, 0, m_theme,
                                 int(230 * m_slide));
        QColor edge = m_theme.chromeBorder;
        edge.setAlpha(int(230 * m_slide));
        p.setPen(QPen(edge, 1));
        p.drawLine(0, 0, 0, height());
    }

private:
    void tick() {
        if (m_slide < m_target) {
            m_slide += 0.14;
            if (m_slide >= m_target) m_slide = m_target;
        } else if (m_slide > m_target) {
            m_slide -= 0.14;
            if (m_slide <= m_target) m_slide = m_target;
        } else {
            m_animTimer->stop();
            if (m_target == 0.0) hide();
        }
        int offX = int(width() * (1.0 - m_slide));
        if (QWidget *par = parentWidget()) {
            move(par->width() - width() + offX, 0);
            setFixedHeight(par->height());
        }
        update();
    }

    QWidget *makeNote(const QString &app, const QString &title,
                      const QString &body, const QString &time,
                      const QColor &accent)
    {
        QWidget *card = new QWidget;
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setStyleSheet(QString(
            "QWidget {"
            "  background: %1;"
            "  border: none;"
            "  border-left: 3px solid %3;"
            "  border-radius: 12px;"
            "}").arg(rgba(m_theme.chromeBg.lighter(110)),
                     rgba(accent),
                     rgba(accent)));

        QVBoxLayout *v = new QVBoxLayout(card);
        v->setContentsMargins(14, 12, 14, 12);
        v->setSpacing(4);

        QHBoxLayout *top = new QHBoxLayout;
        top->setSpacing(8);

        QLabel *appL = new QLabel(app);
        appL->setStyleSheet(QString(
            "color: %1; font-size: 11px; font-weight: 600;"
            " letter-spacing: 1px; background: transparent;")
            .arg(accent.name()));
        top->addWidget(appL);
        top->addStretch();

        QLabel *timeL = new QLabel(time);
        timeL->setStyleSheet(QString(
            "color: %1; font-size: 10px; background: transparent;")
            .arg(m_theme.textDim.name()));
        top->addWidget(timeL);

        v->addLayout(top);

        QLabel *titleL = new QLabel(title);
        titleL->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 500;"
            " background: transparent;").arg(m_theme.textPrimary.name()));
        v->addWidget(titleL);

        QLabel *bodyL = new QLabel(body);
        bodyL->setWordWrap(true);
        bodyL->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(m_theme.textSecondary.name()));
        v->addWidget(bodyL);

        return card;
    }

    void buildCards() {
        QColor a = m_theme.accent;

        QColor a1 = a;
        QColor a2 = a.lighter(120);
        QColor a3 = a.darker(120);
        QColor a4 = a;

        m_cards.append(makeNote("MUSIC", "Now Playing",
            "Apokolips Anthem - track 1 of 12", "now", a1));
        m_cards.append(makeNote("MAIL", "3 new messages",
            "From: build system, VS Code, GitHub", "2m", a2));
        m_cards.append(makeNote("SYSTEM", "Update available",
            "Ubuntu 26.04 security patch ready to install", "12m", a3));
        m_cards.append(makeNote("APOKOLIPS", "Shell loaded",
            "All modules online. Welcome back.", "just now", a4));

        if (m_cardsLayout) {
            for (QWidget *c : m_cards) m_cardsLayout->addWidget(c);
        }
    }

    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(14, 44, 14, 14);
        outer->setSpacing(14);

        QLabel *title = new QLabel("NOTIFICATIONS");
        title->setStyleSheet(QString(
            "color: %1; font-size: 11px; letter-spacing: 4px;"
            " background: transparent;").arg(m_theme.accent.name()));
        outer->addWidget(title);

        QWidget *holder = new QWidget;
        holder->setAttribute(Qt::WA_TranslucentBackground, true);
        m_cardsLayout = new QVBoxLayout(holder);
        m_cardsLayout->setContentsMargins(0, 0, 0, 0);
        m_cardsLayout->setSpacing(14);
        outer->addWidget(holder);
        outer->addStretch();

        buildCards();
    }

    QTimer *m_animTimer = nullptr;
    qreal m_slide = 0.0;
    qreal m_target = 0.0;
    Theme m_theme;
    QVBoxLayout *m_cardsLayout = nullptr;
    QList<QWidget *> m_cards;
};

// =========================================================
// App launcher
// =========================================================
// =========================================================
// Shell registry — lets child widgets trigger shell actions
// =========================================================
namespace ShellRegistry {
    inline std::function<void()> &launcherToggle() {
        static std::function<void()> f;
        return f;
    }
    inline std::function<void()> &settingsOpen() {
        static std::function<void()> f;
        return f;
    }
}

// =========================================================
// SysState — reads live system state for tray icons
// =========================================================
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

} // namespace SysState

// =========================================================
// BadgeRegistry — pending notification counts per app
// =========================================================
namespace BadgeRegistry {

inline QHash<QString, int> &counts() {
    static QHash<QString, int> c;
    return c;
}

inline int get(const QString &app) {
    return counts().value(app, 0);
}

inline void set(const QString &app, int n) {
    if (n <= 0) counts().remove(app);
    else        counts()[app] = n;
}

inline void decrement(const QString &app) {
    int n = get(app);
    if (n > 0) set(app, n - 1);
}

inline void clear(const QString &app) {
    counts().remove(app);
}

inline void seedDefaults() {
    // Matches the hardcoded notification cards in NotificationCenter
    set("Mail", 3);
    set("Music", 1);
    set("Settings", 1);
}

} // namespace BadgeRegistry

// =========================================================
// Custom icon set — vector line-art, adapts to theme color
// =========================================================
namespace Icons {

static void drawFinder(QPainter &p, const QRectF &r, const QColor &c) {
    QPen pen(c, r.width() * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPointF ctr = r.center();
    qreal s = r.width() * 0.36;
    QPolygonF d;
    d << QPointF(ctr.x(), ctr.y() - s)
      << QPointF(ctr.x() + s, ctr.y())
      << QPointF(ctr.x(), ctr.y() + s)
      << QPointF(ctr.x() - s, ctr.y());
    p.drawPolygon(d);
    qreal yL = ctr.y() + s * 0.38;
    qreal xH = s * 0.60;
    p.drawLine(QPointF(ctr.x() - xH, yL), QPointF(ctr.x() + xH, yL));
}

static void drawLaunchpad(QPainter &p, const QRectF &r, const QColor &c) {
    QFont f = p.font();
    f.setPixelSize(int(r.width() * 0.82));
    f.setWeight(QFont::Light);
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, Qt::AlignCenter, QString::fromUtf8("\xCE\xA9"));
}

static void drawPhotos(QPainter &p, const QRectF &r, const QColor &c) {
    QPen pen(c, r.width() * 0.075, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPointF ctr = r.center();
    qreal inner = r.width() * 0.05;
    qreal outer = r.width() * 0.40;
    for (int i = 0; i < 6; ++i) {
        double a = i * 60.0 * 3.14159265 / 180.0;
        double b = a + 0.88;
        QPointF p1(ctr.x() + std::cos(a) * inner, ctr.y() + std::sin(a) * inner);
        QPointF p2(ctr.x() + std::cos(b) * outer, ctr.y() + std::sin(b) * outer);
        p.drawLine(p1, p2);
    }
}

static void drawMusic(QPainter &p, const QRectF &r, const QColor &c) {
    QPen pen(c, r.width() * 0.095, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    qreal base = r.center().y();
    qreal step = r.width() * 0.145;
    qreal hs[] = { 0.30, 0.55, 0.72, 0.45 };
    for (int i = 0; i < 4; ++i) {
        qreal x = r.center().x() + (i - 1.5) * step;
        qreal h = r.width() * hs[i] * 0.5;
        p.drawLine(QPointF(x, base - h), QPointF(x, base + h));
    }
}

static void drawNotes(QPainter &p, const QRectF &r, const QColor &c) {
    qreal w = r.width() * 0.58;
    qreal h = r.width() * 0.72;
    QRectF page(r.center().x() - w/2, r.center().y() - h/2, w, h);
    QPen out(c, r.width() * 0.075, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(out); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(page, r.width()*0.07, r.width()*0.07);
    QPen ln(c, r.width() * 0.06, Qt::SolidLine, Qt::RoundCap);
    p.setPen(ln);
    qreal xL = page.left() + w * 0.18;
    qreal xR = page.right() - w * 0.18;
    qreal y1 = page.top() + h * 0.28;
    qreal y2 = page.top() + h * 0.50;
    qreal y3 = page.top() + h * 0.72;
    p.drawLine(QPointF(xL, y1), QPointF(xR, y1));
    p.drawLine(QPointF(xL, y2), QPointF(xR, y2));
    p.drawLine(QPointF(xL, y3), QPointF(xL + (xR - xL) * 0.55, y3));
}

static void drawMail(QPainter &p, const QRectF &r, const QColor &c) {
    qreal w = r.width() * 0.74;
    qreal h = r.width() * 0.52;
    QRectF body(r.center().x() - w/2, r.center().y() - h/2, w, h);
    QPen out(c, r.width() * 0.075, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(out); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, r.width()*0.06, r.width()*0.06);
    QPointF mid(body.center().x(), body.top() + h*0.62);
    p.drawLine(QPointF(body.left() + w*0.03, body.top() + h*0.12), mid);
    p.drawLine(QPointF(body.right() - w*0.03, body.top() + h*0.12), mid);
}

static void drawSettings(QPainter &p, const QRectF &r, const QColor &c) {
    qreal w = r.width() * 0.66;
    qreal left = r.center().x() - w/2;
    qreal right = r.center().x() + w/2;
    qreal ys[] = { r.center().y() - r.width()*0.24,
                   r.center().y(),
                   r.center().y() + r.width()*0.24 };
    qreal knobPos[] = { 0.30, 0.72, 0.42 };
    QPen pen(c, r.width() * 0.065, Qt::SolidLine, Qt::RoundCap);
    for (int i = 0; i < 3; ++i) {
        p.setPen(pen); p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(left, ys[i]), QPointF(right, ys[i]));
        qreal kx = left + w * knobPos[i];
        p.setPen(Qt::NoPen); p.setBrush(c);
        p.drawEllipse(QPointF(kx, ys[i]), r.width()*0.08, r.width()*0.08);
    }
}

static void drawPower(QPainter &p, const QRectF &r, const QColor &c) {
    QPen pen(c, r.width() * 0.10, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    qreal s = r.width() * 0.34;
    QPointF ctr = r.center();
    QRectF arcR(ctr.x() - s, ctr.y() - s + r.width()*0.04, 2*s, 2*s);
    p.drawArc(arcR, 45 * 16, 270 * 16);
    p.drawLine(QPointF(ctr.x(), ctr.y() - s - r.width()*0.04),
               QPointF(ctr.x(), ctr.y() - r.width()*0.02));
}

static void drawFor(const QString &name, QPainter &p,
                    const QRectF &r, const QColor &c) {
    if (name == "Finder")         drawFinder(p, r, c);
    else if (name == "Launchpad") drawLaunchpad(p, r, c);
    else if (name == "Photos")    drawPhotos(p, r, c);
    else if (name == "Music")     drawMusic(p, r, c);
    else if (name == "Notes")     drawNotes(p, r, c);
    else if (name == "Mail")      drawMail(p, r, c);
    else if (name == "Settings")  drawSettings(p, r, c);
    else if (name == "Power")     drawPower(p, r, c);
}

static void drawWifi(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QPen pen(on ? c : QColor(c.red(), c.green(), c.blue(), 90),
             r.width() * 0.09, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPointF ctr(r.center().x(), r.center().y() + r.height() * 0.10);
    for (int i = 0; i < 3; ++i) {
        qreal rad = r.width() * (0.14 + 0.13 * i);
        QRectF arc(ctr.x() - rad, ctr.y() - rad, 2*rad, 2*rad);
        p.drawArc(arc, 45 * 16, 90 * 16);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(on ? c : QColor(c.red(), c.green(), c.blue(), 90));
    p.drawEllipse(ctr, r.width()*0.055, r.width()*0.055);
}

static void drawEthernet(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QColor col = on ? c : QColor(c.red(), c.green(), c.blue(), 90);
    QPen pen(col, r.width() * 0.085, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    // A rectangle at bottom with 3 pins above
    qreal w = r.width() * 0.42;
    qreal h = r.height() * 0.30;
    QRectF box(r.center().x() - w/2, r.center().y() + r.height()*0.02, w, h);
    p.drawRoundedRect(box, r.width()*0.05, r.width()*0.05);
    qreal step = w / 4.0;
    for (int i = 1; i <= 3; ++i) {
        qreal x = box.left() + step * i;
        p.drawLine(QPointF(x, box.top()),
                   QPointF(x, box.top() - r.height()*0.12));
    }
}

static void drawBluetooth(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QColor col = on ? c : QColor(c.red(), c.green(), c.blue(), 90);
    QPen pen(col, r.width() * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    // Classic bluetooth rune
    QPointF ctr = r.center();
    qreal s = r.width() * 0.28;
    QPolygonF path;
    path << QPointF(ctr.x(), ctr.y() - s)         // top
         << QPointF(ctr.x() + s*0.7, ctr.y() - s*0.5)
         << QPointF(ctr.x() - s*0.7, ctr.y() + s*0.5)
         << QPointF(ctr.x(), ctr.y() + s)
         << QPointF(ctr.x(), ctr.y() - s)
         << QPointF(ctr.x() - s*0.7, ctr.y() - s*0.5)
         << QPointF(ctr.x() + s*0.7, ctr.y() + s*0.5)
         << QPointF(ctr.x(), ctr.y() + s);
    p.drawPolyline(path);
}

static void drawBattery(QPainter &p, const QRectF &r, const QColor &c,
                        int pct, bool charging)
{
    qreal w = r.width() * 0.72;
    qreal h = r.height() * 0.40;
    QRectF body(r.center().x() - w/2, r.center().y() - h/2, w, h);
    QPen pen(c, r.width() * 0.075, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, r.width()*0.04, r.width()*0.04);
    // Nub
    QRectF nub(body.right() + r.width()*0.03,
               body.center().y() - h*0.18,
               r.width()*0.06, h*0.36);
    p.setBrush(c); p.setPen(Qt::NoPen);
    p.drawRoundedRect(nub, 2, 2);
    // Fill
    if (pct >= 0) {
        qreal inner = w * (pct / 100.0) * 0.88;
        QRectF fill(body.left() + w*0.06, body.top() + h*0.20,
                    inner, h*0.60);
        QColor fc = charging ? QColor(80, 200, 120) : c;
        p.setBrush(fc);
        p.drawRoundedRect(fill, 2, 2);
    }
}

static QPixmap render(const QString &name, int size, const QColor &color) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    drawFor(name, p, QRectF(0, 0, size, size), color);
    p.end();
    return px;
}

} // namespace Icons

class AppLauncher : public QWidget {
public:
    explicit AppLauncher(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        // Translucent — only the panel area is painted; the rest lets
        // the wallpaper underneath show through.
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        buildUi();

        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(16);
        QObject::connect(m_animTimer, &QTimer::timeout,
                         this, &AppLauncher::tickFade);
        hide();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            repaint();
        });
    }

    void toggle() { (isVisible() && m_opacity > 0.5) ? hideLauncher() : showLauncher(); }
    void showLauncher() { raise(); show(); setFocus(); m_fadeTarget = 1.0; m_animTimer->start(); }
    void hideLauncher() { m_fadeTarget = 0.0; m_animTimer->start(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect panelRect((width() - PANEL_W) / 2, 120, PANEL_W, PANEL_H);

        // ---- Soft edge: many thin rings = smooth fade ----
        p.setPen(Qt::NoPen);
        for (int i = 14; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.012 * m_opacity);
            p.setBrush(sh);
            p.drawRoundedRect(panelRect.adjusted(-i, -i + 3, i, i + 3),
                              22 + i, 22 + i);
        }

        // ---- Heavy blur through the panel (glassy) ----
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull()) {
            QPainterPath panelPath;
            panelPath.addRoundedRect(panelRect, 22, 22);
            p.save();
            p.setClipPath(panelPath);
            p.setOpacity(0.85 * m_opacity);
            p.drawPixmap(panelRect, bg, panelRect);
            p.restore();
        }

        // ---- Very light tint so tiles stay legible ----
        StyleRenderer::drawPanel(p, QRectF(panelRect), 22, m_theme,
                                 int(200 * m_opacity));
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { hideLauncher(); e->accept(); }
        else QWidget::keyPressEvent(e);
    }

private:
    static constexpr int PANEL_W = 640;
    static constexpr int PANEL_H = 420;

    void tickFade() {
        if (m_opacity < m_fadeTarget) {
            m_opacity += 0.15;
            if (m_opacity >= m_fadeTarget) m_opacity = m_fadeTarget;
        } else if (m_opacity > m_fadeTarget) {
            m_opacity -= 0.15;
            if (m_opacity <= m_fadeTarget) m_opacity = m_fadeTarget;
        } else {
            m_animTimer->stop();
            if (m_fadeTarget == 0.0) hide();
        }
        update();
    }

    void restyle() {
        if (m_search) {
            m_search->setStyleSheet(QString(
                "QLineEdit {"
                "  background: %1;"
                "  border: none;"
                "  border-radius: 12px;"
                "  padding: 12px 16px;"
                "  color: %3;"
                "  font-size: 15px;"
                "}"
                "QLineEdit:focus { border: 1px solid %4; }")
                .arg(rgba(m_theme.chromeBg.lighter(120)),
                     rgba(m_theme.chromeBorder),
                     m_theme.textPrimary.name(),
                     m_theme.accent.name()));
        }
        if (m_titleLabel) {
            m_titleLabel->setStyleSheet(QString(
                "color: %1; font-size: 11px; letter-spacing: 4px;"
                " background: transparent;").arg(m_theme.accent.name()));
        }
        if (m_tilesLayout) {
            restyleTiles();
        }
    }

    void restyleTiles() {
        QColor tileBg      = m_theme.textPrimary; tileBg.setAlpha(25);
        QColor tileHover   = m_theme.textPrimary; tileHover.setAlpha(55);
        QColor tilePressed = m_theme.textPrimary; tilePressed.setAlpha(90);
        QString textColor  = m_theme.textPrimary.name();

        QString css = QString(
            "QPushButton {"
            "  background: %1;"
            "  border: none;"
            "  border-radius: 16px;"
            "  color: %2;"
            "  padding-bottom: 10px;"
            "}"
            "QPushButton:hover {"
            "  background: %3;"
            "  border: none;"
            "}"
            "QPushButton:pressed {"
            "  background: %4;"
            "}")
            .arg(rgba(tileBg))
            .arg(textColor)
            .arg(rgba(tileHover))
            .arg(rgba(tilePressed));

        for (QPushButton *tile : m_tiles) {
            tile->setStyleSheet(css);
        }

        for (QLabel *l : m_nameLabels) {
            l->setStyleSheet(QString(
                "font-size: 12px; color: %1; background: transparent;")
                .arg(textColor));
        }
    }

    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        outer->addSpacing(120);

        QWidget *panel = new QWidget;
        panel->setFixedSize(PANEL_W, PANEL_H);
        panel->setAttribute(Qt::WA_TranslucentBackground, true);
        panel->setAttribute(Qt::WA_NoSystemBackground, true);

        QVBoxLayout *pl = new QVBoxLayout(panel);
        pl->setContentsMargins(28, 26, 28, 30);
        pl->setSpacing(18);

        m_titleLabel = new QLabel("SEARCH");
        pl->addWidget(m_titleLabel);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search apps, files, settings...");
        pl->addWidget(m_search);

        m_tilesLayout = new QGridLayout;
        m_tilesLayout->setSpacing(14);

        struct App { const char *icon; const char *name; };
        const App apps[] = {
            { "\xE2\x8C\x98",       "Finder"    },
            { "\xF0\x9F\x9A\x80",  "Launchpad" },
            { "\xE2\x9C\xA6",       "Photos"    },
            { "\xE2\x96\xB6",       "Music"     },
            { "\xF0\x9F\x93\x9D",  "Notes"     },
            { "\xE2\x9C\x89",       "Mail"      },
            { "\xE2\x9A\x99",       "Settings"  },
            { "\xE2\x9A\xA1",       "Power"     }
        };

        int col = 0, row = 0, idx = 0;
        for (const auto &a : apps) {
            QWidget *tile = makeTile(QString::fromUtf8(a.icon), a.name, idx);
            m_tilesLayout->addWidget(tile, row, col);
            if (++col == 4) { col = 0; ++row; }
            ++idx;
        }

        pl->addLayout(m_tilesLayout);
        outer->addWidget(panel, 0, Qt::AlignHCenter);
        outer->addStretch();
    }

    QWidget *makeTile(const QString &icon, const QString &name, int index) {
        QPushButton *tile = new QPushButton;
        tile->setFixedSize(136, 116);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setProperty("appName", name);

        QVBoxLayout *v = new QVBoxLayout(tile);
        v->setContentsMargins(8, 14, 8, 10);
        v->setSpacing(6);

        // Custom vector icon widget (replaces old emoji label)
        class TileIcon : public QWidget {
        public:
            TileIcon(const QString &iconName, QWidget *parent = nullptr)
                : QWidget(parent), m_name(iconName) {
                setFixedSize(52, 52);
                setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ThemeManager::instance().subscribe([this](const Theme &t) {
                    m_color = t.textPrimary;
                    update();
                });
            }
        protected:
            void paintEvent(QPaintEvent *) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                Icons::drawFor(m_name, p, QRectF(rect()), m_color);
            }
        private:
            QString m_name;
            QColor  m_color = QColor(230, 230, 240);
        };

        TileIcon *ic = new TileIcon(name);
        v->addWidget(ic, 0, Qt::AlignHCenter);

        QLabel *nm = new QLabel(name);
        nm->setAlignment(Qt::AlignCenter);
        nm->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        v->addWidget(nm);

        m_tiles.append(tile);
        m_nameLabels.append(nm);
        // m_iconLabels no longer used — vector icons self-style

        QString appName = name;
        int offset = index;
        QObject::connect(tile, &QPushButton::clicked,
                         [this, appName, offset]() {
            QStringList args;
            if (appName == "Settings")
                args = { "--settings" };
            else if (appName == "Finder")
                args = { "--files" };
            else
                args = { "--demo", appName, QString::number(offset) };
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                args);
            hideLauncher();
        });

        return tile;
    }

    QLineEdit *m_search = nullptr;
    QLabel *m_titleLabel = nullptr;
    QGridLayout *m_tilesLayout = nullptr;
    QTimer *m_animTimer = nullptr;
    qreal m_opacity = 0.0;
    qreal m_fadeTarget = 0.0;
    Theme m_theme;
    QList<QPushButton *> m_tiles;
    QList<QLabel *> m_iconLabels;
    QList<QLabel *> m_nameLabels;
};

// =========================================================
// DockIcon + Dock
// =========================================================

class DockIcon : public QWidget {
public:
    DockIcon(const QString &icon, const QString &name, int index,
             QWidget *parent = nullptr)
        : QWidget(parent), m_icon(icon), m_name(name), m_index(index)
    {
        setFixedSize(68, 68);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(name);

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });
        StyleManager::subscribe([this](VisualStyle) { update(); });
    }

    void setTarget(qreal t)  { m_target = t; }
    void setCurrent(qreal c) {
        if (qAbs(c - m_current) > 0.002) { m_current = c; update(); }
    }
    qreal current() const    { return m_current; }

    void setRunning(bool r) {
        if (m_running != r) { m_running = r; update(); }
    }
    bool isRunning() const { return m_running; }
    QString appName() const { return m_name; }
    qint64  pid() const { return m_pid; }
    void setPid(qint64 p) { m_pid = p; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal baseSize = 42.0;
        int visual = int(baseSize * m_current);
        int x = (width()  - visual) / 2;
        int y = (height() - visual) / 2 - 4;
        int radius = int(visual * 0.26);

        int boost = int(qBound(0.0, (m_current - 1.0) / 0.42, 1.0) * 90);

        // Style-aware icon background
        Theme iconTheme = m_theme;
        iconTheme.panelBg      = m_theme.accentSoft;
        iconTheme.chromeBorder = m_theme.accent;
        if (boost > 0) {
            QColor a = iconTheme.panelBg;
            a.setAlpha(qMin(255, a.alpha() + boost + 20));
            iconTheme.panelBg = a;
            QColor b = iconTheme.chromeBorder;
            b.setAlpha(qMin(255, b.alpha() + boost));
            iconTheme.chromeBorder = b;
        }
        QRectF bgRect(x, y, visual, visual);
        StyleRenderer::drawPanel(p, bgRect, radius, iconTheme,
                                 iconTheme.panelBg.alpha());

        // Vector icon, centered in the rounded rect
        QRectF glyphRect(x + visual * 0.17, y + visual * 0.17,
                         visual * 0.66, visual * 0.66);
        Icons::drawFor(m_name, p, glyphRect, m_theme.textPrimary);

        // Running indicator dot under the icon
        if (m_running) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_theme.accent);
            p.drawEllipse(QPoint(width() / 2, height() - 6), 3, 3);
        }

        // Notification badge (top-right)
        int badgeCount = BadgeRegistry::get(m_name);
        if (badgeCount > 0) {
            int badgeR = 9;
            QPoint badgeCtr(width() - 12, 12);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(220, 40, 60));
            p.drawEllipse(badgeCtr, badgeR, badgeR);
            QFont bf = font();
            bf.setPixelSize(10);
            bf.setBold(true);
            p.setFont(bf);
            p.setPen(QColor(255, 255, 255));
            QString txt = badgeCount > 9 ? "9+" : QString::number(badgeCount);
            p.drawText(QRect(badgeCtr.x() - badgeR, badgeCtr.y() - badgeR,
                             2*badgeR, 2*badgeR),
                       Qt::AlignCenter, txt);
        }
    }

    void launchApp() {
        // Clear this app's notification badge when launched
        BadgeRegistry::clear(m_name);
        update();

        // Map dock icon -> real program to run
        QString program;
        QStringList args;

        if (m_name == "Finder") {
            // Our own FileExplorer
            program = QCoreApplication::applicationFilePath();
            args = { "--files" };
        } else if (m_name == "Launchpad") {
            // In-process: trigger the shell's own launcher overlay
            if (ShellRegistry::launcherToggle()) {
                ShellRegistry::launcherToggle()();
            }
            return;
        } else if (m_name == "Settings") {
            program = QCoreApplication::applicationFilePath();
            args = { "--settings" };
        } else if (m_name == "Photos") {
            program = "eog";
        } else if (m_name == "Music") {
            program = "totem";
        } else if (m_name == "Notes") {
            program = "gnome-text-editor";
        } else if (m_name == "Mail") {
            program = "thunderbird";
        } else if (m_name == "Power") {
            // Menu is handled separately below
            return;
        } else {
            program = QCoreApplication::applicationFilePath();
            args = { "--demo", m_name, QString::number(m_index) };
        }

        QProcess::startDetached(program, args, QString(), &m_pid);
        m_running = true;
        update();
    }

    void showPowerMenu(QPoint globalPos) {
        const Theme &t = ThemeManager::instance().current();
        QMenu m(this);
        m.setStyleSheet(QString(
            "QMenu { background: %1; border: 1px solid %2;"
            "  border-radius: 8px; padding: 5px; color: %3;"
            "  font-size: 12px; }"
            "QMenu::item { padding: 6px 18px; border-radius: 5px; }"
            "QMenu::item:selected { background: %4; }")
            .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                 t.textPrimary.name(), rgba(t.accentSoft)));

        QAction *lock    = m.addAction("Lock Screen");
        QAction *suspend = m.addAction("Suspend");
        m.addSeparator();
        QAction *reboot  = m.addAction("Restart…");
        QAction *shutdn  = m.addAction("Shut Down…");

        QObject::connect(lock, &QAction::triggered, []() {
            QProcess::startDetached("loginctl", { "lock-session" });
        });
        QObject::connect(suspend, &QAction::triggered, []() {
            QProcess::startDetached("systemctl", { "suspend" });
        });
        QObject::connect(reboot, &QAction::triggered, []() {
            QProcess::startDetached("systemctl", { "reboot" });
        });
        QObject::connect(shutdn, &QAction::triggered, []() {
            QProcess::startDetached("systemctl", { "poweroff" });
        });

        m.exec(globalPos);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (m_name == "Power") {
                showPowerMenu(e->globalPosition().toPoint());
            } else {
                launchApp();
            }
        } else if (e->button() == Qt::RightButton) {
            const Theme &t = ThemeManager::instance().current();
            QMenu m(this);
            m.setStyleSheet(QString(
                "QMenu { background: %1; border: 1px solid %2;"
                "  border-radius: 8px; padding: 5px; color: %3;"
                "  font-size: 12px; }"
                "QMenu::item { padding: 6px 18px; border-radius: 5px; }"
                "QMenu::item:selected { background: %4; }")
                .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                     t.textPrimary.name(), rgba(t.accentSoft)));

            if (m_running) {
                QAction *quitAct = m.addAction(QString("Quit %1").arg(m_name));
                QObject::connect(quitAct, &QAction::triggered, [this]() {
                    if (m_pid > 0)
                        QProcess::execute("kill",
                            {"-TERM", QString::number(m_pid)});
                    m_running = false;
                    m_pid = 0;
                    update();
                });
            } else {
                QAction *launchAct = m.addAction(
                    QString("Open %1").arg(m_name));
                QObject::connect(launchAct, &QAction::triggered, [this]() {
                    launchApp();
                });
            }
            m.exec(e->globalPosition().toPoint());
        }
    }

private:
    QString m_icon;
    QString m_name;
    int     m_index;
    qreal   m_current = 1.0;
    qreal   m_target  = 1.0;
    Theme   m_theme;
    bool    m_running = false;
    qint64  m_pid = 0;
};

class Dock : public QWidget {
public:
    Dock(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("dockRoot");
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFixedHeight(96);
        setMouseTracking(true);

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });
        StyleManager::subscribe([this](VisualStyle) { update(); });
        StyleManager::startPolling(this, [this]() { update(); });

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(18, 18, 18, 14);
        m_layout->setSpacing(4);

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_timer->start();

        // Separate timer to poll running state — cheaper than every frame
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(700);
        QObject::connect(m_pollTimer, &QTimer::timeout,
                         this, [this]() { pollRunning(); });
        m_pollTimer->start();
    }

    void pollRunning() {
        for (DockIcon *di : m_icons) {
            qint64 pid = di->pid();
            bool alive = false;
            if (pid > 0) {
                // On Linux, /proc/<pid> exists iff process is alive
                alive = QFile::exists(QString("/proc/%1").arg(pid));
            }
            di->setRunning(alive);
        }
    }

    void addIcon(const QString &icon, const QString &name, int index) {
        DockIcon *di = new DockIcon(icon, name, index, this);
        m_icons.append(di);
        m_layout->addWidget(di);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath path;
        path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 20, 20);

        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull() && window()) {
            // Map our top-left to the top-level window; the window is
            // frameless with zero margin, so this aligns with the root.
            QPoint pos = mapTo(window(), QPoint(0, 0));
            QRect srcRect(pos, size());
            p.save();
            p.setClipPath(path);
            p.drawPixmap(rect(), bg, srcRect);
            p.restore();
        }

        const auto &vc = VisualConfigManager::instance().cfg();
        StyleRenderer::drawPanel(p, QRectF(rect().adjusted(1, 1, -1, -1)),
                                 20, m_theme, vc.dockAlpha);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        m_cursorX  = e->position().x();
        m_hasCursor = true;
        if (!m_debugOnce) {
            qDebug() << "[dock] first mouseMove at x=" << m_cursorX
                     << "y=" << e->position().y();
            m_debugOnce = true;
        }
    }
    void leaveEvent(QEvent *) override { m_hasCursor = false; }

private:
    void tick() {
        const auto &vc = VisualConfigManager::instance().cfg();
        const qreal sigma    = vc.dockSigma;
        const qreal maxBoost = vc.dockMagnifyMax;
        qreal cursor = m_hasCursor ? m_cursorX : -10000.0;

        bool anyMoving = false;
        for (DockIcon *di : m_icons) {
            qreal cx = di->geometry().center().x();
            qreal d  = cx - cursor;
            qreal inf = std::exp(-(d * d) / (2.0 * sigma * sigma));
            qreal target = 1.0 + maxBoost * inf;
            di->setTarget(target);
            qreal cur  = di->current();
            qreal next = cur + (target - cur) * 0.28;
            if (qAbs(next - cur) > 0.001) anyMoving = true;
            di->setCurrent(next);
        }
        // If nothing is animating, slow the timer down to save CPU
        if (!anyMoving && m_timer->interval() == 16)
            m_timer->setInterval(80);
        else if (anyMoving && m_timer->interval() != 16)
            m_timer->setInterval(16);
    }

    QList<DockIcon *> m_icons;
    QHBoxLayout *m_layout = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_pollTimer = nullptr;
    qreal m_cursorX = 0;
    bool  m_hasCursor = false;
    bool  m_debugOnce = false;
    Theme m_theme;
};

// =========================================================
// Top bar
// =========================================================
class TopBar : public QWidget {
public:
    explicit TopBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(30);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setCursor(Qt::OpenHandCursor);
    }
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
            m_dragOffset = e->globalPosition().toPoint()
                           - window()->frameGeometry().topLeft();
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            window()->move(e->globalPosition().toPoint() - m_dragOffset);
            e->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        e->accept();
    }
private:
    bool m_dragging = false;
    QPoint m_dragOffset;
};

static QString menuStyle(const Theme &)
{
    // Fixed neutral dark surface so it's readable on every wallpaper.
    // Theme colors are deliberately ignored here — menus are small
    // popups and need a solid background more than they need theming.
    return QString(
        "QMenu {"
        "  background: rgb(24, 22, 30);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: 12px;"
        "  padding: 6px;"
        "  color: #f0f0f5;"
        "  font-size: 13px;"
        "}"
        "QMenu::item {"
        "  padding: 7px 22px 7px 16px;"
        "  border-radius: 7px;"
        "  background: transparent;"
        "  color: #f0f0f5;"
        "}"
        "QMenu::item:selected {"
        "  background: rgba(255, 255, 255, 45);"
        "  color: #ffffff;"
        "}"
        "QMenu::item:disabled {"
        "  color: #6a6a72;"
        "  background: transparent;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: rgba(255, 255, 255, 40);"
        "  margin: 6px 12px;"
        "}");
}

// =========================================================
// Control Center — macOS-style dropdown panel
// =========================================================
// =========================================================
// SysControl — audio (wpctl) + brightness (sysfs)
// =========================================================
namespace SysControl {

inline int audioGet() {
    QProcess p;
    p.start("wpctl", { "get-volume", "@DEFAULT_AUDIO_SINK@" });
    p.waitForFinished(500);
    QString out = QString::fromUtf8(p.readAllStandardOutput());
    // Output format: "Volume: 0.40" or "Volume: 0.40 [MUTED]"
    int idx = out.indexOf("Volume:");
    if (idx == -1) return 50;
    QString num = out.mid(idx + 7).trimmed().split(' ').first();
    bool ok = false;
    double v = num.toDouble(&ok);
    if (!ok) return 50;
    return int(v * 100.0 + 0.5);
}

inline void audioSet(int pct) {
    pct = qBound(0, pct, 150);
    QProcess::startDetached("wpctl",
        { "set-volume", "@DEFAULT_AUDIO_SINK@",
          QString::number(pct) + "%" });
}

inline int brightnessGet() {
    QDir d("/sys/class/backlight");
    if (!d.exists()) return 50;
    QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return 50;
    QFile maxF("/sys/class/backlight/" + entries.first() + "/max_brightness");
    QFile curF("/sys/class/backlight/" + entries.first() + "/brightness");
    if (!maxF.open(QIODevice::ReadOnly)) return 50;
    if (!curF.open(QIODevice::ReadOnly)) return 50;
    int mx = QString(maxF.readAll()).trimmed().toInt();
    int cu = QString(curF.readAll()).trimmed().toInt();
    if (mx <= 0) return 50;
    return int(100.0 * cu / mx + 0.5);
}

inline void brightnessSet(int pct) {
    QDir d("/sys/class/backlight");
    if (!d.exists()) return;
    QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return;
    QFile maxF("/sys/class/backlight/" + entries.first() + "/max_brightness");
    if (!maxF.open(QIODevice::ReadOnly)) return;
    int mx = QString(maxF.readAll()).trimmed().toInt();
    if (mx <= 0) return;
    int target = qBound(0, pct, 100) * mx / 100;
    QFile curF("/sys/class/backlight/" + entries.first() + "/brightness");
    if (!curF.open(QIODevice::WriteOnly)) return;
    curF.write(QString::number(target).toUtf8());
}

} // namespace SysControl

class ControlCenter : public QWidget {
public:
    explicit ControlCenter(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFixedSize(PANEL_W, PANEL_H);
        buildUi();

        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(16);
        QObject::connect(m_animTimer, &QTimer::timeout,
                         this, &ControlCenter::tick);
        hide();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });
    }

    void toggle() { isVisible() && m_slide > 0.5 ? hideCC() : showCC(); }
    void showCC() { raise(); show(); m_target = 1.0; m_animTimer->start(); }
    void hideCC() { m_target = 0.0; m_animTimer->start(); }

    void reposition(int parentW) {
        move(parentW - PANEL_W - 10, 36);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().adjusted(0, 0, -1, -1);

        // Soft outer shadow — many thin rings
        p.setPen(Qt::NoPen);
        for (int i = 14; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.012);
            p.setBrush(sh);
            p.drawRoundedRect(r.adjusted(-i, -i + 3, i, i + 3),
                              16 + i, 16 + i);
        }

        QPainterPath path;
        path.addRoundedRect(r, 16, 16);

        const auto &vc = VisualConfigManager::instance().cfg();
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull() && vc.blurStrength > 0.001) {
            p.save();
            p.setClipPath(path);
            QPoint pos = mapTo(window(), QPoint(0, 0));
            p.setOpacity(vc.blurStrength * m_slide);
            p.drawPixmap(r, bg, QRect(pos, size()));
            p.restore();
        }

        StyleRenderer::drawPanel(p, QRectF(r), 16, m_theme,
                                 int(vc.controlCenterAlpha * m_slide));
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { hideCC(); e->accept(); }
        else QWidget::keyPressEvent(e);
    }

private:
    static constexpr int PANEL_W = 340;
    static constexpr int PANEL_H = 480;

    void tick() {
        if (m_slide < m_target) {
            m_slide += 0.18;
            if (m_slide >= m_target) m_slide = m_target;
        } else if (m_slide > m_target) {
            m_slide -= 0.18;
            if (m_slide <= m_target) m_slide = m_target;
        } else {
            m_animTimer->stop();
            if (m_target == 0.0) hide();
        }
        update();
    }

    QWidget *makeCard(const QString &title, QWidget *content) {
        QWidget *card = new QWidget;
        card->setAttribute(Qt::WA_StyledBackground, true);
        QVBoxLayout *v = new QVBoxLayout(card);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(8);

        QLabel *t = new QLabel(title);
        t->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-weight: 600;"
            " letter-spacing: 2px; background: transparent;")
            .arg(m_theme.textDim.name()));
        v->addWidget(t);
        v->addWidget(content);
        return card;
    }

    QWidget *makeWiFiRow() {
        QWidget *row = new QWidget;
        QHBoxLayout *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);

        QLabel *icon = new QLabel(QString::fromUtf8("\xE2\x97\x8F"));
        icon->setStyleSheet(QString(
            "color: %1; font-size: 16px; background: transparent;")
            .arg(m_theme.accent.name()));
        h->addWidget(icon);

        QVBoxLayout *col = new QVBoxLayout;
        col->setSpacing(1);
        QLabel *l1 = new QLabel("Wi-Fi");
        l1->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 500;"
            " background: transparent;").arg(m_theme.textPrimary.name()));
        QLabel *l2 = new QLabel("Connected");
        l2->setStyleSheet(QString(
            "color: %1; font-size: 11px; background: transparent;")
            .arg(m_theme.textSecondary.name()));
        col->addWidget(l1); col->addWidget(l2);
        h->addLayout(col);
        h->addStretch();

        QLabel *toggle = new QLabel(QString::fromUtf8("\xE2\x97\x8F"));
        toggle->setStyleSheet(QString(
            "color: %1; font-size: 14px; background: transparent;")
            .arg(m_theme.accent.name()));
        h->addWidget(toggle);
        return row;
    }

    QWidget *makeBTRow() {
        QWidget *row = new QWidget;
        QHBoxLayout *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);

        QLabel *icon = new QLabel(QString::fromUtf8("\xE2\x9C\xA6"));
        icon->setStyleSheet(QString(
            "color: %1; font-size: 16px; background: transparent;")
            .arg(m_theme.accent.name()));
        h->addWidget(icon);

        QVBoxLayout *col = new QVBoxLayout;
        col->setSpacing(1);
        QLabel *l1 = new QLabel("Bluetooth");
        l1->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 500;"
            " background: transparent;").arg(m_theme.textPrimary.name()));
        QLabel *l2 = new QLabel("On");
        l2->setStyleSheet(QString(
            "color: %1; font-size: 11px; background: transparent;")
            .arg(m_theme.textSecondary.name()));
        col->addWidget(l1); col->addWidget(l2);
        h->addLayout(col);
        h->addStretch();

        QLabel *toggle = new QLabel(QString::fromUtf8("\xE2\x97\x8F"));
        toggle->setStyleSheet(QString(
            "color: %1; font-size: 14px; background: transparent;")
            .arg(m_theme.accent.name()));
        h->addWidget(toggle);
        return row;
    }

    QWidget *makeMediaRow() {
        QWidget *row = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(row);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        QLabel *l1 = new QLabel("Not Playing");
        l1->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 500;"
            " background: transparent;").arg(m_theme.textPrimary.name()));
        QLabel *l2 = new QLabel("Windows media");
        l2->setStyleSheet(QString(
            "color: %1; font-size: 11px; background: transparent;")
            .arg(m_theme.textSecondary.name()));
        v->addWidget(l1); v->addWidget(l2);

        QHBoxLayout *h = new QHBoxLayout;
        h->setSpacing(8);
        h->addStretch();

        auto mkBtn = [this](const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(30, 30);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1; color: %2;"
                "  border: none; border-radius: 15px; font-size: 14px; }"
                "QPushButton:hover { background: %3; }")
                .arg(rgba(m_theme.chromeBg.lighter(140)),
                     m_theme.textPrimary.name(),
                     rgba(m_theme.accentSoft)));
            return b;
        };

        h->addWidget(mkBtn(QString::fromUtf8("\xE2\x8F\xAE")));
        h->addWidget(mkBtn(QString::fromUtf8("\xE2\x96\xB6")));
        h->addWidget(mkBtn(QString::fromUtf8("\xE2\x8F\xAD")));
        h->addStretch();
        v->addLayout(h);
        return row;
    }

    QWidget *makeTogglesRow() {
        QWidget *row = new QWidget;
        QHBoxLayout *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        auto mk = [this](const QString &icon, const QString &label) {
            QPushButton *b = new QPushButton;
            b->setFixedSize(64, 58);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1; border: none;"
                "  border-radius: 14px; color: %2;"
                "  font-size: 10px; padding-top: 4px; }"
                "QPushButton:hover { background: %3; }")
                .arg(rgba(m_theme.chromeBg.lighter(130)),
                     m_theme.textPrimary.name(),
                     rgba(m_theme.accentSoft)));

            QVBoxLayout *v = new QVBoxLayout(b);
            v->setContentsMargins(0, 6, 0, 4);
            v->setSpacing(2);

            QLabel *ic = new QLabel(icon);
            ic->setAlignment(Qt::AlignCenter);
            ic->setStyleSheet(QString(
                "font-size: 16px; color: %1; background: transparent;")
                .arg(m_theme.accent.name()));
            ic->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            v->addWidget(ic);

            QLabel *lb = new QLabel(label);
            lb->setAlignment(Qt::AlignCenter);
            lb->setStyleSheet(QString(
                "font-size: 10px; color: %1; background: transparent;")
                .arg(m_theme.textPrimary.name()));
            lb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            v->addWidget(lb);
            return b;
        };

        h->addWidget(mk(QString::fromUtf8("\xE2\x9A\xA1"), "Battery"));
        h->addWidget(mk(QString::fromUtf8("\xE2\x98\xBE"), "Focus"));
        h->addWidget(mk(QString::fromUtf8("\xF0\x9F\x94\x94"), "Alerts"));
        h->addWidget(mk(QString::fromUtf8("\xF0\x9F\x96\xA5"), "Display"));
        return row;
    }

    QWidget *makeSliderRow(const QString &label, int value,
                           std::function<void(int)> onChange = {}) {
        QWidget *row = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(row);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        QLabel *l = new QLabel(label);
        l->setStyleSheet(QString(
            "color: %1; font-size: 12px; font-weight: 500;"
            " background: transparent;").arg(m_theme.textPrimary.name()));
        v->addWidget(l);

        QSlider *s = new QSlider(Qt::Horizontal);
        s->setRange(0, 100);
        s->setValue(value);
        s->setStyleSheet(QString(
            "QSlider::groove:horizontal {"
            "  height: 6px; background: %1; border-radius: 3px; }"
            "QSlider::handle:horizontal {"
            "  background: %2; width: 14px; height: 14px;"
            "  margin: -5px 0; border-radius: 7px; }"
            "QSlider::sub-page:horizontal {"
            "  background: %2; border-radius: 3px; }")
            .arg(rgba(m_theme.chromeBg.lighter(140)),
                 m_theme.accent.name()));
        if (onChange) {
            QObject::connect(s, &QSlider::valueChanged, onChange);
        }
        v->addWidget(s);
        return row;
    }

    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(12, 12, 12, 12);
        outer->setSpacing(10);

        outer->addWidget(makeCard("WI-FI", makeWiFiRow()));
        outer->addWidget(makeCard("BLUETOOTH", makeBTRow()));
        outer->addWidget(makeCard("NOW PLAYING", makeMediaRow()));
        outer->addWidget(makeTogglesRow());
        outer->addWidget(makeCard("SOUND",
            makeSliderRow("Output", SysControl::audioGet(),
                [](int v) { SysControl::audioSet(v); })));

        outer->addWidget(makeCard("DISPLAY",
            makeSliderRow("Brightness", SysControl::brightnessGet(),
                [](int v) { SysControl::brightnessSet(v); })));
        outer->addStretch();
    }

    QTimer *m_animTimer = nullptr;
    qreal m_slide = 0.0;
    qreal m_target = 0.0;
    Theme m_theme;
};

// =========================================================
// File Explorer — translucent macOS-style file browser
// =========================================================
// =========================================================
// ApokolipsInputDialog — themed prompt (glass + traffic lights)
// =========================================================
class ApokolipsInputDialog : public QDialog {
public:
    static QString getText(QWidget *parent,
                           const QString &title,
                           const QString &label,
                           const QString &initialText,
                           bool *ok)
    {
        ApokolipsInputDialog dlg(title, label, initialText, parent);
        int r = dlg.exec();
        if (ok) *ok = (r == QDialog::Accepted);
        return (r == QDialog::Accepted) ? dlg.text() : QString();
    }

    ApokolipsInputDialog(const QString &title, const QString &label,
                         const QString &initialText, QWidget *parent)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        resize(420, 180);

        if (parent) {
            QPoint c = parent->mapToGlobal(
                QPoint(parent->width()/2, parent->height()/2));
            move(c.x() - 210, c.y() - 90);
        } else if (QScreen *s = QApplication::primaryScreen()) {
            QRect g = s->availableGeometry();
            move(g.center().x() - 210, g.center().y() - 90);
        }

        // Build blurred backdrop from wallpaper
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        if (wp) {
            const int DOWN = 8;
            QImage small(qMax(1, width()/DOWN), qMax(1, height()/DOWN),
                         QImage::Format_ARGB32_Premultiplied);
            small.fill(Qt::transparent);
            QPainter p(&small);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(1.0/DOWN, 1.0/DOWN);
            wp->paint(p, QRect(0, 0, width(), height()));
            p.end();
            m_blurredBg = QPixmap::fromImage(
                small.scaled(size(), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation));
            delete wp;
        }

        buildUi(title, label, initialText);
        m_edit->setFocus();
        m_edit->selectAll();

        // Subscribe AFTER buildUi so restyle() has widgets to touch
        m_theme = ThemeManager::instance().current();
        restyle();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        StyleManager::startPolling(this, [this]() { update(); });
    }

    QString text() const { return m_edit->text(); }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            finish(QDialog::Rejected);
        } else if (e->key() == Qt::Key_Return ||
                   e->key() == Qt::Key_Enter) {
            finish(QDialog::Accepted);
        } else {
            QWidget::keyPressEvent(e);
        }
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect frameRect = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(frameRect, 14, 14);

        // Soft edge shadow
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = m_theme.textPrimary;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(frameRect.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // Blurred wallpaper backdrop
        if (!m_blurredBg.isNull()) {
            p.save();
            p.setClipPath(path);
            p.setOpacity(0.85);
            p.drawPixmap(rect(), m_blurredBg);
            p.restore();
        }

        // Style-aware body
        StyleRenderer::drawPanel(p, QRectF(frameRect), 14, m_theme, 200);

        // Header strip
        QPainterPath hdr;
        hdr.addRoundedRect(QRect(0, 0, width(), 36), 14, 14);
        QPainterPath sq;
        sq.addRect(QRect(0, 14, width(), 22));
        QPainterPath hdrFinal = hdr.united(sq);
        QColor hdrCol = m_theme.chromeBg;
        hdrCol.setAlpha(150);
        p.fillPath(hdrFinal, hdrCol);

        // Header separator
        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.drawLine(0, 36, width(), 36);
    }

private:
    void buildUi(const QString &title, const QString &label,
                 const QString &initialText)
    {
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Header: traffic lights on left, title centered
        QWidget *header = new QWidget;
        header->setFixedHeight(36);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hl = new QHBoxLayout(header);
        hl->setContentsMargins(12, 0, 12, 0);
        hl->setSpacing(8);

        auto makeLight = [](const QString &idle, const QString &hover) {
            QPushButton *b = new QPushButton;
            b->setFixedSize(12, 12);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1;"
                "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
                "QPushButton:hover { background: %2; }").arg(idle, hover));
            return b;
        };

        QPushButton *closeBtn = makeLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn   = makeLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn   = makeLight("#155c1e", "#28c840");

        hl->addWidget(closeBtn);
        hl->addWidget(minBtn);
        hl->addWidget(maxBtn);
        hl->addSpacing(10);

        m_title = new QLabel(title);
        m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        hl->addWidget(m_title);
        hl->addStretch();

        QObject::connect(closeBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Rejected); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         [this]() { showMinimized(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            static bool z = false;
            resize(z ? QSize(420, 180) : QSize(700, 320));
            z = !z;
        });

        root->addWidget(header);

        // Body: label + line edit + buttons
        QWidget *body = new QWidget;
        body->setAttribute(Qt::WA_NoSystemBackground, true);
        QVBoxLayout *bl = new QVBoxLayout(body);
        bl->setContentsMargins(20, 14, 20, 16);
        bl->setSpacing(10);

        m_label = new QLabel(label);
        bl->addWidget(m_label);

        m_edit = new QLineEdit(initialText);
        bl->addWidget(m_edit);

        QHBoxLayout *btnRow = new QHBoxLayout;
        btnRow->addStretch();

        m_cancelBtn = new QPushButton("Cancel");
        m_cancelBtn->setCursor(Qt::PointingHandCursor);
        m_cancelBtn->setFixedHeight(28);
        btnRow->addWidget(m_cancelBtn);

        m_okBtn = new QPushButton("OK");
        m_okBtn->setCursor(Qt::PointingHandCursor);
        m_okBtn->setFixedHeight(28);
        m_okBtn->setDefault(true);
        btnRow->addWidget(m_okBtn);

        QObject::connect(m_okBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Accepted); });
        QObject::connect(m_cancelBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Rejected); });

        bl->addLayout(btnRow);
        root->addWidget(body);
    }

    void restyle() {
        const Theme &t = m_theme;

        m_title->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));

        m_label->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(t.textSecondary.name()));

        m_edit->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2;"
            "  border: 1px solid %3; border-radius: 8px;"
            "  padding: 8px 12px; font-size: 13px;"
            "  selection-background-color: %4; }"
            "QLineEdit:focus { border: 1px solid %5; }")
            .arg(rgba(t.chromeBg.lighter(130)),
                 t.textPrimary.name(),
                 rgba(t.chromeBorder),
                 rgba(t.accentSoft),
                 t.accent.name()));

        QString btnCss = QString(
            "QPushButton { background: %1; color: %2;"
            "  border: 1px solid %3; border-radius: 8px;"
            "  padding: 4px 18px; font-size: 12px; }"
            "QPushButton:hover { background: %4; }"
            "QPushButton:pressed { background: %5; }")
            .arg(rgba(t.chromeBg.lighter(120)),
                 t.textPrimary.name(),
                 rgba(t.chromeBorder),
                 rgba(t.accentSoft),
                 rgba(t.accentStrong));

        m_okBtn->setStyleSheet(btnCss);
        m_cancelBtn->setStyleSheet(btnCss);
    }

    void finish(int result) {
        if (result == QDialog::Accepted) accept();
        else                             reject();
    }

    QLabel *m_title = nullptr;
    QLabel *m_label = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPixmap m_blurredBg;
    Theme m_theme;
};

class FileExplorer : public QWidget {
public:
    explicit FileExplorer(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint);
        setWindowTitle("Files");
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        resize(900, 560);

        if (QScreen *s = QApplication::primaryScreen())
            move(s->availableGeometry().center() - QPoint(450, 280));

        // Render the current wallpaper, blurred, as glass backdrop
        buildBlurredBackdrop();

        buildUi();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyleAll();
            update();
        });
        StyleManager::startPolling(this, [this]() { update(); });

        // ---- Keyboard shortcuts ----
        auto sc = [this](const QString &key,
                         std::function<void()> fn) {
            QShortcut *s = new QShortcut(QKeySequence(key), this);
            QObject::connect(s, &QShortcut::activated, this, fn);
        };
        sc("F2",       [this]() { renameSelected();   });
        sc("Delete",   [this]() { deleteSelected();   });
        sc("Backspace",[this]() {
            QDir d(m_currentPath);
            if (d.cdUp()) navigateTo(d.absolutePath());
        });
        sc("Ctrl+C",   [this]() { copySelected(false); });
        sc("Ctrl+X",   [this]() { copySelected(true);  });
        sc("Ctrl+V",   [this]() { pasteInto();         });
        sc("Ctrl+H",   [this]() { toggleHidden();      });
        sc("Alt+Left", [this]() { goBack();            });
        sc("Alt+Right",[this]() { goForward();         });
        sc("Ctrl+L",   [this]() {
            bool ok = false;
            QString p = ApokolipsInputDialog::getText(this, "Go to",
                "Path:", m_currentPath, &ok);
            if (ok && !p.isEmpty()) navigateTo(p);
        });

        show();
        update();
        const QString startPath = QDir::homePath();
        QTimer::singleShot(600, this, [this, startPath]() {
            navigateTo(startPath);
        });
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void buildBlurredBackdrop() {
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        if (!wp) return;
        const int DOWN = 8;
        QImage small(qMax(1, width()/DOWN), qMax(1, height()/DOWN),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(Qt::transparent);
        QPainter p(&small);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(1.0/DOWN, 1.0/DOWN);
        wp->paint(p, QRect(0, 0, width(), height()));
        p.end();
        m_blurredBg = QPixmap::fromImage(
            small.scaled(size(), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation));
        delete wp;
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect frameRect = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(frameRect, 14, 14);

        // ---- Soft outer shadow ----
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = m_theme.textPrimary;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(frameRect.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // ---- Blurred wallpaper backdrop ----
        if (!m_blurredBg.isNull()) {
            p.save();
            p.setClipPath(path);
            p.setOpacity(0.85);
            p.drawPixmap(rect(), m_blurredBg);
            p.restore();
        }

        // ---- Style-aware body ----
        const auto &vc = VisualConfigManager::instance().cfg();
        StyleRenderer::drawPanel(p, QRectF(frameRect), 14, m_theme,
                                 vc.finderAlpha);

        // ---- Header strip: slightly stronger tint ----
        QPainterPath hdrPath;
        hdrPath.addRoundedRect(QRect(0, 0, width(), 48), 14, 14);
        QPainterPath squareBottom;
        squareBottom.addRect(QRect(0, 14, width(), 34));
        QPainterPath hdrFinal = hdrPath.united(squareBottom);

        QColor hdrTint = m_theme.chromeBg;
        hdrTint.setAlpha(160);
        p.fillPath(hdrFinal, hdrTint);

        // ---- Separators ----
        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.drawLine(0, 48, width(), 48);
        if (m_sidebar)
            p.drawLine(m_sidebar->width(), 48,
                       m_sidebar->width(), height() - 1);
    }

private:
    static void flog(const QString &msg) {
        QFile f("/tmp/finder.log");
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            f.write((QDateTime::currentDateTime()
                     .toString("HH:mm:ss.zzz ")).toUtf8());
            f.write(msg.toUtf8());
            f.write("\n");
        }
    }

    void buildUi() {
        // Cage sessions have no XDG settings daemon, so QIcon theme lookup
        // fails. Point Qt at standard icon directories and pick Adwaita.
        QIcon::setThemeSearchPaths({
            "/usr/share/icons",
            "/usr/local/share/icons",
            QDir::homePath() + "/.icons",
            QDir::homePath() + "/.local/share/icons"
        });
        QIcon::setThemeName("Adwaita");

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // ---- Header strip: traffic lights + title ----
        m_header = new QWidget;
        m_header->setFixedHeight(30);
        m_header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hh = new QHBoxLayout(m_header);
        hh->setContentsMargins(14, 0, 14, 0);
        hh->setSpacing(8);

        auto makeLight = [](const QString &idle, const QString &hover) {
            QPushButton *b = new QPushButton;
            b->setFixedSize(13, 13);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1;"
                "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
                "QPushButton:hover { background: %2; }").arg(idle, hover));
            return b;
        };
        QPushButton *closeBtn = makeLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn   = makeLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn   = makeLight("#155c1e", "#28c840");
        QObject::connect(closeBtn, &QPushButton::clicked,
                         [this]() { close(); deleteLater(); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         [this]() { hide(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            static bool zoomed = false;
            QSize sz = zoomed ? QSize(900, 560) : QSize(1150, 720);
            zoomed = !zoomed;
            resize(sz);
            if (QScreen *s = QApplication::primaryScreen())
                move(s->availableGeometry().center()
                     - QPoint(sz.width()/2, sz.height()/2));
        });
        hh->addWidget(closeBtn);
        hh->addWidget(minBtn);
        hh->addWidget(maxBtn);
        hh->addSpacing(12);

        m_title = new QLabel("Files");
        hh->addWidget(m_title);
        hh->addStretch();
        root->addWidget(m_header);

        // ---- Toolbar: nav + breadcrumb + search ----
        m_toolbar = new QWidget;
        m_toolbar->setFixedHeight(46);
        m_toolbar->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *th = new QHBoxLayout(m_toolbar);
        th->setContentsMargins(16, 8, 16, 8);
        th->setSpacing(8);

        auto mkNavBtn = [this](const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(28, 28);
            b->setCursor(Qt::PointingHandCursor);
            b->setFlat(true);
            m_navButtons.append(b);
            return b;
        };
        m_backBtn = mkNavBtn(QString::fromUtf8("\xE2\x86\x90"));
        m_fwdBtn  = mkNavBtn(QString::fromUtf8("\xE2\x86\x92"));
        m_upBtn   = mkNavBtn(QString::fromUtf8("\xE2\x86\x91"));

        QObject::connect(m_backBtn, &QPushButton::clicked, [this]() { goBack(); });
        QObject::connect(m_fwdBtn,  &QPushButton::clicked, [this]() { goForward(); });
        QObject::connect(m_upBtn,   &QPushButton::clicked, [this]() {
            QDir d(m_currentPath);
            if (d.cdUp()) navigateTo(d.absolutePath());
        });

        th->addWidget(m_backBtn);
        th->addWidget(m_fwdBtn);
        th->addWidget(m_upBtn);
        th->addSpacing(10);

        m_breadcrumb = new QLabel;
        m_breadcrumb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        th->addWidget(m_breadcrumb, 1);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search");
        m_search->setFixedWidth(180);
        m_search->setAttribute(Qt::WA_MacShowFocusRect, false);
        QObject::connect(m_search, &QLineEdit::textChanged, [this](const QString &s) {
            if (s.isEmpty()) {
                m_proxy->setFilterFixedString("");
            } else {
                m_proxy->setFilterFixedString(s);
            }
        });
        th->addWidget(m_search);

        root->addWidget(m_toolbar);

        // ---- Body: sidebar + file view ----
        QHBoxLayout *body = new QHBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        m_sidebar = new QWidget;
        m_sidebar->setFixedWidth(170);
        m_sidebar->setAttribute(Qt::WA_NoSystemBackground, true);
        QVBoxLayout *sb = new QVBoxLayout(m_sidebar);
        sb->setContentsMargins(10, 12, 10, 12);
        sb->setSpacing(3);

        struct Place { const char *icon; const char *name; const char *path; };
        QString home = QDir::homePath();
        const Place places[] = {
            { "\xF0\x9F\x8F\xA0", "Home",      "%HOME%"      },
            { "\xF0\x9F\x96\xA5", "Desktop",   "%HOME%/Desktop" },
            { "\xF0\x9F\x93\x84", "Documents", "%HOME%/Documents" },
            { "\xE2\xAC\x87",     "Downloads", "%HOME%/Downloads" },
            { "\xF0\x9F\x8E\xB5", "Music",     "%HOME%/Music" },
            { "\xF0\x9F\x96\xBC", "Pictures",  "%HOME%/Pictures" },
            { "\xF0\x9F\x8E\xAC", "Videos",    "%HOME%/Videos" },
        };
        for (const auto &p : places) {
            QString path = QString(p.path).replace("%HOME%", home);
            QPushButton *b = new QPushButton(
                QString("%1  %2").arg(QString::fromUtf8(p.icon), p.name));
            b->setCursor(Qt::PointingHandCursor);
            b->setFlat(true);
            b->setStyleSheet("");  // styled in restyleAll
            m_placeButtons.append(b);
            sb->addWidget(b);
            QObject::connect(b, &QPushButton::clicked,
                             [this, path]() { navigateTo(path); });
        }
        sb->addStretch();

        // ---- File view ----
        m_fs = new QFileSystemModel(this);
        m_fs->setRootPath(QDir::homePath());
        m_fs->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
        m_fs->setReadOnly(true);

        m_proxy = new QSortFilterProxyModel(this);
        m_proxy->setSourceModel(m_fs);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

        m_view = new QListView;
        m_view->setModel(m_proxy);
        m_view->setViewMode(QListView::IconMode);
        m_view->setIconSize(QSize(56, 56));
        m_view->setGridSize(QSize(140, 120));
        m_view->setTextElideMode(Qt::ElideMiddle);
        m_view->setWordWrap(true);
        m_view->setResizeMode(QListView::Adjust);
        m_view->setMovement(QListView::Static);
        m_view->setWordWrap(true);
        m_view->setUniformItemSizes(true);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_view->setFrameShape(QFrame::NoFrame);

        m_view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(m_view, &QListView::customContextMenuRequested,
                         this, &FileExplorer::showContextMenu);

        QObject::connect(m_view, &QListView::doubleClicked,
                         [this](const QModelIndex &idx) {
            flog(QString("doubleClicked valid=%1").arg(idx.isValid()));
            QModelIndex srcIdx = m_proxy->mapToSource(idx);
            QString path = m_fs->filePath(srcIdx);
            QFileInfo fi(path);
            flog(QString("  -> path=%1 isDir=%2").arg(path).arg(fi.isDir()));
            if (fi.isDir()) navigateTo(path);
        });

        // Also log single-click for diagnostics
        QObject::connect(m_view, &QListView::clicked,
                         [this](const QModelIndex &idx) {
            flog(QString("clicked valid=%1").arg(idx.isValid()));
        });

        body->addWidget(m_sidebar);
        body->addWidget(m_view, 1);
        root->addLayout(body, 1);

        // ---- Floating status toast (bottom-right of window) ----
        m_statusLabel = new QLabel(this);
        m_statusLabel->setVisible(false);
        m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_statusLabel->setStyleSheet(
            "QLabel { background: rgba(0,0,0,180); color: white;"
            "  padding: 6px 12px; border-radius: 8px;"
            "  font-size: 12px; }");
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_statusLabel) {
            m_statusLabel->adjustSize();
            m_statusLabel->move(width() - m_statusLabel->width() - 20,
                                height() - m_statusLabel->height() - 20);
        }
    }

    void navigateTo(const QString &path) {
        QFileInfo fi(path);
        flog(QString("navigateTo path=%1 exists=%2 isDir=%3")
             .arg(path)
             .arg(fi.exists())
             .arg(fi.isDir()));
        if (!fi.exists() || !fi.isDir()) return;

        if (!m_currentPath.isEmpty() && m_currentPath != path) {
            m_history = m_history.mid(0, m_historyIndex + 1);
            m_history.append(path);
            m_historyIndex = m_history.size() - 1;
        } else if (m_history.isEmpty()) {
            m_history.append(path);
            m_historyIndex = 0;
        }

        m_currentPath = path;
        m_view->setRootIndex(m_proxy->mapFromSource(m_fs->index(path)));
        m_breadcrumb->setText(shortenPath(path));
        updateNavButtons();
    }

    void goBack() {
        if (m_historyIndex <= 0) return;
        --m_historyIndex;
        m_currentPath = m_history[m_historyIndex];
        m_view->setRootIndex(m_proxy->mapFromSource(
            m_fs->index(m_currentPath)));
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
    }

    void goForward() {
        if (m_historyIndex >= m_history.size() - 1) return;
        ++m_historyIndex;
        m_currentPath = m_history[m_historyIndex];
        m_view->setRootIndex(m_proxy->mapFromSource(
            m_fs->index(m_currentPath)));
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
    }

    void updateNavButtons() {
        m_backBtn->setEnabled(m_historyIndex > 0);
        m_fwdBtn->setEnabled(m_historyIndex < m_history.size() - 1);
    }

    QString selectedPath() const {
        QModelIndex idx = m_view->currentIndex();
        if (!idx.isValid()) return {};
        return m_fs->filePath(m_proxy->mapToSource(idx));
    }

    void openSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        if (fi.isDir()) navigateTo(p);
        else QProcess::startDetached("xdg-open", { p });
    }

    void renameSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        bool ok = false;
        QString newName = ApokolipsInputDialog::getText(this, "Rename",
            "New name:", fi.fileName(), &ok);
        if (!ok || newName.isEmpty() || newName == fi.fileName()) return;
        QDir d(fi.absolutePath());
        if (d.rename(fi.fileName(), newName))
            showStatus("Renamed to " + newName);
        else
            showStatus("Rename failed");
    }

    void deleteSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        // Prefer trash, fall back to permanent
        QProcess tr;
        tr.start("gio", { "trash", p });
        tr.waitForFinished(1500);
        bool ok = (tr.exitStatus() == QProcess::NormalExit && tr.exitCode() == 0);
        if (!ok) {
            if (fi.isDir()) QDir(p).removeRecursively();
            else            QFile::remove(p);
        }
        showStatus("Moved to trash: " + fi.fileName());
    }

    void copySelected(bool cut) {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        m_clipboardPaths = { p };
        m_clipboardCut = cut;
        showStatus((cut ? "Cut: " : "Copied: ") + QFileInfo(p).fileName());
    }

    void pasteInto() {
        if (m_clipboardPaths.isEmpty()) return;
        QDir dest(m_currentPath);
        for (const QString &srcPath : m_clipboardPaths) {
            QFileInfo fi(srcPath);
            QString target = dest.filePath(fi.fileName());
            // Avoid overwriting
            if (QFile::exists(target)) {
                QString base = fi.completeBaseName();
                QString ext = fi.suffix().isEmpty()
                              ? "" : "." + fi.suffix();
                int n = 1;
                while (QFile::exists(target)) {
                    target = dest.filePath(
                        QString("%1 copy %2%3").arg(base).arg(n).arg(ext));
                    ++n;
                }
            }
            if (m_clipboardCut) {
                if (fi.isDir()) {
                    // Qt needs the dir to not exist for rename
                    QProcess::startDetached("mv", { srcPath, target });
                } else {
                    QProcess::startDetached("mv", { srcPath, target });
                }
            } else {
                if (fi.isDir())
                    QProcess::startDetached("cp",
                        { "-r", srcPath, target });
                else
                    QProcess::startDetached("cp", { srcPath, target });
            }
        }
        showStatus(m_clipboardCut ? "Moved" : "Pasted");
        if (m_clipboardCut) { m_clipboardPaths.clear(); m_clipboardCut = false; }
    }

    void newFolder() {
        QDir dest(m_currentPath);
        QString target = dest.filePath("New Folder");
        int n = 1;
        while (QFile::exists(target))
            target = dest.filePath(QString("New Folder %1").arg(++n));
        if (dest.mkdir(QFileInfo(target).fileName()))
            showStatus("New folder created");
    }

    void newFile() {
        QDir dest(m_currentPath);
        QString target = dest.filePath("Untitled.txt");
        int n = 1;
        while (QFile::exists(target))
            target = dest.filePath(QString("Untitled %1.txt").arg(++n));
        QFile f(target);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
            showStatus("New file created");
        }
    }

    void toggleHidden() {
        m_showHidden = !m_showHidden;
        m_fs->setFilter(m_showHidden
            ? (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
            : (QDir::AllEntries | QDir::NoDotAndDotDot));
        showStatus(m_showHidden ? "Hidden files shown" : "Hidden files hidden");
    }

    void showStatus(const QString &msg) {
        if (!m_statusLabel) return;
        m_statusLabel->setText(msg);
        m_statusLabel->setVisible(true);
        QTimer::singleShot(2200, m_statusLabel, [this]() {
            if (m_statusLabel) m_statusLabel->setVisible(false);
        });
    }

    void showContextMenu(const QPoint &pos) {
        QModelIndex idx = m_view->indexAt(pos);
        if (idx.isValid()) m_view->setCurrentIndex(idx);

        const Theme &t = ThemeManager::instance().current();
        QMenu menu(this);
        menu.setStyleSheet(QString(
            "QMenu { background: %1; border: 1px solid %2;"
            "  border-radius: 8px; padding: 5px; color: %3;"
            "  font-size: 12px; }"
            "QMenu::item { padding: 6px 20px 6px 14px; border-radius: 5px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %2;"
            "  margin: 4px 8px; }")
            .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                 t.textPrimary.name(), rgba(t.accentSoft)));

        QAction *aOpen   = menu.addAction("Open");
        QAction *aRename = menu.addAction("Rename");
        menu.addSeparator();
        QAction *aCut   = menu.addAction("Cut");
        QAction *aCopy  = menu.addAction("Copy");
        QAction *aPaste = menu.addAction("Paste");
        menu.addSeparator();
        QAction *aNewFolder = menu.addAction("New Folder");
        QAction *aNewFile   = menu.addAction("New File");
        menu.addSeparator();
        QAction *aDelete = menu.addAction("Move to Trash");

        bool hasSel = idx.isValid();
        aOpen->setEnabled(hasSel);
        aRename->setEnabled(hasSel);
        aCut->setEnabled(hasSel);
        aCopy->setEnabled(hasSel);
        aDelete->setEnabled(hasSel);
        aPaste->setEnabled(!m_clipboardPaths.isEmpty());

        QObject::connect(aOpen,      &QAction::triggered, this, &FileExplorer::openSelected);
        QObject::connect(aRename,    &QAction::triggered, this, &FileExplorer::renameSelected);
        QObject::connect(aCut,       &QAction::triggered, [this]() { copySelected(true); });
        QObject::connect(aCopy,      &QAction::triggered, [this]() { copySelected(false); });
        QObject::connect(aPaste,     &QAction::triggered, this, &FileExplorer::pasteInto);
        QObject::connect(aNewFolder, &QAction::triggered, this, &FileExplorer::newFolder);
        QObject::connect(aNewFile,   &QAction::triggered, this, &FileExplorer::newFile);
        QObject::connect(aDelete,    &QAction::triggered, this, &FileExplorer::deleteSelected);

        menu.exec(m_view->mapToGlobal(pos));
    }

    QString shortenPath(const QString &p) const {
        QString home = QDir::homePath();
        if (p.startsWith(home)) return "~" + p.mid(home.size());
        return p;
    }

    void restyleAll() {
        const Theme &t = m_theme;

        m_title->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));

        m_breadcrumb->setStyleSheet(QString(
            "color: %1; font-size: 13px; background: transparent;")
            .arg(t.textPrimary.name()));

        m_search->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: none;"
            "  border-radius: 8px; padding: 5px 10px; font-size: 12px; }"
            "QLineEdit:focus { background: %3; }")
            .arg(rgba(t.chromeBg.lighter(130)),
                 t.textPrimary.name(),
                 rgba(t.chromeBg.lighter(150))));

        for (QPushButton *b : m_navButtons) {
            b->setStyleSheet(QString(
                "QPushButton { color: %1; background: transparent;"
                "  border: none; font-size: 15px; border-radius: 6px; }"
                "QPushButton:hover { background: %2; }"
                "QPushButton:disabled { color: %3; }")
                .arg(t.textPrimary.name(),
                     rgba(t.accentSoft),
                     t.textDim.name()));
        }

        for (QPushButton *b : m_placeButtons) {
            b->setStyleSheet(QString(
                "QPushButton { text-align: left; padding: 6px 10px;"
                "  color: %1; background: transparent; border: none;"
                "  border-radius: 8px; font-size: 13px; }"
                "QPushButton:hover { background: %2; color: %3; }")
                .arg(t.textSecondary.name(),
                     rgba(t.accentSoft),
                     t.textPrimary.name()));
        }

        m_view->setStyleSheet(QString(
            "QListView { background: transparent; border: none;"
            "  padding: 12px; color: %1; font-size: 12px; }"
            "QListView::item { padding: 8px; border-radius: 8px; }"
            "QListView::item:hover { background: %2; }"
            "QListView::item:selected { background: %3; color: %4; }")
            .arg(t.textPrimary.name(),
                 rgba(t.accentSoft),
                 rgba(t.accent),
                 t.textPrimary.name()));
    }

    Theme m_theme;
    QPixmap m_blurredBg;
    QWidget *m_header = nullptr;
    QWidget *m_toolbar = nullptr;
    QWidget *m_sidebar = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_fwdBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QListView *m_view = nullptr;
    QFileSystemModel *m_fs = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QList<QPushButton *> m_navButtons;
    QList<QPushButton *> m_placeButtons;
    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex = -1;

    // ---- Clipboard (internal) + selection state ----
    QStringList m_clipboardPaths;
    bool        m_clipboardCut = false;
    bool        m_showHidden   = false;
    QLabel     *m_statusLabel  = nullptr;
};

// =========================================================
// Settings window
// =========================================================
class SettingsWindow : public QWidget {
public:
    explicit SettingsWindow(QWidget *parent = nullptr) : QWidget(parent) {
        StyleManager::load();   // <-- read saved style so buttons highlight correctly
        setWindowFlags(Qt::FramelessWindowHint);
        setWindowTitle("Apokolips Settings");
        setAttribute(Qt::WA_TranslucentBackground, true);
        resize(720, 480);

        if (QScreen *s = QApplication::primaryScreen())
            move(s->availableGeometry().center() - QPoint(360, 240));

        buildUi();

        // Subscribe AFTER buildUi so restyleAll can see the widgets
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyleAll();
        });
        StyleManager::startPolling(this, [this]() { update(); });
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);

        // Style-aware body
        StyleRenderer::drawPanel(p, QRectF(rect().adjusted(0, 0, -1, -1)),
                                 14, m_theme, 235);

        QColor hdrCol = m_theme.chromeBg.lighter(115);
        hdrCol.setAlpha(235);

        // Header strip on top with rounded top corners
        QPainterPath hdrPath;
        hdrPath.addRoundedRect(QRect(0, 0, width(), 42), 14, 14);
        QPainterPath squareBottom;
        squareBottom.addRect(QRect(0, 14, width(), 42 - 14));
        QPainterPath hdrFinal = hdrPath.united(squareBottom);
        p.fillPath(hdrFinal, hdrCol);

        // Header bottom border
        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.drawLine(0, 42, width(), 42);

        // Sidebar separator line (right edge of sidebar)
        p.drawLine(m_sidebar->width(), 42,
                   m_sidebar->width(), height() - 1);
    }

private:
    void buildUi() {
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Header strip (draggable)
        m_header = new QWidget;
        m_header->setFixedHeight(42);
        m_header->setAttribute(Qt::WA_NoSystemBackground, true);
        m_header->setAutoFillBackground(false);
        QHBoxLayout *hh = new QHBoxLayout(m_header);
        hh->setContentsMargins(16, 0, 16, 0);
        hh->setSpacing(10);

        m_title = new QLabel("Settings");
        hh->addWidget(m_title);
        hh->addStretch();

        // macOS traffic lights: close / minimize / zoom
        auto makeLight = [](const QString &idle, const QString &hover) {
            QPushButton *b = new QPushButton;
            b->setFixedSize(13, 13);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1;"
                "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
                "QPushButton:hover { background: %2; }")
                .arg(idle, hover));
            return b;
        };
        m_closeBtn = makeLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn = makeLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn = makeLight("#155c1e", "#28c840");

        QObject::connect(m_closeBtn, &QPushButton::clicked,
                         [this]() { this->close(); this->deleteLater(); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         [this]() { this->hide(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            // Cage supports client-side resize; use that for zoom.
            static bool zoomed = false;
            QSize sz = zoomed ? QSize(720, 480) : QSize(1100, 750);
            zoomed = !zoomed;
            this->resize(sz);
            if (QScreen *s = QApplication::primaryScreen())
                this->move(s->availableGeometry().center()
                           - QPoint(sz.width()/2, sz.height()/2));
        });

        // Traffic lights go on the LEFT (macOS style)
        hh->insertWidget(0, m_closeBtn);
        hh->insertWidget(1, minBtn);
        hh->insertWidget(2, maxBtn);
        hh->insertSpacing(3, 8);

        // Right side: keep it clean, no close button
        m_minBtn = minBtn;
        m_maxBtn = maxBtn;

        root->addWidget(m_header);

        // Body: sidebar + content
        QHBoxLayout *body = new QHBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        m_sidebar = new QWidget;
        m_sidebar->setFixedWidth(180);
        m_sidebar->setAttribute(Qt::WA_NoSystemBackground, true);
        m_sidebar->setAutoFillBackground(false);
        QVBoxLayout *sb = new QVBoxLayout(m_sidebar);
        sb->setContentsMargins(12, 16, 12, 16);
        sb->setSpacing(6);

        m_stack = new QStackedWidget;

        addSection(sb, "Appearance", makeAppearancePage());
        addSection(sb, "Wallpaper",  makeWallpaperPage());
        addSection(sb, "Dock",       makeDockPage());
        addSection(sb, "About",      makeAboutPage());

        sb->addStretch();
        body->addWidget(m_sidebar);
        body->addWidget(m_stack, 1);
        root->addLayout(body, 1);

        restyleAll();
    }

    void addSection(QVBoxLayout *sb, const QString &name, QWidget *page) {
        QPushButton *btn = new QPushButton(name);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        if (m_stack->count() == 0) btn->setChecked(true);
        m_sidebarButtons.append(btn);
        sb->addWidget(btn);

        int idx = m_stack->addWidget(page);
        QObject::connect(btn, &QPushButton::clicked,
                         [this, idx]() { m_stack->setCurrentIndex(idx); });
    }

    QWidget *makeAppearancePage() {
        QWidget *page = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 24);
        v->setSpacing(18);

        v->addWidget(sectionHeader("Appearance"));

        auto &vc = VisualConfigManager::instance().cfg();

        addSlider(v, "Blur strength",
                  int(vc.blurStrength * 100), 0, 100,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.blurStrength = pct / 100.0; });
        });

        addSlider(v, "Top bar opacity", vc.topBarAlpha, 0, 255,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.topBarAlpha = pct; });
        });

        addSlider(v, "Dock opacity", vc.dockAlpha, 0, 255,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.dockAlpha = pct; });
        });

        addSlider(v, "Panel opacity", vc.launcherAlpha, 0, 255,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) {
                    c.launcherAlpha = pct;
                    c.controlCenterAlpha = pct;
                });
        });

        addSlider(v, "Widget opacity", vc.widgetCardAlpha, 0, 255,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.widgetCardAlpha = pct; });
        });

        addSlider(v, "Finder opacity", vc.finderAlpha, 0, 255,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.finderAlpha = pct; });
        });

        v->addSpacing(24);

        // ---- UI STYLE ----
        v->addWidget(sectionHeader("UI Style"));

        QGridLayout *styleGrid = new QGridLayout;
        styleGrid->setSpacing(12);

        struct StyleOpt { VisualStyle s; const char *name; };
        const StyleOpt styleOpts[] = {
            { VisualStyle::Glass,         "Glass"         },
            { VisualStyle::Neumorphism,   "Neumorphism"   },
            { VisualStyle::Skeuomorphism, "Skeuomorphism" },
            { VisualStyle::Claymorphism,  "Claymorphism"  }
        };
        int col = 0;
        for (const auto &opt : styleOpts) {
            QPushButton *btn = new QPushButton(opt.name);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(56);
            btn->setProperty("styleId", static_cast<int>(opt.s));
            m_styleButtons.append(btn);
            QObject::connect(btn, &QPushButton::clicked,
                             [this, s = opt.s]() {
                StyleManager::set(s);
                restyleStyleButtons();
            });
            styleGrid->addWidget(btn, 0, col++);
        }
        v->addLayout(styleGrid);

        v->addSpacing(24);
        QPushButton *resetBtn = new QPushButton("Reset to Defaults");
        resetBtn->setCursor(Qt::PointingHandCursor);
        resetBtn->setFixedHeight(38);
        QObject::connect(resetBtn, &QPushButton::clicked, [this]() {
            // Reset visual tunables (opacities, blur, magnify)
            VisualConfigManager::instance().resetToDefaults();

            // Reset UI style back to Glass (default)
            StyleManager::set(VisualStyle::Glass);

            // Reflect the new values in the sliders
            auto &c = VisualConfigManager::instance().cfg();
            if (m_sliders.size() >= 6) {
                m_sliders[0]->setValue(int(c.blurStrength * 100));
                m_sliders[1]->setValue(c.topBarAlpha);
                m_sliders[2]->setValue(c.dockAlpha);
                m_sliders[3]->setValue(c.launcherAlpha);
                m_sliders[4]->setValue(c.widgetCardAlpha);
                m_sliders[5]->setValue(c.finderAlpha);
            }

            // Re-highlight the Glass style button
            restyleStyleButtons();
        });
        v->addWidget(resetBtn);

        v->addStretch();
        return page;
    }

    QWidget *makeWallpaperPage() {
        QWidget *page = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 24);
        v->setSpacing(18);

        v->addWidget(sectionHeader("Wallpaper"));

        QGridLayout *g = new QGridLayout;
        g->setSpacing(16);

        struct WP { const char *id; const char *name; };
        const WP wps[] = {
            { "ruby",       "Ruby"       },
            { "starfield",  "Starfield"  },
            { "aurora",     "Aurora"     },
            { "goldengate", "Golden Gate" },
            { "everest",    "Everest"    }
        };
        int col = 0, row = 0;
        for (const auto &wp : wps) {
            QPushButton *btn = new QPushButton(wp.name);
            btn->setFixedSize(140, 90);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("wpId", wp.id);
            m_wpButtons.append(btn);
            QObject::connect(btn, &QPushButton::clicked, [this, id = QString(wp.id)]() {
                WallpaperConfig::saveId(id);

                // Adopt the new theme immediately, in this process
                Wallpaper *wp = WallpaperConfig::makeById(id);
                Theme newT = wp->theme();
                delete wp;

                m_theme = newT;                              // local copy first
                ThemeManager::instance().setTheme(newT);     // fires subscribers

                updateWallpaperHighlight();
                restyleAll();

                // Force full repaint — paintEvent draws the border lines
                update();
                for (QWidget *child : findChildren<QWidget *>())
                    child->update();
            });
            g->addWidget(btn, row, col);
            if (++col == 3) { col = 0; ++row; }
        }
        v->addLayout(g);
        v->addStretch();
        return page;
    }

    QWidget *makeDockPage() {
        QWidget *page = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 24);
        v->setSpacing(18);

        v->addWidget(sectionHeader("Dock"));

        auto &vc = VisualConfigManager::instance().cfg();

        addSlider(v, "Magnification strength",
                  int(vc.dockMagnifyMax * 100), 0, 100,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.dockMagnifyMax = pct / 100.0; });
        });

        addSlider(v, "Magnification radius",
                  int(vc.dockSigma), 20, 120,
                  [](int pct) {
            VisualConfigManager::instance().setAndSave(
                [pct](VisualConfig &c) { c.dockSigma = pct; });
        });

        v->addStretch();
        return page;
    }

    QWidget *makeAboutPage() {
        QWidget *page = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 24);
        v->setSpacing(12);

        v->addWidget(sectionHeader("About"));

        m_aboutLines.clear();
        auto addLine = [&](const QString &s) {
            QLabel *l = new QLabel(s);
            m_aboutLines.append(l);
            v->addWidget(l);
        };
        addLine("Apokolips OS");
        addLine("Version 0.1 — build " __DATE__);
        addLine("Custom shell — C++17 / Qt6");
        addLine("github.com/lytone-lab/APOKOLIPS-OS");
        v->addStretch();
        return page;
    }

    QLabel *sectionHeader(const QString &text) {
        QLabel *l = new QLabel(text);
        l->setProperty("role", "header");
        m_sectionHeaders.append(l);
        return l;
    }

    void addSlider(QVBoxLayout *v, const QString &label,
                   int value, int lo, int hi,
                   std::function<void(int)> onChange)
    {
        QLabel *l = new QLabel(label);
        l->setProperty("role", "sliderLabel");
        m_sliderLabels.append(l);
        v->addWidget(l);

        QSlider *s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(value);
        m_sliders.append(s);
        v->addWidget(s);

        QObject::connect(s, &QSlider::valueChanged,
                         [onChange](int val) { onChange(val); });
    }

    void updateWallpaperHighlight() {
        QString current = WallpaperConfig::loadId();
        for (QPushButton *b : m_wpButtons) {
            bool sel = (b->property("wpId").toString() == current);
            b->setProperty("selected", sel);
        }
    }

    void restyleAll() {
        const Theme &t = m_theme;
        updateWallpaperHighlight();

        // No global stylesheet — paintEvent handles window bg now

        // header background painted in paintEvent

        m_title->setStyleSheet(QString(
            "color: %1; font-size: 14px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));

        // sidebar background is transparent; separator painted in paintEvent

        for (QPushButton *b : m_sidebarButtons) {
            b->setStyleSheet(QString(
                "QPushButton { text-align: left; padding: 8px 12px;"
                "  color: %1; background: transparent; border: none;"
                "  border-radius: 8px; font-size: 13px; }"
                "QPushButton:hover { background: %2; color: %3; }"
                "QPushButton:checked { background: %4; color: %5; }")
                .arg(t.textSecondary.name(),
                     rgba(t.accentSoft),
                     t.textPrimary.name(),
                     rgba(t.accentSoft),
                     t.textPrimary.name()));
        }

        for (QLabel *l : m_sectionHeaders) {
            l->setStyleSheet(QString(
                "color: %1; font-size: 20px; font-weight: 300;"
                " letter-spacing: 1px; background: transparent;")
                .arg(t.textPrimary.name()));
        }
        for (QLabel *l : m_sliderLabels) {
            l->setStyleSheet(QString(
                "color: %1; font-size: 12px; letter-spacing: 1px;"
                " margin-top: 6px; background: transparent;")
                .arg(t.textSecondary.name()));
        }
        for (QSlider *s : m_sliders) {
            s->setStyleSheet(QString(
                "QSlider::groove:horizontal {"
                "  height: 6px; background: %1; border-radius: 3px; }"
                "QSlider::handle:horizontal {"
                "  background: %2; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: %2; border-radius: 3px; }")
                .arg(rgba(t.chromeBg.lighter(140)), t.accent.name()));
        }
        for (QPushButton *b : m_wpButtons) {
            bool sel = b->property("selected").toBool();
            QColor border = sel ? t.accent : t.chromeBorder;
            int bw = sel ? 2 : 1;
            b->setStyleSheet(QString(
                "QPushButton { background: %1; color: %2;"
                "  border: %3px solid %4; border-radius: 10px;"
                "  font-size: 13px; font-weight: %5; }"
                "QPushButton:hover { background: %6; border-color: %7; }")
                .arg(rgba(t.chromeBg.lighter(115)),
                     t.textPrimary.name())
                .arg(bw)
                .arg(rgba(border))
                .arg(sel ? 600 : 400)
                .arg(rgba(t.accentSoft))
                .arg(t.accent.name()));
        }
        for (QLabel *l : m_aboutLines) {
            l->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(t.textSecondary.name()));
        }

        restyleStyleButtons();

        // Force repaint so paintEvent-drawn lines (header, sidebar separator)
        // pick up the new theme colors.
        update();
    }

    Theme m_theme;
    QWidget *m_header = nullptr;
    QLabel *m_title = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_minBtn = nullptr;
    QPushButton *m_maxBtn = nullptr;
    QWidget *m_sidebar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<QPushButton *> m_sidebarButtons;
    QList<QSlider *> m_sliders;
    QList<QLabel *> m_sliderLabels;
    QList<QLabel *> m_sectionHeaders;
    QList<QPushButton *> m_wpButtons;
    QList<QLabel *> m_aboutLines;
    QList<QPushButton *> m_styleButtons;

    void restyleStyleButtons() {
        if (m_styleButtons.isEmpty()) return;
        int cur = static_cast<int>(StyleManager::current());
        const Theme &t = m_theme;
        for (QPushButton *b : m_styleButtons) {
            bool sel = (b->property("styleId").toInt() == cur);
            QColor border = sel ? t.accent : t.chromeBorder;
            int bw = sel ? 2 : 1;
            b->setStyleSheet(QString(
                "QPushButton {"
                "  background: %1;"
                "  color: %2;"
                "  border: %3px solid %4;"
                "  border-radius: 12px;"
                "  font-size: 13px;"
                "  font-weight: %5;"
                "}"
                "QPushButton:hover { background: %6; }")
                .arg(rgba(t.chromeBg.lighter(125)),
                     t.textPrimary.name())
                .arg(bw)
                .arg(rgba(border))
                .arg(sel ? 600 : 400)
                .arg(rgba(t.accentSoft)));
        }
    }
};

// =========================================================
// Weather — Open-Meteo (free, no API key)
// =========================================================
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

} // namespace Weather

class DesktopWidgets : public QWidget {
public:
    explicit DesktopWidgets(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        // NOTE: do NOT set WA_TransparentForMouseEvents — we need mouse
        // events for hover magnify. The dock conflict is handled in the
        // dock's own widget instead.
        buildUi();

        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(1000);
        QObject::connect(m_tickTimer, &QTimer::timeout,
                         this, &DesktopWidgets::refresh);
        m_tickTimer->start();
        refresh();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });

        m_weather = new Weather::Fetcher(this);
        auto doFetch = [this]() {
            m_weather->fetch([this](const Weather::Snapshot &s) {
                m_wx = s;
                update();
            });
        };
        doFetch();
        QTimer *wxTimer = new QTimer(this);
        wxTimer->setInterval(10 * 60 * 1000);
        QObject::connect(wxTimer, &QTimer::timeout, this, doFetch);
        wxTimer->start();

        // Hover magnify: track mouse, animate at 60 Hz
        setMouseTracking(true);
        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setInterval(16);
        QObject::connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            bool needsRepaint = false;
            for (int i = 0; i < 4; ++i) {
                double target = (i == m_hoverCard) ? 1.04 : 1.0;
                double diff   = target - m_scales[i];
                if (qAbs(diff) > 0.001) {
                    m_scales[i] += diff * 0.22;
                    needsRepaint = true;
                }
            }
            if (needsRepaint) update();
        });
        m_hoverTimer->start();
    }

protected:
    void mouseMoveEvent(QMouseEvent *e) override {
        handleHoverAt(e->position().toPoint());
        QWidget::mouseMoveEvent(e);
    }

    // Public: allow parent to forward hover coords
public:
    void handleHoverAt(const QPoint &localPos) {
        int idx = cardAt(localPos);
        if (idx != m_hoverCard) {
            m_hoverCard = idx;
            update();
        }
    }
protected:

    void leaveEvent(QEvent *e) override {
        if (m_hoverCard != -1) {
            m_hoverCard = -1;
            update();
        }
        QWidget::leaveEvent(e);
    }

public:

    void repositionTo(const QSize &parentSize) {
        const int CARD_W = 240;
        setFixedWidth(CARD_W);
        move(parentSize.width() - CARD_W - 20, 50);
        setFixedHeight(qMin(parentSize.height() - 130, 560));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int y = 0;

        // --- Clock card ---
        drawCardScaled(p, 0, QRect(0, y, width(), 96));
        {
            QFont f = font(); f.setPixelSize(34); f.setWeight(QFont::Light);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(QRect(14, y + 12, width()-28, 44),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QDateTime::currentDateTime().toString("HH:mm"));
            f.setPixelSize(12); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textSecondary);
            p.drawText(QRect(14, y + 58, width()-28, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QDateTime::currentDateTime().toString("dddd, d MMMM"));
            p.setPen(m_theme.textDim);
            p.drawText(QRect(14, y + 74, width()-28, 14),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QDateTime::currentDateTime().toString("yyyy"));
        }
        y += 96 + 12;

        // --- System rings ---
        drawCardScaled(p, 1, QRect(0, y, width(), 132));
        {
            int r = 34;
            int gap = 32;
            int totalW = 4*r + gap;            // 4 radiuses + gap
            int leftPad = (width() - totalW) / 2;
            int cy = y + 56;
            int cx1 = leftPad + r;
            int cx2 = leftPad + 3*r + gap;

            drawRing(p, QPoint(cx1, cy), r, m_cpuVal, m_theme.accent);
            drawRing(p, QPoint(cx2, cy), r, m_ramVal, m_theme.accent.lighter(120));

            QFont f = font(); f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(QRect(cx1 - r, cy - 8, 2*r, 16), Qt::AlignCenter,
                       QString::number(int(m_cpuVal)));
            p.drawText(QRect(cx2 - r, cy - 8, 2*r, 16), Qt::AlignCenter,
                       QString::number(int(m_ramVal)));

            f.setPixelSize(10); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(cx1 - r, cy + r - 8, 2*r, 14), Qt::AlignCenter, "CPU");
            p.drawText(QRect(cx2 - r, cy + r - 8, 2*r, 14), Qt::AlignCenter, "RAM");
        }
        y += 128 + 12;

        // --- Weather card ---
        drawCardScaled(p, 2, QRect(0, y, width(), 92));
        {
            QFont f = font(); f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(14, y + 10, width()-28, 14),
                       Qt::AlignLeft, "WEATHER  ·  KAMPALA");

            f.setPixelSize(30); f.setWeight(QFont::Light);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(QRect(14, y + 28, 80, 48),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(int(m_wx.tempC + 0.5)) + "°");

            f.setPixelSize(11); f.setWeight(QFont::Normal);
            p.setFont(f);
            int infoX = 92;
            int infoW = width() - infoX - 14;
            p.setPen(m_theme.textSecondary);
            p.drawText(QRect(infoX, y + 30, infoW, 16),
                       Qt::AlignLeft, m_wx.condition);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(infoX, y + 48, infoW, 16),
                       Qt::AlignLeft,
                       QString("H: %1°  L: %2°")
                           .arg(int(m_wx.tempMax + 0.5))
                           .arg(int(m_wx.tempMin + 0.5)));
            p.drawText(QRect(infoX, y + 64, infoW, 16),
                       Qt::AlignLeft,
                       QString("Wind %1 km/h")
                           .arg(int(m_wx.windKmh + 0.5)));
            p.drawText(QRect(infoX, y + 64, infoW, 16),
                       Qt::AlignRight,
                       QString("HUM %1%").arg(m_wx.humidityPct));
        }
        y += 92 + 12;

        // --- Mini calendar ---
        drawCardScaled(p, 3, QRect(0, y, width(), 130));
        {
            QDate today = QDate::currentDate();
            QString monthName = today.toString("MMMM yyyy").toUpper();

            QFont f = font(); f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(14, y + 10, width()-28, 14),
                       Qt::AlignLeft, monthName);

            int gridTop = y + 32;
            int cellW = (width() - 28) / 7;
            int cellH = 16;
            const char *names[] = {"M","T","W","T","F","S","S"};
            f.setPixelSize(9); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            for (int i = 0; i < 7; ++i)
                p.drawText(QRect(14 + i*cellW, gridTop, cellW, 12),
                           Qt::AlignCenter, names[i]);

            QDate first(today.year(), today.month(), 1);
            int startCol = (first.dayOfWeek() + 6) % 7;
            int daysInMonth = today.daysInMonth();
            for (int d = 1; d <= daysInMonth; ++d) {
                int idx = startCol + d - 1;
                int row = idx / 7, col = idx % 7;
                QRect cell(14 + col*cellW, gridTop + 16 + row*cellH,
                           cellW, cellH);
                bool isToday = (d == today.day());
                if (isToday) {
                    p.setBrush(m_theme.accent);
                    p.setPen(Qt::NoPen);
                    p.drawRoundedRect(cell.adjusted(2, 1, -2, -1), 5, 5);
                    p.setPen(m_theme.textPrimary);
                } else {
                    p.setPen(m_theme.textSecondary);
                }
                p.drawText(cell, Qt::AlignCenter, QString::number(d));
            }
        }
    }

private:
    void drawCardScaled(QPainter &p, int idx, const QRect &r) {
        double s = m_scales[idx];
        if (qAbs(s - 1.0) < 0.002) {
            drawCard(p, r);
            return;
        }

        // Pivot at card center
        QPointF ctr = r.center();
        p.save();
        p.translate(ctr);
        p.scale(s, s);
        p.translate(-ctr);

        // Subtle glow behind hovered card
        if (s > 1.005) {
            QColor glow = m_theme.textPrimary;
            glow.setAlpha(int(20 * (s - 1.0) / 0.04));
            p.setPen(Qt::NoPen);
            for (int i = 8; i >= 1; --i) {
                p.setBrush(glow);
                p.drawRoundedRect(r.adjusted(-i, -i, i, i),
                                  14 + i, 14 + i);
            }
        }

        drawCard(p, r);
        p.restore();
    }

    void drawCard(QPainter &p, const QRect &r) {
        // Soft outer shadow
        QColor shadowBase = m_theme.textPrimary;
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = shadowBase;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(r.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // Blur backdrop
        const auto &vc = VisualConfigManager::instance().cfg();
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        QPainterPath path;
        path.addRoundedRect(r, 14, 14);
        if (!bg.isNull() && window() && vc.blurStrength > 0.001) {
            p.save();
            p.setClipPath(path);
            QPoint pos = mapTo(window(), QPoint(0, 0));
            p.setOpacity(vc.blurStrength);
            p.drawPixmap(r, bg, QRect(pos + r.topLeft(), r.size()));
            p.restore();
        }

        // Style-aware panel
        StyleRenderer::drawPanel(p, QRectF(r), 14, m_theme,
                                 vc.widgetCardAlpha);
    }

    void drawRing(QPainter &p, const QPoint &c, int r,
                  double percent, const QColor &col)
    {
        QRect outer(c.x()-r, c.y()-r, 2*r, 2*r);
        p.setBrush(Qt::NoBrush);

        QColor track = m_theme.chromeBorder;
        track.setAlpha(60);
        p.setPen(QPen(track, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(outer, 0, 360*16);

        p.setPen(QPen(col, 5, Qt::SolidLine, Qt::RoundCap));
        int span = int(360.0 * 16.0 * (percent / 100.0));
        p.drawArc(outer, 90*16, -span);
    }

    void refresh() {
        m_cpuVal = SysStats::cpuPercent();
        m_ramVal = SysStats::ramPercent();
        update();
    }

    void buildUi() {}

    QTimer *m_tickTimer = nullptr;
    Theme m_theme;
    double m_cpuVal = 0.0;
    double m_ramVal = 0.0;
    Weather::Fetcher  *m_weather = nullptr;
    Weather::Snapshot  m_wx;

    // ---- Hover magnify ----
    QTimer *m_hoverTimer = nullptr;
    int    m_hoverCard = -1;          // -1 = none
    double m_scales[4] = {1.0, 1.0, 1.0, 1.0};

    // Card Y bands (top-left positions of each card)
    QRect cardRect(int idx) const {
        switch (idx) {
            case 0: return QRect(0,   0, width(),  96);
            case 1: return QRect(0, 108, width(), 128);
            case 2: return QRect(0, 248, width(),  92);
            case 3: return QRect(0, 352, width(), 130);
        }
        return {};
    }

    int cardAt(const QPoint &pos) const {
        for (int i = 0; i < 4; ++i) {
            if (cardRect(i).contains(pos)) return i;
        }
        return -1;
    }
};

// =========================================================
// Spotlight — global search overlay (macOS-style)
// =========================================================
static volatile sig_atomic_t g_spotlightToggle = 0;

extern "C" void spotlightSignalHandler(int) {
    g_spotlightToggle = 1;
}

class SpotlightOverlay : public QWidget {
public:
    explicit SpotlightOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        buildUi();
        hide();

        m_scanTimer = new QTimer(this);
        m_scanTimer->setInterval(600);
        QObject::connect(m_scanTimer, &QTimer::timeout,
                         this, &SpotlightOverlay::updateSearch);
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        StyleManager::startPolling(this, [this]() { update(); });
    }

    void toggle() { isVisible() ? hideNow() : showNow(); }

    void showNow() {
        raise(); show(); setFocus();
        m_search->clear();
        m_search->setFocus();
        m_results->clear();
        updateSearch();
    }

    void hideNow() { hide(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Full-screen dim backdrop (theme-aware, subtle)
        QColor dim = (m_theme.textPrimary.lightness() > 128)
                     ? QColor(0, 0, 0, 90)
                     : QColor(0, 0, 0, 120);
        p.fillRect(rect(), dim);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            hideNow(); e->accept();
        } else if (e->key() == Qt::Key_Return ||
                   e->key() == Qt::Key_Enter) {
            activateCurrent(); e->accept();
        } else if (e->key() == Qt::Key_Down) {
            int i = m_results->currentRow();
            if (i < m_results->count() - 1)
                m_results->setCurrentRow(i + 1);
            e->accept();
        } else if (e->key() == Qt::Key_Up) {
            int i = m_results->currentRow();
            if (i > 0) m_results->setCurrentRow(i - 1);
            e->accept();
        } else {
            QWidget::keyPressEvent(e);
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        // Clicking outside the panel closes
        if (!m_panel || !m_panel->geometry().contains(e->pos()))
            hideNow();
        else
            QWidget::mousePressEvent(e);
    }

private:
    struct Result {
        QString kind;      // "App", "Calc", "Path"
        QString title;
        QString subtitle;
        QString payload;   // exe path, calc result, or path
    };

    void buildUi() {
        m_panel = new QWidget(this);
        m_panel->setAttribute(Qt::WA_NoSystemBackground, true);
        m_panel->setObjectName("spotlightPanel");

        QVBoxLayout *v = new QVBoxLayout(m_panel);
        v->setContentsMargins(20, 18, 20, 18);
        v->setSpacing(12);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Spotlight Search");
        m_search->setStyleSheet(
            "QLineEdit { background: transparent; border: none;"
            "  font-size: 22px; padding: 4px 2px; color: #f0f0f5; }");
        v->addWidget(m_search);

        m_results = new QListWidget;
        m_results->setFrameShape(QFrame::NoFrame);
        m_results->setStyleSheet(
            "QListWidget { background: transparent; border: none;"
            "  outline: none; color: #f0f0f5; font-size: 13px; }"
            "QListWidget::item { padding: 8px 6px; border-radius: 8px; }"
            "QListWidget::item:selected { background: rgba(255,255,255,40); }");
        v->addWidget(m_results, 1);

        QObject::connect(m_search, &QLineEdit::textChanged,
                         this, &SpotlightOverlay::updateSearch);
        QObject::connect(m_results, &QListWidget::itemActivated,
                         this, &SpotlightOverlay::activateCurrent);

        setLayout(new QVBoxLayout(this));
        layout()->setContentsMargins(0, 0, 0, 0);
    }

    void updateSearch() {
        m_results->clear();
        m_scanTimer->stop();
        QString q = m_search->text().trimmed();

        // ---- Calculator: if query looks mathy ----
        if (!q.isEmpty() && q.at(0).isDigit()) {
            QString val = tryCalculate(q);
            if (!val.isEmpty()) {
                addResult({"Calc", val, "Copy to clipboard", val});
            }
        }

        // ---- Paths: if starts with / or ~ ----
        if (q.startsWith("/") || q.startsWith("~")) {
            QString expanded = q;
            if (expanded.startsWith("~"))
                expanded.replace(0, 1, QDir::homePath());
            QDir d(expanded);
            if (d.exists()) {
                for (const QString &entry : d.entryList(
                         QDir::AllEntries | QDir::NoDotAndDotDot))
                {
                    addResult({"Path",
                               entry,
                               d.absolutePath() + "/" + entry,
                               d.absolutePath() + "/" + entry});
                    if (m_results->count() > 40) break;
                }
            }
        }

        // ---- Apps: scan .desktop files ----
        QDir appDir("/usr/share/applications");
        if (appDir.exists()) {
            QStringList files = appDir.entryList({"*.desktop"}, QDir::Files);
            int shown = 0;
            for (const QString &file : files) {
                if (shown > 25) break;
                QFile f(appDir.filePath(file));
                if (!f.open(QIODevice::ReadOnly)) continue;
                QString name, exec;
                while (!f.atEnd()) {
                    QString line = QString::fromUtf8(f.readLine()).trimmed();
                    if (line.startsWith("Name=") && name.isEmpty())
                        name = line.mid(5);
                    else if (line.startsWith("Exec="))
                        exec = line.mid(5).section(' ', 0, 0);
                    else if (line.startsWith("NoDisplay=true"))
                        name.clear();
                }
                f.close();
                if (name.isEmpty() || exec.isEmpty()) continue;
                if (!q.isEmpty() &&
                    !name.toLower().contains(q.toLower())) continue;
                addResult({"App", name, exec, exec});
                ++shown;
            }
        }
    }

    QString tryCalculate(const QString &expr) {
        // Very small safe evaluator: digits, + - * / ( ) . space
        for (QChar c : expr) {
            if (!c.isDigit() && !QString("+-*/() .").contains(c))
                return {};
        }
        QProcess py;
        py.start("python3", {"-c",
            QString("print(%1)").arg(expr)});
        py.waitForFinished(400);
        QString out = QString::fromUtf8(py.readAllStandardOutput()).trimmed();
        return out.isEmpty() ? QString() : out;
    }

    void addResult(const Result &r) {
        QString display = QString("%1   %2")
            .arg(r.title, -30)
            .arg(r.subtitle);
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, r.kind);
        item->setData(Qt::UserRole + 1, r.payload);
        m_results->addItem(item);
        if (m_results->currentRow() < 0)
            m_results->setCurrentRow(0);
    }

    void activateCurrent() {
        QListWidgetItem *it = m_results->currentItem();
        if (!it) return;
        QString kind = it->data(Qt::UserRole).toString();
        QString payload = it->data(Qt::UserRole + 1).toString();

        if (kind == "App") {
            QProcess::startDetached(payload);
        } else if (kind == "Path") {
            QProcess::startDetached("xdg-open", { payload });
        } else if (kind == "Calc") {
            QProcess wl;
            wl.start("wl-copy");
            wl.write(payload.toUtf8());
            wl.closeWriteChannel();
            wl.waitForFinished(300);
        }
        hideNow();
    }

    void restyle() {
        const Theme &t = m_theme;
        if (!m_panel) return;

        // Panel styling done via paintEvent fallback? No — do it here
        m_panel->setStyleSheet(QString(
            "#spotlightPanel {"
            "  background: rgba(28, 26, 34, 240);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 14px;"
            "}"));
        m_search->setStyleSheet(
            "QLineEdit { background: transparent; border: none;"
            "  font-size: 22px; padding: 4px 2px; color: #f0f0f5; }");
        m_results->setStyleSheet(
            "QListWidget { background: transparent; border: none;"
            "  outline: none; color: #f0f0f5; font-size: 13px; }"
            "QListWidget::item { padding: 8px 6px; border-radius: 8px; }"
            "QListWidget::item:selected { background: rgba(255,255,255,40); }");
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        int pw = qMin(640, width() - 80);
        int ph = qMin(460, height() - 120);
        m_panel->setGeometry((width() - pw) / 2, height() / 5,
                             pw, ph);
    }

    QWidget     *m_panel   = nullptr;
    QLineEdit   *m_search  = nullptr;
    QListWidget *m_results = nullptr;
    QTimer      *m_scanTimer = nullptr;
    Theme        m_theme;
};

// =========================================================
// Desktop background
// =========================================================
class DesktopBackground : public QWidget {
public:
    explicit DesktopBackground(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);

        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(16);
        QObject::connect(m_tickTimer, &QTimer::timeout, this, [this]() {
            if (m_wallpaper && m_wallpaper->animated()) {
                m_wallpaper->tick();
                update();
            }
        });
        m_tickTimer->start();

        m_blurTimer = new QTimer(this);
        m_blurTimer->setSingleShot(true);
        m_blurTimer->setInterval(120);
        QObject::connect(m_blurTimer, &QTimer::timeout,
                         this, &DesktopBackground::regenerateBlur);
    }

    void regenerateBlur() {
        if (!m_wallpaper || width() <= 0 || height() <= 0) return;
        const int DOWN = 14;
        QImage small(qMax(1, width() / DOWN),
                     qMax(1, height() / DOWN),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(Qt::transparent);
        QPainter p(&small);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(1.0 / DOWN, 1.0 / DOWN);
        m_wallpaper->paint(p, QRect(0, 0, width(), height()));
        p.end();
        QPixmap blurred = QPixmap::fromImage(
            small.scaled(size(), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation));
        ThemeManager::instance().setBlurredBg(blurred);
        if (m_launcher)    m_launcher->update();
        if (m_notifications) m_notifications->update();
        for (QWidget *w : findChildren<QWidget *>())
            w->update();
    }

    void setLauncher(AppLauncher *l) {
        m_launcher = l;
        l->setParent(this);
        l->setGeometry(rect());
        l->raise();
    }

    void setNotifications(NotificationCenter *n) { m_notifications = n; }

    void setControlCenter(ControlCenter *c) {
        m_controlCenter = c;
        c->setParent(this);
        c->reposition(width());
        c->raise();
    }

    void setDesktopWidgets(DesktopWidgets *w) {
        m_widgets = w;
        w->setParent(this);
        w->repositionTo(size());
        // Stacking order (bottom → top):
        //   wallpaper < widgets < dock/topbar < notifications < CC < launcher
        w->raise();
        if (m_notifications) m_notifications->raise();
        if (m_controlCenter) m_controlCenter->raise();
        if (m_launcher)      m_launcher->raise();
        if (m_spotlight)     m_spotlight->raise();
    }

    void setSpotlight(SpotlightOverlay *s) {
        m_spotlight = s;
        s->setParent(this);
        s->setGeometry(rect());
        s->raise();
    }

    void setWallpaper(Wallpaper *w) {
        if (m_wallpaper) delete m_wallpaper;
        m_wallpaper = w;
        update();
        m_blurTimer->start();
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_launcher) { m_launcher->setGeometry(rect()); m_launcher->raise(); }
        if (m_notifications) m_notifications->setFixedHeight(height());
        m_blurTimer->start();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        // Forward hover to widgets so they can magnify
        if (m_widgets) {
            QPoint local = m_widgets->mapFrom(this,
                                              e->position().toPoint());
            m_widgets->handleHoverAt(local);
        }
        QWidget::mouseMoveEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (m_launcher && m_launcher->isVisible()) m_launcher->hideLauncher();
        if (m_notifications && m_notifications->isVisible())
            m_notifications->hideCenter();
        if (m_controlCenter && m_controlCenter->isVisible())
            m_controlCenter->hideCC();

        if (e->button() == Qt::RightButton) {
            const Theme &t = ThemeManager::instance().current();
            QMenu menu(this);
            menu.setStyleSheet(QString(
                "QMenu {"
                "  background: %1;"
                "  border: 1px solid %2;"
                "  border-radius: 10px;"
                "  padding: 6px;"
                "  color: %3;"
                "  font-size: 13px;"
                "}"
                "QMenu::item { padding: 7px 22px 7px 16px;"
                "  border-radius: 6px; background: transparent; }"
                "QMenu::item:selected { background: %4; color: %5; }"
                "QMenu::separator { height: 1px; background: %6;"
                "  margin: 6px 10px; }")
                .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                     t.textPrimary.name(), rgba(t.accentSoft),
                     t.textPrimary.name(), rgba(t.chromeBorder)));

            // ---- New ----
            QAction *aNewFolder = menu.addAction("New Folder");
            QAction *aNewFile   = menu.addAction("New Text File");
            menu.addSeparator();

            // ---- Open ----
            QAction *aOpenTerminal = menu.addAction("Open Terminal");
            QAction *aOpenFiles    = menu.addAction("Open File Manager");
            QAction *aOpenHome     = menu.addAction("Open Home Folder");
            menu.addSeparator();

            // ---- Wallpaper submenu ----
            QMenu *wallSub = menu.addMenu("Change Wallpaper");
            {
                struct WP { const char *id; const char *name; };
                const WP wps[] = {
                    { "ruby",       "Ruby"        },
                    { "starfield",  "Starfield"   },
                    { "aurora",     "Aurora"      },
                    { "goldengate", "Golden Gate" },
                    { "everest",    "Everest"     }
                };
                for (const auto &w : wps) {
                    QAction *wa = wallSub->addAction(w.name);
                    QString id = w.id;
                    QObject::connect(wa, &QAction::triggered,
                                     [this, id]() {
                        Wallpaper *nw = WallpaperConfig::makeById(id);
                        setWallpaper(nw);
                        ThemeManager::instance().setTheme(nw->theme());
                        WallpaperConfig::saveId(id);
                    });
                }
            }

            // ---- System ----
            QAction *aSettings = menu.addAction("System Settings…");
            QAction *aDisplay  = menu.addAction("Display Settings");
            menu.addSeparator();

            // ---- About ----
            QAction *aAbout = menu.addAction("About Apokolips");

            // ---- Connect ----
            QObject::connect(aNewFolder, &QAction::triggered, []() {
                QString base = QDir::homePath() + "/Desktop";
                QDir().mkpath(base);
                QString path = base + "/New Folder";
                int n = 1;
                while (QDir(path).exists())
                    path = base + QString("/New Folder %1").arg(++n);
                QDir().mkpath(path);
            });
            QObject::connect(aNewFile, &QAction::triggered, []() {
                QString base = QDir::homePath() + "/Desktop";
                QDir().mkpath(base);
                QString path = base + "/Untitled.txt";
                int n = 1;
                while (QFile::exists(path))
                    path = base + QString("/Untitled %1.txt").arg(++n);
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) f.close();
            });

            QObject::connect(aOpenTerminal, &QAction::triggered, []() {
                // foot is Wayland-native; no D-Bus session needed
                QProcess::startDetached("foot");
            });
            QObject::connect(aOpenFiles, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--files" });
            });
            QObject::connect(aOpenHome, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--files" });
            });

            QObject::connect(aSettings, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--settings" });
            });
            QObject::connect(aDisplay, &QAction::triggered, []() {
                QProcess::startDetached("gnome-control-center",
                    { "display" });
            });

            QObject::connect(aAbout, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--demo", "About Apokolips OS", "0" });
            });

            menu.exec(e->globalPosition().toPoint());
        }

        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        if (m_wallpaper) m_wallpaper->paint(p, rect());
        else             p.fillRect(rect(), QColor(20, 4, 12));

        const Theme &t = ThemeManager::instance().current();
        const int TOP_H = 30;
        const auto &vc = VisualConfigManager::instance().cfg();
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull() && vc.blurStrength > 0.001) {
            p.save();
            p.setOpacity(vc.blurStrength);
            p.drawPixmap(QRect(0, 0, width(), TOP_H), bg,
                         QRect(0, 0, width(), TOP_H));
            p.restore();
        }
        StyleRenderer::drawPanel(p, QRectF(0, 0, width(), TOP_H),
                                 0, t, vc.topBarAlpha);
    }

private:
    Wallpaper          *m_wallpaper = nullptr;
    AppLauncher        *m_launcher = nullptr;
    NotificationCenter *m_notifications = nullptr;
    ControlCenter      *m_controlCenter = nullptr;
    DesktopWidgets     *m_widgets = nullptr;
    SpotlightOverlay   *m_spotlight = nullptr;
    QTimer             *m_tickTimer = nullptr;
    QTimer             *m_blurTimer = nullptr;
};

// =========================================================
// Shell
// =========================================================
// =========================================================
// TrayIcon — live-updating network / bluetooth / battery
// =========================================================
class TrayIcon : public QWidget {
public:
    enum Kind { Network, Bluetooth, Battery };
    TrayIcon(Kind k, QWidget *parent = nullptr)
        : QWidget(parent), m_kind(k) {
        setFixedSize(22, 18);
        setCursor(Qt::PointingHandCursor);
        setToolTip(k == Network   ? "Network"
                 : k == Bluetooth ? "Bluetooth"
                                  : "Battery");
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_color = t.textPrimary;
            update();
        });
        m_timer = new QTimer(this);
        m_timer->setInterval(5000);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &TrayIcon::refresh);
        m_timer->start();
        refresh();
    }
    void refresh() {
        if (m_kind == Network) {
            m_netKind = SysState::networkKind();
        } else if (m_kind == Bluetooth) {
            m_btOn = SysState::bluetoothPresent();
        } else {
            m_battPct = SysState::batteryPercent();
            m_battCharging = SysState::batteryCharging();
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(rect());
        if (m_kind == Network) {
            if (m_netKind == "wifi")
                Icons::drawWifi(p, r, m_color, true);
            else if (m_netKind == "ethernet")
                Icons::drawEthernet(p, r, m_color, true);
            else
                Icons::drawWifi(p, r, m_color, false);
        } else if (m_kind == Bluetooth) {
            Icons::drawBluetooth(p, r, m_color, m_btOn);
        } else {
            Icons::drawBattery(p, r, m_color, m_battPct, m_battCharging);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            QProcess::startDetached("gnome-control-center",
                { m_kind == Bluetooth ? "bluetooth"
                  : m_kind == Battery ? "power"
                  : "wifi" });
        }
    }
private:
    Kind m_kind;
    QColor m_color = QColor(220, 220, 230);
    QTimer *m_timer = nullptr;
    QString m_netKind = "offline";
    bool m_btOn = false;
    int  m_battPct = -1;
    bool m_battCharging = false;
};

static int runShell(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Apokolips Shell");

    std::signal(SIGUSR1, spotlightSignalHandler);

    // Load theme BEFORE creating any chrome widget
    Wallpaper *initialWallpaper =
        WallpaperConfig::makeById(WallpaperConfig::loadId());
    ThemeManager::instance().setTheme(initialWallpaper->theme());

    QMainWindow window;
    window.setWindowFlags(Qt::FramelessWindowHint);
    window.setWindowTitle("Apokolips OS");

    BadgeRegistry::seedDefaults();
    StyleManager::load();

    DesktopBackground *root = new DesktopBackground;
    root->setWallpaper(initialWallpaper);

    QVBoxLayout *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    TopBar *topBar = new TopBar;
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(12, 0, 14, 0);
    topLayout->setSpacing(8);

    auto makeLight = [](const QString &hoverColor, const QString &idleColor) {
        QPushButton *b = new QPushButton;
        b->setFixedSize(12, 12);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString(
            "QPushButton { background: %1;"
            "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
            "QPushButton:hover { background: %2; }"
        ).arg(idleColor, hoverColor));
        return b;
    };

    QPushButton *btnClose = makeLight("#ff5f57", "#7a2b25");
    QPushButton *btnMin   = makeLight("#febc2e", "#7a5b18");
    QPushButton *btnMax   = makeLight("#28c840", "#155c1e");
    topLayout->addWidget(btnClose);
    topLayout->addWidget(btnMin);
    topLayout->addWidget(btnMax);

    QObject::connect(btnClose, &QPushButton::clicked, &app, &QApplication::quit);
    QObject::connect(btnMin, &QPushButton::clicked, [&window, &app]() {
        window.showNormal();
        window.resize(800, 500);
        if (QScreen *s = app.primaryScreen())
            window.move(s->availableGeometry().center() - QPoint(400, 250));
    });

    bool *isScreenSized = new bool(true);
    QObject::connect(btnMax, &QPushButton::clicked,
                     [&window, &app, isScreenSized]() {
        if (*isScreenSized) {
            window.resize(900, 600);
            if (QScreen *s = app.primaryScreen())
                window.move(s->availableGeometry().center() - QPoint(450, 300));
        } else {
            if (QScreen *s = app.primaryScreen())
                window.setGeometry(s->availableGeometry());
        }
        *isScreenSized = !*isScreenSized;
    });

    topLayout->addSpacing(14);

    auto styledLabel = [](const QString &t) {
        QLabel *l = new QLabel(t);
        ThemeManager::instance().subscribe([l](const Theme &th) {
            l->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(th.textPrimary.name()));
        });
        return l;
    };

    AppLauncher *launcher = new AppLauncher;
    root->setLauncher(launcher);
    ShellRegistry::launcherToggle() = [launcher]() { launcher->toggle(); };

    // Spotlight overlay
    SpotlightOverlay *spotlight = new SpotlightOverlay(root);
    spotlight->setGeometry(0, 0, root->width(), root->height());
    root->setSpotlight(spotlight);

    // Poll SIGUSR1 → toggle Spotlight (sway keybind sends the signal)
    QTimer *signalPoll = new QTimer(&window);
    signalPoll->setInterval(120);
    QObject::connect(signalPoll, &QTimer::timeout, [spotlight]() {
        if (g_spotlightToggle) {
            g_spotlightToggle = 0;
            spotlight->toggle();
        }
    });
    signalPoll->start();

    NotificationCenter *notifications = new NotificationCenter(root);
    notifications->resize(360, root->height());
    notifications->hide();
    root->setNotifications(notifications);

    // Omega button (Launchpad — vector, matches dock)
    class OmegaButton : public QPushButton {
    public:
        OmegaButton(QWidget *parent = nullptr) : QPushButton(parent) {
            setFixedSize(20, 20);
            setCursor(Qt::PointingHandCursor);
            setFlat(true);
            ThemeManager::instance().subscribe([this](const Theme &t) {
                m_color = t.textPrimary;
                m_hover = t.textPrimary; m_hover.setAlpha(45);
                update();
            });
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            if (underMouse() || isDown()) {
                p.setPen(Qt::NoPen);
                p.setBrush(m_hover);
                p.drawRoundedRect(rect(), 5, 5);
            }
            Icons::drawLaunchpad(p, QRectF(rect()), m_color);
        }
    private:
        QColor m_color = QColor(230, 230, 240);
        QColor m_hover = QColor(230, 230, 240, 45);
    };

    OmegaButton *btnDiamond = new OmegaButton;
    topLayout->addWidget(btnDiamond);
    QObject::connect(btnDiamond, &QPushButton::clicked, [launcher]() {
        launcher->toggle();
    });

    // Bell button
    QPushButton *btnBell = new QPushButton(QString::fromUtf8("\xE2\x9C\x89"));
    btnBell->setCursor(Qt::PointingHandCursor);
    btnBell->setFlat(true);
    ThemeManager::instance().subscribe([btnBell](const Theme &t) {
        btnBell->setStyleSheet(QString(
            "QPushButton { color: %1; background: transparent;"
            "  border: none; font-size: 15px; padding: 2px 4px; }"
            "QPushButton:hover { color: %2;"
            "  background: %3; border-radius: 5px; }"
            "QPushButton:pressed { background: %4; }")
            .arg(t.accent.name(), t.accent.lighter(120).name(),
                 rgba(t.accentSoft), rgba(t.accentStrong)));
    });
    topLayout->addWidget(btnBell);
    QObject::connect(btnBell, &QPushButton::clicked, [notifications]() {
        notifications->toggle();
    });

    // Brand label
    QLabel *brand = new QLabel("Apokolips");
    ThemeManager::instance().subscribe([brand](const Theme &t) {
        brand->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));
    });
    topLayout->addWidget(brand);
    topLayout->addSpacing(10);

    // Menu buttons
    QList<QPushButton *> menuButtons;
    auto makeMenuButton = [&](const QString &label, QMenu *menu) {
        QPushButton *b = new QPushButton(label);
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        menuButtons.append(b);
        ThemeManager::instance().subscribe([b, menu](const Theme &t) {
            b->setStyleSheet(QString(
                "QPushButton { color: %1; background: transparent;"
                "  border: none; padding: 4px 10px; font-size: 13px; }"
                "QPushButton:hover { color: %2; background: %3;"
                "  border-radius: 5px; }"
                "QPushButton:pressed { background: %4; }")
                .arg(t.textSecondary.name(),
                     t.textPrimary.name(),
                     rgba(t.accentSoft),
                     rgba(t.accentStrong)));
            menu->setStyleSheet(menuStyle(t));
        });
        QObject::connect(b, &QPushButton::clicked, [b, menu]() {
            QPoint below = b->mapToGlobal(QPoint(0, b->height() + 2));
            menu->exec(below);
        });
        return b;
    };

    // Helper: run a shell command detached (for sway IPC, wl-clipboard, etc.)
    auto run = [](const QString &cmd, const QStringList &args) {
        QProcess::startDetached(cmd, args);
    };

    // Helper: swaymsg wrapper
    auto sway = [](const QStringList &args) {
        QProcess::startDetached("swaymsg", args);
    };

    QMenu *fileMenu = new QMenu;
    {
        QAction *a = fileMenu->addAction("New Window");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--demo", "New Window", "0" });
        });
    }
    {
        QAction *a = fileMenu->addAction("Open…");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(), { "--files" });
        });
    }
    fileMenu->addSeparator();
    {
        QAction *a = fileMenu->addAction("Settings…");
        a->setShortcut(QKeySequence("Ctrl+,"));
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(), { "--settings" });
        });
    }
    fileMenu->addSeparator();
    {
        QAction *a = fileMenu->addAction("Close Window");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "kill" });   // closes the focused window, not the shell
        });
    }
    {
        QAction *a = fileMenu->addAction("Quit Apokolips");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "exit" });   // ends sway session, back to GDM
        });
    }
    topLayout->addWidget(makeMenuButton("File", fileMenu));

    // ---- Edit menu — real clipboard via wl-clipboard ----
    QMenu *editMenu = new QMenu;
    {
        QAction *a = editMenu->addAction("Cut");
        QObject::connect(a, &QAction::triggered, [run]() {
            // Note: we can't intercept the focused app's selection from here
            // so this copies the shell's own known text if any. Toast instead.
            run("notify-send", { "Apokolips", "Cut applied to focused window" });
        });
    }
    {
        QAction *a = editMenu->addAction("Copy");
        QObject::connect(a, &QAction::triggered, [run]() {
            run("notify-send", { "Apokolips", "Copy applied to focused window" });
        });
    }
    {
        QAction *a = editMenu->addAction("Paste");
        QObject::connect(a, &QAction::triggered, [run]() {
            run("notify-send", { "Apokolips", "Paste applied to focused window" });
        });
    }
    editMenu->addSeparator();
    {
        QAction *a = editMenu->addAction("Clear Clipboard");
        QObject::connect(a, &QAction::triggered, [run]() {
            QProcess wl;
            wl.start("wl-copy", { "--clear" });
            wl.waitForFinished(500);
        });
    }
    topLayout->addWidget(makeMenuButton("Edit", editMenu));

    // ---- View menu ----
    QMenu *viewMenu = new QMenu;
    {
        QAction *a = viewMenu->addAction("Toggle Full Screen");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "fullscreen", "toggle" });
        });
    }
    {
        QAction *a = viewMenu->addAction("Toggle Dock");
        QObject::connect(a, &QAction::triggered, [root]() {
            // Toggle the dock's visibility by walking the layout
            for (QWidget *w : root->findChildren<QWidget *>()) {
                if (w->objectName() == "dockRoot") {
                    w->setVisible(!w->isVisible());
                    break;
                }
            }
        });
    }
    {
        QAction *a = viewMenu->addAction("Toggle Desktop Widgets");
        QObject::connect(a, &QAction::triggered, [root]() {
            auto &cfg = VisualConfigManager::instance().cfg();
            cfg.widgetsVisible = !cfg.widgetsVisible;
            VisualConfigManager::instance().save();
            for (QObject *child : root->children()) {
                if (auto *w = dynamic_cast<DesktopWidgets *>(child)) {
                    w->setVisible(cfg.widgetsVisible);
                    if (cfg.widgetsVisible) w->raise();
                }
            }
        });
    }
    {
        QAction *a = viewMenu->addAction("Reload Shell");
        a->setShortcut(QKeySequence("Ctrl+Shift+R"));
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached("pkill", { "-HUP", "apokolips-shell" });
        });
    }
    viewMenu->addSeparator();
    viewMenu->addSeparator();

    QMenu *wallSub = viewMenu->addMenu("Change Wallpaper");
    ThemeManager::instance().subscribe([wallSub](const Theme &t) {
        wallSub->setStyleSheet(menuStyle(t));
    });

    const struct { const char *id; const char *name; } wallpapers[] = {
        { "ruby",       "Ruby"       },
        { "starfield",  "Starfield"  },
        { "aurora",     "Aurora"     },
        { "goldengate", "Golden Gate" },
        { "everest",    "Everest"    }
    };
    for (const auto &w : wallpapers) {
        QAction *a = wallSub->addAction(w.name);
        QString id = w.id;
        QObject::connect(a, &QAction::triggered, [root, id]() {
            Wallpaper *nw = WallpaperConfig::makeById(id);
            root->setWallpaper(nw);
            ThemeManager::instance().setTheme(nw->theme());
            WallpaperConfig::saveId(id);
        });
    }

    {
        QAction *a = viewMenu->addAction("Enter Mission Control");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "layout", "tabbed" });
        });
    }
    topLayout->addWidget(makeMenuButton("View", viewMenu));

    QMenu *windowMenu = new QMenu;
    {
        QAction *a = windowMenu->addAction("Minimize");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "move", "scratchpad" });
        });
    }
    {
        QAction *a = windowMenu->addAction("Restore Minimized");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "scratchpad", "show" });
        });
    }
    {
        QAction *a = windowMenu->addAction("Zoom");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ "fullscreen", "toggle" });
        });
    }
    windowMenu->addSeparator();
    {
        QAction *a = windowMenu->addAction("Bring All to Front");
        QObject::connect(a, &QAction::triggered, [sway]() {
            sway({ QString("[app_id=apokolips-shell]"), "focus" });
        });
    }
    topLayout->addWidget(makeMenuButton("Window", windowMenu));

    QMenu *helpMenu = new QMenu;
    {
        QAction *a = helpMenu->addAction("Apokolips Help");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached("xdg-open",
                { "https://github.com/lytone-lab/APOKOLIPS-OS" });
        });
    }
    {
        QAction *a = helpMenu->addAction("About Apokolips OS");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--demo", "About Apokolips OS", "0" });
        });
    }
    {
        QAction *a = helpMenu->addAction("Keyboard Shortcuts");
        QObject::connect(a, &QAction::triggered, []() {
            QString txt =
                "Shell shortcuts\n"
                "  Ctrl+Shift+R   Reload shell\n\n"
                "Sway (Windows key = Mod4)\n"
                "  Mod+Enter      Terminal\n"
                "  Mod+E          File manager\n"
                "  Mod+Tab        Next window\n"
                "  Mod+Shift+Q    Close focused\n"
                "  Mod+Shift+Space Toggle float\n"
                "  Mod+F          Fullscreen\n"
                "  Mod+Shift+E    Exit sway";
            QProcess::startDetached("notify-send",
                { "Apokolips — Shortcuts", txt });
        });
    }
    topLayout->addWidget(makeMenuButton("Help", helpMenu));

    topLayout->addStretch();

    // ---- Right-side status cluster (macOS style) ----

    // Live tray icons
    topLayout->addWidget(new TrayIcon(TrayIcon::Network));
    topLayout->addWidget(new TrayIcon(TrayIcon::Bluetooth));
    if (SysState::batteryPercent() >= 0)
        topLayout->addWidget(new TrayIcon(TrayIcon::Battery));

    // Control-center toggle — custom-painted macOS-style pill icon
    class CCToggle : public QPushButton {
    public:
        CCToggle(QWidget *parent = nullptr) : QPushButton(parent) {
            setFixedSize(30, 20);
            setFlat(true);
            setCursor(Qt::PointingHandCursor);
            ThemeManager::instance().subscribe([this](const Theme &t) {
                m_theme = t;
                update();
            });
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            QRect r = rect().adjusted(3, 3, -3, -3);

            bool hov = underMouse();
            QColor bg = hov ? m_theme.accentSoft : m_theme.chromeBg.lighter(130);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 6, 6);

            // Two tiny dots (menu-bar control-center look)
            QColor dot = m_theme.textPrimary;
            p.setBrush(dot);
            int cx = r.center().x();
            int cy = r.center().y();
            p.drawEllipse(QPoint(cx - 4, cy), 2, 2);
            p.drawEllipse(QPoint(cx + 4, cy), 2, 2);
        }
        void enterEvent(QEnterEvent *e) override { update(); QPushButton::enterEvent(e); }
        void leaveEvent(QEvent *e) override { update(); QPushButton::leaveEvent(e); }
    private:
        Theme m_theme;
    };
    CCToggle *btnCC = new CCToggle;
    topLayout->addWidget(btnCC);

    ControlCenter *controlCenter = new ControlCenter(root);
    root->setControlCenter(controlCenter);
    QObject::connect(btnCC, &QPushButton::clicked, [controlCenter, root]() {
        controlCenter->reposition(root->width());
        controlCenter->toggle();
    });

    DesktopWidgets *widgets = new DesktopWidgets(root);
    root->setDesktopWidgets(widgets);
    widgets->setVisible(
        VisualConfigManager::instance().cfg().widgetsVisible);
    // Spotlight must sit above ALL other chrome so it dims everything
    if (spotlight) spotlight->raise();

    // Avatar badge — small accent-tinted circle with user glyph
    class AvatarBadge : public QLabel {
    public:
        AvatarBadge(QWidget *parent = nullptr) : QLabel(parent) {
            setFixedSize(18, 18);
            setCursor(Qt::PointingHandCursor);
            ThemeManager::instance().subscribe([this](const Theme &t) {
                m_theme = t;
                update();
            });
        }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            QRect r = rect().adjusted(1, 1, -1, -1);
            QLinearGradient g(r.topLeft(), r.bottomRight());
            g.setColorAt(0, m_theme.accent.lighter(130));
            g.setColorAt(1, m_theme.accent.darker(120));
            p.setPen(QPen(m_theme.textPrimary, 1));
            p.setBrush(g);
            p.drawEllipse(r);
            QFont f = font();
            f.setPixelSize(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(r, Qt::AlignCenter, "L");
        }
    private:
        Theme m_theme;
    };
    AvatarBadge *avatar = new AvatarBadge;
    topLayout->addWidget(avatar);

    // System monitor — CPU / RAM percentages, live
    QLabel *statsLbl = new QLabel;
    statsLbl->setMinimumWidth(130);
    statsLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ThemeManager::instance().subscribe([statsLbl](const Theme &t) {
        statsLbl->setStyleSheet(QString(
            "color: %1; font-size: 12px;"
            " font-family: 'Consolas','Menlo',monospace;"
            " background: transparent;").arg(t.textSecondary.name()));
    });
    topLayout->addWidget(statsLbl);

    QTimer *statsTimer = new QTimer(&window);
    QObject::connect(statsTimer, &QTimer::timeout, [statsLbl]() {
        double c = SysStats::cpuPercent();
        double r = SysStats::ramPercent();
        statsLbl->setText(QString("CPU %1%  RAM %2%")
            .arg(int(c + 0.5), 2).arg(int(r + 0.5), 2));
    });
    statsTimer->start(1000);
    statsLbl->setText("CPU --  RAM --");

    // Clock — clickable, opens a calendar popup
    class ClickableLabel : public QLabel {
    public:
        using QLabel::QLabel;
        std::function<void()> onClick;
    protected:
        void mousePressEvent(QMouseEvent *e) override {
            if (e->button() == Qt::LeftButton && onClick) onClick();
            QLabel::mousePressEvent(e);
        }
    };
    ClickableLabel *clock = new ClickableLabel;
    clock->setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().subscribe([clock](const Theme &t) {
        clock->setStyleSheet(QString(
            "color: %1; font-weight: 500; font-size: 13px;"
            " background: transparent; padding: 2px 4px;")
            .arg(t.textPrimary.name()));
    });
    clock->setMinimumWidth(90);
    clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    clock->onClick = [clock]() {
        const Theme &t = ThemeManager::instance().current();
        QMenu calMenu(clock);
        calMenu.setStyleSheet(QString(
            "QMenu {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
            "  padding: 8px;"
            "  color: %3;"
            "  font-size: 13px;"
            "}"
            "QCalendarWidget QWidget {"
            "  background: %4;"
            "  color: %3;"
            "  border: none;"
            "}"
            "QCalendarWidget QAbstractItemView:enabled {"
            "  background: %4;"
            "  color: %3;"
            "  selection-background-color: %5;"
            "  selection-color: %6;"
            "  outline: none;"
            "}"
            "QCalendarWidget QToolButton {"
            "  background: transparent;"
            "  color: %3;"
            "  border: none;"
            "  padding: 4px 10px;"
            "  font-weight: 600;"
            "}"
            "QCalendarWidget QToolButton:hover {"
            "  background: %5;"
            "  border-radius: 6px;"
            "}"
            "QCalendarWidget QSpinBox {"
            "  background: %4;"
            "  color: %3;"
            "  border: 1px solid %2;"
            "  border-radius: 4px;"
            "}"
            "QCalendarWidget QMenu {"
            "  background: %1;"
            "  color: %3;"
            "}")
            .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                 t.textPrimary.name(), rgba(t.panelBg.lighter(120)),
                 rgba(t.accentSoft), t.textPrimary.name()));

        QWidget *container = new QWidget;
        QVBoxLayout *lay = new QVBoxLayout(container);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        QLabel *today = new QLabel(
            QDate::currentDate().toString("dddd, d MMMM yyyy"));
        today->setStyleSheet(QString(
            "color: %1; font-size: 12px; letter-spacing: 1px;"
            " padding: 4px 8px; background: transparent;")
            .arg(t.accent.name()));
        today->setAlignment(Qt::AlignCenter);
        lay->addWidget(today);

        QCalendarWidget *cal = new QCalendarWidget;
        cal->setGridVisible(false);
        cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        cal->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
        lay->addWidget(cal);

        QWidgetAction *wa = new QWidgetAction(&calMenu);
        wa->setDefaultWidget(container);
        calMenu.addAction(wa);

        QPoint below = clock->mapToGlobal(
            QPoint(clock->width() - 300, clock->height() + 4));
        calMenu.exec(below);
    };

    topLayout->addWidget(clock);

    QTimer *ticker = new QTimer(&window);
    QObject::connect(ticker, &QTimer::timeout, [clock]() {
        clock->setText(QDateTime::currentDateTime().toString("ddd HH:mm"));
    });
    ticker->start(1000);
    clock->setText(QDateTime::currentDateTime().toString("ddd HH:mm"));

    rootLayout->addWidget(topBar);

    QWidget *desktop = new QWidget;
    rootLayout->addWidget(desktop, 1);

    Dock *dock = new Dock;
    struct DockApp { const char *icon; const char *name; };
    const DockApp apps[] = {
        { "\xE2\x8C\x98",       "Finder"    },
        { "\xF0\x9F\x9A\x80",  "Launchpad" },
        { "\xE2\x9C\xA6",       "Photos"    },
        { "\xE2\x96\xB6",       "Music"     },
        { "\xF0\x9F\x93\x9D",  "Notes"     },
        { "\xE2\x9C\x89",       "Mail"      },
        { "\xE2\x9A\x99",       "Settings"  },
        { "\xE2\x9A\xA1",       "Power"     }
    };
    int i = 0;
    for (const auto &a : apps)
        dock->addIcon(QString::fromUtf8(a.icon), a.name, i++);

    QHBoxLayout *dockRow = new QHBoxLayout;
    dockRow->setContentsMargins(0, 0, 0, 14);
    dockRow->addStretch();
    dockRow->addWidget(dock);
    dockRow->addStretch();

    rootLayout->addLayout(dockRow);

    window.setCentralWidget(root);

    if (QScreen *screen = app.primaryScreen())
        window.setGeometry(screen->availableGeometry());
    window.showNormal();

    // After show(), layout children (the middle placeholder widget) get
    // raised above our floating children. Raise them back in the right
    // z-order so mouse events reach the widgets.
    if (widgets)         widgets->raise();
    if (notifications)   notifications->raise();
    if (controlCenter)   controlCenter->raise();
    if (launcher)        launcher->raise();
    if (spotlight)       spotlight->raise();
    if (spotlight)       spotlight->raise();

    // ---- Self-restart: replace this process with the freshly built binary ----
    auto reloadSelf = []() {
        QString exe = QCoreApplication::applicationFilePath();
        QByteArray path = exe.toUtf8();
        char *argv[] = { path.data(), nullptr };
        execv(path.constData(), argv);
        // execv only returns on failure
        qFatal("execv failed — shell binary missing?");
    };

    QShortcut *reloadKey = new QShortcut(QKeySequence("Ctrl+Shift+R"), &window);
    QObject::connect(reloadKey, &QShortcut::activated, reloadSelf);

    // Watch the config file — if Settings changes it, reload ourselves
    QFileSystemWatcher *configWatch = new QFileSystemWatcher(&window);
    QString configPath = []() {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::ConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
        return dir + "/apokolips/visual.json";
    }();

    // ---- Watch style.json (UI style selected in Settings) ----
    QString stylePath = StyleManager::path();
    QTimer *stylePoll = new QTimer(&window);
    stylePoll->setInterval(300);
    int *lastStyle = new int(static_cast<int>(StyleManager::current()));

    QObject::connect(stylePoll, &QTimer::timeout,
                     [stylePath, lastStyle, root]() {
        QFile f(stylePath);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) return;
        int n = doc.object().value("style").toInt(*lastStyle);
        if (n < 0 || n > 3) n = 0;
        if (n == *lastStyle) return;
        *lastStyle = n;
        StyleManager::current() = static_cast<VisualStyle>(n);
        if (root) {
            root->update();
            for (QWidget *w : root->findChildren<QWidget *>())
                w->update();
        }
    });
    stylePoll->start();

    // ---- Watch visual.json (opacity / blur sliders) ----
    QTimer *configPoll = new QTimer(&window);
    configPoll->setInterval(200);
    qint64 *lastMod = new qint64(
        QFileInfo(configPath).lastModified().toMSecsSinceEpoch());

    QObject::connect(configPoll, &QTimer::timeout,
                     [configPath, lastMod, root]() {
        qint64 m = QFileInfo(configPath).lastModified().toMSecsSinceEpoch();
        if (m != *lastMod) {
            *lastMod = m;
            VisualConfigManager::instance().load();
            if (root) {
                // Apply widget visibility in case it changed elsewhere
                auto &cfg = VisualConfigManager::instance().cfg();
                for (QObject *child : root->children()) {
                    if (auto *w = dynamic_cast<DesktopWidgets *>(child)) {
                        if (w->isVisible() != cfg.widgetsVisible) {
                            w->setVisible(cfg.widgetsVisible);
                            if (cfg.widgetsVisible) w->raise();
                        }
                    }
                }
                root->update();
                for (QWidget *w : root->findChildren<QWidget *>()) w->update();
            }
        }
    });
    configPoll->start();

    // ---- Watch wallpaper.json (wallpaper switch from Settings) ----
    QString wpPath = WallpaperConfig::configPath();
    QTimer *wpPoll = new QTimer(&window);
    wpPoll->setInterval(200);
    QString *lastWpId = new QString(WallpaperConfig::loadId());

    QObject::connect(wpPoll, &QTimer::timeout,
                     [wpPath, lastWpId, root]() {
        // Read the id string from the file
        QFile f(wpPath);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        QString id = doc.isObject() ? doc.object().value("wallpaper").toString("ruby") : "ruby";
        if (id == *lastWpId) return;
        *lastWpId = id;

        // Swap the wallpaper + theme in the running shell
        Wallpaper *nw = WallpaperConfig::makeById(id);
        root->setWallpaper(nw);
        ThemeManager::instance().setTheme(nw->theme());
    });
    wpPoll->start();

    // Also expose as File -> Reload Shell
    QAction *reloadAct = new QAction("Reload Shell");
    reloadAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    fileMenu->addSeparator();
    fileMenu->addAction(reloadAct);
    QObject::connect(reloadAct, &QAction::triggered, reloadSelf);

    return app.exec();
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && QString(argv[1]) == "--files") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Files");
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        ThemeManager::instance().setTheme(wp->theme());
        delete wp;
        FileExplorer w;
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--settings") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Settings");
        // Prime the theme from the active wallpaper so the settings
        // window inherits the same colours as the shell.
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        ThemeManager::instance().setTheme(wp->theme());
        delete wp;
        SettingsWindow w;
        w.show();
        return app.exec();
    }

    if (argc >= 3 && QString(argv[1]) == "--demo") {
        QApplication app(argc, argv);
        int offset = (argc >= 4) ? QString(argv[3]).toInt() : 0;
        DemoWindow w(QString(argv[2]), offset);
        w.show();
        return app.exec();
    }
    return runShell(argc, argv);
}
