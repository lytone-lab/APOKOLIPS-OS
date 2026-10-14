#ifndef APOKOLIPS_OVERLAYS_H
#define APOKOLIPS_OVERLAYS_H

// Animator daemon + Genie/Scale/Suck overlays (Step 5)

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"

struct AnimRequest {
    QString style;
    QString png;
    QRect   rect;
    QPoint  dock;
    bool    reverse = false;
    int     durationMs = 0;
    QString readySock;
};

static void genieHandshakeReady(const QString &sockName)
{
    qInfo() << "[hs] send: entered, name=" << sockName;
    if (sockName.isEmpty()) { qInfo() << "[hs] send: name empty, abort"; return; }
    QLocalSocket sock;
    sock.connectToServer(sockName);
    if (sock.waitForConnected(400)) {
        sock.write("ready");
        sock.waitForBytesWritten(200);
        sock.disconnectFromServer();
        qInfo() << "[hs] send: OK";
    } else {
        qInfo() << "[hs] send: connect FAILED" << sock.errorString();
    }
}

// Second ping — sent when the overlay has painted its LAST frame.
// Distinct from genieHandshakeReady (first paint). The restore path
// in main.cpp waits for this one before revealing the real window.
static void genieHandshakeDone(const QString &sockName)
{
    if (sockName.isEmpty()) return;
    QLocalSocket sock;
    sock.connectToServer(sockName);
    if (sock.waitForConnected(400)) {
        sock.write("done");
        sock.waitForBytesWritten(200);
        sock.disconnectFromServer();
    }
}


static QString genieMakeHandshakeName()
{
    return QString("apokolips-genie-ready-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

static double genieTimelineProgress(int frame, int frameCount, bool reverse)
{
    const double denominator = frameCount > 1
        ? static_cast<double>(frameCount - 1) : 1.0;
    double forward = static_cast<double>(frame) / denominator;
    if (forward < 0.0) forward = 0.0;
    if (forward > 1.0) forward = 1.0;
    return reverse ? 1.0 - forward : forward;
}

class SuckOverlay : public QWidget {
public:
    SuckOverlay(const QString &pngPath, QRect startRect,
                QPoint dockGlobal, int durationOverrideMs = 0,
                const QString &readySock = QString(),
                bool reverse = false,
                QWidget *parent = nullptr)
        : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
          m_reverse(reverse)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        if (!m_pix.load(pngPath) || startRect.isEmpty()) {
            qWarning() << "[suck] load failed:" << pngPath;
            QTimer::singleShot(0, qApp, &QApplication::quit);
            return;
        }
        QFile::remove(pngPath);

        QScreen *screen = QApplication::screenAt(startRect.center());
        if (!screen) screen = QApplication::primaryScreen();
        const QRect screenRect =
            screen ? screen->geometry() : QRect(0, 0, 1, 1);

        setGeometry(screenRect);
        m_origin = screenRect.topLeft();
        m_start = startRect.translated(-m_origin);
        m_dock = dockGlobal - m_origin;

        const auto &cfg = VisualConfigManager::instance().cfg();
        const int requestedMs = durationOverrideMs > 0
            ? durationOverrideMs : cfg.genieDurationMs;
        const int durationMs = qBound(128, requestedMs, 5000);
        m_frames = qBound(8, (durationMs + 15) / 16, 313);

        qDebug() << "[suck] durationMs=" << durationMs
                 << "frames=" << m_frames;

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &SuckOverlay::tick);
        m_readySock = readySock;
        QTimer::singleShot(30, this, [this]() {
            if (m_timer) m_timer->start();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_pix.isNull() || m_start.isEmpty()
            || width() <= 0 || height() <= 0)
            return;

        if (!m_readyPinged && !m_readySock.isEmpty()) {
            m_readyPinged = true;
            qInfo() << "[hs] overlay: first paint, will ping in 25ms, sock="
                    << m_readySock;
            QTimer::singleShot(25, this, [this]() {
                genieHandshakeReady(m_readySock);
            });
        }

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);

        qreal t = qBound<qreal>(0.0, qreal(m_frame) / qreal(qMax(1, m_frames - 1)), 1.0);
        if (m_reverse) t = 1.0 - t;
        const auto smoothStep = [](qreal v) {
            v = qBound<qreal>(0.0, v, 1.0);
            return v * v * (3.0 - 2.0 * v);
        };

        // Keep the top edge fixed while the bottom collapses to the dock.
        // Then draw the remaining triangle down to its final point.
        const qreal bottomProgress = smoothStep(t / 0.55);
        const qreal topProgress = smoothStep((t - 0.55) / 0.45);
        const QPointF dock(m_dock);
        const auto pullToDock = [&dock](const QPointF &from, qreal amount) {
            return from + (dock - from) * amount;
        };

        const QRectF source(m_start);
        const QPointF topLeft = pullToDock(source.topLeft(), topProgress);
        const QPointF topRight = pullToDock(source.topRight(), topProgress);
        const QPointF bottomRight =
            pullToDock(source.bottomRight(), bottomProgress);
        const QPointF bottomLeft =
            pullToDock(source.bottomLeft(), bottomProgress);

        QPainterPath silhouette;
        silhouette.moveTo(topLeft);
        silhouette.lineTo(topRight);
        silhouette.lineTo(bottomRight);
        silhouette.lineTo(bottomLeft);
        silhouette.closeSubpath();

        const QRectF bounds = silhouette.boundingRect();
        if (bounds.width() < 0.5 || bounds.height() < 0.5)
            return;

        const qreal fadeStart = 0.94;
        const qreal opacity = t <= fadeStart
            ? 1.0 : (1.0 - t) / (1.0 - fadeStart);
        p.setOpacity(qBound<qreal>(0.0, opacity, 1.0));
        p.setClipPath(silhouette, Qt::IntersectClip);
        p.drawPixmap(bounds, m_pix, QRectF(m_pix.rect()));
    }

private:
    void tick()
    {
        if (m_frame < m_frames - 1) {
            ++m_frame;
            update();
            return;
        }

        m_timer->stop();
        update();
        // Close the widget, not the app — the daemon must survive
        // between animations. quitOnLastWindowClosed still exits
        // the standalone-spawned overlay process automatically.
        QTimer::singleShot(80, this, [this]() { close(); });
    }

    QPixmap m_pix;
    QRect m_start;
    QPoint m_dock;
    QPoint m_origin;
    QTimer *m_timer = nullptr;
    int m_frame = 0;
    int m_frames = 30;
    bool m_reverse = false;
    QString m_readySock;
    bool    m_paintPinged = false;

    bool    m_readyPinged = false;};

