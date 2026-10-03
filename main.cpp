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
#include <QStackedWidget>
#include <unistd.h>
#include <QShortcut>
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
// VisualConfig — every chrome tuning knob. Settings app targets this.
// =========================================================
struct VisualConfig {
    double blurStrength       = 0.25;   // 0=none, 1=heavy
    int    topBarAlpha        = 190;
    int    dockAlpha          = 205;
    int    launcherAlpha      = 230;
    int    controlCenterAlpha = 230;
    int    widgetCardAlpha    = 210;
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
        m_cfg.blurRefreshMs      = getI("blurRefreshMs",      m_cfg.blurRefreshMs);
        m_cfg.dockMagnifyMax     = getD("dockMagnifyMax",     m_cfg.dockMagnifyMax);
        m_cfg.dockSigma          = getD("dockSigma",          m_cfg.dockSigma);
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
        for (auto &cb : m_callbacks) cb(m_theme);
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
    QString id() const override { return "goldengate"; }
    QString displayName() const override { return "Golden Gate"; }

    Theme theme() const override {
        Theme t;
        t.chromeBg      = QColor(14, 20, 32, 205);
        t.chromeBorder  = QColor(120, 170, 240, 150);
        t.accent        = QColor(120, 180, 255);
        t.accentSoft    = QColor(120, 180, 255, 90);
        t.accentStrong  = QColor(120, 180, 255, 160);
        t.textPrimary   = QColor(224, 232, 244);
        t.textSecondary = QColor(168, 184, 208);
        t.textDim       = QColor(104, 120, 152);
        t.panelBg       = QColor(12, 18, 30, 240);
        return t;
    }

    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::Antialiasing);

        // Deep navy base
        QLinearGradient base(0, 0, 0, r.height());
        base.setColorAt(0.0, QColor(20, 30, 52));
        base.setColorAt(0.4, QColor(14, 22, 40));
        base.setColorAt(1.0, QColor(6, 10, 20));
        p.fillRect(r, base);

        // Soft blue light from upper-left
        QRadialGradient glow(r.width() * 0.3, r.height() * 0.2,
                             r.width() * 0.75);
        glow.setColorAt(0.0, QColor(70, 110, 170, 100));
        glow.setColorAt(0.45, QColor(40, 70, 120, 45));
        glow.setColorAt(1.0, QColor(15, 25, 50, 0));
        p.fillRect(r, glow);

        // Sweeping arcs — 3 curves with decreasing brightness
        auto arc = [&](double yFactor, double ampFactor,
                       const QColor &col, double thickness) {
            QPainterPath path;
            double x0 = -r.width() * 0.1;
            double x3 = r.width() * 1.1;
            double yBase = r.height() * yFactor;
            double amp = r.height() * ampFactor;

            path.moveTo(x0, yBase);
            path.cubicTo(
                r.width() * 0.25, yBase - amp,
                r.width() * 0.75, yBase + amp * 0.6,
                x3,                yBase - amp * 0.9
            );

            QPen pen(col, thickness);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        };

        arc(0.72, 0.55, QColor(120, 180, 255, 100), 1.4);
        arc(0.62, 0.45, QColor(140, 200, 255, 160), 1.6);
        arc(0.52, 0.38, QColor(160, 210, 255, 200), 1.8);

        // Few sparse stars high in the sky
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 80));
        for (int i = 0; i < 40; ++i) {
            int sx = (i * 211) % r.width();
            int sy = (i * 151) % (r.height() / 3);
            p.drawEllipse(QPoint(sx, sy), 1, 1);
        }
    }
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

        QWidget *header = new QWidget;
        header->setFixedHeight(40);
        header->setAttribute(Qt::WA_StyledBackground, true);
        QWidget *body = new QWidget;
        body->setAttribute(Qt::WA_StyledBackground, true);
        QLabel *dot = new QLabel(QString::fromUtf8("\xE2\x97\x86"));
        QLabel *title = new QLabel(appName);
        QPushButton *close = new QPushButton;
        close->setFixedSize(13, 13);
        close->setCursor(Qt::PointingHandCursor);
        QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);

        QLabel *big = new QLabel(appName);
        big->setAlignment(Qt::AlignCenter);
        QLabel *sub = new QLabel("running under Apokolips Shell");
        sub->setAlignment(Qt::AlignCenter);

        QHBoxLayout *hLayout = new QHBoxLayout(header);
        hLayout->setContentsMargins(14, 0, 14, 0);
        hLayout->setSpacing(10);
        hLayout->addWidget(dot);
        hLayout->addWidget(title);
        hLayout->addStretch();
        hLayout->addWidget(close);

        QVBoxLayout *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setAlignment(Qt::AlignCenter);
        bodyLayout->addWidget(big);
        bodyLayout->addWidget(sub);

        layout->addWidget(header);
        layout->addWidget(body, 1);

        ThemeManager::instance().subscribe(
            [header, body, dot, title, close, big, sub](const Theme &t) {
            header->setStyleSheet(QString(
                "QWidget { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
                "  stop:0 %1, stop:1 %2); }")
                .arg(rgba(t.chromeBg.lighter(115)),
                     rgba(t.chromeBg)));

            body->setStyleSheet(QString("QWidget { background: %1; }")
                .arg(rgba(t.panelBg)));

            dot->setStyleSheet(QString(
                "color: %1; font-size: 14px; background: transparent;")
                .arg(t.accent.name()));

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
        });
    }
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

        QColor panel = m_theme.panelBg;
        panel.setAlpha(int(240 * m_slide));
        p.fillRect(rect(), panel);

        QColor edge = m_theme.accent;
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
class AppLauncher : public QWidget {
public:
    explicit AppLauncher(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
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

        QColor backdrop(6, 4, 10);
        backdrop.setAlphaF(0.90 * m_opacity);
        p.fillRect(rect(), backdrop);

        QColor g1 = m_theme.accent; g1.setAlphaF(0.30 * m_opacity);
        QColor g2 = m_theme.accent; g2.setAlphaF(0.0);
        QRadialGradient glow(width() * 0.5, height() * 0.35, width() * 0.5);
        glow.setColorAt(0.0, g1);
        glow.setColorAt(1.0, g2);
        p.fillRect(rect(), glow);

        QRect panelRect((width() - PANEL_W) / 2, 120, PANEL_W, PANEL_H);
        for (int i = 6; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.06 * m_opacity);
            p.setPen(Qt::NoPen);
            p.setBrush(sh);
            p.drawRoundedRect(
                panelRect.adjusted(-i*2, -i*2 + 6, i*2, i*2 + 6),
                22 + i, 22 + i);
        }

        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull()) {
            QPainterPath panelPath;
            panelPath.addRoundedRect(panelRect, 22, 22);
            p.save();
            p.setClipPath(panelPath);
            QColor dim(0, 0, 0);
            dim.setAlphaF(0.35 * m_opacity);
            p.fillRect(panelRect, dim);
            p.setOpacity(0.55 * m_opacity);
            p.drawPixmap(panelRect, bg, panelRect);
            p.setOpacity(1.0);
            p.restore();
        }
        QColor panel = m_theme.panelBg;
        panel.setAlpha(int(170 * m_opacity));
        p.setBrush(panel);
        QColor border = m_theme.accent;
        border.setAlphaF(0.40 * m_opacity);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(panelRect, 22, 22);
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
        for (QPushButton *tile : m_tiles) {
            QString appName = tile->property("appName").toString();
            Q_UNUSED(appName);
            tile->setStyleSheet(QString(
                "QPushButton {"
                "  background: %1;"
                "  border: none;"
                "  border-radius: 16px;"
                "  color: %3;"
                "  padding-bottom: 10px;"
                "}"
                "QPushButton:hover {"
                "  background: %4;"
                "  border: none;"
                "}"
                "QPushButton:pressed {"
                "  background: %6;"
                "}")
                .arg(rgba(QColor(0, 0, 0, 60)),
                     rgba(m_theme.accentSoft),
                     m_theme.textPrimary.name(),
                     rgba(QColor(255, 255, 255, 40)),
                     rgba(m_theme.accent),
                     rgba(m_theme.accentStrong)));
        }
        for (QLabel *l : m_iconLabels) {
            l->setStyleSheet(QString(
                "font-size: 38px; color: %1; background: transparent;")
                .arg(m_theme.accent.name()));
        }
        for (QLabel *l : m_nameLabels) {
            l->setStyleSheet(QString(
                "font-size: 12px; color: %1; background: transparent;")
                .arg(m_theme.textPrimary.name()));
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

        QLabel *ic = new QLabel(icon);
        ic->setAlignment(Qt::AlignCenter);
        ic->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        v->addWidget(ic);

        QLabel *nm = new QLabel(name);
        nm->setAlignment(Qt::AlignCenter);
        nm->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        v->addWidget(nm);

        m_tiles.append(tile);
        m_iconLabels.append(ic);
        m_nameLabels.append(nm);

        QString appName = name;
        int offset = index;
        QObject::connect(tile, &QPushButton::clicked,
                         [this, appName, offset]() {
            QStringList args;
            if (appName == "Settings")
                args = { "--settings" };
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

        // Softer gradient — dialed back from 150/150, diagonal flow
        QColor top = m_theme.accentSoft.lighter(120);
        QColor mid = m_theme.accentSoft;
        QColor bot = m_theme.accentSoft.darker(130);
        top.setAlpha(qMin(255, top.alpha() + boost + 20));
        mid.setAlpha(qMin(255, mid.alpha() + boost));
        bot.setAlpha(qMin(255, bot.alpha() + boost - 15));

        // Diagonal: top-left → bottom-right
        QLinearGradient grad(x, y, x + visual, y + visual);
        grad.setColorAt(0.0, top);
        grad.setColorAt(0.5, mid);
        grad.setColorAt(1.0, bot);

        QColor br = m_theme.accent;
        br.setAlpha(qMin(255, br.alpha() + boost));

        p.setBrush(grad);
        p.setPen(QPen(br, 1));
        p.drawRoundedRect(x, y, visual, visual, radius, radius);

        QFont f = font();
        f.setPixelSize(int(visual * 0.44));
        p.setFont(f);
        p.setPen(m_theme.textPrimary);
        p.drawText(QRect(x, y, visual, visual), Qt::AlignCenter, m_icon);

        // Running indicator dot under the icon
        if (m_running) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_theme.accent);
            p.drawEllipse(QPoint(width() / 2, height() - 6), 3, 3);
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            QStringList args;
            if (m_name == "Settings")
                args = { "--settings" };
            else
                args = { "--demo", m_name, QString::number(m_index) };

            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                args, QString(), &m_pid);
            m_running = true;
            update();
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
                    QStringList args;
                    if (m_name == "Settings")
                        args = { "--settings" };
                    else
                        args = { "--demo", m_name, QString::number(m_index) };
                    QProcess::startDetached(
                        QCoreApplication::applicationFilePath(),
                        args, QString(), &m_pid);
                    m_running = true;
                    update();
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

        QColor tint = m_theme.chromeBg;
        tint.setAlpha(160);
        p.fillPath(path, tint);

        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        m_cursorX  = e->position().x();
        m_hasCursor = true;
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

static QString menuStyle(const Theme &t)
{
    return QString(
        "QMenu {"
        "  background: %1;"
        "  border: none;"
        "  border-radius: 10px;"
        "  padding: 6px;"
        "  color: %3;"
        "  font-size: 13px;"
        "}"
        "QMenu::item {"
        "  padding: 7px 22px 7px 16px;"
        "  border-radius: 6px;"
        "  background: transparent;"
        "}"
        "QMenu::item:selected {"
        "  background: %4;"
        "  color: %5;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: %6;"
        "  margin: 6px 10px;"
        "}")
        .arg(rgba(t.panelBg),
             rgba(t.chromeBorder),
             t.textPrimary.name(),
             rgba(t.accentSoft),
             t.textPrimary.name(),
             rgba(t.chromeBorder));
}

// =========================================================
// Control Center — macOS-style dropdown panel
// =========================================================
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

        QColor panel = m_theme.panelBg;
        panel.setAlpha(int(vc.controlCenterAlpha * m_slide));
        p.setBrush(panel);
        QColor border = m_theme.chromeBorder;
        border.setAlpha(int(140 * m_slide));
        p.setPen(QPen(border, 1));
        p.drawPath(path);
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

    QWidget *makeSliderRow(const QString &label, int value) {
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
        outer->addWidget(makeCard("SOUND", makeSliderRow("Output", 65)));
        outer->addWidget(makeCard("DISPLAY", makeSliderRow("Brightness", 80)));
        outer->addStretch();
    }

    QTimer *m_animTimer = nullptr;
    qreal m_slide = 0.0;
    qreal m_target = 0.0;
    Theme m_theme;
};

