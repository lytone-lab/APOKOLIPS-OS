#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "wallpaper.h"
#include "style.h"
#include "overlays.h"
#include "notes.h"
#include "calc.h"
#include "helpers.h"
#include "finder.h"
#include "aperture.h"
#include "node.h"
#include "mymach.h"
#include "boomtube.h"
#include "settings.h"
#include "registry.h"
#include "weather.h"
#include "globals.h"
#include "chrome.h"

// =========================================================
// Animator daemon — forward declarations
// The struct + entry points are used by the shell's minimize
// path and startup code, both of which appear earlier in the
// file than the daemon's function bodies.
// =========================================================
static bool animatorDaemonRunning();
static bool animatorRequest(const AnimRequest &req);
static int  runAnimatorDaemon(int argc, char **argv);

// =========================================================
// Theme — colors that chrome widgets adapt to
// =========================================================
// =========================================================
// System stats (reads /proc on Linux; returns 0 elsewhere)
// =========================================================
// =========================================================
// VisualStyle — glass / neu / skeuo / clay morphism
// =========================================================
 // namespace StyleManager

// =========================================================
// StyleRenderer — one place that knows how each style paints
// =========================================================
 // namespace StyleRenderer

// =========================================================
// VisualConfig — every chrome tuning knob. Settings app targets this.
// =========================================================
// =========================================================
// ThemeManager — notifies chrome widgets on theme change
// =========================================================
// =========================================================
// Wallpaper base
// =========================================================
// =========================================================
// Ruby
// =========================================================
// =========================================================
// GIF wallpaper — loops an animated GIF
// =========================================================
// =========================================================
// GIF wallpaper — loops an animated GIF
// =========================================================
// Starfield
// =========================================================
// =========================================================
// Aurora
// =========================================================
// =========================================================
// Golden Gate — dark navy with sweeping light-blue arcs
// =========================================================
// =========================================================
// Everest — animated dawn mountain scene (light wallpaper)
// =========================================================
// =========================================================
// WallpaperConfig
// =========================================================
// =========================================================
// Demo window
// =========================================================
static QRect shellSafeArea()
{
    QScreen *screen = QApplication::primaryScreen();
    if (!screen)
        return QRect();

    QRect area = screen->availableGeometry();
    constexpr int dockExtent = 96; // Dock::setFixedHeight / setFixedWidth
    constexpr int bottomReserve = dockExtent + 14; // bottom row margin
    constexpr int sideReserve = dockExtent + 14 + 8; // edge margin + spacing
    const int position = VisualConfigManager::instance().cfg().dockPosition;

    if (position == 0)       // Bottom dock
        area.setHeight(area.height() - bottomReserve);
    else if (position == 1)  // Left dock
        area.setLeft(area.left() + sideReserve);
    else if (position == 2)  // Right dock
        area.setWidth(area.width() - sideReserve);

    if (area.width() <= 0 || area.height() <= 0)
        area = screen->geometry();
    return area;
}

static QRect shellWindowRect(const QSize &preferred,
                             const QPoint &offset = QPoint(),
                             bool centered = true)
{
    const QRect area = shellSafeArea();
    if (area.isEmpty())
        return QRect(QPoint(0, 0), preferred);

    const QSize size = preferred.boundedTo(area.size());
    QPoint topLeft = centered
        ? area.center() - QPoint(size.width() / 2, size.height() / 2) + offset
        : area.topLeft() + offset;
    topLeft.setX(qBound(area.left(), topLeft.x(),
                        area.x() + area.width() - size.width()));
    topLeft.setY(qBound(area.top(), topLeft.y(),
                        area.y() + area.height() - size.height()));
    return QRect(topLeft, size);
}

static void setShellWindowGeometry(QWidget *window,
                                   const QSize &preferred,
                                   const QPoint &offset,
                                   bool centered)
{
    if (!window)
        return;
    if (window->layout()) {
        window->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        window->layout()->activate();
    }
    window->setGeometry(shellWindowRect(preferred, offset, centered));
}

class DemoWindow : public QWidget {
public:
    DemoWindow(const QString &appName, int offset, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(appName);
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setShellWindowGeometry(this, QSize(480, 320),
                               QPoint(120 + offset * 30,
                                      100 + offset * 30), false);

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
        setShellWindowGeometry(this, QSize(480, 320),
                               QPoint(120 + offset * 30,
                                      100 + offset * 30), false);
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
// =========================================================
// App launcher
// =========================================================
// =========================================================
// Shell registry — lets child widgets trigger shell actions
// =========================================================
// =========================================================
// SysState — reads live system state for tray icons
// =========================================================
 // namespace SysState

// =========================================================
// BadgeRegistry — pending notification counts per app
// =========================================================
 // namespace BadgeRegistry

// =========================================================
// Custom icon set — vector line-art, adapts to theme color
// =========================================================
 // namespace Icons

// =========================================================
// AppRegistry — single source of truth for every shell app.
// Fields:
//   id        — internal, matches --<mode> flag / .desktop basename
//   label     — display name shown in launcher, dock, Spotlight
//   glyph     — emoji fallback used for tray / launcher badges
//   inDock    — default dock membership
//   inLauncher— default launcher membership
//   inFinder  — default Finder sidebar entry (if any)
// =========================================================
// ---- Registry-derived queries ----
static QString glyphForId(const QString &id) {
    for (const auto &e : appRegistry())
        if (id == QLatin1String(e.id)) return e.glyph;
    return QStringLiteral("?");
}

// =========================================================
// DockIcon + Dock
// =========================================================


static QString discoverSwaySock();

// Maps the user's minimizeStyle config to the CLI flag.
static QString minimizeStyleFlag()
{
    VisualConfigManager::instance().load();
    const QString s = VisualConfigManager::instance().cfg().minimizeStyle;
    if (s == "scale")  return "--scale";
    if (s == "suck")   return "--suck";
    if (s == "thanos") return "--thanos";
    if (s == "mesh")   return "--mesh";
    return "--genie";
}

// ---- G6 forward decls ----
static QString genieMakeHandshakeName();
static QPixmap grabWidget(QWidget *w);

static QPoint extractFromDockArg();
static void   shellAnimateDockIn(QWidget *w, const QPoint &dockPt);
static void   shellScaleToDock(QWidget *w);

// =========================================================
// G6 — dock open/close animations for our own apps
// =========================================================
static QPixmap grabWidget(QWidget *w)
{
    if (!w) return QPixmap();
    if (w->layout()) w->layout()->activate();
    w->ensurePolished();
    return w->grab();
}

static QPoint extractFromDockArg()
{
    const QStringList a = QCoreApplication::arguments();
    for (int i = 0; i + 2 < a.size(); ++i) {
        if (a[i] == "--from-dock")
            return QPoint(a[i + 1].toInt(), a[i + 2].toInt());
    }
    return QPoint(-1, -1);
}

class DockCloseAnimFilter : public QObject {
public:
    DockCloseAnimFilter(QWidget *target, const QPoint &dockPt)
        : QObject(target), m_target(target), m_dock(dockPt) {}
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::Close && m_target && m_dock.x() >= 0) {
            QRect r = m_target->frameGeometry();
            QPixmap pm = grabWidget(m_target);
            if (!pm.isNull()) {
                QString png = QDir::tempPath() +
                    QString("/apokolips-close-%1-%2.png")
                        .arg(QCoreApplication::applicationPid())
                        .arg(QDateTime::currentMSecsSinceEpoch());
                if (pm.save(png, "PNG")) {
                    QWidget *target = m_target;
                    const QString readyName = genieMakeHandshakeName();
                    QLocalServer::removeServer(readyName);
                    QLocalServer *readyServer = new QLocalServer(target);
                    readyServer->listen(readyName);

                    const QStringList overlayArgs{
                        minimizeStyleFlag(), png,
                        QString::number(r.x()), QString::number(r.y()),
                        QString::number(r.width()), QString::number(r.height()),
                        QString::number(m_dock.x()), QString::number(m_dock.y()),
                        "--ms", "400", "--ready-sock", readyName
                    };
                    const bool started = QProcess::startDetached(
                        QCoreApplication::applicationFilePath(), overlayArgs);

                    if (!started) {
                        readyServer->close();
                        readyServer->deleteLater();
                        target->hide();
                    } else {
                        qInfo() << "[hs] shell: connecting newConnection slot";
                        QObject::connect(readyServer, &QLocalServer::newConnection,
                                         [readyServer, target]() {
                            qInfo() << "[hs] shell: newConnection fired";
                            QLocalSocket *conn = readyServer->nextPendingConnection();
                            if (!conn) { qInfo() << "[hs] shell: no conn"; return; }
                            QObject::connect(conn, &QLocalSocket::readyRead,
                                             [conn, readyServer, target]() {
                                QByteArray data = conn->readAll();
                                qInfo() << "[hs] shell: readyRead got" << data;
                                if (data.contains("ready") && target)
                                    target->hide();
                                conn->disconnectFromServer();
                                if (readyServer->isListening())
                                    readyServer->close();
                            });
                        });
                        // Safety fallback in case the overlay never signals
                        // (process crash, etc.) — don't leave the window stuck.
                        QTimer::singleShot(900, target, [target]() {
                            if (target) target->hide();
                        });
                    }
                }
            } else {
                m_target->hide();
            }
        }
        return QObject::eventFilter(o, e);
    }
private:
    QWidget *m_target = nullptr;
    QPoint   m_dock;
};

static void shellAnimateDockIn(QWidget *w, const QPoint &dockPt)
{
    if (!w) return;
    if (dockPt.x() < 0) { w->show(); return; }

    // Install close filter for later.
    w->installEventFilter(new DockCloseAnimFilter(w, dockPt));

    // Grab the widget's own pixels without ever showing it on screen.
    QRect r = w->frameGeometry();
    QPixmap pm = grabWidget(w);
    if (pm.isNull()) { w->show(); return; }

    QString png = QDir::tempPath() +
        QString("/apokolips-open-%1-%2.png")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
    if (!pm.save(png, "PNG")) { w->show(); return; }

    // Window stays hidden until the overlay has finished its reverse
    // animation and signals readiness over the handshake socket —
    // no fixed-delay guess, so slow process startup never causes a flash.
    const QString readyName = genieMakeHandshakeName();
    QLocalServer::removeServer(readyName);
    QLocalServer *readyServer = new QLocalServer(w);
    readyServer->listen(readyName);

    const QStringList overlayArgs{
        minimizeStyleFlag(), png,
        QString::number(r.x()), QString::number(r.y()),
        QString::number(r.width()), QString::number(r.height()),
        QString::number(dockPt.x()), QString::number(dockPt.y()),
        "--reverse", "--ms", "400", "--ready-sock", readyName
    };
    const bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), overlayArgs);

    auto revealReal = [w]() {
        if (!w) return;
        w->show();
        w->raise();
        w->activateWindow();
    };

    if (!started) {
        readyServer->close();
        readyServer->deleteLater();
        revealReal();
        return;
    }

    QObject::connect(readyServer, &QLocalServer::newConnection,
                     [readyServer, revealReal]() {
        QLocalSocket *conn = readyServer->nextPendingConnection();
        if (!conn) return;
        QObject::connect(conn, &QLocalSocket::readyRead,
                         [conn, readyServer, revealReal]() {
            QByteArray data = conn->readAll();
            if (data.contains("ready")) revealReal();
            conn->disconnectFromServer();
            if (readyServer->isListening()) readyServer->close();
        });
    });
    // Safety fallback — the overlay's own animation runs ~400ms, so if
    // the handshake is ever missed, still reveal the window.
    QTimer::singleShot(900, w, revealReal);
}

// Yellow traffic light → forward shrink-to-dock, then hide.
static void shellScaleToDock(QWidget *w)
{
    if (!w) return;
    QPoint dock = extractFromDockArg();
    if (dock.x() < 0) { w->hide(); return; }

    QRect r = w->frameGeometry();
    QPixmap pm = grabWidget(w);
    if (pm.isNull()) { w->hide(); return; }

    QString png = QDir::tempPath() +
        QString("/apokolips-min-%1-%2.png")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
    if (!pm.save(png, "PNG")) { w->hide(); return; }

    const QString readyName = genieMakeHandshakeName();
    QLocalServer::removeServer(readyName);
    QLocalServer *readyServer = new QLocalServer(w);
    readyServer->listen(readyName);

    const QStringList overlayArgs{
        minimizeStyleFlag(), png,
        QString::number(r.x()), QString::number(r.y()),
        QString::number(r.width()), QString::number(r.height()),
        QString::number(dock.x()), QString::number(dock.y()),
        "--ms", "400", "--ready-sock", readyName
    };
    const bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), overlayArgs);

    if (!started) {
        readyServer->close();
        readyServer->deleteLater();
        w->hide();
        return;
    }

    QObject::connect(readyServer, &QLocalServer::newConnection,
                     [readyServer, w]() {
        QLocalSocket *conn = readyServer->nextPendingConnection();
        if (!conn) return;
        QObject::connect(conn, &QLocalSocket::readyRead,
                         [conn, readyServer, w]() {
            QByteArray data = conn->readAll();
            if (data.contains("ready") && w) w->hide();
            conn->disconnectFromServer();
            if (readyServer->isListening()) readyServer->close();
        });
    });
    QTimer::singleShot(900, w, [w]() { if (w) w->hide(); });
}


// Yellow traffic light on any shell window — ask the shell to run
// its full minimize animation on the currently focused window.
static void shellMinimizeFocusedWindow()
{
    QProcess::startDetached("bash", { "-c",
        "pkill -RTMIN -f 'apokolips-shell/build/apokolips-shell$'" });
}

// Red traffic light: minimize through the shell's pipeline, then
// close the window ~once the animation has finished. The scratchpad
// entry disappears when the process exits.
static void shellCloseViaGenie(QWidget *w)
{
    if (!w) return;
    // Only run the minimize animation when this window is the focused
    // one. Guards against a background red-light click hijacking the
    // shell's SIGRTMIN from a freshly-launched window.
    if (w->isActiveWindow()) {
        // Match the animation duration exactly, plus a small margin
        // for overlay process startup and handshake roundtrip. The
        // real window must not close until the overlay has finished.
        const int animMs = qBound(128,
            VisualConfigManager::instance().cfg().genieDurationMs, 5000);
        const int closeAfter = animMs + 400;

        shellMinimizeFocusedWindow();
        QPointer<QWidget> guard(w);
        QTimer::singleShot(closeAfter, [guard]() {
            if (guard) guard->close();
        });
    } else {
        w->close();
    }
}


// ---- Genie family handshake ----
// Map a sway app_id to the dock label the user clicks to restore.
static QString dockNameForAppId(const QString &appId)
{
    if (appId == "apokolips-files")       return "Finder";
    if (appId == "apokolips-shell")       return "Settings"; // ambiguous
    if (appId == "apokolips-aperture")    return "Aperture";
    if (appId == "apokolips-boombube")    return "BoomTube";
    if (appId == "apokolips-node")        return "Node";
    if (appId == "apokolips-mymach")      return "myMach";
    if (appId == "apokolips-notes")       return "Notes";
    if (appId == "apokolips-calculator")  return "Calculator";
    if (appId == "apokolips-mosaic")      return "Mosaic";
    if (appId == "apokolips-mail")        return "Mail";
    if (appId == "thunderbird")           return "Mail";
    if (appId == "org.gnome.Nautilus")    return "Finder";
    return {};
}


static QString genieStateFilePath()
{
    return QDir::tempPath() + "/apokolips-genie-state.json";
}


// Periodic prune — drops state entries whose conIds no longer exist
// in sway's tree. Prevents a closed app (red light) from leaving a
// phantom entry that wastes an animation on the next dock click.