class ThanosOverlay : public QWidget {
public:
    ThanosOverlay(const QString &pngPath, QRect startRect,
                  QPoint dockGlobal, int durationOverrideMs = 0,
                  const QString &readySock = QString(),
                  bool reverse = false,
                  QWidget *parent = nullptr)
        : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
          m_reverse(reverse)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        if (!m_pix.load(pngPath) || startRect.isEmpty()) {
            qWarning() << "[thanos] load failed:" << pngPath;
            QTimer::singleShot(0, qApp, &QApplication::quit);
            return;
        }
        QFile::remove(pngPath);

        QScreen *screen = QApplication::screenAt(startRect.center());
        if (!screen) screen = QApplication::primaryScreen();
        const QRect screenRect =
            screen ? screen->geometry() : QRect(0, 0, 1, 1);

        setGeometry(screenRect);
        m_origin = screenRect.topLeft();
        m_start = startRect.translated(-m_origin);
        m_dock = dockGlobal - m_origin;

        buildParticles();

        const auto &cfg = VisualConfigManager::instance().cfg();
        // Thanos reads as a slow dissolve. 4.5x a normal genie
        // gives the particles time to be seen individually.
        const int requestedMs = durationOverrideMs > 0
            ? durationOverrideMs
            : int(cfg.genieDurationMs * 6.0);
        const int durationMs = qBound(1500, requestedMs, 5000);
        m_frames = qBound(20, (durationMs + 15) / 16, 200);

        qInfo() << "[thanos] durationMs=" << durationMs
                << "frames=" << m_frames
                << "particles=" << m_particles.size();

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &ThanosOverlay::tick);
        m_readySock = readySock;
        QTimer::singleShot(30, this, [this]() {
            if (m_timer) m_timer->start();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_pix.isNull() || m_particles.isEmpty()
            || width() <= 0 || height() <= 0)
            return;

        if (!m_readyPinged && !m_readySock.isEmpty()) {
            m_readyPinged = true;
            QTimer::singleShot(25, this, [this]() {
                genieHandshakeReady(m_readySock);
            });
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        qreal t = qBound<qreal>(0.0,
            qreal(m_frame) / qreal(qMax(1, m_frames - 1)), 1.0);
        if (m_reverse) t = 1.0 - t;

        for (const Particle &pt : m_particles) {
            qreal raw = (t - pt.delay)
                / qMax<qreal>(0.0001, 1.0 - pt.delay);
            raw = qBound<qreal>(0.0, raw, 1.0);
            const qreal eased = raw * raw * (3.0 - 2.0 * raw);
            const qreal x = pt.origin.x() + pt.driftX * eased;
            const qreal y = pt.origin.y() + pt.driftY * eased;
            const int a = int((1.0 - eased) * 255.0);
            if (a <= 0) continue;
            QColor c = pt.color;
            c.setAlpha(a);
            p.fillRect(int(x), int(y), 3, 3, c);
        }
    }

private:
    struct Particle {
        QPointF origin;
        QColor  color;
        qreal   delay;
        qreal   driftX;
        qreal   driftY;
    };

    void buildParticles()
    {
        m_particles.clear();
        const int cellSize = 7;
        const int cols = m_pix.width()  / cellSize;
        const int rows = m_pix.height() / cellSize;
        QImage img = m_pix.toImage();

        quint32 seed = 0x9E3779B9u;
        auto nextRand = [&seed]() -> qreal {
            seed = seed * 1103515245u + 12345u;
            return qreal(seed % 10000u) / 10000.0;
        };

        for (int cy = 0; cy < rows; ++cy) {
            for (int cx = 0; cx < cols; ++cx) {
                const int rx = cx * cellSize;
                const int ry = cy * cellSize;
                qint64 rSum = 0, gSum = 0, bSum = 0, aSum = 0;
                int n = 0;
                for (int y = ry; y < ry + cellSize
                                && y < img.height(); y += 2) {
                    for (int x = rx; x < rx + cellSize
                                    && x < img.width(); x += 2) {
                        QColor c = img.pixelColor(x, y);
                        rSum += c.red();
                        gSum += c.green();
                        bSum += c.blue();
                        aSum += c.alpha();
                        ++n;
                    }
                }
                if (n == 0) continue;
                QColor avg(int(rSum / n), int(gSum / n),
                           int(bSum / n), int(aSum / n));
                if (avg.alpha() < 4) continue;

                Particle pt;
                pt.origin = QPointF(m_start.left() + rx,
                                    m_start.top()  + ry);
                pt.color  = avg;
                pt.delay  = nextRand() * 0.35;
                pt.driftX = 20.0 + nextRand() * 90.0;
                pt.driftY = -40.0 + nextRand() * 60.0;
                m_particles.append(pt);
            }
        }
    }

    void tick()
    {
        if (++m_frame >= m_frames) {
            m_timer->stop();
            close();
        }
        update();
    }

    QPixmap         m_pix;
    QRect           m_start;
    QPoint          m_origin;
    QPoint          m_dock;
    QList<Particle> m_particles;
    int             m_frame = 0;
    int             m_frames = 60;
    bool            m_reverse = false;
    QString         m_readySock;
    bool            m_readyPinged = false;
    QTimer         *m_timer = nullptr;
};