// =========================================================
// Settings window
// =========================================================
class SettingsWindow : public QWidget {
public:
    explicit SettingsWindow(QWidget *parent = nullptr) : QWidget(parent) {
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

        // Full window background (header strip + main body)
        QColor bodyCol = m_theme.panelBg;
        bodyCol.setAlpha(235);
        QColor hdrCol = m_theme.chromeBg.lighter(115);
        hdrCol.setAlpha(235);

        p.fillPath(path, bodyCol);

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
            { "goldengate", "Golden Gate" }
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
                // Notify running shell via a small config touch (shell polls it)
                // Settings window also adopts the new theme right away
                Wallpaper *wp = WallpaperConfig::makeById(id);
                ThemeManager::instance().setTheme(wp->theme());
                delete wp;
                updateWallpaperHighlight();
                restyleAll();
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
};

// =========================================================
// Desktop Widgets — floating glass cards on the left
// =========================================================
class DesktopWidgets : public QWidget {
public:
    explicit DesktopWidgets(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
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
    }

    void repositionTo(const QSize &parentSize) {
        const int CARD_W = 200;
        setFixedWidth(CARD_W);
        move(20, 50);
        setFixedHeight(qMin(parentSize.height() - 130, 560));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int y = 0;

        // --- Clock card ---
        drawCard(p, QRect(0, y, width(), 96));
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
        drawCard(p, QRect(0, y, width(), 128));
        {
            int r = 34;
            int cx1 = 14 + r, cy = y + 56;
            int cx2 = 14 + r + 2*r + 30 + r;

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
        drawCard(p, QRect(0, y, width(), 92));
        {
            QFont f = font(); f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(14, y + 10, width()-28, 14),
                       Qt::AlignLeft, "WEATHER  ·  KAMPALA");

            f.setPixelSize(30); f.setWeight(QFont::Light);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(QRect(14, y + 28, 90, 48),
                       Qt::AlignLeft | Qt::AlignVCenter, "21°");

            f.setPixelSize(11); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textSecondary);
            p.drawText(QRect(100, y + 30, width()-114, 16),
                       Qt::AlignLeft, "Partly cloudy");
            p.setPen(m_theme.textDim);
            p.drawText(QRect(100, y + 48, width()-114, 16),
                       Qt::AlignLeft, "H: 27°  L: 17°");
            p.drawText(QRect(100, y + 64, width()-114, 16),
                       Qt::AlignLeft, "Wind 7 km/h  ·  HUM 68%");
        }
        y += 92 + 12;