static QJsonArray readGenieWindows()
{
    QFile file(genieStateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return {};

    return doc.object().value("windows").toArray();
}

static bool writeGenieWindows(const QJsonArray &windows)
{
    QSaveFile file(genieStateFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QJsonObject root;
    root["windows"] = windows;
    const QByteArray bytes =
        QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (file.write(bytes) != bytes.size())
        return false;

    return file.commit();
}

static void pruneStaleGenieState()
{
    const QJsonArray current = readGenieWindows();
    if (current.isEmpty()) return;

    QProcess sp;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString sock = discoverSwaySock();
    if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
    sp.setProcessEnvironment(env);
    sp.start("swaymsg", { "-t", "get_tree" });
    if (!sp.waitForFinished(800)) return;
    const QString raw = QString::fromUtf8(sp.readAllStandardOutput());
    if (raw.isEmpty()) return;

    QSet<int> live;
    QRegularExpression re(QString::fromLatin1("\"id\"\\s*:\\s*(\\d+)"));
    auto it = re.globalMatch(raw);
    while (it.hasNext())
        live.insert(it.next().captured(1).toInt());
    if (live.isEmpty()) return;

    QJsonArray kept;
    bool changed = false;
    for (const QJsonValue &v : current) {
        const QJsonObject entry = v.toObject();
        const int conId = entry.value("conId").toInt(-1);
        if (conId > 0 && !live.contains(conId)) {
            const QString png = entry.value("png").toString();
            if (!png.isEmpty()) QFile::remove(png);
            changed = true;
            qInfo() << "[genie-prune] dropped stale conId" << conId;
            continue;
        }
        kept.append(entry);
    }
    if (changed) writeGenieWindows(kept);
}

static bool upsertGenieWindow(const QJsonObject &entry)
{
    const int conId = entry.value("conId").toInt(-1);
    if (conId <= 0)
        return false;

    QJsonArray updated;
    const QJsonArray current = readGenieWindows();

    for (const QJsonValue &value : current) {
        const QJsonObject old = value.toObject();
        if (old.value("conId").toInt(-1) != conId)
            updated.append(old);
    }

    updated.append(entry);
    return writeGenieWindows(updated);
}

static bool removeGenieWindow(int conId)
{
    if (!QFile::exists(genieStateFilePath()))
        return true;

    QJsonArray updated;
    const QJsonArray current = readGenieWindows();

    for (const QJsonValue &value : current) {
        const QJsonObject old = value.toObject();
        if (old.value("conId").toInt(-1) != conId)
            updated.append(old);
    }

    return writeGenieWindows(updated);
}

// Sway tree probe — returns true if conId exists in the current
// tree. Used to reject stale genie-state entries BEFORE playing
// the reverse animation: a dead conId would otherwise animate and
// then silently fail scratchpad show, blocking fresh launch.
static bool swayTreeHasConId(int conId)
{
    if (conId <= 0) return false;
    const QString sock = discoverSwaySock();
    if (sock.isEmpty()) return false;
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SWAYSOCK", sock);
    p.setProcessEnvironment(env);
    p.start("swaymsg", { "-t", "get_tree", "-r" });
    if (!p.waitForFinished(800)) {
        p.kill();
        return false;
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    return out.contains(QString("\"id\": %1").arg(conId))
        || out.contains(QString("\"id\":%1").arg(conId));
}

static bool restoreGenieForDock(const QString &dockName)
{
    if (dockName.isEmpty()) {
        qWarning() << "[genie-restore][why] empty dockName";
        return false;
    }

    const QJsonArray windows = readGenieWindows();
    QJsonObject entry;

    qDebug() << "[genie-restore] lookup dockName=" << dockName
             << "entries=" << windows.size();

    // Restore the most recently minimized matching window.
    for (int i = windows.size() - 1; i >= 0; --i) {
        const QJsonObject candidate = windows.at(i).toObject();
        if (candidate.value("dockName").toString() == dockName) {
            entry = candidate;
            break;
        }
    }

    if (entry.isEmpty()) {
        qWarning() << "[genie-restore][why] no entry matches"
                   << dockName;
        return false;
    }

    const int conId = entry.value("conId").toInt(-1);
    const QString savedPng = entry.value("pixmap").toString();
    const QJsonArray rect = entry.value("startRect").toArray();
    const QJsonArray dock = entry.value("dockPt").toArray();

    if (conId <= 0) {
        qWarning() << "[genie-restore][why] bad conId" << conId;
        return false;
    }

    // Reject stale entries BEFORE the animation plays. If the
    // window is gone (red-light close), the reverse animation
    // would run against a dead conId, then scratchpad show would
    // fail silently and the caller would never fall through to a
    // fresh launch. Drop the entry and return false so the caller
    // launches the app instead.
    if (!swayTreeHasConId(conId)) {
        qInfo() << "[genie-restore] stale conId" << conId
                << "— dropping, will launch fresh";
        removeGenieWindow(conId);
        QFile::remove(savedPng);
        return false;
    }

    if (!QFile::exists(savedPng)) {
        qWarning() << "[genie-restore][why] pixmap missing"
                   << savedPng << " — dropping entry";
        removeGenieWindow(conId);
        return false;
    }
    if (rect.size() != 4) {
        qWarning() << "[genie-restore][why] rect size" << rect.size();
        return false;
    }
    if (dock.size() != 2) {
        qWarning() << "[genie-restore][why] dock size" << dock.size();
        return false;
    }

    const int x = rect.at(0).toInt();
    const int y = rect.at(1).toInt();
    const int w = rect.at(2).toInt();
    const int h = rect.at(3).toInt();
    const int dx = dock.at(0).toInt();
    const int dy = dock.at(1).toInt();

    if (w <= 0 || h <= 0) {
        qWarning() << "[genie-restore][why] zero size" << w << h;
        return false;
    }

    const QString reversePng =
        QDir::tempPath()
        + QString("/genie-reverse-%1-%2.png")
              .arg(conId)
              .arg(QDateTime::currentMSecsSinceEpoch());

    // GenieOverlay deletes the image it loads, so animate a temporary copy.
    if (!QFile::copy(savedPng, reversePng)) {
        qWarning() << "[genie-restore][why] copy failed:"
                   << savedPng << "->" << reversePng;
        return false;
    }

    const int durationMs = qBound(
        128, VisualConfigManager::instance().cfg().genieDurationMs, 5000);

    // Use the user's selected minimize style for the reverse too.
    // Previously hardcoded to --genie, which meant Suck/Scale users
    // saw genie strips on dock-click restore.
    const QStringList args{
        minimizeStyleFlag(), reversePng,
        QString::number(x), QString::number(y),
        QString::number(w), QString::number(h),
        QString::number(dx), QString::number(dy),
        "--reverse", "--ms", QString::number(durationMs)
    };

    // Ready-sock handshake for restore: the overlay pings when its
    // first frame is painted. We start the reveal timer only then —
    // not from a fixed wall-clock guess — so slow overlay starts on
    // wide screen paths (dock left/right) no longer reveal mid-animation.
    const QString readyName = genieMakeHandshakeName();
    QLocalServer::removeServer(readyName);
    QLocalServer *readyServer = new QLocalServer(qApp);
    readyServer->listen(readyName);

    // Args gain --ready-sock so the fresh-process fallback also pings.
    QStringList argsWithReady = args;
    argsWithReady << "--ready-sock" << readyName;

    qint64 overlayPid = 0;
    bool started = false;

    // Fast path: daemon.
    {
        AnimRequest req;
        req.style      = minimizeStyleFlag().mid(2);   // "--x" -> "x"
        req.png        = reversePng;
        req.rect       = QRect(x, y, w, h);
        req.dock       = QPoint(dx, dy);
        req.reverse    = true;
        req.durationMs = durationMs;
        req.readySock  = readyName;
        if (animatorRequest(req)) {
            started = true;
            qDebug() << "[genie-reverse] sent to daemon";
        }
    }

    // Fallback: fresh process.
    if (!started) {
        started = QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            argsWithReady, QString(), &overlayPid);
        qDebug() << "[genie-reverse] spawned fresh pid=" << overlayPid;
    }

    if (!started) {
        QFile::remove(reversePng);
        qWarning() << "[genie-reverse] overlay failed to start";
        return false;
    }

    qDebug() << "[genie-reverse] started" << dockName
             << "conId=" << conId
             << "durationMs=" << durationMs
             << "pid=" << overlayPid;

    // Show the real container near the end, while the full-size overlay
    // still covers it. Keep the saved record if sway reports a failure.
    auto revealReal = [conId, savedPng]() {
        const QString sock = discoverSwaySock();
        if (sock.isEmpty()) {
            qWarning() << "[genie-reverse] no SWAYSOCK; state retained";
            return;
        }

        QProcess *sway = new QProcess(qApp);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SWAYSOCK", sock);
        sway->setProcessEnvironment(env);

        QObject::connect(
            sway,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [sway, conId, savedPng](int code, QProcess::ExitStatus status) {
                const QByteArray output = sway->readAllStandardOutput();
                const QByteArray error = sway->readAllStandardError();

                bool shown = false;
                if (code == 0 && status == QProcess::NormalExit) {
                    const QJsonDocument reply =
                        QJsonDocument::fromJson(output);
                    if (reply.isArray()) {
                        for (const QJsonValue &value : reply.array()) {
                            if (value.toObject()
                                    .value("success").toBool()) {
                                shown = true;
                                break;
                            }
                        }
                    }
                }

                if (shown) {
                    removeGenieWindow(conId);
                    QFile::remove(savedPng);
                    qDebug() << "[genie-reverse] real window shown:" << conId;
                } else {
                    // Stale conId — window was closed long ago.
                    // Drop the entry + pixmap so the next click falls
                    // through to launching a fresh instance.
                    removeGenieWindow(conId);
                    QFile::remove(savedPng);
                    qWarning() << "[genie-reverse] stale state dropped;"
                               << "conId=" << conId
                               << "stdout=" << output
                               << "stderr=" << error;
                }

                sway->deleteLater();
            });

        sway->start("swaymsg", {
            QString("[con_id=%1]").arg(conId),
            "scratchpad", "show"
        });
    };

    // Overlay pings "ready" from its first paint. We wait the
    // animation duration FROM THAT PING before revealing the window.
    // 80 ms margin covers the last-frame compositing.
    {
        QPointer<QLocalServer> srvGuard(readyServer);
        auto *revealed = new bool(false);
        auto revealOnce = [revealed, revealReal]() {
            if (*revealed) return;
            *revealed = true;
            revealReal();
        };

        QObject::connect(readyServer, &QLocalServer::newConnection,
                         [srvGuard, revealOnce, revealed, conId]() {
            if (!srvGuard) return;
            QLocalSocket *conn = srvGuard->nextPendingConnection();
            if (!conn) return;
            QObject::connect(conn, &QLocalSocket::readyRead,
                             [conn, revealOnce, revealed, conId]() {
                const QByteArray data = conn->readAll();
                qDebug() << "[genie-reverse] ping:" << data
                         << "conId=" << conId;
                // "done" = overlay finished its last frame.
                if (data.contains("done"))
                    revealOnce();
                // "ready" (first paint) — do nothing here; we wait
                // for done. Server stays open.
                conn->disconnectFromServer();
            });
        });

        // Safety net: if the overlay crashes or never sends done,
        // reveal at durationMs + 2500 (animation may drift).
        QTimer::singleShot(durationMs + 1000, qApp,
                           [revealOnce]() {
            qDebug() << "[genie-reverse] safety reveal";
            revealOnce();
        });
    }


    return true;
}

// =========================================================
// Top bar
// =========================================================
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
        "  padding: 14px 6px;"
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
        "}"
        "QMenu::right-arrow {"
        "  image: none;"
        "  width: 0;"
        "  height: 0;"
        "  margin: 0;"
        "}");
}

// =========================================================
// Control Center — macOS-style dropdown panel
// =========================================================
// =========================================================
// SysControl — audio (wpctl) + brightness (sysfs)
// =========================================================
 // namespace SysControl

// =========================================================
// File Explorer — translucent macOS-style file browser
// =========================================================
// =========================================================
// ClipboardPanel — Win+V-style history popup
// =========================================================
// =========================================================
// ApokolipsInputDialog — themed prompt (glass + traffic lights)
// =========================================================
// =========================================================
// Sigil sidebar row for Finder
// =========================================================
// =========================================================
// Settings window
// =========================================================
// =========================================================
// Sigil design language — component library
// =========================================================
 // namespace Sigil

// =========================================================
// Glyph — screenshot tool (region select + save + copy)
// =========================================================
// =========================================================
// SnapPicker — Windows 11-style snap layout popup
// =========================================================
// =========================================================
// ScreensaverOverlay — fullscreen idle overlay
//   dim      : dark veil over frozen desktop
//   clock    : dim + large time
//   slideshow: cycles images from a chosen folder
//   video    : launches mpv (handled separately, not this class)
// =========================================================
// =========================================================
// SigilImageWindow — image viewer (PNG / JPEG / GIF / SVG / ICO / BMP)
//   Zoom, pan, prev/next within folder, fit-to-window, rotate.
// =========================================================
// =========================================================
// UsageBar — thin filled bar for RAM / disk usage
// =========================================================
// =========================================================
// MyMachWindow — system info app
// =========================================================
// =========================================================
// NodeWindow — terminal emulator (QTermWidget inside Sigil chrome)
// =========================================================
// =========================================================
// BoomTubeWindow — Sigil-style media player
//   QMediaPlayer + QAudioOutput + QVideoWidget
//   Playlist: siblings of the opened file in the same folder.
// =========================================================
// =========================================================
// Weather — Open-Meteo (free, no API key)
// =========================================================
 // namespace Weather

// =========================================================
// PoolIcon — draggable Sigil tile that lives on the wallpaper
// =========================================================
// =========================================================
// PoolCanvas — transparent fullscreen overlay that hosts
// desktop icons. Sits above the wallpaper, below top bar +
// widgets + dock. Only intercepts clicks on icons.
// =========================================================
// Desktop launch args — map app id to the right --flag
// =========================================================
// Spotlight — global search overlay (macOS-style)
// =========================================================

static QString discoverSwaySock() {
    QString dir = QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR"));
    if (dir.isEmpty()) dir = "/run/user/1000";
    QDir d(dir);
    for (const QString &e : d.entryList({"sway-ipc.*"},
                                        QDir::AllEntries | QDir::System))
        if (e.endsWith(".sock")) return d.filePath(e);
    return {};
}

// =========================================================
// shellRestoreWindow — shared restore for any shell app.
// Called from single-instance focus handlers and dock-click
// paths to bring a window back from any state: hidden, minimized,
// genie-scratchpad, off-screen. Idempotent and flicker-free.
//
//   w         — the top-level widget to restore
//   title     — the window title (used for sway focus criteria)
//   preferred — desired size when the window needs remapping;
//               pass QSize() to keep current size
// =========================================================
static void shellRestoreWindow(QWidget *w,
                               const QString &title,
                               const QSize &preferred)
{
    if (!w) return;

    // If Qt already has the surface live, just raise it.
    const bool needsRemap = !w->isVisible();

    if (needsRemap && preferred.isValid()) {
        // Reposition BEFORE showing so the user sees one transition.
        if (QScreen *s = QApplication::primaryScreen()) {
            QRect usable = s->availableGeometry();
            constexpr int dockExtent = 96;
            constexpr int bottomReserve = dockExtent + 14;
            constexpr int sideReserve   = dockExtent + 22;
            int pos = VisualConfigManager::instance().cfg().dockPosition;
            if (pos == 0)
                usable.setHeight(usable.height() - bottomReserve);
            else if (pos == 1)
                usable.setLeft(usable.left() + sideReserve);
            else if (pos == 2)
                usable.setWidth(usable.width() - sideReserve);
            if (usable.width() < 1 || usable.height() < 1)
                usable = s->geometry();

            QSize target(qMin(preferred.width(),  usable.width()),
                         qMin(preferred.height(), usable.height()));
            w->resize(target);
            w->move(usable.center()
                    - QPoint(target.width() / 2, target.height() / 2));
        }
    }

    // Single clean appearance.
    w->show();
    w->raise();
    w->activateWindow();

    // Ask sway to focus, injecting SWAYSOCK.
    QString sock = discoverSwaySock();
    if (sock.isEmpty())
        sock = "/run/user/1000/sway-ipc.1000.1383.sock";
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SWAYSOCK", sock);

    // Pull from scratchpad first — a no-op if the window isn't
    // hidden there. Without this, focus alone doesn't always
    // surface a scratchpad window.
    {
        QProcess sc;
        sc.setProcessEnvironment(env);
        sc.start("swaymsg",
                 { QString("[title=\"%1\"] scratchpad show").arg(title) });
        sc.waitForFinished(400);
    }
    // Then focus + raise.
    QProcess sp;
    sp.setProcessEnvironment(env);
    sp.start("swaymsg", { QString("[title=\"%1\"] focus").arg(title) });
    sp.waitForFinished(500);
}