class ScaleOverlay : public QWidget {
public:
    ScaleOverlay(const QString &pngPath, QRect startRect,
                 QPoint dockGlobal, int durationOverrideMs = 0,
                 const QString &readySock = QString(),
                 bool reverse = false,
                 QWidget *parent = nullptr)
        : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
          m_reverse(reverse)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        if (!m_pix.load(pngPath) || startRect.isEmpty()) {
            qWarning() << "[scale] load failed:" << pngPath;
            QTimer::singleShot(0, qApp, &QApplication::quit);
            return;
        }
        QFile::remove(pngPath);

        QScreen *screen = QApplication::screenAt(startRect.center());
        if (!screen) screen = QApplication::primaryScreen();
        const QRect screenRect =
            screen ? screen->geometry() : QRect(0, 0, 1, 1);

        setGeometry(screenRect);
        m_origin = screenRect.topLeft();
        m_start = startRect.translated(-m_origin);
        m_dock = dockGlobal - m_origin;

        const auto &cfg = VisualConfigManager::instance().cfg();
        const int requestedMs = durationOverrideMs > 0
            ? durationOverrideMs : cfg.genieDurationMs;
        const int durationMs = qBound(128, requestedMs, 5000);
        m_frames = qBound(8, (durationMs + 15) / 16, 313);

        qDebug() << "[scale] durationMs=" << durationMs
                 << "frames=" << m_frames;

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &ScaleOverlay::tick);
        m_readySock = readySock;
        QTimer::singleShot(30, this, [this]() {
            if (m_timer) m_timer->start();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_pix.isNull() || m_start.isEmpty()
            || width() <= 0 || height() <= 0)
            return;

        if (!m_readyPinged && !m_readySock.isEmpty()) {
            m_readyPinged = true;
            qInfo() << "[hs] overlay: first paint, will ping in 25ms, sock="
                    << m_readySock;
            QTimer::singleShot(25, this, [this]() {
                genieHandshakeReady(m_readySock);
            });
        }

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        qreal raw = qBound<qreal>(
            0.0, qreal(m_frame) / qreal(qMax(1, m_frames - 1)), 1.0);
        if (m_reverse) raw = 1.0 - raw;
        const qreal t = raw * raw * (3.0 - 2.0 * raw);

        const qreal fadeStart = 0.94;
        const qreal opacity = t <= fadeStart
            ? 1.0 : (1.0 - t) / (1.0 - fadeStart);
        p.setOpacity(qBound<qreal>(0.0, opacity, 1.0));

        const QPointF startCenter = m_start.center();
        const QPointF center = startCenter +
            (QPointF(m_dock) - startCenter) * t;
        const qreal maxSourceSide =
            qMax<qreal>(m_start.width(), m_start.height());
        const qreal scale = maxSourceSide > 0.0
            ? 1.0 + (34.0 / maxSourceSide - 1.0) * t : 0.0;
        const QSizeF size(m_start.width() * scale,
                          m_start.height() * scale);
        const QRectF destination(center.x() - size.width() * 0.5,
                                 center.y() - size.height() * 0.5,
                                 size.width(), size.height());

        p.drawPixmap(destination, m_pix, QRectF(m_pix.rect()));
    }

private:
    void tick()
    {
        if (m_frame < m_frames - 1) {
            ++m_frame;
            update();
            return;
        }

        m_timer->stop();
        update();
        // Close the widget, not the app — the daemon must survive
        // between animations. quitOnLastWindowClosed still exits
        // the standalone-spawned overlay process automatically.
        QTimer::singleShot(80, this, [this]() { close(); });
    }

    QPixmap m_pix;
    QRect m_start;
    QPoint m_dock;
    QPoint m_origin;
    QTimer *m_timer = nullptr;
    int m_frame = 0;
    int m_frames = 30;
    bool m_reverse = false;
    QString m_readySock;
    bool    m_paintPinged = false;

    bool    m_readyPinged = false;};

class GenieOverlay : public QWidget {
public:
    GenieOverlay(const QString &pngPath, QRect startRect,
                 QPoint dockGlobal, bool reverse = false,
                 int durationOverrideMs = 0,
                 const QString &readySock = QString(),
                 QWidget *parent = nullptr)
        : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
          m_reverse(reverse)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        if (!m_pix.load(pngPath) || startRect.isEmpty()) {
            qWarning() << "[genie] load failed:" << pngPath;
            QTimer::singleShot(0, qApp, &QApplication::quit);
            return;
        }
        QFile::remove(pngPath);

        QScreen *screen = QApplication::screenAt(startRect.center());
        if (!screen) screen = QApplication::primaryScreen();
        const QRect screenRect =
            screen ? screen->geometry() : QRect(0, 0, 1, 1);

        setGeometry(screenRect);
        m_origin = screenRect.topLeft();
        m_start = startRect.translated(-m_origin);
        m_dock = dockGlobal - m_origin;

