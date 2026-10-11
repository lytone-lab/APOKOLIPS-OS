#ifndef APOKOLIPS_WALLPAPER_H
#define APOKOLIPS_WALLPAPER_H

// Wallpaper base + all subclass wallpapers (Step 3)

#include "shell.h"
#include "theme.h"

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

class GifWallpaper : public Wallpaper {
public:
    explicit GifWallpaper(const QString &path) : m_path(path) {
        m_movie = new QMovie(path);
        m_movie->setCacheMode(QMovie::CacheAll);
        if (m_movie->isValid()) m_movie->start();
        QObject::connect(m_movie, &QMovie::frameChanged, [this]() {
            m_cached = QPixmap();
            m_cachedSize = QSize();
        });
    }
    ~GifWallpaper() override {
        if (m_movie) { m_movie->stop(); delete m_movie; }
    }
    QString id() const override { return "gif"; }
    QString displayName() const override { return "GIF"; }
    bool animated() const override { return true; }
    Theme theme() const override {
        Theme t;
        t.chromeBg      = QColor(14, 14, 18, 90);
        t.chromeBorder  = QColor(255, 255, 255, 55);
        t.accent        = QColor(235, 240, 250);
        t.accentSoft    = QColor(235, 240, 250, 50);
        t.accentStrong  = QColor(235, 240, 250, 100);
        t.textPrimary   = QColor(248, 250, 252);
        t.textSecondary = QColor(200, 208, 218);
        t.textDim       = QColor(140, 148, 160);
        t.panelBg       = QColor(14, 14, 18, 165);
        return t;
    }
    void tick() override {
        // QMovie self-ticks. Nothing extra needed.
    }
    void paint(QPainter &p, const QRect &r) override {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!m_movie || !m_movie->isValid()) {
            p.fillRect(r, QColor(20, 20, 30));
            return;
        }
        QPixmap src = m_movie->currentPixmap();
        if (src.isNull()) {
            p.fillRect(r, QColor(20, 20, 30));
            return;
        }
        if (m_cached.isNull() || m_cachedSize != r.size()) {
            QPixmap scaled = src.scaled(r.size(),
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation);
            int sx = (scaled.width()  - r.width())  / 2;
            int sy = (scaled.height() - r.height()) / 2;
            m_cached = scaled.copy(sx, sy, r.width(), r.height());
            m_cachedSize = r.size();
        }
        p.drawPixmap(r.topLeft(), m_cached);
    }
private:
    QString  m_path;
    QMovie  *m_movie = nullptr;
    QPixmap  m_cached;
    QSize    m_cachedSize;
};

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

#endif // APOKOLIPS_WALLPAPER_H
