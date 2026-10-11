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

        const qreal t = genieTimelineProgress(
            m_frame, m_frames, m_reverse);

        auto smoothStep = [](qreal v) {
            v = qBound<qreal>(0.0, v, 1.0);
            return v * v * (3.0 - 2.0 * v);
        };
        auto lerp = [](qreal a, qreal b, qreal k) {
            return a + (b - a) * k;
        };

        const qreal leftGap = m_dock.x();
        const qreal rightGap = width() - m_dock.x();
        const qreal topGap = m_dock.y();
        const qreal bottomGap = height() - m_dock.y();
        const bool sideDock =
            qMin(leftGap, rightGap) < qMin(topGap, bottomGap);
        const bool positive = sideDock
            ? (m_dock.x() >= width() * 0.5)
            : (m_dock.y() >= height() * 0.5);

        const qreal axisStart = sideDock ? m_start.x() : m_start.y();
        const qreal axisExtent = sideDock ? m_start.width() : m_start.height();
        const qreal crossStart = sideDock ? m_start.y() : m_start.x();
        const qreal crossExtent = sideDock ? m_start.height() : m_start.width();
        const qreal dockAxis = sideDock ? m_dock.x() : m_dock.y();
        const qreal dockCross = sideDock ? m_dock.y() : m_dock.x();
        const qreal sourceCrossCenter = crossStart + crossExtent * 0.5;

        const qreal fadeStart = qMin<qreal>(0.98, m_fadeFrom + 0.03);
        const qreal opacity = t <= fadeStart
            ? 1.0
            : 1.0 - (t - fadeStart) / (1.0 - fadeStart);
        p.setOpacity(qBound<qreal>(0.0, opacity, 1.0));

        constexpr qreal minSize = 0.12;

        for (int i = 0; i < m_strips; ++i) {
            const qreal f0 = qreal(i) / m_strips;
            const qreal f1 = qreal(i + 1) / m_strips;
            const qreal fm = (f0 + f1) * 0.5;

            const qreal order = positive ? fm : (1.0 - fm);
            const qreal delay = (1.0 - order) * m_stagger;
            const qreal localT = qBound<qreal>(
                0.0, (t - delay) / (1.0 - delay), 1.0);
            const qreal eased = smoothStep(localT);
            const qreal easedCross = std::pow(eased, m_curve);

            const qreal srcAxisCenter = axisStart + fm * axisExtent;
            const qreal srcAxisSize = (f1 - f0) * axisExtent;
            const qreal dstAxisCenter = lerp(srcAxisCenter, dockAxis, eased);
            const qreal dstAxisSize = lerp(srcAxisSize, minSize, eased);
            const qreal dstCrossCenter =
                lerp(sourceCrossCenter, dockCross, eased);
            const qreal dstCrossSize =
                lerp(crossExtent, minSize, easedCross);

            // Overlap must scale with strip size — a fixed 1px leaves
            // visible seams when bands are compressed near the dock.
            const qreal overlap = qMax<qreal>(2.5, dstAxisSize * 0.35);

            QRectF dst;
            if (sideDock) {
                dst = QRectF(
                    dstAxisCenter - (dstAxisSize + overlap) * 0.5,
                    dstCrossCenter - dstCrossSize * 0.5,
                    dstAxisSize + overlap,
                    dstCrossSize);
            } else {
                dst = QRectF(
                    dstCrossCenter - dstCrossSize * 0.5,
                    dstAxisCenter - (dstAxisSize + overlap) * 0.5,
                    dstCrossSize,
                    dstAxisSize + overlap);
            }

            QRectF src;
            if (sideDock) {
                src = QRectF(f0 * m_pix.width(), 0.0,
                             (f1 - f0) * m_pix.width(), m_pix.height());
            } else {
                src = QRectF(0.0, f0 * m_pix.height(),
                             m_pix.width(), (f1 - f0) * m_pix.height());
            }
            p.drawPixmap(dst, m_pix, src);
        }
    }

private:
    void tick()
    {
        if (m_frame < m_frames - 1) { ++m_frame; update(); return; }
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
    int m_strips = 140;
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

#endif // APOKOLIPS_OVERLAYS_H