        const auto &cfg = VisualConfigManager::instance().cfg();
        m_strips = qBound(40, cfg.genieStrips, 320);
        m_sCount = qBound(1, cfg.genieSCount, 5);
        m_stagger = qBound(0.0, cfg.genieStagger, 0.8);
        m_curve = qBound(0.3, cfg.genieCurve, 2.0);
        m_fadeFrom = qBound(0.5, cfg.genieFadeStart, 1.0);

        const int requestedMs = durationOverrideMs > 0
            ? durationOverrideMs : cfg.genieDurationMs;
        const int durationMs = qBound(128, requestedMs, 5000);
        m_frames = qBound(8, (durationMs + 15) / 16, 313);

        qDebug() << "[genie] reverse=" << m_reverse
                 << "durationMs=" << durationMs
                 << "frames=" << m_frames;

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &GenieOverlay::tick);
        m_readySock = readySock;
        QTimer::singleShot(30, this, [this]() {
            if (m_timer) m_timer->start();
        });
    }

protected:
        void paintEvent(QPaintEvent *) override
    {
        if (m_pix.isNull() || m_start.isEmpty()
            || width() <= 0 || height() <= 0) return;

        if (!m_readyPinged && !m_readySock.isEmpty()) {
            m_readyPinged = true;
            qInfo() << "[hs] overlay: first paint, will ping in 25ms, sock="
                    << m_readySock;
            QTimer::singleShot(25, this, [this]() {
                genieHandshakeReady(m_readySock);
            });
        }

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(1.0);

        const qreal t = qBound<qreal>(
            0.0, genieTimelineProgress(m_frame, m_frames, m_reverse), 1.0);

        const auto easeWithImmediateMotion = [](qreal x) {
            x = qBound<qreal>(0.0, x, 1.0);
            return x * (2.0 - x);
        };
        const auto dist = [](const QPointF &a, const QPointF &b) {
            return std::hypot(a.x() - b.x(), a.y() - b.y());
        };

        const QRectF src(m_start);
        const QPointF dock(m_dock);

        // Nearest of ALL FOUR corners to the dock tile. Handles
        // every dock position: bottom, left, right, and any corner
        // of any window. Previous version only checked bottom-left
        // and bottom-right, which is wrong when the dock is on the
        // right and the window sits high on screen.
        const QPointF corners[4] = {
            src.topLeft(), src.topRight(),
            src.bottomRight(), src.bottomLeft()
        };
        int tipIdx = 0;
        qreal bestDist = 1e18;
        for (int i = 0; i < 4; ++i) {
            qreal d = dist(corners[i], dock);
            if (d < bestDist) { bestDist = d; tipIdx = i; }
        }
        const QPointF tipSrc = corners[tipIdx];

        // Once per animation: log the corner that was selected.
        // Lets us verify corner-aware placement without guessing.
        if (m_frame == 0) {
            qInfo() << "[genie-tip] corner idx=" << tipIdx
                    << "(0=TL 1=TR 2=BR 3=BL)"
                    << "tipSrc=" << tipSrc
                    << "dock=" << dock;
        }

        // Normalizer: longest diagonal of the window.
        const qreal diag = qMax<qreal>(1.0, dist(src.topLeft(),
                                                src.bottomRight()));

        // Every point pours directly to the dock. Rate depends on
        // how close the point starts to the tip corner:
        //   points on the tip corner:  rate 3.5  (arrive fast)
        //   points on the opposite:    rate 0.5  (trail behind)
        // Result: the tip-side of the window rushes to the dock
        // while the far side follows — a pour, not a slide.
        //
        // A single rate per point means straight edges — no C curve.
        const auto destForPoint = [&](const QPointF &sp) -> QPointF {
            const qreal d = dist(sp, tipSrc);
            const qreal dn = qBound<qreal>(0.0, d / diag, 1.0);
            const qreal rate = 5.5 - 5.0 * dn;   // pour without slivers
            const qreal base = easeWithImmediateMotion(t);
            const qreal progress = 1.0 - std::pow(1.0 - base, rate);
            return sp + (dock - sp) * progress;
        };

        constexpr int SEGS = 24;
        const qreal segSrcH = src.height() / SEGS;
        const qreal segPixH = m_pix.height() / qreal(SEGS);
        const qreal segW = m_pix.width();

        for (int i = 0; i < SEGS; ++i) {
            const qreal y0 = src.top() + i * segSrcH;
            const qreal y1 = y0 + segSrcH;

            const QPointF dtl = destForPoint(QPointF(src.left(), y0));
            const QPointF dtr = destForPoint(QPointF(src.right(), y0));
            const QPointF dbr = destForPoint(QPointF(src.right(), y1));
            const QPointF dbl = destForPoint(QPointF(src.left(), y1));

            // Reject degenerate and near-degenerate quads.
            //  - maxD < 0.4        : a point (skip)
            //  - minSpan < 0.6     : a sliver (quadToQuad garbage)
            qreal maxD = 0.0;
            const QPointF pts[4] = { dtl, dtr, dbr, dbl };
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                    maxD = qMax(maxD, dist(pts[a], pts[b]));
            if (maxD < 0.4) continue;

            const qreal minSpan = qMin(
                qMin(dist(dtl, dbl), dist(dtr, dbr)),
                qMin(dist(dtl, dtr), dist(dbl, dbr)));
            if (minSpan < 0.6) continue;

            QPolygonF srcQuad;
            srcQuad << QPointF(0, 0)
                    << QPointF(segW, 0)
                    << QPointF(segW, segSrcH)
                    << QPointF(0, segSrcH);

            QPolygonF dstQuad;
            dstQuad << dtl << dtr << dbr << dbl;

            QTransform xf;
            if (!QTransform::quadToQuad(srcQuad, dstQuad, xf))
                continue;

            p.save();
            p.setTransform(xf, false);
            p.drawPixmap(0, 0, m_pix,
                         0, int(i * segPixH),
                         int(segW), int(segPixH));
            p.restore();
        }
    }