        // --- Mini calendar ---
        drawCard(p, QRect(0, y, width(), 130));
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
    void drawCard(QPainter &p, const QRect &r) {
        QPainterPath path;
        path.addRoundedRect(r, 14, 14);

        const auto &vc = VisualConfigManager::instance().cfg();
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull() && window() && vc.blurStrength > 0.001) {
            p.save();
            p.setClipPath(path);
            QPoint pos = mapTo(window(), QPoint(0, 0));
            p.setOpacity(vc.blurStrength);
            p.drawPixmap(r, bg, QRect(pos + r.topLeft(), r.size()));
            p.restore();
        }

        QColor fill = m_theme.panelBg;
        fill.setAlpha(vc.widgetCardAlpha);
        p.setBrush(fill);
        QColor edge = m_theme.chromeBorder;
        edge.setAlpha(120);
        p.setPen(QPen(edge, 1));
        p.drawPath(path);
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
};

// =========================================================
// Desktop background
// =========================================================
class DesktopBackground : public QWidget {
public:
    explicit DesktopBackground(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);

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

            QAction *aNewFolder = menu.addAction("New Folder");
            QAction *aNewFile   = menu.addAction("New File");
            menu.addSeparator();
            QAction *aChangeBg  = menu.addAction("Change Wallpaper...");
            QAction *aTerminal  = menu.addAction("Open Terminal Here");
            menu.addSeparator();
            QAction *aDisplay   = menu.addAction("Display Settings");
            QAction *aAbout     = menu.addAction("About Apokolips");

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
                f.open(QIODevice::WriteOnly);
                f.close();
            });
            QObject::connect(aTerminal, &QAction::triggered, []() {
                QProcess::startDetached("x-terminal-emulator",
                    { "--working-directory=" + QDir::homePath() });
            });
            QObject::connect(aDisplay, &QAction::triggered, []() {
                QProcess::startDetached("gnome-control-center",
                    { "display" });
            });
            QObject::connect(aAbout, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--demo", "About Apokolips", "0" });
            });

            // Change Wallpaper delegates to the same signal the View
            // menu uses — but we don't have a direct handle to root here,
            // so it re-emits via a lightweight path: open the submenu.
            QMenu *wallSub = menu.addMenu("Change Wallpaper");
            Q_UNUSED(aChangeBg);
            Q_UNUSED(wallSub);

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
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull()) {
            p.drawPixmap(QRect(0, 0, width(), TOP_H), bg,
                         QRect(0, 0, width(), TOP_H));
        }
        QColor tint = t.chromeBg;
        tint.setAlpha(150);
        p.fillRect(QRect(0, 0, width(), TOP_H), tint);
        p.setPen(QPen(t.accent, 2));
        p.drawLine(0, TOP_H - 1, width(), TOP_H - 1);
    }