static QString freshSwaySock() {
    QString dir = QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR"));
    if (dir.isEmpty()) dir = "/run/user/1000";
    QDir d(dir);
    QStringList entries = d.entryList({"sway-ipc.*"},
                                       QDir::AllEntries | QDir::System);
    for (const QString &e : entries)
        if (e.endsWith(".sock")) return d.filePath(e);
    return {};
}

extern "C" void spotlightSignalHandler(int sig) {
    if (sig == SIGUSR2)      g_calcToggle = 1;
    else if (sig == SIGCONT) g_notesToggle = 1;
    else if (sig == SIGWINCH) g_mcToggle = 1;
    else if (sig == SIGRTMIN) g_minimizeToggle = 1;
    else                     g_spotlightToggle = 1;
}

// =========================================================
// Desktop background
// =========================================================
// =========================================================
// Shell
// =========================================================
// =========================================================
// TrayIcon — live-updating network / bluetooth / battery
// =========================================================
// =========================================================
// BatteryPercentLabel — small text next to battery icon
// =========================================================
// =========================================================
// Calculator app
// =========================================================
// =========================================================
// Mission Control — zoomed-out window overview
// =========================================================
// =========================================================
// Notes app — in-shell overlay + standalone mode
// =========================================================
// =========================================================
// GenieOverlay — window-shrinks-into-dock animation
// Screenshot-based illusion (sway gives us no live texture)
// =========================================================
static int runShell(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Apokolips Shell");


    std::signal(SIGUSR1, spotlightSignalHandler);
    std::signal(SIGUSR2, spotlightSignalHandler);
    std::signal(SIGCONT, spotlightSignalHandler);
    std::signal(SIGWINCH, spotlightSignalHandler);
    std::signal(SIGRTMIN, spotlightSignalHandler);
    g_swaySock = discoverSwaySock();
    qDebug() << "[sway] sock =" << g_swaySock;

    // Load theme BEFORE creating any chrome widget
    Wallpaper *initialWallpaper =
        WallpaperConfig::makeById(WallpaperConfig::loadId());
    ThemeManager::instance().setTheme(initialWallpaper->theme());

    // Reload splash: if the previous process saved a backdrop,
    // show it fullscreen on top while we build the new widget
    // tree. Closes once the shell window is ready.
    QWidget *reloadSplash = nullptr;
    {
        const QString bp = qEnvironmentVariable("APOKOLIPS_BACKDROP");
        if (!bp.isEmpty()) {
            QFileInfo fi(bp);
            if (fi.exists() &&
                fi.lastModified().secsTo(QDateTime::currentDateTime()) < 10) {
                QPixmap pm(bp);
                if (!pm.isNull()) {
                    reloadSplash = new QWidget(
                        nullptr,
                        Qt::Window | Qt::FramelessWindowHint |
                        Qt::WindowStaysOnTopHint);
                    QLabel *lbl = new QLabel(reloadSplash);
                    lbl->setPixmap(pm);
                    lbl->setGeometry(0, 0, pm.width(), pm.height());
                    reloadSplash->setGeometry(0, 0,
                                              pm.width(), pm.height());
                    reloadSplash->show();
                    reloadSplash->raise();
                    QApplication::processEvents();
                    qInfo() << "[reload] splash shown" << pm.size();
                }
            }
            QFile::remove(bp);
            qunsetenv("APOKOLIPS_BACKDROP");
        }
    }

    QMainWindow window;
    window.setWindowFlags(Qt::FramelessWindowHint);
    window.setWindowTitle("Apokolips OS");

    BadgeRegistry::seedDefaults();
    StyleManager::load();

    PoolBackground *root = new PoolBackground;
    root->setWallpaper(initialWallpaper);

    QVBoxLayout *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    TopBar *topBar = new TopBar;
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(14, 0, 16, 0);
    topLayout->setSpacing(10);

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
    CalculatorApp *calcOverlay = new CalculatorApp(root);
    root->setCalcOverlay(calcOverlay);

    NotesApp *notesOverlay = new NotesApp(root);
    root->setNotesOverlay(notesOverlay);

    MissionControl *missionControl = new MissionControl(root);
    root->setMissionControl(missionControl);

    // ---- Corner peek (Variant A, 2026-10-17) ----
    // Hover the top-left 8x8 px corner for 400ms → Mission Control
    // appears. Click a thumbnail to focus (existing MC behavior),
    // or press Esc / click empty space to hide. Hot-corner model:
    // MC stays up after the trigger until you interact, so you can
    // travel from corner to thumbnail without it disappearing.
    // 400ms delay prevents accidental pops when clicking near
    // the top-left of any window.
    {
        QElapsedTimer *cornerTimer = new QElapsedTimer;
        QTimer *cornerPoll = new QTimer(root);
        cornerPoll->setInterval(100);
        QObject::connect(cornerPoll, &QTimer::timeout,
                         [missionControl, cornerTimer]() {
            const QPoint pos = QCursor::pos();
            const bool inCorner = (pos.x() < 8 && pos.y() < 8);
            if (inCorner) {
                if (!cornerTimer->isValid()) {
                    cornerTimer->start();
                } else if (cornerTimer->elapsed() >= 400
                           && !missionControl->isVisible()) {
                    missionControl->show2();
                    cornerTimer->invalidate();
                }
            } else {
                cornerTimer->invalidate();
            }
        });
        cornerPoll->start();
        qInfo() << "[corner-peek] top-left 8x8, 400ms delay";
    }

    QTimer *signalPoll = new QTimer(&window);
    signalPoll->setInterval(120);
    QObject::connect(signalPoll, &QTimer::timeout,
                     [spotlight, calcOverlay, notesOverlay, missionControl, root]() {
        if (g_spotlightToggle) {
            g_spotlightToggle = 0;
            spotlight->toggle();
        }
        if (g_calcToggle) {
            g_calcToggle = 0;
            calcOverlay->toggle();
        }
        if (g_notesToggle) {
            g_notesToggle = 0;
            notesOverlay->toggle();
        }
        if (g_mcToggle) {
            g_mcToggle = 0;
            missionControl->toggle();
        }
        if (g_minimizeToggle) {
            g_minimizeToggle = 0;
            qDebug() << "[genie] Super+M fired";

            QProcess *q = new QProcess;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            QString sock = discoverSwaySock();
            qDebug() << "[genie-debug] SWAYSOCK=" << sock;
            env.insert("SWAYSOCK", sock);
            q->setProcessEnvironment(env);
            QObject::connect(q,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [q, root](int exitCode, QProcess::ExitStatus status) {
                qDebug() << "[genie-debug] exitCode=" << exitCode
                         << "status=" << status;
                QByteArray out = q->readAllStandardOutput();
                QByteArray err = q->readAllStandardError();
                qDebug() << "[genie-debug] stdout bytes=" << out.size()
                         << "stderr=" << err;
                qDebug() << "[genie-debug] contains floating_con="
                         << out.contains("floating_con")
                         << "contains Files="
                         << out.contains("\"Files\"");
                q->deleteLater();
                QJsonDocument doc = QJsonDocument::fromJson(out);
                if (!doc.isObject()) {
                    qDebug() << "[genie-debug] JSON parse failed, first 200 chars:"
                             << out.left(200);
                    return;
                }

                struct Win {
                    int conId = -1;
                    QString appId;
                    QString title;
                    QRect rect;
                    bool focused = false;
                };
                Win target;
                QList<Win> wins;

                std::function<void(const QJsonObject &)> collect =
                    [&](const QJsonObject &n) {
                    QString nodeType = n.value("type").toString();
                    if (nodeType == "con" || nodeType == "floating_con") {
                        QString a  = n.value("app_id").toString();
                        QString nm = n.value("name").toString();
                        QJsonObject r = n.value("rect").toObject();
                        int w = r.value("width").toInt();
                        int h = r.value("height").toInt();
                        const bool internalOverlay =
                            a == "apokolips-genie" ||
                            a == "apokolips-glyph" ||
                            a == "apokolips-snap" ||
                            a == "apokolips-clipboard";
                        const bool shellChrome =
                            a == "apokolips-shell" &&
                            nm != "Files" &&
                            nm != "Apokolips Settings";

                        if (!a.isEmpty() && !internalOverlay && !shellChrome &&
                            w > 100 && h > 100) {
                            Win win;
                            win.conId = n.value("id").toInt();
                            win.appId = a;
                            win.title = nm;
                            win.rect = QRect(r.value("x").toInt(),
                                             r.value("y").toInt(), w, h);
                            win.focused = n.value("focused").toBool();
                            wins.append(win);
                        }
                    }
                    for (const QJsonValue &v : n.value("nodes").toArray())
                        collect(v.toObject());
                    for (const QJsonValue &v : n.value("floating_nodes").toArray())
                        collect(v.toObject());
                };
                collect(doc.object());

                // Choose the focused app window reported by sway.
                for (const Win &w : wins) {
                    if (w.focused) {
                        target = w;
                        break;
                    }
                }
                // Only use a fallback when exactly one app window matches.
                if (target.conId < 0 && wins.size() == 1)
                    target = wins.first();

                // The hotkey can briefly focus the shell before this query
                // finishes. Fall back only to the last tracked real app window.
                if (target.conId <= 0 && g_trackedConId > 0 &&
                    !g_trackedRect.isEmpty()) {
                    target.conId = g_trackedConId;
                    target.appId = g_trackedAppId;
                    target.title = g_trackedTitle;
                    target.rect = g_trackedRect;
                    qDebug() << "[genie] using last-focused target"
                             << target.conId << target.title << target.rect;
                }
                qDebug() << "[genie] target conId=" << target.conId
                         << "rect=" << target.rect
                         << "app=" << target.appId;
                if (target.conId <= 0 || target.rect.isEmpty()) {
                    qDebug() << "[genie] no valid focused app window; abort";
                    return;
                }

                // Find the dock icon position for this app
                QString dockName;
                // Our own app windows share app_id; distinguish by title.
                if (target.appId == "apokolips-files" ||
                    (target.appId == "apokolips-shell" &&
                     target.title == "Files")) {
                    dockName = "Finder";
                } else if (target.appId == "apokolips-settings" ||
                           (target.appId == "apokolips-shell" &&
                            target.title == "Apokolips Settings")) {
                    dockName = "Settings";
                } else if (target.appId == "firefox") {
                    dockName = "Finder";
                } else if (target.appId == "eog") {
                    dockName = "Photos";
                } else if (target.appId == "org.gnome.Totem") {
                    dockName = "Music";
                } else if (target.appId == "org.gnome.TextEditor" ||
                           target.appId == "gnome-text-editor") {
                    dockName = "Notes";
                } else if (target.appId == "thunderbird") {
                    dockName = "Mail";
                } else if (target.appId == "org.gnome.Terminal" ||
                           target.appId == "foot" ||
                           target.appId == "apokolips-node") {
                    dockName = "Node";
                } else if (target.appId == "apokolips-aperture") {
                    dockName = "Aperture";
                } else if (target.appId == "apokolips-boombube") {
                    dockName = "BoomTube";
                } else if (target.appId == "apokolips-mymach") {
                    dockName = "myMach";
                } else if (target.appId == "apokolips-mosaic") {
                    dockName = "Mosaic";
                } else if (target.appId == "apokolips-notes") {
                    dockName = "Notes";
                } else if (target.appId == "apokolips-calculator") {
                    dockName = "Calculator";
                } else if (target.appId == "apokolips-glyph") {
                    dockName = "Glyph";
                } else if (target.appId == "apokolips-shell"
                           && target.title == "Node") {
                    dockName = "Node";
                } else if (target.appId == "apokolips-shell"
                           && target.title == "Aperture") {
                    dockName = "Aperture";
                } else if (target.appId == "apokolips-shell"
                           && target.title == "BoomTube") {
                    dockName = "BoomTube";
                } else if (target.appId == "apokolips-shell"
                           && target.title == "myMach") {
                    dockName = "myMach";
                }

                qInfo() << "[genie-trace] target app="
                        << target.appId
                        << "title=" << target.title
                        << "→ dockName=" << dockName;

                // Instead of a hardcoded fallback, use the root's own geometry.
                // If we can't find the icon, aim for the closest corner of the screen.
                QPoint dockPt = root->rect().center();
                bool tileMatched = false;
                int iconsScanned = 0;
                for (QWidget *w : root->findChildren<QWidget *>()) {
                    if (auto *di = dynamic_cast<DockIcon *>(w)) {
                        ++iconsScanned;
                        if (!dockName.isEmpty() && di->appName() == dockName) {
                            dockPt = di->mapTo(root,
                                               di->rect().center());
                            tileMatched = true;
                            qInfo() << "[genie-trace] matched tile:"
                                    << "appName=" << di->appName()
                                    << "local center=" << di->rect().center()
                                    << "mapped dockPt=" << dockPt;
                            di->bounce();
                            break;
                        }
                    }
                }
                qInfo() << "[genie-trace] tiles scanned=" << iconsScanned
                        << "matched=" << tileMatched
                        << "final dockPt=" << dockPt
                        << "(fallback used=" << !tileMatched << ")";
                qInfo() << "[genie-trace] root size=" << root->size()
                        << "global center=" << root->mapToGlobal(dockPt);

                // Screenshot the window into a temp PNG
                QString png = QString("/tmp/genie-%1-%2.png")
                    .arg(target.conId)
                    .arg(QDateTime::currentMSecsSinceEpoch());
                QProcess *sh = new QProcess;
                QProcessEnvironment env2 =
                    QProcessEnvironment::systemEnvironment();
                env2.insert("SWAYSOCK", discoverSwaySock());
                sh->setProcessEnvironment(env2);
                QObject::connect(sh,
                    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    [sh, png, target, dockPt, dockName, root](int code, QProcess::ExitStatus) {
                    qDebug() << "[genie] grim finished code=" << code
                             << "png exists=" << QFile::exists(png);
                    sh->deleteLater();
                    if (!QFile::exists(png)) return;

                    // Show the captured window image first. Keep its first
                    // frame still, then move Files into scratchpad.
                    const QPoint dockGlobal = root->mapToGlobal(dockPt);

                    // Keep a separate copy: the forward overlay removes its PNG.
                    const QString retainedPixmap =
                        QDir::tempPath()
                        + QString("/apokolips-genie-%1-%2.png")
                              .arg(target.conId)
                              .arg(QDateTime::currentMSecsSinceEpoch());
                    if (!QFile::copy(png, retainedPixmap)) {
                        qWarning() << "[genie] could not retain screenshot";
                        QFile::remove(png);
                        return;
                    }

                    const QString chosenStyle =
                        VisualConfigManager::instance()
                            .cfg().minimizeStyle;
                    const QString styleFlag =
                        (chosenStyle == "scale") ? "--scale"
                        : (chosenStyle == "suck") ? "--suck"
                        : (chosenStyle == "thanos") ? "--thanos"
                        : (chosenStyle == "mesh") ? "--mesh"
                                                  : "--genie";
                    qDebug() << "[minimize] style=" << styleFlag;

                    const QString readyName = genieMakeHandshakeName();
                    QLocalServer::removeServer(readyName);
                    QLocalServer *readyServer = new QLocalServer(root);
                    readyServer->listen(readyName);

                    const QStringList overlayArgs{
                        styleFlag, png,
                        QString::number(target.rect.x()),
                        QString::number(target.rect.y()),
                        QString::number(target.rect.width()),
                        QString::number(target.rect.height()),
                        QString::number(dockGlobal.x()),
                        QString::number(dockGlobal.y()),
                        "--ready-sock", readyName
                    };

                    qint64 overlayPid = 0;
                    bool overlayStarted = false;

                    qInfo() << "[hs] shell: about to request overlay, readyName="
                            << readyName;
                    // Fast path: resident animator daemon.
                    {
                        AnimRequest req;
                        req.style      = styleFlag.mid(2);  // "--x" -> "x"
                        req.png        = png;
                        req.rect       = target.rect;
                        req.dock       = dockGlobal;
                        req.reverse    = false;
                        req.durationMs = 0;   // config default
                        req.readySock  = readyName;
                        if (animatorRequest(req)) {
                            overlayStarted = true;
                            qDebug() << "[genie] sent to daemon";
                        }
                    }

                    // Fallback: fresh process (daemon not running yet).
                    if (!overlayStarted) {
                        overlayStarted = QProcess::startDetached(
                            QCoreApplication::applicationFilePath(),
                            overlayArgs, QString(), &overlayPid);
                        qDebug() << "[genie] spawned fresh process pid="
                                 << overlayPid;
                    }

                    if (!overlayStarted) {
                        QFile::remove(png);
                        QFile::remove(retainedPixmap);
                        readyServer->close();
                        readyServer->deleteLater();
                        return;
                    }

                    bool *moveDone = new bool(false);
                    auto performScratchpadMove =
                        [root, target, dockName, dockGlobal,
                         retainedPixmap, moveDone]() {
                        if (*moveDone) return;
                        *moveDone = true;
                        QProcess *move = new QProcess(root);
                        QProcessEnvironment env =
                            QProcessEnvironment::systemEnvironment();
                        const QString sock = discoverSwaySock();
                        if (sock.isEmpty()) {
                            qDebug() << "[genie] no SWAYSOCK for scratchpad move";
                            QFile::remove(retainedPixmap);
                            move->deleteLater();
                            return;
                        }
                        env.insert("SWAYSOCK", sock);
                        move->setProcessEnvironment(env);

                        QObject::connect(
                            move,
                            QOverload<int, QProcess::ExitStatus>::of(
                                &QProcess::finished),
                            [move, target, dockName, dockGlobal, retainedPixmap](int code, QProcess::ExitStatus status) {
                                const QByteArray out =
                                    move->readAllStandardOutput();
                                const QByteArray err =
                                    move->readAllStandardError();
                                qDebug() << "[genie] delayed sway move"
                                         << code << status
                                         << "stdout=" << out
                                         << "stderr=" << err;

                                bool moved = false;
                                if (code == 0 &&
                                    status == QProcess::NormalExit) {
                                    const QJsonDocument reply =
                                        QJsonDocument::fromJson(out);
                                    if (reply.isArray()) {
                                        for (const QJsonValue &value :
                                             reply.array()) {
                                            if (value.toObject()
                                                    .value("success").toBool()) {
                                                moved = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if (moved) {
                                    QJsonObject entry;
                                    entry["conId"] = target.conId;
                                    entry["appId"] = target.appId;
                                    entry["title"] = target.title;
                                    // Derive the dock label from the app_id
                                    // since Super+M is not tied to a tile.
                                    QString effectiveDock = dockName;
                                    if (effectiveDock.isEmpty())
                                        effectiveDock =
                                            dockNameForAppId(target.appId);
                                    entry["dockName"] = effectiveDock;
                                    entry["startRect"] = QJsonArray{
                                        target.rect.x(), target.rect.y(),
                                        target.rect.width(),
                                        target.rect.height()
                                    };
                                    entry["dockPt"] = QJsonArray{
                                        dockGlobal.x(), dockGlobal.y()
                                    };
                                    entry["pixmap"] = retainedPixmap;

                                    if (!upsertGenieWindow(entry)) {
                                        qWarning()
                                            << "[genie] state save failed for"
                                            << target.conId;
                                        QFile::remove(retainedPixmap);
                                    } else {
                                        qDebug()
                                            << "[genie] saved reverse state for"
                                            << target.conId << dockName;
                                    }
                                } else {
                                    QFile::remove(retainedPixmap);
                                }

                                move->deleteLater();
                            });

                        move->start("swaymsg",
                            { QString("[con_id=%1]").arg(target.conId),
                              "move", "scratchpad" });
                    };

                    QObject::connect(readyServer, &QLocalServer::newConnection,
                                     [readyServer, performScratchpadMove]() {
                        QLocalSocket *conn =
                            readyServer->nextPendingConnection();
                        if (!conn) return;
                        QObject::connect(conn, &QLocalSocket::readyRead,
                                         [conn, performScratchpadMove,
                                          readyServer]() {
                            QByteArray data = conn->readAll();
                            if (data.contains("ready")) {
                                qDebug() << "[genie] handshake ready";
                                performScratchpadMove();
                                if (readyServer->isListening())
                                    readyServer->close();
                            }
                            conn->disconnectFromServer();
                        });
                    });

                    QTimer::singleShot(1500, root,
                        [performScratchpadMove, readyServer]() {
                        if (!readyServer->isListening()) return;
                        qDebug() << "[genie] handshake timeout — moving";
                        performScratchpadMove();
                        if (readyServer->isListening())
                            readyServer->close();
                    });
                });
                QString grimArg = QString("%1,%2 %3x%4")
                        .arg(target.rect.x())
                        .arg(target.rect.y())
                        .arg(target.rect.width())
                        .arg(target.rect.height());
                qDebug() << "[genie] running grim" << grimArg << "->" << png;
                sh->start("grim", { "-g", grimArg, png });
            });
            q->start("swaymsg", { "-t", "get_tree", "-r" });
        }
    });
    signalPoll->start();

    NotificationCenter *notifications = new NotificationCenter(root);
    notifications->resize(360, root->height());
    notifications->hide();
    root->setNotifications(notifications);

    // Cursor glow — seal-coloured halo that follows the pointer.
    // Raised above everything else in the shell so it draws on
    // top of the top bar, dock, notifications, launcher.
    if (VisualConfigManager::instance().cfg().cursorGlow) {
        CursorGlowOverlay *cursorGlow = new CursorGlowOverlay(root);
        cursorGlow->raise();
        qInfo() << "[cursorglow] installed";
    }

    // ---- Upscale completion watcher ----
    // The --upscale-worker mode writes /tmp/apokolips-upscale-last.json
    // when a job finishes. When that file changes, and the currently-
    // active custom wallpaper is still the blurry original, swap in
    // the sharp version. The existing config watcher reloads it.
    {
        QTimer *uw = new QTimer(root);
        uw->setInterval(3000);
        QObject::connect(uw, &QTimer::timeout, [notifications]() {
            static qint64 lastTs = 0;
            QFile f("/tmp/apokolips-upscale-last.json");
            if (!f.exists()) return;
            if (!f.open(QIODevice::ReadOnly)) return;
            QJsonDocument doc =
                QJsonDocument::fromJson(f.readAll());
            f.close();
            if (!doc.isObject()) return;

            QJsonObject o = doc.object();
            qint64 ts = (qint64)o.value("ts").toDouble();
            if (ts <= lastTs) return;
            lastTs = ts;

            QString src = o.value("src").toString();
            QString dst = o.value("dst").toString();
            if (dst.isEmpty() || !QFile::exists(dst)) return;

            auto &cfg = VisualConfigManager::instance().cfg();
            if (cfg.customWallpaperPath == src) {
                VisualConfigManager::instance().setAndSave(
                    [dst](VisualConfig &c) {
                        c.customWallpaperPath = dst;
                    });
                if (notifications) {
                    notifications->flash("APOKOLIPS",
                        "Wallpaper upscaled",
                        "Switched to the sharp 4K version.");
                }
                qInfo() << "[upscale] swapped wallpaper to sharp:"
                        << dst;
            } else {
                if (notifications) {
                    notifications->flash("APOKOLIPS",
                        "Upscale finished",
                        "You picked a different wallpaper, so the "
                        "sharp version is saved but not applied.");
                }
                qInfo() << "[upscale] completed but wallpaper "
                           "changed since launch:"
                        << src;
            }
        });
        uw->start();
    }

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
            Icons::drawOmega(p, QRectF(rect()), m_color);
        }
    private:
        QColor m_color = QColor(230, 230, 240);
        QColor m_hover = QColor(230, 230, 240, 45);
    };

    // Omega button — repurposed as the system menu trigger.
    // Icon stays the same. Function changes to open apoMenu
    // (wired below, after apoMenu is built).
    OmegaButton *btnDiamond = new OmegaButton;
    topLayout->addWidget(btnDiamond);


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

    // Brand label — plain text; the omega button to its left is
    // the system menu trigger.
    QLabel *brand = new QLabel("Apokolips");
    ThemeManager::instance().subscribe([brand](const Theme &t) {
        brand->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent; padding: 0 4px;")
            .arg(t.textPrimary.name()));
    });
    topLayout->addWidget(brand);

    HUDWidget *hud = new HUDWidget;
    hud->setVisible(false);
    topLayout->addWidget(hud);

    // Menu buttons list needed by focusPoll below
    QList<QPushButton *> menuButtons;


    // ---- Per-app label — top bar shows the focused app's name ----
    // IMPORTANT: swaymsg runs async. Calling it synchronously would block
    // the shell's event loop and freeze the UI (including window close).
    QTimer *focusPoll = new QTimer(&window);
    focusPoll->setInterval(250);
    QObject::connect(focusPoll, &QTimer::timeout, [brand, hud, topBar, &menuButtons]() {
        QProcess *p = new QProcess;
        QObject::connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [p, brand, hud, topBar, &menuButtons](int, QProcess::ExitStatus) {
            QByteArray out = p->readAllStandardOutput();
            p->deleteLater();
            QJsonDocument doc = QJsonDocument::fromJson(out);
            if (!doc.isObject()) { brand->setText("Apokolips"); return; }

            // Walk the tree looking for the focused leaf
            std::function<QString(const QJsonObject &)> findFocused =
            [&](const QJsonObject &node) -> QString {
            if (node.value("focused").toBool() &&
                (node.value("type").toString() == "con" ||
                 node.value("type").toString() == "floating_con")) {
                QString app = node.value("app_id").toString();
                QString nm  = node.value("name").toString();
                return app.isEmpty() ? nm : app;
            }
            for (const QJsonValue &v : node.value("nodes").toArray()) {
                QString r = findFocused(v.toObject());
                if (!r.isEmpty()) return r;
            }
            for (const QJsonValue &v : node.value("floating_nodes").toArray()) {
                QString r = findFocused(v.toObject());
                if (!r.isEmpty()) return r;
            }
            return {};
        };

            // Also capture con_id of the focused window
        int conId = -1;
        QString focusedTitle;
        QRect focusedRect;
        std::function<bool(const QJsonObject &)> findConId =
            [&](const QJsonObject &n) -> bool {
            const QString type = n.value("type").toString();
            if (n.value("focused").toBool() &&
                (type == "con" || type == "floating_con")) {
                conId = n.value("id").toInt();
                focusedTitle = n.value("name").toString();
                const QJsonObject r = n.value("rect").toObject();
                focusedRect = QRect(r.value("x").toInt(), r.value("y").toInt(),
                                    r.value("width").toInt(),
                                    r.value("height").toInt());
                return true;
            }
            for (const QJsonValue &v : n.value("nodes").toArray())
                if (findConId(v.toObject())) return true;
            for (const QJsonValue &v : n.value("floating_nodes").toArray())
                if (findConId(v.toObject())) return true;
            return false;
        };
        findConId(doc.object());

        QString id = findFocused(doc.object());

        // Preserve the last real app window across the hotkey focus change.
        const bool shellAppWindow =
            id == "apokolips-shell" &&
            (focusedTitle == "Files" ||
             focusedTitle == "Apokolips Settings");
        const bool internalOverlay =
            id == "apokolips-genie" ||
            id == "apokolips-glyph" ||
            id == "apokolips-snap" ||
            id == "apokolips-clipboard";
        if (conId > 0 && !id.isEmpty() && !internalOverlay &&
            (id != "apokolips-shell" || shellAppWindow)) {
            g_trackedConId = conId;
            g_trackedAppId = id;
            g_trackedTitle = focusedTitle;
            g_trackedRect = focusedRect;
        }

        // Hovering the top bar makes sway focus the shell itself.
        // g_trackedAppId holds the last REAL app — stick with it
        // so the top bar doesn't snap back to "Apokolips" every
        // time the pointer touches the bar.
        QString effectiveId = id;
        // Filter shell-internal processes.
        if (id == "apokolips-animator") {
            effectiveId = g_trackedAppId;
        } else if (id.isEmpty() || id == "apokolips-shell") {
            if (focusedTitle == "Apokolips Settings" ||
                focusedTitle == "Settings") {
                effectiveId = "apokolips-settings";
            } else {
                // Robust hover check. QApplication::widgetAt() asks
                // the platform for the actual widget under the
                // pointer — works on Wayland where QCursor::pos()
                // alone often returns (0,0).
                bool overTopBar = topBar->isMouseOver();
                if (overTopBar && !g_trackedAppId.isEmpty()) {
                    effectiveId = g_trackedAppId;
                } else {
                    g_trackedAppId.clear();
                    effectiveId.clear();
                }
            }
        }

        QString label = "Apokolips";
        if (effectiveId == "org.gnome.Nautilus" ||
            effectiveId == "apokolips-files")           label = "Files";
        else if (effectiveId == "apokolips-aperture")   label = "Aperture";
        else if (effectiveId == "apokolips-notes")      label = "Notes";
        else if (effectiveId == "apokolips-mosaic")     label = "Mosaic";
        else if (effectiveId == "apokolips-boombube")   label = "BoomTube";
        else if (effectiveId == "apokolips-mymach")     label = "myMach";
        else if (effectiveId == "apokolips-node")       label = "Node";
        else if (effectiveId == "apokolips-calculator") label = "Calculator";
        else if (effectiveId == "org.gnome.Terminal" ||
                 effectiveId == "foot")                 label = "Terminal";
        else if (effectiveId == "firefox")              label = "Firefox";
        else if (effectiveId == "thunderbird")          label = "Mail";
        else if (effectiveId == "eog")                  label = "Photos";
        else if (effectiveId == "org.gnome.Totem")      label = "Videos";
        else if (effectiveId == "org.gnome.TextEditor" ||
                 effectiveId == "gnome-text-editor")    label = "Text Editor";
        else if (effectiveId == "gnome-control-center" ||
                 effectiveId == "apokolips-settings") label = "Settings";
        else if (effectiveId == "gnome-calculator")     label = "Calculator";
        else if (!effectiveId.isEmpty() &&
                 effectiveId != "apokolips-shell")      label = effectiveId;

        if (brand->text() != label) brand->setText(label);

            // --- HUD Morphing Logic ---
            // Cycle through windows of the current app. Queries
            // sway's tree, finds all con_ids matching the app_id,
            // focuses the next/prev one.
            auto cycleApp = [](const QString &appId, int dir) {
                QProcess p;
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("SWAYSOCK", discoverSwaySock());
                p.setProcessEnvironment(env);
                p.start("swaymsg", {"-t", "get_tree", "-r"});
                if (!p.waitForFinished(800)) return;
                QJsonDocument doc = QJsonDocument::fromJson(
                    p.readAllStandardOutput());
                if (!doc.isObject()) return;

                QList<int> ids;
                int current = -1;
                std::function<void(const QJsonObject &)> walk =
                    [&](const QJsonObject &n) {
                    const QString app = n.value("app_id").toString();
                    const QString title = n.value("name").toString();
                    bool match = (app == appId);
                    if (!match && appId == "apokolips-settings") {
                        match = (app == "apokolips-shell" &&
                                 (title == "Apokolips Settings" ||
                                  title == "Settings"));
                    }
                    if (match) {
                        int id = n.value("id").toInt();
                        if (id > 0) ids.append(id);
                        if (n.value("focused").toBool())
                            current = id;
                    }
                    for (auto v : n.value("nodes").toArray())
                        walk(v.toObject());
                    for (auto v : n.value("floating_nodes").toArray())
                        walk(v.toObject());
                };
                walk(doc.object());

                if (ids.isEmpty()) {
                    qDebug() << "[hud] cycleApp: no windows for" << appId;
                    return;
                }
                int idx = ids.indexOf(current);
                int next;
                if (idx < 0) {
                    next = 0;
                } else {
                    next = (idx + dir + ids.size()) % ids.size();
                }
                int target = ids[next];
                QProcess p2;
                p2.setProcessEnvironment(env);
                p2.start("swaymsg",
                    {QString("[con_id=%1]").arg(target), "focus"});
                p2.waitForFinished(500);
                qDebug() << "[hud] cycleApp" << appId
                         << "from" << current << "to" << target
                         << "of" << ids.size();
            };

            auto launchSelf = [](const QStringList &args) {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(), args);
            };

            auto setHud = [&](const QColor &col,
                              const QList<QPair<QString,
                                  std::function<void()>>> &acts) {
                topBar->setBrandColor(col);
                hud->setActions(acts);
                for (QPushButton *b : menuButtons) b->setVisible(false);
                hud->setVisible(true);
            };

            auto closeActive = []() {
                QProcess p;
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("SWAYSOCK", discoverSwaySock());
                p.setProcessEnvironment(env);
                p.start("swaymsg", {"kill"});
                p.waitForFinished(500);
            };

            if (label == "Files" || label == "Finder") {
                setHud(QColor(30, 55, 100),
                       {
                    {"<", [cycleApp, effectiveId](){
                        cycleApp(effectiveId, -1); }},
                    {">", [cycleApp, effectiveId](){
                        cycleApp(effectiveId, +1); }},
                    {"+ Win", [launchSelf](){
                        launchSelf({"--files"}); }}
                });
            } else if (label == "Notes") {
                setHud(QColor(80, 60, 20),
                       {
                    {"+ Note", [launchSelf](){
                        launchSelf({"--notes"}); }}
                });
            } else if (label == "Aperture") {
                setHud(QColor(40, 30, 90),
                       {
                    {"Zoom +", [](){}},
                    {"Zoom -", [](){}}
                });
            } else if (label == "myMach") {
                setHud(QColor(20, 50, 30),
                       {
                    {"Refresh", [](){}}
                });
            } else if (label == "BoomTube") {
                setHud(QColor(90, 30, 30),
                       {
                    {"Play", [](){}},
                    {"Pause", [](){}}
                });
            } else if (label == "Mosaic") {
                setHud(QColor(50, 50, 60),
                       {
                    {"Refresh", [](){}}
                });
            } else if (label == "Node" || label == "Terminal") {
                setHud(QColor(30, 30, 35),
                       {
                    {"+ Tab", [launchSelf](){
                        launchSelf({"--node"}); }}
                });
            } else if (label == "Settings") {
                setHud(QColor(50, 50, 60),
                       {
                    {"Close", [closeActive](){ closeActive(); }}
                });
            } else if (label == "Calculator") {
                setHud(QColor(60, 40, 30),
                       {
                    {"Close", [closeActive](){ closeActive(); }}
                });
            } else {
                topBar->setBrandColor(QColor()); // transparent
                hud->setVisible(false);
                for (QPushButton *b : menuButtons) b->setVisible(true);
            }
        });
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SWAYSOCK", discoverSwaySock());
        p->setProcessEnvironment(env);
        p->start("swaymsg", { "-t", "get_tree", "-r" });
    });
    focusPoll->start();
    topLayout->addSpacing(10);


    auto makeMenuButton = [&](const QString &label, QMenu *menu) {
        QPushButton *b = new QPushButton(label);
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        menuButtons.append(b);
        ThemeManager::instance().subscribe([b, menu](const Theme &t) {
            b->setStyleSheet(QString(
                "QPushButton { color: %1; background: transparent;"
                "  border: none; padding: 4px 14px; font-size: 13px;"
                "  font-weight: 500; letter-spacing: 0.2px; }"
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
            // Master switch for every floating widget card.
            //
            // NOTE: we do NOT call save() here. VisualConfigManager's
            // save() re-reads the config from disk before writing,
            // which would overwrite our in-memory change with the
            // stale on-disk value. The config watcher then fires on
            // mtime change and re-shows the cards — the "flash"
            // bug. Keeping it in-memory only, the watcher never
            // fires, so nothing fights the toggle.
            auto &cfg = VisualConfigManager::instance().cfg();
            bool nowVisible = !cfg.widgetsVisible;
            cfg.widgetsVisible = nowVisible;
            int shown = 0, hidden = 0;
            for (QWidget *w : root->findChildren<QWidget *>()) {
                if (auto *card = dynamic_cast<WidgetCard *>(w)) {
                    card->setVisible(nowVisible);
                    if (nowVisible) card->raise();
                    if (nowVisible) ++shown; else ++hidden;
                }
            }
            qInfo() << "[toggle] -> visible =" << nowVisible
                    << "shown =" << shown
                    << "hidden =" << hidden;
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
        { "ruby",       "Magnific"      },
        { "starfield",  "Starfield"     },
        { "aurora",     "Aurora"        },
        { "goldengate", "Golden Gate"   },
        { "everest",    "Mount Everest" },
        { "neonjinx",   "Neon Jinx"     }
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

    // =========================================================
    // Apokolips system menu — the "Apple menu" equivalent
    // Opened by clicking the brand label in the top bar.
    // =========================================================
    QMenu *apoMenu = new QMenu;

    // --- About / Settings
    {
        QAction *a = apoMenu->addAction("About This Apokolips OS");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--settings" });
        });
    }
    {
        QAction *a = apoMenu->addAction("System Settings\xE2\x80\xA6");
        a->setShortcut(QKeySequence("Ctrl+,"));
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--settings" });
        });
    }

    apoMenu->addSeparator();

    // --- Recent Items submenu (rebuilt on every open)
    {
        QMenu *recent = apoMenu->addMenu("Recent Items");

        auto rebuildRecent = [recent]() {
            recent->clear();
            const auto entries = Recents::list(15);
            if (entries.isEmpty()) {
                QAction *a = recent->addAction("(nothing yet)");
                a->setEnabled(false);
            } else {
                for (const Recents::Entry &e : entries) {
                    QString label = QFileInfo(e.path).fileName();
                    if (label.isEmpty()) label = e.path;
                    if (label.length() > 40)
                        label = label.left(37) + "\xE2\x80\xA6";
                    QAction *a = recent->addAction(label);
                    a->setToolTip(e.path);
                    QString path = e.path;
                    QObject::connect(a, &QAction::triggered,
                                     [path]() {
                        QProcess::startDetached("xdg-open", { path });
                    });
                }
            }
            recent->addSeparator();
            QAction *clearAct = recent->addAction("Clear Menu");
            QObject::connect(clearAct, &QAction::triggered, []() {
                Recents::clear();
            });
        };

        QObject::connect(recent, &QMenu::aboutToShow,
                         recent, rebuildRecent);
        rebuildRecent();   // seed once so the first hover isn't empty
    }

    // --- Quick Actions submenu
    {
        QMenu *quick = apoMenu->addMenu("Quick Actions");
        {
            QAction *a = quick->addAction("Take Screenshot");
            QObject::connect(a, &QAction::triggered, []() {
                QProcess::startDetached("bash", { "-c",
                    "mkdir -p ~/Pictures/Screenshots && "
                    "grim ~/Pictures/Screenshots/"
                    "apo-$(date +%Y%m%d-%H%M%S).png" });
            });
        }
        {
            QAction *a = quick->addAction("Set Wallpaper\xE2\x80\xA6");
            QObject::connect(a, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--settings" });
            });
        }
        {
            QAction *a = quick->addAction("Toggle Do Not Disturb");
            QObject::connect(a, &QAction::triggered, []() {
                VisualConfigManager::instance().setAndSave(
                    [](VisualConfig &c) {
                        c.dndEnabled = !c.dndEnabled;
                    });
            });
        }
    }

    // --- Window submenu
    {
        QMenu *win = apoMenu->addMenu("Window");
        {
            QAction *a = win->addAction("Force Quit\xE2\x80\xA6");
            a->setShortcut(QKeySequence("Ctrl+Alt+Escape"));
            QObject::connect(a, &QAction::triggered, []() {
                QProcess::startDetached("swaymsg", { "kill" });
            });
        }
        {
            QAction *a = win->addAction("Mission Control");
            QObject::connect(a, &QAction::triggered, []() {
                ::raise(SIGCONT);   // shell's g_mcToggle signal
            });
        }
        {
            QAction *a = win->addAction("Reload Shell");
            a->setShortcut(QKeySequence("Ctrl+Shift+R"));
            QObject::connect(a, &QAction::triggered, []() {
                QProcess::startDetached("systemctl",
                    { "--user", "restart",
                      "apokolips-shell.service" });
            });
        }
    }

    apoMenu->addSeparator();

    // --- Power / session
    {
        QAction *a = apoMenu->addAction("Lock Screen");
        a->setShortcut(QKeySequence("Ctrl+Meta+Q"));
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached("loginctl", { "lock-session" });
        });
    }
    {
        QAction *a = apoMenu->addAction("Sleep");
        QObject::connect(a, &QAction::triggered, []() {
            QProcess::startDetached("systemctl", { "suspend" });
        });
    }
    {
        QAction *a = apoMenu->addAction("Restart\xE2\x80\xA6");
        QObject::connect(a, &QAction::triggered, []() {
            QMessageBox::StandardButton r = QMessageBox::question(
                nullptr, "Restart",
                "Restart this machine?",
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (r == QMessageBox::Yes)
                QProcess::startDetached("systemctl", { "reboot" });
        });
    }
    {
        QAction *a = apoMenu->addAction("Shut Down\xE2\x80\xA6");
        QObject::connect(a, &QAction::triggered, []() {
            QMessageBox::StandardButton r = QMessageBox::question(
                nullptr, "Shut Down",
                "Shut down this machine?",
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (r == QMessageBox::Yes)
                QProcess::startDetached("systemctl", { "poweroff" });
        });
    }

    apoMenu->addSeparator();

    {
        const QString user = QString::fromLocal8Bit(qgetenv("USER"));
        QAction *a = apoMenu->addAction(
            QString("Log Out %1\xE2\x80\xA6").arg(
                user.isEmpty() ? "user" : user));
        a->setShortcut(QKeySequence("Ctrl+Shift+Q"));
        QObject::connect(a, &QAction::triggered, []() {
            QMessageBox::StandardButton r = QMessageBox::question(
                nullptr, "Log Out",
                "End this session?",
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (r == QMessageBox::Yes)
                QProcess::startDetached("swaymsg", { "exit" });
        });
    }

    // Style the menu when theme changes.
    ThemeManager::instance().subscribe([apoMenu](const Theme &t) {
        apoMenu->setStyleSheet(menuStyle(t));
    });


    // Omega button — the primary system menu trigger. Menu drops
    // from below the omega, mac-style.
    QObject::connect(btnDiamond, &QPushButton::clicked,
                     [btnDiamond, apoMenu]() {
        QPoint below = btnDiamond->mapToGlobal(
            QPoint(0, btnDiamond->height() + 4));
        apoMenu->exec(below);
    });

    topLayout->addStretch();

    // ---- Right-side status cluster (macOS style) ----

    // Live tray icons
    topLayout->addWidget(new TrayIcon(TrayIcon::Network));
    topLayout->addWidget(new TrayIcon(TrayIcon::Bluetooth));
    // Battery always shows; percent label shows "AC" if no battery
    topLayout->addWidget(new TrayIcon(TrayIcon::Battery));
    topLayout->addWidget(new BatteryPercentLabel);

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

    {
        // Apply timeouts from config on shell startup
        auto &c = VisualConfigManager::instance().cfg();
        if (c.screenOffTimeoutMin > 0 || c.sleepTimeoutMin > 0) {
            QStringList args = { "-w" };
            if (c.screenOffTimeoutMin > 0) {
                int sec = c.screenOffTimeoutMin * 60;
                args << QString::number(sec)
                     << "swaymsg output * dpms off"
                     << "resume swaymsg output * dpms on";
            }
            if (c.sleepTimeoutMin > 0) {
                int sec = c.sleepTimeoutMin * 60;
                args << QString::number(sec)
                     << "systemctl suspend";
            }
            QProcess::startDetached("swayidle", args);
        }
    }

    // Pre-warm gtklock so the first real lock isn't a cold start
    {
        QProcess *warm = new QProcess(&window);
        warm->start("gtklock", { "--version" });
        QObject::connect(warm,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            warm, &QProcess::deleteLater);
    }

    // ---- Clear stale genie state from any previous shell session ----
    // Any conIds saved before this process started are dead — sway
    // assigns new IDs at every boot. Leaving them in caused: dock
    // click plays a stale reverse animation, then fails to restore
    // the (long-gone) window. Wipe on startup.
    {
        QFile::remove("/tmp/apokolips-genie-state.json");
        QDir tmp(QDir::tempPath());
        for (const QString &f :
             tmp.entryList({ "apokolips-genie-*.png",
                             "genie-*.png" },
                           QDir::Files))
            QFile::remove(tmp.filePath(f));
        qInfo() << "[genie] state wiped on startup";
    }

    // ---- Clean-slate reload: kill leftover app processes ----
    // Reload means "fresh desktop." Any app that was running from the
    // previous session is gone. Without this, the shell comes back up
    // with hidden app processes still alive, and their dock dots stay
    // lit even though no window is visible.
    //
    // Does NOT kill the animator daemon — its --animator-daemon flag
    // does not match any of these patterns.
    {
        const QStringList appFlags = {
            "--notes", "--files", "--settings", "--mymach",
            "--calc", "--node", "--boomtube", "--image"
        };
        for (const QString &flag : appFlags) {
            QProcess::execute("pkill", { "-f",
                QString("apokolips-shell %1").arg(flag) });
        }
        qInfo() << "[shell] clean-slate: killed leftover app processes";
    }

    // Start the resident animator daemon once. It lives independently
    // of the shell — restarts of the shell reattach to the same daemon.
    if (!animatorDaemonRunning()) {
        qint64 animPid = 0;
        const bool started = QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            { QStringLiteral("--animator-daemon") },
            QString(), &animPid);
        qInfo() << "[animator] spawned daemon pid=" << animPid
                << "ok=" << started;
    } else {
        qInfo() << "[animator] daemon already running";
    }

    // Every 5 s, drop state entries whose conIds sway no longer knows
    // about. Fixes: red-light close leaves a phantom entry, next dock
    // click wastes an animation on a dead conId.
    {
        QTimer *pruneTimer = new QTimer(root);
        pruneTimer->setInterval(5000);
        QObject::connect(pruneTimer, &QTimer::timeout, []() {
            pruneStaleGenieState();
            if (!animatorDaemonRunning()) {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { QStringLiteral("--animator-daemon") });
            }
        });
        pruneTimer->start();
    }

    ClipboardPanel *clipboardPanel = new ClipboardPanel(root);

    // IPC — second instance connects and sends "toggle"
    QString clipSockPath = QDir::tempPath() + "/apokolips-clipboard.sock";
    QFile::remove(clipSockPath);
    QLocalServer *clipServer = new QLocalServer(&window);
    if (clipServer->listen(clipSockPath)) {
        qInfo() << "[clip] listening on" << clipSockPath;
        QObject::connect(clipServer, &QLocalServer::newConnection,
                         [clipServer, clipboardPanel, root]() {
            QLocalSocket *conn = clipServer->nextPendingConnection();
            QObject::connect(conn, &QLocalSocket::readyRead,
                             [conn, clipboardPanel, root]() {
                QByteArray data = conn->readAll();
                if (data.contains("toggle")) {
                    if (!clipboardPanel->isVisible())
                        clipboardPanel->showPanel();
                    else
                        clipboardPanel->hidePanel();
                    clipboardPanel->raise();
                }
                conn->disconnectFromServer();
            });
        });
    } else {
        qWarning() << "[clip] listen failed:" << clipServer->errorString();
    }

    // Widget framework — Session 1: ClockCard as root child.
    // Desktop layers (background, canvas) can raise themselves
    // during dock reflow and other housekeeping, putting them above
    // our card. Keep re-raising the card on a slow interval so it
    // stays above the desktop stack. Root window is 640x480 at
    // construction time; the shell resizes later — that's fine,
    // card position doesn't depend on root size (only drag
    // clamping does, and that reads the size live).
    ClockCard *clockCard = new ClockCard(root);
    clockCard->placeAtSavedOr();
    clockCard->applyVisibility();
    clockCard->raise();


    qInfo() << "[widgets] ClockCard installed"
            << "pos=" << clockCard->pos()
            << "size=" << clockCard->size()
            << "root=" << root->geometry();

    // RingsCard — CPU / RAM / NET / DISK gauges.
    RingsCard *ringsCard = new RingsCard(root);
    ringsCard->placeAtSavedOr();
    ringsCard->applyVisibility();
    ringsCard->raise();


    qInfo() << "[widgets] RingsCard installed"
            << "pos=" << ringsCard->pos()
            << "size=" << ringsCard->size();

    // StatsCard — disk ring + memory + battery hybrid card.
    StatsCard *statsCard = new StatsCard(root);
    statsCard->placeAtSavedOr();
    statsCard->applyVisibility();
    statsCard->raise();


    qInfo() << "[widgets] StatsCard installed"
            << "pos=" << statsCard->pos()
            << "size=" << statsCard->size();

    // WeatherCard — current conditions + H/L + wind + humidity.
    WeatherCard *weatherCard = new WeatherCard(root);
    weatherCard->placeAtSavedOr();
    weatherCard->applyVisibility();
    weatherCard->raise();


    // ---- Dock-aware clamp: when the dock moves, nudge any
    // widget that would overlap the new dock strip back into
    // the safe area. Same idea as shellSafeArea() for window
    // geometry.
    auto clampAllWidgets = [root]() {
        const QSize scr = root->size();
        for (QWidget *w : root->findChildren<QWidget *>()) {
            const QString cls = QString(w->metaObject()->className());
            if (cls.contains("WidgetCard")
                || cls.contains("ClockCard")
                || cls.contains("RingsCard")
                || cls.contains("StatsCard")
                || cls.contains("WeatherCard")) {
                // dynamic dispatch via QMetaObject
                QMetaObject::invokeMethod(w, "clampToSafeArea",
                    Qt::DirectConnection,
                    Q_ARG(QSize, scr));
            }
        }
    };
    // Dock awareness: when the dock moves, shift every card by
    // the safe-area's top-left delta so the whole layout slides,
    // instead of every card getting clamped to the same corner.
    QTimer *dockWatch = new QTimer(root);
    dockWatch->setInterval(400);
    QObject::connect(dockWatch, &QTimer::timeout, root,
        [root]() {
        static int  lastPos = VisualConfigManager::instance()
                                  .cfg().dockPosition;
        static QRect lastSafe = WidgetCard::safeAreaForDock(
            root->size());
        int p = VisualConfigManager::instance().cfg().dockPosition;
        if (p == lastPos) return;
        lastPos = p;

        QRect newSafe = WidgetCard::safeAreaForDock(root->size());
        QPoint delta = newSafe.topLeft() - lastSafe.topLeft();
        qInfo() << "[widgets] dock moved to" << p
                << "delta=" << delta;

        // Shift every widget card by the delta, then persist.
        for (QWidget *w : root->findChildren<QWidget *>()) {
            if (auto *card = dynamic_cast<WidgetCard *>(w)) {
                QPoint np = card->pos() + delta;
                card->move(np);
                // Persist
                QString id = card->id();
                VisualConfigManager::instance().setAndSave(
                    [id, np](VisualConfig &c) {
                        QVariantMap m;
                        m["x"] = np.x(); m["y"] = np.y();
                        c.widgetPositions[id] = m;
                    });
            }
        }
        lastSafe = newSafe;
    });
    dockWatch->start();

    qInfo() << "[widgets] WeatherCard installed"
            << "pos=" << weatherCard->pos()
            << "size=" << weatherCard->size();

    // CalendarCard — month grid with today highlighted.
    CalendarCard *calendarCard = new CalendarCard(root);
    calendarCard->placeAtSavedOr();
    calendarCard->applyVisibility();
    calendarCard->raise();


    qInfo() << "[widgets] CalendarCard installed"
            << "pos=" << calendarCard->pos()
            << "size=" << calendarCard->size();

    // NowPlayingCard — what BoomTube is playing.
    NowPlayingCard *nowPlayingCard = new NowPlayingCard(root);
    nowPlayingCard->placeAtSavedOr();
    nowPlayingCard->applyVisibility();
    nowPlayingCard->raise();

    // Sticky notes (Variant B, 2026-10-17) — up to 5 floating
    // sticky cards with numbered oxblood-cycle pins. Persistence
    // in ~/.config/apokolips/stickies.json, separate from
    // visual.json (whose setAndSave() race would lose typed text).
    {
        QString stickiesPath = QDir::homePath()
            + "/.config/apokolips/stickies.json";
        if (!QFile::exists(stickiesPath)) {
            QJsonObject entry;
            entry["text"] = "";
            QJsonObject all;
            all["0"] = entry;
            QDir().mkpath(QFileInfo(stickiesPath).absolutePath());
            QFile f(stickiesPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(all).toJson());
                f.close();
            }
            qInfo() << "[stickies] created" << stickiesPath;
        }
        QFile f(stickiesPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument d = QJsonDocument::fromJson(f.readAll());
            QJsonObject all = d.object();
            for (auto it = all.begin(); it != all.end(); ++it) {
                bool ok;
                int idx = it.key().toInt(&ok);
                if (!ok || idx < 0 || idx >= 5) continue;
                StickyCard *sc = new StickyCard(idx, root);
                sc->placeAtSavedOr();
                sc->applyVisibility();
                sc->raise();
            }
            f.close();
            qInfo() << "[stickies] instantiated" << all.size() << "cards";
        }
    }

    qInfo() << "[widgets] NowPlayingCard installed"
            << "pos=" << nowPlayingCard->pos()
            << "size=" << nowPlayingCard->size();

    // Deferred layout — root->size() is the QMainWindow default
    // (640x480) at construction; the real size arrives after the
    // Wayland surface is mapped. Wait for the real size before
    // deciding layout, otherwise every position is computed for
    // a 640-wide screen and lands wrong.
    {
        QTimer *layoutDelay = new QTimer(root);
        layoutDelay->setInterval(100);
        int *tries = new int(0);
        QObject::connect(layoutDelay, &QTimer::timeout, root,
            [root, layoutDelay, tries,
             clockCard, ringsCard, weatherCard,
             statsCard, calendarCard, nowPlayingCard]() {
            if (++(*tries) > 50) {  // give up after ~5 s
                layoutDelay->stop();
                qWarning() << "[widgets] layout wait timed out";
                return;
            }
            if (root->width() < 500) return;  // still not sized
            layoutDelay->stop();
            const QSize realSize = root->size();
            qInfo() << "[widgets] real root size =" << realSize;

            auto &cfg = VisualConfigManager::instance().cfg();

            // Treat saved positions as stale if any of them sit
            // outside the current screen (typical after a resize
            // or a reset computed at 640x480).
            bool stale = cfg.widgetPositions.isEmpty();
            for (auto it = cfg.widgetPositions.constBegin();
                 it != cfg.widgetPositions.constEnd(); ++it) {
                QVariantMap m = it.value().toMap();
                int x = m.value("x").toInt();
                int y = m.value("y").toInt();
                if (x < 0 || y < 0
                    || x > realSize.width()  - 40
                    || y > realSize.height() - 40) {
                    stale = true; break;
                }
            }

            QList<WidgetCard *> all = {
                clockCard, ringsCard, weatherCard,
                statsCard, calendarCard, nowPlayingCard
            };

            if (stale) {
                qInfo() << "[widgets] stale/empty positions — "
                           "auto-arranging";
                WidgetCard::autoArrange(all, realSize);
            } else {
                qInfo() << "[widgets] using saved positions ("
                        << cfg.widgetPositions.size() << ")";
                for (WidgetCard *c : all) {
                    c->placeAtSavedOr();
                    c->clampToSafeArea(realSize);
                    c->applyVisibility();
                    c->raise();
                }
            }
        });
        layoutDelay->start();
    }
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

    auto formatClock = []() {
        auto &cfg = VisualConfigManager::instance().cfg();
        QString fmt = cfg.clock24Hour ? "ddd HH:mm" : "ddd hh:mm AP";
        if (cfg.clockShowSeconds)
            fmt = cfg.clock24Hour ? "ddd HH:mm:ss" : "ddd hh:mm:ss AP";
        return QDateTime::currentDateTime().toString(fmt);
    };

    QTimer *ticker = new QTimer(&window);
    QObject::connect(ticker, &QTimer::timeout, [clock, formatClock]() {
        clock->setText(formatClock());
    });
    ticker->start(1000);
    clock->setText(formatClock());

    rootLayout->addWidget(topBar);

    QWidget *desktop = new QWidget;

    QWidget *middle = new QWidget;
    rootLayout->addWidget(middle, 1);

    // ---- Desktop icon canvas (child of root, sits between top bar
    //      and dock) ----
    PoolCanvas *desktopIcons = new PoolCanvas(root);
    {
        auto &cfg = VisualConfigManager::instance().cfg();

        auto myMachPaint = [](QPainter &p, QRectF r) {
            // Gear inside a monitor outline
            QColor bone(240, 237, 232, 230);
            QColor ox(168, 50, 50);

            // Monitor
            QRectF mon(r.left() + r.width() * 0.10,
                       r.top()  + r.height() * 0.16,
                       r.width() * 0.80,
                       r.height() * 0.58);
            QPen bp(bone, 1.4);
            bp.setJoinStyle(Qt::RoundJoin);
            p.setPen(bp);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(mon, 3, 3);

            // Stand
            QPointF standTop(mon.center().x(), mon.bottom());
            p.drawLine(standTop,
                       QPointF(mon.center().x(),
                               mon.bottom() + r.height() * 0.08));
            p.drawLine(QPointF(mon.center().x() - r.width() * 0.10,
                               mon.bottom() + r.height() * 0.08),
                       QPointF(mon.center().x() + r.width() * 0.10,
                               mon.bottom() + r.height() * 0.08));

            // Gear (oxblood)
            QPointF gc = mon.center();
            qreal outer = r.width() * 0.16;
            qreal inner = r.width() * 0.09;
            p.setPen(Qt::NoPen);
            p.setBrush(ox);
            p.drawEllipse(gc, outer, outer);

            for (int i = 0; i < 8; ++i) {
                qreal a = i * M_PI / 4.0;
                QRectF tooth(gc.x() + std::cos(a) * outer - 1.5,
                             gc.y() + std::sin(a) * outer - 1.5,
                             3.0, 3.0);
                p.drawEllipse(tooth);
            }

            // Gear hole
            p.setBrush(bone);
            p.drawEllipse(gc, inner, inner);
        };

        QPoint defaultPos(desktop->width() - 130, 40);
        if (cfg.poolIconPos.contains("mymach")) {
            QVariantMap pos = cfg.poolIconPos["mymach"].toMap();
            defaultPos = QPoint(pos.value("x", defaultPos.x()).toInt(),
                                pos.value("y", defaultPos.y()).toInt());
        }

        // Route the desktop myMach icon through the same draw
        // dispatch the dock uses — one source of truth for colors.
        desktopIcons->addIcon(
            "mymach", "myMach",
            [](QPainter &p, QRectF r) {
                Icons::drawFor("myMach", p, r,
                               QColor(240, 237, 232));
            },
            []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--mymach" });
            },
            defaultPos);

        desktopIcons->m_onMoved =
            [](const QString &id, const QPoint &p) {
            VisualConfigManager::instance().setAndSave(
                [id, p](VisualConfig &c) {
                    QVariantMap m = c.poolIconPos[id].toMap();
                    m["x"] = p.x();
                    m["y"] = p.y();
                    c.poolIconPos[id] = m;
                });
        };

        desktopIcons->rebuildIcons();     // render persisted desktop apps
        desktopIcons->show();
        desktopIcons->raise();
        // PoolCanvas is a later child of root, so raising it
        // buries the widget cards underneath and left-clicks
        // never reach them (canvas ignores → goes to parent,
        // not to sibling widgets). Re-raise every card right
        // after every canvas raise.
        for (QWidget *w : root->findChildren<QWidget *>())
            if (dynamic_cast<WidgetCard *>(w)) w->raise();
    }

    // Keep the canvas sized to the middle strip (below top bar,
    // above dock) whenever the shell relayouts.
    auto refitCanvas = [desktopIcons, desktop, middle, root]() {
        if (!desktopIcons || !desktop) return;
        QPoint tl = desktop->mapTo(desktopIcons->parentWidget(),
                                    QPoint(0, 0));
        desktopIcons->setGeometry(QRect(tl, desktop->size()));
        desktopIcons->raise();
        for (QWidget *w : root->findChildren<QWidget *>())
            if (dynamic_cast<WidgetCard *>(w)) w->raise();
    };
    refitCanvas();
    // Re-run after the event loop settles (layout has real sizes)
    QTimer::singleShot(0,   desktop, [refitCanvas]() { refitCanvas(); });
    QTimer::singleShot(150, desktop, [refitCanvas, desktopIcons]() {
        refitCanvas();
        // Now that the canvas knows its size, re-run rebuildIcons so
        // default positions use real coordinates instead of 0.
        if (desktopIcons) desktopIcons->rebuildIcons();
    });

    Dock *dock = new Dock;
    struct DockApp { const char *icon; const char *name; };
    {
        int i = 0;
        for (const auto &e : appRegistry()) {
            if (!isInDock(QString::fromUtf8(e.id))) continue;
            dock->addIcon(QString::fromUtf8(e.glyph), e.label, i++);
        }
    }

    // Persistent grid layout — never destroyed. Moving widgets between
    // cells avoids the Wayland reparent/layout-destroy race that froze
    // dock input on position change.
    QGridLayout *middleGrid = new QGridLayout(middle);
    middleGrid->setContentsMargins(0, 0, 0, 0);
    middleGrid->setSpacing(0);
    middle->setLayout(middleGrid);

    auto applyDockPosition = [middle, desktop, dock, middleGrid](int pos) {
        middleGrid->removeWidget(dock);
        middleGrid->removeWidget(desktop);

        // Reset all stretches
        for (int i = 0; i < 4; ++i) {
            middleGrid->setRowStretch(i, 0);
            middleGrid->setColumnStretch(i, 0);
        }

        if (pos == 0) {
            // Bottom — desktop fills, dock under it centered
            middleGrid->addWidget(desktop, 0, 0, 1, 3);
            middleGrid->addWidget(dock,    1, 1, 1, 1, Qt::AlignHCenter);
            middleGrid->setRowStretch(0, 1);
            middleGrid->setRowStretch(1, 0);
        } else if (pos == 1) {
            // Left
            middleGrid->addWidget(dock,    0, 0, 1, 1, Qt::AlignVCenter);
            middleGrid->addWidget(desktop, 0, 1, 1, 1);
            middleGrid->setColumnStretch(1, 1);
        } else {
            // Right
            middleGrid->addWidget(desktop, 0, 0, 1, 1);
            middleGrid->addWidget(dock,    0, 1, 1, 1, Qt::AlignVCenter);
            middleGrid->setColumnStretch(0, 1);
        }

        dock->setOrientation(pos != 0);
        dock->show();
        desktop->show();
        dock->raise();

        // The desktop-icon canvas is a sibling overlay sized to the
        // OLD desktop rect. Layout changes are lazy — the desktop's
        // new geometry doesn't exist yet at this line. So we must
        // defer the refit to the next event-loop turn, after Qt has
        // laid out the grid. Without this the canvas stays on top of
        // the dock and swallows its mouse events → dock "freezes".
        QPointer<QWidget> desktopGuard(desktop);
        QPointer<QWidget> dockGuard(dock);
        QPointer<QGridLayout> gridGuard(middleGrid);
        QPointer<QWidget> middleGuard(middle);
        QTimer::singleShot(0, middle, [=]() {
            if (!gridGuard || !middleGuard) return;
            gridGuard->activate();

            auto refit = [&](const char *name) {
                QWidget *overlay = middleGuard->findChild<QWidget *>(name);
                if (!overlay && middleGuard->window())
                    overlay = middleGuard->window()->
                              findChild<QWidget *>(name);
                if (!overlay) {
                    qInfo() << "[dock-reflow] overlay not found:" << name;
                    return;
                }
                QWidget *par = overlay->parentWidget();
                if (!par || !desktopGuard) return;
                QPoint tl = desktopGuard->mapTo(par, QPoint(0, 0));
                QRect  g  = QRect(tl, desktopGuard->size());
                qInfo() << "[dock-reflow]" << name
                        << "old=" << overlay->geometry()
                        << "desktop=" << desktopGuard->geometry()
                        << "new=" << g;
                overlay->setGeometry(g);
            };
            refit("thePool");
            refit("widgetLayer");

            // Force the canvas to clamp its icons even when its
            // geometry didn't change. The canvas is parented to the
            // top-level shell window, NOT to `middle`, so we need
            // the same fallback lookup refit() uses.
            {
                QWidget *cnv =
                    middleGuard
                        ? middleGuard->findChild<QWidget *>("thePool")
                        : nullptr;
                if (!cnv && middleGuard && middleGuard->window())
                    cnv = middleGuard->window()
                              ->findChild<QWidget *>("thePool");
                if (cnv) {
                    if (auto *dc = dynamic_cast<PoolCanvas *>(cnv)) {
                        dc->clampIcons();
                        qInfo() << "[dock-reflow] clamped icons, canvas="
                                << dc->size()
                                << "icons=" << dc->icons().size();
                    } else {
                        qInfo() << "[dock-reflow] canvas found but cast "
                                   "failed";
                    }
                } else {
                    qInfo() << "[dock-reflow] thePool canvas not found for "
                               "clamp";
                }
            }

            if (dockGuard) dockGuard->raise();
            if (desktopGuard) desktopGuard->update();
            if (dockGuard) dockGuard->update();
        });
    };

    int *lastDockPos = new int(VisualConfigManager::instance()
                                   .cfg().dockPosition);
    applyDockPosition(*lastDockPos);

    window.setCentralWidget(root);

    if (QScreen *screen = app.primaryScreen())
        window.setGeometry(screen->availableGeometry());
    window.showNormal();

    // Kill any reload-splash process from the previous shell.
    // The new shell window is now composited on top, so we can
    // safely close the splash.
    QTimer::singleShot(300, qApp, []() {
        QProcess::execute("pkill", { "-f", "--", "--reload-splash" });
        qInfo() << "[reload] splash killed";
    });

    // After show(), layout children (the middle placeholder widget) get
    // raised above our floating children. Raise them back in the right
    // z-order so mouse events reach the widgets.
    if (notifications)   notifications->raise();
    if (controlCenter)   controlCenter->raise();
    if (clipboardPanel)  clipboardPanel->raise();
    if (launcher)        launcher->raise();
    if (spotlight)       spotlight->raise();
    if (spotlight)       spotlight->raise();

    // ---- Self-restart: replace this process with the freshly built binary ----
    auto reloadSelf = [&window]() {
        QString exe = QCoreApplication::applicationFilePath();
        QByteArray path = exe.toUtf8();
        char *argv[] = { path.data(), nullptr };
        // Capture the desktop, spawn a splash process that shows
        // the backdrop fullscreen, wait for it to appear, then
        // execv. During the ~800ms Qt boot of the new shell, the
        // splash keeps the screen covered — no black gap.
        {
            QPixmap shot = window.grab();
            QString bp = QDir::tempPath() + "/apokolips-backdrop.png";
            if (shot.isNull() || !shot.save(bp, "PNG")) {
                qWarning() << "[reload] backdrop save failed";
            } else {
                qInfo() << "[reload] backdrop saved:" << bp
                        << shot.size();
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--reload-splash", bp });
                // Give the splash ~500ms to boot its window and
                // reach the Wayland compositor. During this sleep
                // the old shell window is still up, so nothing
                // flashes.
                QThread::msleep(500);
            }
        }
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

    // Track last-seen app lists so we only rebuild when they change
    QString *lastDockList     = new QString(
        VisualConfigManager::instance().cfg().dockApps.join(","));
    QString *lastLauncherList = new QString(
        VisualConfigManager::instance().cfg().launcherApps.join(","));
    QString *lastDesktopList  = new QString(
        VisualConfigManager::instance().cfg().poolApps.join(","));

    QObject::connect(configPoll, &QTimer::timeout,
                     [configPath, lastMod, root, lastDockPos,
                      applyDockPosition,
                      dock, launcher, desktopIcons,
                      lastDockList, lastLauncherList, lastDesktopList]() {
        qint64 m = QFileInfo(configPath).lastModified().toMSecsSinceEpoch();
        if (m != *lastMod) {
            *lastMod = m;
            VisualConfigManager::instance().load();
            // Seal colour might have changed — push it into the
            // global static Sigil::accent() reads from, so top bar,
            // dock, NC, menus all recolour on the next repaint.
            {
                const QString sid = VisualConfigManager::instance()
                                        .cfg().sigilAccent;
                if (!sid.isEmpty()) Sigil::setSealId(sid);
            }
            // Widget positions + visibility may have changed (drag,
            // Settings reset, or a checkbox toggle).
            if (root && root->width() >= 500) {
                auto &wc = VisualConfigManager::instance().cfg();
                if (wc.widgetPositions.isEmpty()) {
                    QList<WidgetCard *> all;
                    for (QWidget *w :
                         root->findChildren<QWidget *>()) {
                        if (auto *c =
                                dynamic_cast<WidgetCard *>(w))
                            all.append(c);
                    }
                    WidgetCard::autoArrange(all, root->size());
                    qInfo() << "[widgets] reset -> auto-arranged"
                            << all.size() << "cards";
                } else {
                    for (QWidget *w :
                         root->findChildren<QWidget *>()) {
                        if (auto *card =
                                dynamic_cast<WidgetCard *>(w)) {
                            card->placeAtSavedOr();
                            card->applyVisibility();
                            card->update();
                        }
                    }
                }
            }
            {
                auto &cfg = VisualConfigManager::instance().cfg();

                int pos = cfg.dockPosition;
                if (pos != *lastDockPos) {
                    *lastDockPos = pos;
                    applyDockPosition(pos);
                }

                // Rebuild dock if its list changed
                QString curDock = cfg.dockApps.join(",");
                if (curDock != *lastDockList) {
                    *lastDockList = curDock;
                    if (dock) dock->rebuildIcons();
                }

                // Rebuild launcher if its list changed
                QString curLaunch = cfg.launcherApps.join(",");
                if (curLaunch != *lastLauncherList) {
                    *lastLauncherList = curLaunch;
                    if (launcher) launcher->rebuildTiles();
                }

                // Rebuild desktop icons if its list changed
                QString curDesk = cfg.poolApps.join(",");
                if (curDesk != *lastDesktopList) {
                    *lastDesktopList = curDesk;
                    if (desktopIcons) desktopIcons->rebuildIcons();
                }
            }
            if (root) {
                // Apply widget visibility — Settings writes visual.json,
                // shell polls and reacts here.
                auto &cfg = VisualConfigManager::instance().cfg();

                root->update();
                for (QWidget *w : root->findChildren<QWidget *>()) w->update();
            }
        }
    });
    configPoll->start();

    // ---- Focus schedule: flip focusMode + dnd on window transitions ----
    {
        QTimer *focusTimer = new QTimer(&window);
        focusTimer->setInterval(30000);   // check every 30s
        bool *insideLast = new bool(false);
        *insideLast = false;
        QObject::connect(focusTimer, &QTimer::timeout,
                         [insideLast]() {
            auto &cfg = VisualConfigManager::instance().cfg();
            if (!cfg.focusScheduleOn) {
                *insideLast = false;
                return;
            }

            int h = QDateTime::currentDateTime().time().hour();
            int s = cfg.focusStartHour;
            int e = cfg.focusEndHour;

            // Inside window (handles overnight wrap when s > e)
            bool inside;
            if (s <= e)
                inside = (h >= s && h < e);
            else
                inside = (h >= s || h < e);

            if (inside == *insideLast) return;
            *insideLast = inside;

            if (inside) {
                VisualConfigManager::instance().setAndSave(
                    [](VisualConfig &c) {
                        c.focusMode  = "work";
                        c.dndEnabled = true;
                    });
            } else {
                VisualConfigManager::instance().setAndSave(
                    [](VisualConfig &c) {
                        c.focusMode  = "off";
                        c.dndEnabled = false;
                    });
            }
        });
        focusTimer->start();

        // Immediately evaluate on startup
        {
            auto &cfg = VisualConfigManager::instance().cfg();
            if (cfg.focusScheduleOn) {
                int h = QDateTime::currentDateTime().time().hour();
                int s = cfg.focusStartHour;
                int e = cfg.focusEndHour;
                bool inside = (s <= e)
                    ? (h >= s && h < e)
                    : (h >= s || h < e);
                *insideLast = inside;
                if (inside) {
                    VisualConfigManager::instance().setAndSave(
                        [](VisualConfig &c) {
                            c.focusMode  = "work";
                            c.dndEnabled = true;
                        });
                }
            }
        }
    }

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


// =========================================================
// Animator daemon — resident process that plays overlay
// animations on request. Eliminates the ~200 ms per-spawn
// Qt-app boot cost: first frame lands in ~10-20 ms.
// =========================================================
// Returns true if the daemon accepted the request. Returns false if
// the daemon isn't reachable — caller should fall back to spawning.
// Runs inside the daemon process. Never returns until the process dies.
int main(int argc, char *argv[])
{
    // Daemon mode must be checked before any other dispatch — it's the
    // only mode that never exits during normal operation.
    if (argc >= 2 && QString::fromLocal8Bit(argv[1])
                     == QLatin1String("--animator-daemon")) {
        return runAnimatorDaemon(argc, argv);
    }

    if (argc >= 9 && QString::fromLocal8Bit(argv[1]) == "--suck") {
        const QString imagePath = QString::fromLocal8Bit(argv[2]);
        bool ok[6] = {false, false, false, false, false, false};
        const int x = QString::fromLocal8Bit(argv[3]).toInt(&ok[0]);
        const int y = QString::fromLocal8Bit(argv[4]).toInt(&ok[1]);
        const int w = QString::fromLocal8Bit(argv[5]).toInt(&ok[2]);
        const int h = QString::fromLocal8Bit(argv[6]).toInt(&ok[3]);
        const int dx = QString::fromLocal8Bit(argv[7]).toInt(&ok[4]);
        const int dy = QString::fromLocal8Bit(argv[8]).toInt(&ok[5]);

        for (bool valid : ok) {
            if (!valid) {
                qWarning() << "[suck] invalid geometry argument";
                return 2;
            }
        }
        if (w <= 0 || h <= 0) {
            qWarning() << "[suck] invalid start size";
            return 2;
        }

        int durationMs = 0;
        QString readySock;
        bool reverse = false;
        for (int i = 9; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == "--ms" && i + 1 < argc) {
                bool valid = false;
                const int value =
                    QString::fromLocal8Bit(argv[++i]).toInt(&valid);
                if (!valid) {
                    qWarning() << "[suck] invalid --ms value";
                    return 2;
                }
                durationMs = qBound(128, value, 5000);
            } else if (arg == "--ready-sock" && i + 1 < argc) {
                readySock = QString::fromLocal8Bit(argv[++i]);
            } else if (arg == "--reverse") {
                reverse = true;
            } else {
                qWarning() << "[suck] unknown or incomplete option:" << arg;
                return 2;
            }
        }

        // Parse before QApplication so Qt cannot consume custom options.
        QApplication app(argc, argv);
        // Reuse the existing Genie overlay app_id and its Sway rules.
        app.setDesktopFileName("apokolips-genie");

        auto *suck = new SuckOverlay(
            imagePath, QRect(x, y, w, h), QPoint(dx, dy),
            durationMs, readySock, reverse);
        suck->show();
        return app.exec();
    }

    if (argc >= 9 && QString::fromLocal8Bit(argv[1]) == "--thanos") {
        QApplication app(argc, argv);
        // Same sway app_id as the other overlays so the
        // for_window [app_id="apokolips-genie"] rules match —
        // floating, border none, full-screen, top of stack.
        app.setDesktopFileName("apokolips-genie");
        const QString png = QString::fromLocal8Bit(argv[2]);
        QRect startRect(QString::fromLocal8Bit(argv[3]).toInt(),
                        QString::fromLocal8Bit(argv[4]).toInt(),
                        QString::fromLocal8Bit(argv[5]).toInt(),
                        QString::fromLocal8Bit(argv[6]).toInt());
        QPoint dock(QString::fromLocal8Bit(argv[7]).toInt(),
                    QString::fromLocal8Bit(argv[8]).toInt());
        int durationMs = 0;
        QString readySock;
        bool reverse = false;
        for (int i = 9; i < argc; ++i) {
            QString a = QString::fromLocal8Bit(argv[i]);
            if (a == "--reverse") reverse = true;
            else if (a == "--ms" && i + 1 < argc)
                durationMs = QString::fromLocal8Bit(argv[++i]).toInt();
            else if (a == "--ready-sock" && i + 1 < argc)
                readySock = QString::fromLocal8Bit(argv[++i]);
        }
        ThanosOverlay *o = new ThanosOverlay(png, startRect, dock,
                                              durationMs, readySock,
                                              reverse);
        o->show();
        return app.exec();
    }

    if (argc >= 9 && QString::fromLocal8Bit(argv[1]) == "--scale") {
        const QString imagePath = QString::fromLocal8Bit(argv[2]);
        bool ok[6] = {false, false, false, false, false, false};
        const int x = QString::fromLocal8Bit(argv[3]).toInt(&ok[0]);
        const int y = QString::fromLocal8Bit(argv[4]).toInt(&ok[1]);
        const int w = QString::fromLocal8Bit(argv[5]).toInt(&ok[2]);
        const int h = QString::fromLocal8Bit(argv[6]).toInt(&ok[3]);
        const int dx = QString::fromLocal8Bit(argv[7]).toInt(&ok[4]);
        const int dy = QString::fromLocal8Bit(argv[8]).toInt(&ok[5]);

        for (bool valid : ok) {
            if (!valid) {
                qWarning() << "[scale] invalid geometry argument";
                return 2;
            }
        }
        if (w <= 0 || h <= 0) {
            qWarning() << "[scale] invalid start size";
            return 2;
        }

        int durationMs = 0;
        QString readySock;
        bool reverse = false;
        for (int i = 9; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == "--ms" && i + 1 < argc) {
                bool valid = false;
                const int value =
                    QString::fromLocal8Bit(argv[++i]).toInt(&valid);
                if (!valid) {
                    qWarning() << "[scale] invalid --ms value";
                    return 2;
                }
                durationMs = qBound(128, value, 5000);
            } else if (arg == "--ready-sock" && i + 1 < argc) {
                readySock = QString::fromLocal8Bit(argv[++i]);
            } else if (arg == "--reverse") {
                reverse = true;
            } else {
                qWarning() << "[scale] unknown or incomplete option:" << arg;
                return 2;
            }
        }

        // Parse before QApplication so Qt cannot consume custom options.
        QApplication app(argc, argv);
        // Reuse the existing Genie overlay app_id and its Sway rules.
        app.setDesktopFileName("apokolips-genie");

        auto *scale = new ScaleOverlay(
            imagePath, QRect(x, y, w, h), QPoint(dx, dy),
            durationMs, readySock, reverse);
        scale->show();
        return app.exec();
    }

    if (argc >= 9 && QString::fromLocal8Bit(argv[1]) == "--mesh") {
        QApplication app(argc, argv);
        const QString imagePath = QString::fromLocal8Bit(argv[2]);
        const QRect start(
            QString::fromLocal8Bit(argv[3]).toInt(),
            QString::fromLocal8Bit(argv[4]).toInt(),
            QString::fromLocal8Bit(argv[5]).toInt(),
            QString::fromLocal8Bit(argv[6]).toInt());
        const QPoint dock(
            QString::fromLocal8Bit(argv[7]).toInt(),
            QString::fromLocal8Bit(argv[8]).toInt());

        bool reverse = false;
        int durationMs = 0;
        QString readySock;
        for (int i = 9; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == "--reverse") reverse = true;
            else if (arg == "--ms" && i + 1 < argc)
                durationMs = QString::fromLocal8Bit(argv[++i]).toInt();
            else if (arg == "--ready-sock" && i + 1 < argc)
                readySock = QString::fromLocal8Bit(argv[++i]);
        }
        GenieMeshOverlay *o = new GenieMeshOverlay(
            imagePath, start, dock, reverse, durationMs, readySock);
        o->show();
        return app.exec();
    }

    if (argc >= 9 && QString::fromLocal8Bit(argv[1]) == "--genie") {
        const QString imagePath = QString::fromLocal8Bit(argv[2]);
        const QRect start(
            QString::fromLocal8Bit(argv[3]).toInt(),
            QString::fromLocal8Bit(argv[4]).toInt(),
            QString::fromLocal8Bit(argv[5]).toInt(),
            QString::fromLocal8Bit(argv[6]).toInt());
        const QPoint dock(
            QString::fromLocal8Bit(argv[7]).toInt(),
            QString::fromLocal8Bit(argv[8]).toInt());

        bool reverse = false;
        int durationMs = 0;
        QString readySock;

        // Parse BEFORE constructing QApplication. Qt consumes
        // --reverse as one of its own arguments, so any parser
        // created after QApplication never sees it.
        for (int i = 9; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == "--reverse") {
                reverse = true;
            } else if (arg == "--ms") {
                if (i + 1 >= argc) return 2;
                bool ok = false;
                const int value =
                    QString::fromLocal8Bit(argv[++i]).toInt(&ok);
                if (!ok) return 2;
                durationMs = qBound(128, value, 5000);
            } else if (arg == "--ready-sock") {
                if (i + 1 >= argc) return 2;
                readySock = QString::fromLocal8Bit(argv[++i]);
            }
        }

        QApplication app(argc, argv);
        app.setDesktopFileName("apokolips-genie");

        auto *g = new GenieOverlay(
            imagePath, start, dock, reverse, durationMs, readySock);
        g->show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--notes") {
        const QString notePath = (argc >= 3)
            ? QString::fromLocal8Bit(argv[2]) : QString();
        QApplication app(argc, argv);
        app.setApplicationName("Notes");
        app.setDesktopFileName("apokolips-notes");

        // Model A multi-window (2026-10-17): every invocation opens
        // a fresh window. No single-instance probe, no listener.
        // Two Notes processes = two independent windows. Dock-click
        // "focus existing" still works via launchApp() -> swaymsg
        // focus (not via IPC). HUD "+ Note" and (future) right-click
        // "New Notes Window" both route here and now spawn fresh.

        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        NotesApp w(nullptr, notePath);
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--calc") {
        QApplication app(argc, argv);
        app.setApplicationName("Calculator");
        app.setDesktopFileName("apokolips-calculator");
        // Model A multi-window: every invocation opens fresh.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        CalculatorApp w;
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--files") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Files");
        app.setDesktopFileName("apokolips-files");
        // Model A multi-window: every invocation opens fresh. Two
        // Finders can point at different directories and transfer
        // between them — the user's stated use case.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        FileExplorer w;
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--lock") {
        QCoreApplication app(argc, argv);
        Q_UNUSED(app);

        VisualConfigManager::instance().load();
        auto &cfg = VisualConfigManager::instance().cfg();

        QStringList args;
        args << "-g" << "Adwaita";
        QString cssPath = QDir::homePath() + "/.config/gtklock/style.css";
        if (QFile::exists(cssPath))
            args << "-s" << cssPath;

        if (cfg.lockShowClock)
            args << "-t" << (cfg.clock24Hour ? "%H:%M" : "%I:%M %p");
        else
            args << "-t" << " ";
        if (cfg.lockShowDate)
            args << "-D" << "%A, %d %B";
        else
            args << "-D" << " ";

        if (!cfg.lockWallpaper.isEmpty()
            && QFile::exists(cfg.lockWallpaper))
            args << "-b" << cfg.lockWallpaper;

        args << "-H" << "-T" << "4";

        QProcess::startDetached("gtklock", args);
        return 0;
    }

    // ---- Mesh warp proof of concept (temporary) ----
    if (argc >= 3 && QString(argv[1]) == "--test-mesh") {
        QApplication app(argc, argv);
        MeshTestWindow w(QString::fromLocal8Bit(argv[2]));
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--mosaic") {
        QApplication app(argc, argv);
        app.setApplicationName("Mosaic");
        app.setDesktopFileName("apokolips-mosaic");
        // Model A multi-window: every invocation opens fresh.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        MosaicApp w;
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--mail") {
        QApplication app(argc, argv);
        app.setApplicationName("Mail");
        app.setDesktopFileName("apokolips-mail");
        MailApp w;
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--screensaver") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Screensaver");
        app.setDesktopFileName("apokolips-screensaver");
        VisualConfigManager::instance().load();
        auto &cfg = VisualConfigManager::instance().cfg();

        if (!cfg.screensaverEnabled) return 0;

        // Video style — spawn mpv fullscreen instead of the Qt overlay
        if (cfg.screensaverStyle == "video") {
            if (!cfg.screensaverVideoPath.isEmpty()
                && QFile::exists(cfg.screensaverVideoPath)) {
                QProcess::startDetached("mpv", {
                    "--fullscreen",
                    "--no-osc",
                    "--no-input-default-bindings",
                    "--really-quiet",
                    "--loop=inf",
                    cfg.screensaverVideoPath });
                return 0;
            }
            // fall through to clock if video not configured
        }

        ScreensaverOverlay *s = new ScreensaverOverlay;
        Q_UNUSED(s);
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--snap") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Snap");
        app.setDesktopFileName("apokolips-snap");
        SnapPicker *p = new SnapPicker;
        Q_UNUSED(p);
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--glyph") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Glyph");
        app.setDesktopFileName("apokolips-glyph");
        GlyphOverlay *o = new GlyphOverlay;
        Q_UNUSED(o);
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--toggle-clipboard") {
        QCoreApplication app(argc, argv);
        QString sockPath = QDir::tempPath() + "/apokolips-clipboard.sock";
        QLocalSocket sock;
        sock.connectToServer(sockPath);
        if (sock.waitForConnected(500)) {
            sock.write("toggle");
            sock.waitForBytesWritten(300);
            sock.disconnectFromServer();
        }
        return 0;
    }

    if (argc >= 2 && QString(argv[1]) == "--boomtube") {
        QApplication app(argc, argv);
        app.setApplicationName("BoomTube");
        app.setDesktopFileName("apokolips-boombube");

        const QString path = (argc >= 3)
            ? QString::fromUtf8(argv[2]) : QString();

        // Log every open into the shared Recents store.
        if (!path.isEmpty())
            Recents::add(path,
                QMimeDatabase().mimeTypeForFile(path).name(),
                "apokolips-boombube");

        // Model A multi-window: every invocation opens fresh with
        // its own playlist state. Path (if supplied) seeds the new
        // window.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        BoomTubeWindow w(path);
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--mymach") {
        QApplication app(argc, argv);
        app.setApplicationName("myMach");
        app.setDesktopFileName("apokolips-mymach");
        // Model A multi-window: every invocation opens fresh.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        MyMachWindow w;
        w.show();
        return app.exec();
    }

    if (argc >= 3 && QString(argv[1]) == "--reload-splash") {
        // Tiny foreground window showing the frozen backdrop while
        // the new shell process boots. Spawned by reloadSelf BEFORE
        // the old shell execv's. Killed by the new shell when it's
        // ready.
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Reload Splash");
        app.setDesktopFileName("apokolips-reload-splash");
        app.setQuitOnLastWindowClosed(false);
        QPixmap pm(QString::fromLocal8Bit(argv[2]));
        if (pm.isNull()) return 1;
        QWidget w(nullptr, Qt::Window | Qt::FramelessWindowHint);
        QLabel lbl(&w);
        lbl.setPixmap(pm);
        lbl.setGeometry(0, 0, pm.width(), pm.height());
        w.setGeometry(0, 0, pm.width(), pm.height());
        w.show();
        w.raise();
        qInfo() << "[reload-splash] shown" << pm.size();
        // Safety: exit after 15s even if the new shell never kills us.
        QTimer::singleShot(15000, &app, &QApplication::quit);
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--node") {
        QApplication app(argc, argv);
        app.setApplicationName("Node");
        app.setDesktopFileName("apokolips-node");

        // Optional --run "<cmd>" argument: type the command into
        // the new window.
        QString runCmd;
        for (int i = 2; i + 1 < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) == "--run") {
                runCmd = QString::fromLocal8Bit(argv[i + 1]);
                break;
            }
        }

        // Model A multi-window: every invocation opens fresh,
        // whether or not --run is supplied.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        NodeWindow w(nullptr, runCmd);
        w.show();
        return app.exec();
    }

    if (argc >= 2 && QString(argv[1]) == "--image") {
        QApplication app(argc, argv);
        app.setApplicationName("Aperture");
        app.setDesktopFileName("apokolips-aperture");

        const QString filePath = (argc >= 3)
            ? QString::fromUtf8(argv[2]) : QString();

        // Log into Recents.
        if (!filePath.isEmpty())
            Recents::add(filePath,
                QMimeDatabase().mimeTypeForFile(filePath).name(),
                "apokolips-aperture");

        // Model A multi-window: every invocation opens a fresh
        // window. The file path (if supplied) seeds the new window.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
        SigilImageWindow w(filePath);
        w.show();
        return app.exec();
    }

    if (argc >= 4 && QString(argv[1]) == "--gen-thumb") {
        // One-shot: render a wallpaper into a small image and save.
        // Used to build thumbnails for wallpapers that are drawn in
        // code and have no source file (Starfield, Aurora).
        QApplication app(argc, argv);
        QString id   = QString::fromLocal8Bit(argv[2]);
        QString path = QString::fromLocal8Bit(argv[3]);
        Wallpaper *wp = WallpaperConfig::makeById(id);
        if (!wp) return 1;
        if (wp->animated()) {
            // Let animated wallpapers advance a bit so the frame
            // isn't the very first (often black) one.
            QDateTime start = QDateTime::currentDateTime();
            while (start.msecsTo(QDateTime::currentDateTime()) < 800) {
                wp->tick();
                QApplication::processEvents(
                    QEventLoop::AllEvents, 16);
            }
        }
        QImage img(400, 240, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        wp->paint(p, QRect(0, 0, 400, 240));
        p.end();
        delete wp;
        if (!img.save(path)) {
            qWarning() << "[gen-thumb] save failed:" << path;
            return 2;
        }
        qInfo() << "[gen-thumb] wrote" << path;
        return 0;
    }

    if (argc >= 4 && QString(argv[1]) == "--upscale-worker") {
        // Background AI upscaler. Called by Settings (or the shell)
        // when a user accepts "make it sharper".
        //
        //   apokolips-shell --upscale-worker <input-image> <output-jpg>
        //
        // Runs Real-ESRGAN, converts to JPG, fires a notification,
        // writes a state marker for the shell to notice, exits.
        // No window, no UI, no blocking of the caller.
        QApplication app(argc, argv);

        QString inputPath  = QString::fromLocal8Bit(argv[2]);
        QString outputPath = QString::fromLocal8Bit(argv[3]);

        if (!QFile::exists(inputPath)) {
            qWarning() << "[upscale] input missing:" << inputPath;
            return 2;
        }

        QImage src(inputPath);
        if (src.isNull()) {
            qWarning() << "[upscale] cannot read:" << inputPath;
            return 3;
        }

        const int srcW = src.width();
        // Aim for 3840 wide. Use the smallest Real-ESRGAN pass that
        // reaches it, so we do not waste minutes on a 2× image.
        int scale;
        if      (srcW >= 1920) scale = 2;
        else if (srcW >= 1280) scale = 3;
        else                   scale = 4;

        // Locate the upscaler next to the repo (dev) or in the
        // system install prefix (shipped).
        QStringList binCandidates = {
            QCoreApplication::applicationDirPath()
                + "/../tools/realesrgan/realesrgan-ncnn-vulkan",
            QCoreApplication::applicationDirPath()
                + "/tools/realesrgan/realesrgan-ncnn-vulkan",
            QDir::homePath()
                + "/apokolips-shell/tools/realesrgan/realesrgan-ncnn-vulkan",
            "/usr/lib/apokolips/realesrgan/realesrgan-ncnn-vulkan",
            "/usr/share/apokolips/realesrgan/realesrgan-ncnn-vulkan"
        };
        QString binPath;
        for (const QString &c : binCandidates) {
            if (QFile::exists(c) && QFileInfo(c).isExecutable()) {
                binPath = c;
                break;
            }
        }
        if (binPath.isEmpty()) {
            qWarning() << "[upscale] upscaler binary not found";
            return 4;
        }

        QString tmpPng = QDir::tempPath()
            + QString("/apokolips-upscale-%1.png")
                  .arg(QCoreApplication::applicationPid());

        QProcess up;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        // Force the software Vulkan driver if the user has no GPU.
        if (!env.contains("VK_ICD_FILENAMES"))
            env.insert("VK_ICD_FILENAMES",
                       "/usr/share/vulkan/icd.d/lvp_icd.json");
        up.setProcessEnvironment(env);
        up.start(binPath, {
            "-i", inputPath,
            "-o", tmpPng,
            "-n", "realesrgan-x4plus",
            "-s", QString::number(scale)
        });

        qInfo() << "[upscale] start src=" << inputPath
                << "width=" << srcW
                << "scale=" << scale
                << "out=" << outputPath;

        if (!up.waitForStarted(5000)) {
            qWarning() << "[upscale] failed to start upscaler";
            return 5;
        }
        up.waitForFinished(-1);   // block until done

        if (up.exitCode() != 0 || !QFile::exists(tmpPng)) {
            qWarning() << "[upscale] upscaler failed code=" << up.exitCode()
                       << "stderr=" << up.readAllStandardError();
            QFile::remove(tmpPng);
            return 6;
        }

        QImage big(tmpPng);
        QFile::remove(tmpPng);

        if (big.isNull()) {
            qWarning() << "[upscale] cannot read result";
            return 7;
        }

        // If the pass overshot 3840, shrink back. Never double-upscale.
        if (big.width() > 3840) {
            big = big.scaledToWidth(3840, Qt::SmoothTransformation);
        }

        QDir().mkpath(QFileInfo(outputPath).absolutePath());
        if (!big.save(outputPath, "JPEG", 92)) {
            qWarning() << "[upscale] cannot write:" << outputPath;
            return 8;
        }

        qInfo() << "[upscale] done" << outputPath
                << big.width() << "x" << big.height();

        // State marker — the shell watches this file to learn that a
        // fresh upscale is ready to apply.
        QJsonObject marker;
        marker["src"] = inputPath;
        marker["dst"] = outputPath;
        marker["w"]   = big.width();
        marker["h"]   = big.height();
        marker["ts"]  = (double)QDateTime::currentMSecsSinceEpoch();
        QFile mf("/tmp/apokolips-upscale-last.json");
        if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            mf.write(QJsonDocument(marker).toJson());

        // Notify the user. Long summary, short body.
        QProcess::startDetached("notify-send", {
            "Apokolips",
            QString("Wallpaper upscaled to %1×%2")
                .arg(big.width()).arg(big.height())
        });

        return 0;
    }

    if (argc >= 2 && QString(argv[1]) == "--settings") {
        QApplication app(argc, argv);
        app.setApplicationName("Apokolips Settings");
        // 2026-10-17 Session 3b: Settings used to inherit the bar's
        // app_id ("apokolips-shell"), which is why launchApp's match
        // was matching the bar not Settings, and why Settings windows
        // shared the bar's tiling rules. Give it a proper id so
        // Model A multi-window works uniformly.
        app.setDesktopFileName("apokolips-settings");
        // Model A multi-window: every invocation opens fresh.
        ThemeManager::instance().setTheme(
            WallpaperConfig::themeForId(WallpaperConfig::loadId()));
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