private:
    void tick()
    {
        if (m_frame < m_frames - 1) { ++m_frame; update(); return; }
        m_timer->stop();
        // Ping the shell that the animation is finished.
        // genieHandshakeDone writes "done" — the restore path in
        // main.cpp waits for that string specifically.
        if (!m_readySock.isEmpty())
            genieHandshakeDone(m_readySock);

        update();
        // Close the widget, not the app — the daemon must survive
        // between animations. quitOnLastWindowClosed still exits
        // the standalone-spawned overlay process automatically.
        QTimer::singleShot(80, this, [this]() { close(); });
    }

    QPixmap m_pix;
    QRect m_start;
    QPoint m_dock;
    QPoint m_origin;
    QTimer *m_timer = nullptr;
    int m_frame = 0;
    int m_frames = 30;
    int m_strips = 140;
    int m_sCount = 1;
    qreal m_stagger = 0.35;
    qreal m_curve = 0.65;
    qreal m_fadeFrom = 0.95;
    bool m_reverse = false;
    QString m_readySock;
    bool    m_paintPinged = false;

    bool    m_readyPinged = false;};

static QString animatorSocketName()
{
    return QStringLiteral("apokolips-animator");
}

static bool animatorDaemonRunning()
{
    QLocalSocket sock;
    sock.connectToServer(animatorSocketName());
    return sock.waitForConnected(80);
}

static bool animatorRequest(const AnimRequest &req)
{
    QLocalSocket sock;
    sock.connectToServer(animatorSocketName());
    if (!sock.waitForConnected(80)) return false;

    QJsonObject o;
    o["style"]   = req.style;
    o["png"]     = req.png;
    o["x"]       = req.rect.x();
    o["y"]       = req.rect.y();
    o["w"]       = req.rect.width();
    o["h"]       = req.rect.height();
    o["dx"]      = req.dock.x();
    o["dy"]      = req.dock.y();
    o["reverse"] = req.reverse;
    o["ms"]      = req.durationMs;
    o["ready"]   = req.readySock;

    const QByteArray payload =
        QJsonDocument(o).toJson(QJsonDocument::Compact);
    sock.write(payload);
    if (!sock.waitForBytesWritten(80)) return false;
    sock.disconnectFromServer();
    return true;
}

static int runAnimatorDaemon(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Apokolips Animator");
    app.setDesktopFileName("apokolips-animator");

    // Closing the last overlay widget must NOT quit the daemon —
    // the daemon lives between animations.
    app.setQuitOnLastWindowClosed(false);

    QLocalServer::removeServer(animatorSocketName());
    QLocalServer server;
    if (!server.listen(animatorSocketName())) {
        qWarning() << "[animator] listen failed:" << server.errorString();
        return 1;
    }
    qInfo() << "[animator] listening on" << animatorSocketName();

    QObject::connect(&server, &QLocalServer::newConnection, [&server]() {
        while (QLocalSocket *conn = server.nextPendingConnection()) {
            QObject::connect(conn, &QLocalSocket::readyRead,
                             [conn]() {
                const QByteArray data = conn->readAll();
                conn->disconnectFromServer();
                if (data.isEmpty()) return;

                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) return;
                const QJsonObject o = doc.object();

                const QString style = o.value("style").toString("genie");
                const QString png   = o.value("png").toString();
                const QRect rect(o.value("x").toInt(), o.value("y").toInt(),
                                 o.value("w").toInt(), o.value("h").toInt());
                const QPoint dock(o.value("dx").toInt(),
                                  o.value("dy").toInt());
                const bool reverse = o.value("reverse").toBool(false);
                const int  ms      = o.value("ms").toInt(0);
                const QString ready = o.value("ready").toString();

                if (png.isEmpty() || !QFile::exists(png)) {
                    qWarning() << "[animator] missing png:" << png;
                    return;
                }

                QWidget *w = nullptr;
                if (style == "scale")
                    w = new ScaleOverlay(png, rect, dock, ms, ready,
                                         reverse);
                else if (style == "suck")
                    w = new SuckOverlay(png, rect, dock, ms, ready,
                                        reverse);
                else if (style == "thanos")
                    w = new ThanosOverlay(png, rect, dock, ms, ready,
                                          reverse);
                else
                    w = new GenieOverlay(png, rect, dock, reverse, ms,
                                         ready);

                w->setAttribute(Qt::WA_DeleteOnClose, true);
                w->show();
                qInfo() << "[animator] playing" << style
                        << "reverse=" << reverse << "ms=" << ms;
            });
        }
    });

    return app.exec();
}