private:
    Wallpaper          *m_wallpaper = nullptr;
    AppLauncher        *m_launcher = nullptr;
    NotificationCenter *m_notifications = nullptr;
    ControlCenter      *m_controlCenter = nullptr;
    DesktopWidgets     *m_widgets = nullptr;
    QTimer             *m_tickTimer = nullptr;
    QTimer             *m_blurTimer = nullptr;
};

// =========================================================
// Shell
// =========================================================
static int runShell(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Apokolips Shell");

    // Load theme BEFORE creating any chrome widget
    Wallpaper *initialWallpaper =
        WallpaperConfig::makeById(WallpaperConfig::loadId());
    ThemeManager::instance().setTheme(initialWallpaper->theme());

    QMainWindow window;
    window.setWindowFlags(Qt::FramelessWindowHint);
    window.setWindowTitle("Apokolips OS");

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

    NotificationCenter *notifications = new NotificationCenter(root);
    notifications->resize(360, root->height());
    notifications->hide();
    root->setNotifications(notifications);

    // Diamond button
    QPushButton *btnDiamond = new QPushButton(QString::fromUtf8("\xE2\x97\x86"));
    btnDiamond->setCursor(Qt::PointingHandCursor);
    btnDiamond->setFlat(true);
    ThemeManager::instance().subscribe([btnDiamond](const Theme &t) {
        btnDiamond->setStyleSheet(QString(
            "QPushButton { color: %1; background: transparent;"
            "  border: none; font-size: 15px; padding: 2px 4px; }"
            "QPushButton:hover { color: %2;"
            "  background: %3; border-radius: 5px; }"
            "QPushButton:pressed { background: %4; }")
            .arg(t.accent.name(), t.accent.lighter(120).name(),
                 rgba(t.accentSoft), rgba(t.accentStrong)));
    });
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

    QMenu *fileMenu = new QMenu;
    fileMenu->addAction("New Window");
    fileMenu->addAction("Open...");
    fileMenu->addSeparator();

    QAction *settingsAct = fileMenu->addAction("Settings…");
    settingsAct->setShortcut(QKeySequence("Ctrl+,"));
    QObject::connect(settingsAct, &QAction::triggered, []() {
        QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            { "--settings" });
    });

    fileMenu->addSeparator();
    fileMenu->addAction("Close Window");
    fileMenu->addAction("Quit Apokolips");
    topLayout->addWidget(makeMenuButton("File", fileMenu));

    QMenu *editMenu = new QMenu;
    editMenu->addAction("Undo");
    editMenu->addAction("Redo");
    editMenu->addSeparator();
    editMenu->addAction("Cut");
    editMenu->addAction("Copy");
    editMenu->addAction("Paste");
    topLayout->addWidget(makeMenuButton("Edit", editMenu));

    QMenu *viewMenu = new QMenu;
    viewMenu->addAction("Toggle Full Screen");
    viewMenu->addAction("Toggle Dock");
    viewMenu->addSeparator();

    QMenu *wallSub = viewMenu->addMenu("Change Wallpaper");
    ThemeManager::instance().subscribe([wallSub](const Theme &t) {
        wallSub->setStyleSheet(menuStyle(t));
    });

    const struct { const char *id; const char *name; } wallpapers[] = {
        { "ruby",       "Ruby"       },
        { "starfield",  "Starfield"  },
        { "aurora",     "Aurora"     },
        { "goldengate", "Golden Gate" }
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

    viewMenu->addAction("Enter Mission Control");
    topLayout->addWidget(makeMenuButton("View", viewMenu));

    QMenu *windowMenu = new QMenu;
    windowMenu->addAction("Minimize");
    windowMenu->addAction("Zoom");
    windowMenu->addSeparator();
    windowMenu->addAction("Bring All to Front");
    topLayout->addWidget(makeMenuButton("Window", windowMenu));

    QMenu *helpMenu = new QMenu;
    helpMenu->addAction("Apokolips Help");
    helpMenu->addAction("About Apokolips OS");
    topLayout->addWidget(makeMenuButton("Help", helpMenu));

    topLayout->addStretch();

    // ---- Right-side status cluster (macOS style) ----

    // WiFi
    QLabel *wifiLbl = new QLabel(QString::fromUtf8("\xE2\x97\x8F"));
    wifiLbl->setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().subscribe([wifiLbl](const Theme &t) {
        wifiLbl->setStyleSheet(QString(
            "color: %1; background: transparent; font-size: 12px;"
            " padding: 0px 5px;").arg(t.textPrimary.name()));
    });
    topLayout->addWidget(wifiLbl);

    // Bluetooth
    QLabel *btLbl = new QLabel(QString::fromUtf8("\xE2\x9C\xA6"));
    btLbl->setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().subscribe([btLbl](const Theme &t) {
        btLbl->setStyleSheet(QString(
            "color: %1; background: transparent; font-size: 12px;"
            " padding: 0px 5px;").arg(t.textPrimary.name()));
    });
    topLayout->addWidget(btLbl);

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

    // ---- Watch visual.json (opacity / blur sliders) ----
    QTimer *configPoll = new QTimer(&window);
    configPoll->setInterval(500);
    qint64 *lastMod = new qint64(
        QFileInfo(configPath).lastModified().toMSecsSinceEpoch());

    QObject::connect(configPoll, &QTimer::timeout,
                     [configPath, lastMod, root]() {
        qint64 m = QFileInfo(configPath).lastModified().toMSecsSinceEpoch();
        if (m != *lastMod) {
            *lastMod = m;
            VisualConfigManager::instance().load();
            if (root) {
                root->update();
                for (QWidget *w : root->findChildren<QWidget *>()) w->update();
            }
        }
    });
    configPoll->start();

    // ---- Watch wallpaper.json (wallpaper switch from Settings) ----
    QString wpPath = WallpaperConfig::configPath();
    QTimer *wpPoll = new QTimer(&window);
    wpPoll->setInterval(500);
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