// --- Cleanup pass ---
class GlyphOverlay : public QWidget {
public:
    GlyphOverlay() {
        setWindowFlags(Qt::FramelessWindowHint
                       | Qt::WindowStaysOnTopHint
                       | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);

        QProcess p;
        p.start("grim", { "-" });
        p.waitForFinished(2000);
        m_frozen.loadFromData(p.readAllStandardOutput());

        if (QScreen *s = QGuiApplication::primaryScreen())
            setGeometry(s->geometry());
        showFullScreen();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (!m_frozen.isNull())
            p.drawPixmap(rect(), m_frozen);
        else
            p.fillRect(rect(), QColor(20, 20, 22));

        p.fillRect(rect(), QColor(0, 0, 0, 150));

        if (m_sel.isValid() && m_sel.width() > 1 && m_sel.height() > 1) {
            QRect clipped = m_sel.intersected(rect());
            if (!m_frozen.isNull()) {
                p.drawPixmap(clipped, m_frozen,
                             QRect(clipped.topLeft()
                                     - rect().topLeft(),
                                   clipped.size()));
            }
            QPen pen(QColor(168, 50, 50), 2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(clipped.adjusted(0, 0, -1, -1));

            QString dim = QString("%1 × %2")
                .arg(m_sel.width()).arg(m_sel.height());
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            QFontMetrics fm(f);
            int tw = fm.horizontalAdvance(dim) + 16;
            int th = 22;
            int tx = clipped.left();
            int ty = clipped.bottom() + 6;
            if (ty + th > rect().bottom()) ty = clipped.top() - th - 6;
            QRect box(tx, ty, tw, th);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 18, 24, 230));
            p.drawRoundedRect(box, 6, 6);
            p.setPen(QColor(240, 237, 232));
            p.drawText(box, Qt::AlignCenter, dim);
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) {
            if (e->button() == Qt::RightButton) close();
            return;
        }
        m_anchor = e->pos();
        m_sel = QRect(m_anchor, m_anchor);
        m_dragging = true;
        update();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_dragging) return;
        m_sel = QRect(m_anchor, e->pos()).normalized();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return;
        m_dragging = false;
        if (m_sel.width() < 4 || m_sel.height() < 4) {
            m_sel = QRect();
            update();
            return;
        }
        showToolbar();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) close();
    }

private:
    void showToolbar() {
        if (m_toolbar) m_toolbar->deleteLater();

        m_toolbar = new QWidget(nullptr,
            Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
            | Qt::Tool);
        m_toolbar->setAttribute(Qt::WA_DeleteOnClose);
        m_toolbar->setAttribute(Qt::WA_TranslucentBackground, true);
        m_toolbar->setAttribute(Qt::WA_StyledBackground, true);

        QWidget *inner = new QWidget(m_toolbar);
        inner->setObjectName("glyphToolbar");
        inner->setAttribute(Qt::WA_StyledBackground, true);
        inner->setStyleSheet(
            "QWidget#glyphToolbar {"
            "  background: rgba(22, 20, 26, 245);"
            "  border: 1px solid rgba(255, 255, 255, 40);"
            "  border-radius: 12px;"
            "}"
            "QPushButton {"
            "  background: transparent;"
            "  color: rgb(240, 237, 232);"
            "  border: none;"
            "  border-radius: 7px;"
            "  padding: 6px 16px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover { background: rgba(255, 255, 255, 40); }"
            "QPushButton#save {"
            "  background: rgb(168, 50, 50);"
            "  color: #ffffff;"
            "}"
            "QPushButton#save:hover { background: rgb(200, 74, 74); }");

        QVBoxLayout *outerWrap = new QVBoxLayout(m_toolbar);
        outerWrap->setContentsMargins(0, 0, 0, 0);
        outerWrap->setSpacing(0);
        outerWrap->addWidget(inner);

        QHBoxLayout *h = new QHBoxLayout(inner);
        h->setContentsMargins(6, 6, 6, 6);
        h->setSpacing(4);
        h->setSizeConstraint(QLayout::SetFixedSize);

        QPushButton *save = new QPushButton("Save");
        save->setObjectName("save");
        save->setCursor(Qt::PointingHandCursor);
        save->setMinimumWidth(72);
        save->setFixedHeight(28);
        QPushButton *copy = new QPushButton("Copy");
        copy->setCursor(Qt::PointingHandCursor);
        copy->setMinimumWidth(72);
        copy->setFixedHeight(28);
        QPushButton *cancel = new QPushButton("Cancel");
        cancel->setCursor(Qt::PointingHandCursor);
        cancel->setMinimumWidth(72);
        cancel->setFixedHeight(28);

        h->addWidget(save);
        h->addWidget(copy);
        h->addWidget(cancel);

        QObject::connect(save, &QPushButton::clicked, [this]() {
            QString dir = QDir::homePath() + "/Pictures/Screenshots";
            QDir().mkpath(dir);
            QString path = dir + "/Glyph-"
                + QDateTime::currentDateTime()
                    .toString("yyyyMMdd-HHmmss") + ".png";
            if (m_frozen.copy(m_sel).save(path)) {
                QByteArray png;
                QBuffer buf(&png);
                buf.open(QIODevice::WriteOnly);
                m_frozen.copy(m_sel).save(&buf, "PNG");
                QProcess *cp = new QProcess;
                cp->start("wl-copy", { "-t", "image/png" });
                if (cp->waitForStarted(500)) {
                    cp->write(png);
                    cp->closeWriteChannel();
                }
            }
            close();
        });

        QObject::connect(copy, &QPushButton::clicked, [this]() {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            m_frozen.copy(m_sel).save(&buf, "PNG");
            QProcess *cp = new QProcess;
            cp->start("wl-copy", { "-t", "image/png" });
            if (cp->waitForStarted(500)) {
                cp->write(png);
                cp->closeWriteChannel();
            }
            close();
        });

        QObject::connect(cancel, &QPushButton::clicked,
                         [this]() { close(); });

        inner->adjustSize();
        QSize want = inner->sizeHint();
        if (want.width()  < 220) want.setWidth(220);
        if (want.height() < 38)  want.setHeight(38);
        m_toolbar->setMinimumSize(want);
        m_toolbar->setMaximumSize(want);
        m_toolbar->resize(want);

        QRect sel = m_sel;
        int tx = sel.right() - want.width() + 1;
        int ty = sel.bottom() + 8;
        if (ty + want.height() > rect().bottom())
            ty = sel.top() - want.height() - 8;
        if (tx + want.width() > rect().right() + 1)
            tx = rect().right() + 1 - want.width();
        if (tx < 0) tx = 0;
        if (ty < 0) ty = 0;

        QPoint g = mapToGlobal(QPoint(tx, ty));
        m_toolbar->move(g);
        m_toolbar->show();
        m_toolbar->resize(want);
        m_toolbar->raise();
        m_toolbar->activateWindow();
    }

    QPixmap  m_frozen;
    QRect    m_sel;
    QPoint   m_anchor;
    bool     m_dragging = false;
    QWidget *m_toolbar = nullptr;
};

class SnapPicker : public QWidget {
public:
    SnapPicker() {
        setWindowFlags(Qt::FramelessWindowHint
                       | Qt::WindowStaysOnTopHint
                       | Qt::Tool);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFixedSize(248, 248);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);

        if (QScreen *s = QGuiApplication::primaryScreen())
            move(s->geometry().center() - rect().center());

        show();
        raise();
        activateWindow();
    }

protected:
    QRectF cell(int idx) const {
        const qreal pad = 12;
        const qreal gap = 4;
        qreal w = (width()  - 2 * pad - 2 * gap) / 3.0;
        qreal h = (height() - 2 * pad - 2 * gap) / 3.0;
        int col = idx % 3;
        int row = idx / 3;
        return QRectF(pad + col * (w + gap),
                      pad + row * (h + gap), w, h);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Background panel
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 16, 16);
        p.fillPath(path, QColor(22, 20, 26, 245));
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));
        p.drawPath(path);

        for (int i = 0; i < 9; ++i) {
            QRectF r = cell(i);
            bool hov = (i == m_hover);

            // Cell background
            QPainterPath cp;
            cp.addRoundedRect(r, 6, 6);
            p.fillPath(cp, hov ? QColor(168, 50, 50)
                               : QColor(255, 255, 255, 20));

            // Draw the "window" preview inside the cell
            QColor shape = hov ? QColor(255, 255, 255) : QColor(200, 200, 210, 180);
            QRectF inner = r.adjusted(r.width() * 0.14, r.height() * 0.14,
                                      -r.width() * 0.14, -r.height() * 0.14);

            // Which half of the cell to fill, based on idx
            QRectF fill;
            if      (i == 0) fill = QRectF(inner.left(),  inner.top(),
                                            inner.width(), inner.height() / 2); // top half
            else if (i == 2) fill = QRectF(inner.left(),  inner.center().y(),
                                            inner.width(), inner.height() / 2); // bottom half
            else if (i == 3) fill = QRectF(inner.left(),  inner.top(),
                                            inner.width() / 2, inner.height()); // left half
            else if (i == 5) fill = QRectF(inner.center().x(), inner.top(),
                                            inner.width() / 2, inner.height()); // right half
            else if (i == 6) fill = QRectF(inner.left(),  inner.top(),
                                            inner.width() / 2, inner.height() / 2); // TL
            else if (i == 8) fill = QRectF(inner.center().x(), inner.center().y(),
                                            inner.width() / 2, inner.height() / 2); // BR
            else if (i == 4) { // center — smaller inner rect
                fill = inner.adjusted(inner.width() * 0.18,
                                      inner.height() * 0.18,
                                      -inner.width() * 0.18,
                                      -inner.height() * 0.18);
            } else {
                // corners without specific meaning — draw full inner as X
                fill = inner;
            }

            if (i == 4) {
                QPainterPath fp;
                fp.addRoundedRect(fill, 3, 3);
                p.fillPath(fp, shape);
            } else if (i == 1 || i == 7) {
                // Unused — draw X mark
                p.setPen(QPen(QColor(200, 200, 210, 60), 1.5));
                p.drawLine(inner.topLeft(), inner.bottomRight());
                p.drawLine(inner.topRight(), inner.bottomLeft());
            } else {
                QPainterPath fp;
                fp.addRoundedRect(fill, 3, 3);
                p.fillPath(fp, shape);
                // Draw the empty area outline
                p.setPen(QPen(QColor(200, 200, 210, 60), 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(inner, 3, 3);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        int hov = -1;
        for (int i = 0; i < 9; ++i) {
            if (cell(i).contains(e->pos())) { hov = i; break; }
        }
        if (hov != m_hover) { m_hover = hov; update(); }
    }

    void leaveEvent(QEvent *) override {
        m_hover = -1;
        update();
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) { close(); return; }
        int idx = -1;
        for (int i = 0; i < 9; ++i) {
            if (cell(i).contains(e->pos())) { idx = i; break; }
        }
        if (idx >= 0) applySnap(idx);
        close();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) close();
    }

private:
    void applySnap(int idx) {
        QScreen *s = QGuiApplication::primaryScreen();
        if (!s) return;
        QRect g = s->geometry();
        int W = g.width();
        int H = g.height();
        int HW = W / 2;
        int HH = H / 2;

        auto run = [](const QStringList &cmds) {
            for (const QString &c : cmds) {
                QProcess::startDetached("swaymsg", c.split(" "));
            }
        };

        switch (idx) {
        case 0: run({ "resize set " + QString::number(W) + " " + QString::number(HH),
                      "move position " + QString::number(g.x()) + " " + QString::number(g.y()) }); break;
        case 2: run({ "resize set " + QString::number(W) + " " + QString::number(H - HH),
                      "move position " + QString::number(g.x()) + " " + QString::number(g.y() + HH) }); break;
        case 3: run({ "resize set " + QString::number(HW) + " " + QString::number(H),
                      "move position " + QString::number(g.x()) + " " + QString::number(g.y()) }); break;
        case 5: run({ "resize set " + QString::number(W - HW) + " " + QString::number(H),
                      "move position " + QString::number(g.x() + HW) + " " + QString::number(g.y()) }); break;
        case 6: run({ "resize set " + QString::number(HW) + " " + QString::number(HH),
                      "move position " + QString::number(g.x()) + " " + QString::number(g.y()) }); break;
        case 8: run({ "resize set " + QString::number(W - HW) + " " + QString::number(H - HH),
                      "move position " + QString::number(g.x() + HW) + " " + QString::number(g.y() + HH) }); break;
        case 4: {
            int cw = W * 3 / 5;
            int ch = H * 3 / 5;
            run({ "resize set " + QString::number(cw) + " " + QString::number(ch),
                  "move position " + QString::number(g.x() + (W - cw) / 2) + " "
                                  + QString::number(g.y() + (H - ch) / 2) }); break;
        }
        default: break;
        }
    }

    int m_hover = -1;
};

class ScreensaverOverlay : public QWidget {
public:
    ScreensaverOverlay() {
        setWindowFlags(Qt::FramelessWindowHint
                       | Qt::WindowStaysOnTopHint
                       | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setCursor(Qt::BlankCursor);
        setFocusPolicy(Qt::StrongFocus);

        auto &cfg = VisualConfigManager::instance().cfg();
        m_style = cfg.screensaverStyle;

        if (QScreen *s = QGuiApplication::primaryScreen())
            setGeometry(s->geometry());

        // Freeze the current desktop behind the overlay
        QProcess p;
        p.start("grim", { "-" });
        p.waitForFinished(1500);
        m_bg.loadFromData(p.readAllStandardOutput());

        // Slideshow — collect images
        if (m_style == "slideshow" && !cfg.screensaverImageDir.isEmpty()) {
            QDir d(cfg.screensaverImageDir);
            if (d.exists()) {
                QStringList filters = { "*.png", "*.jpg", "*.jpeg",
                                        "*.bmp", "*.webp" };
                for (const QString &f :
                     d.entryList(filters, QDir::Files, QDir::Name))
                    m_images << d.filePath(f);
            }
        }

        if (m_style == "slideshow" && !m_images.isEmpty()) {
            m_slideTimer = new QTimer(this);
            m_slideTimer->setInterval(8000);
            QObject::connect(m_slideTimer, &QTimer::timeout,
                             this, &ScreensaverOverlay::nextImage);
            nextImage();
            m_slideTimer->start();
        }

        // Clock tick (used for clock + slideshow overlay info)
        m_tick = new QTimer(this);
        m_tick->setInterval(500);
        QObject::connect(m_tick, &QTimer::timeout, this,
                         [this]() { update(); });
        m_tick->start();

        showFullScreen();
        raise();
        activateWindow();
        setFocus();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Slideshow: draw the image full-bleed
        if (m_style == "slideshow" && !m_current.isNull()) {
            QRect target = rect();
            QSize sz = m_current.size();
            sz.scale(target.size(), Qt::KeepAspectRatioByExpanding);
            QRect src(0, 0, sz.width(), sz.height());
            src.moveCenter(QRect(QPoint(0, 0), m_current.size()).center());
            p.drawPixmap(target, m_current, src);
            // Dim overlay
            p.fillRect(rect(), QColor(0, 0, 0, 100));
        } else {
            // dim / clock — frozen desktop under dark veil
            if (!m_bg.isNull())
                p.drawPixmap(rect(), m_bg);
            else
                p.fillRect(rect(), QColor(20, 20, 22));
            p.fillRect(rect(), QColor(0, 0, 0, 160));
        }

        // Clock text on top of everything
        if (m_style == "clock" || m_style == "slideshow") {
            QDateTime now = QDateTime::currentDateTime();
            auto &c = VisualConfigManager::instance().cfg();

            QString tfmt = c.clock24Hour ? "HH:mm" : "hh:mm AP";
            if (c.clockShowSeconds)
                tfmt = c.clock24Hour ? "HH:mm:ss" : "hh:mm:ss AP";

            QFont f = font();
            f.setPixelSize(140);
            f.setWeight(QFont::Thin);
            p.setFont(f);
            p.setPen(QColor(240, 237, 232, 230));
            p.drawText(rect().adjusted(0, -60, 0, 0),
                       Qt::AlignCenter, now.toString(tfmt));

            f.setPixelSize(24);
            f.setWeight(QFont::Light);
            p.setFont(f);
            p.setPen(QColor(176, 173, 168, 200));
            p.drawText(rect().adjusted(0, 140, 0, 0),
                       Qt::AlignCenter,
                       now.toString("dddd, d MMMM yyyy"));
        }

        // hint
        QFont h = font();
        h.setPixelSize(12);
        p.setFont(h);
        p.setPen(QColor(122, 120, 115, 140));
        p.drawText(rect().adjusted(0, 0, 0, -24),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   "Move the mouse or press any key to wake");
    }

    void keyPressEvent(QKeyEvent *e) override { Q_UNUSED(e); close(); }
    void mousePressEvent(QMouseEvent *) override { close(); }
    void mouseMoveEvent(QMouseEvent *) override  { close(); }

private:
    void nextImage() {
        if (m_images.isEmpty()) return;
        m_index = (m_index + 1) % m_images.size();
        m_current.load(m_images[m_index]);
        update();
    }

    QString              m_style;
    QPixmap              m_bg;
    QPixmap              m_current;
    QStringList          m_images;
    int                  m_index = -1;
    QTimer              *m_slideTimer = nullptr;
    QTimer              *m_tick = nullptr;
};

#endif // APOKOLIPS_OVERLAYS_H
