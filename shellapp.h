#ifndef APOKOLIPS_SHELLAPP_H
#define APOKOLIPS_SHELLAPP_H

// ShellApp — the base class every Apokolips app inherits.
// Owns: window setup, traffic lights, keyboard shortcuts,
// theme subscription, standard paint, drag-to-move.
// Subclasses override onThemeChanged() for custom restyling.

#include "visualconfig.h"
#include "shell.h"
#include "theme.h"
#include "style.h"
#include "shared_fwd.h"

#include <QShortcut>
#include <QHash>
#include <QCoreApplication>
#include <QPointer>


// Cursors — platform-independent cursor pixmaps. Declared here so
// shellapp.h and settings.h (which compile before helpers.h) can
// use them.
namespace Cursors {
    inline QCursor makeMacCursor() {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath path;
        path.moveTo(2, 2);
        path.lineTo(2, 22);
        path.lineTo(8, 16);
        path.lineTo(14, 26);
        path.lineTo(18, 24);
        path.lineTo(12, 14);
        path.lineTo(20, 14);
        path.closeSubpath();

        p.setPen(QPen(Qt::white, 2, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::black);
        p.drawPath(path);
        p.end();

        return QCursor(pm, 2, 2);
    }
}

class ShellApp : public QWidget {
public:
    // ---- Per-app chrome override ----
    struct AppOverride {
        QString chromeStyle = "system";   // system|flat|glass|solid
        QString trafficSide = "left";     // left|right
        QString accent      = "inherit";  // inherit or #RRGGBB
        int     radius      = -1;         // -1 = default 14
    };

    // Maps an app's title (as passed to setupShellApp) to the
    // canonical id used as the key in cfg.appOverrides.
    static QString appIdForTitle(const QString &title) {
        if (title == "Files")      return "apokolips-files";
        if (title == "Aperture")   return "apokolips-aperture";
        if (title == "Notes")      return "apokolips-notes";
        if (title == "Node")       return "apokolips-node";
        if (title == "myMach")     return "apokolips-mymach";
        if (title == "BoomTube")   return "apokolips-boombute";
        if (title == "Calculator") return "apokolips-calculator";
        if (title == "Settings")   return "apokolips-settings";
        return QString();
    }

    explicit ShellApp(QWidget *parent = nullptr) : QWidget(parent) {}

    // Call from subclass ctor. Sets window flags, subscribes to
    // theme, installs Ctrl+Q / Ctrl+W / Escape / Super+M.
    void setupShellApp(const QString &title,
                       const QSize   &preferred,
                       bool           embedded)
    {
        m_embedded      = embedded;
        m_preferredSize = preferred;
        m_appId         = appIdForTitle(title);
        readOverride();

        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        if (embedded) {
            setWindowFlags(Qt::Widget);
        } else {
            setWindowTitle(title);
            setWindowFlags(Qt::FramelessWindowHint);
            setShellWindowGeometry(this, preferred);
        }

        if (VisualConfigManager::instance().cfg().cursorStyle == 1) {
            setCursor(Cursors::makeMacCursor());
        } else {
            unsetCursor();
        }

        if (VisualConfigManager::instance().cfg().cursorStyle == 1) {
            setCursor(Cursors::makeMacCursor());
        }

        m_appTheme = ThemeManager::instance().current();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_appTheme = t;
            onThemeChanged(t);
            update();
        });

        installStandardShortcuts();
        installMenuFade();
    }

protected:
    // Subclasses override for their own restyling.
    virtual void onThemeChanged(const Theme &) {}

    // Subclasses override for their own Escape behavior.
    // Default: hide (macOS-style).  BoomTube overrides to close.
    virtual void onEscape() { hide(); }

    // Subclasses override for their own Ctrl+W behavior.
    // Default: close this window.  Finder overrides to close-tab.
    virtual void onCloseWindow() { close(); }

    // Build a traffic-light button in the standard style.
    QPushButton *makeTrafficLight(const QString &idle,
                                  const QString &hover,
                                  const QString &glyph = QString(),
                                  int size = 13)
    {
        QPushButton *b = new QPushButton(glyph);
        b->setFixedSize(size, size);
        b->setCursor(Qt::PointingHandCursor);
        // Colour scheme: idle dark, hover bright, glyph invisible
        // at rest and dark on hover (macOS style).
        b->setStyleSheet(QString(
            "QPushButton { background: %1;"
            "  border: 1px solid rgba(0,0,0,60);"
            "  border-radius: %2px;"
            "  color: transparent;"
            "  font-size: %3px;"
            "  font-weight: bold;"
            "  padding: 0; }"
            "QPushButton:hover { background: %4;"
            "  color: rgba(0,0,0,180); }")
            .arg(idle,
                 QString::number(size / 2),
                 QString::number(qMax(9, size - 3)),
                 hover));
        return b;
    }

    // Builds the header with three traffic lights pre-wired:
    //   close -> shellCloseViaWarp(this)
    //   min   -> shellMinimizeFocusedWindow()
    //   max   -> toggle preferred size / fullscreen
    // Returns the hbox; caller adds its own widgets after.
    QHBoxLayout *buildStandardHeader(const QString &title,
                                     const QSize   &preferred,
                                     QLabel       **titleOut = nullptr,
                                     QPushButton  **maxBtnOut = nullptr)
    {
        QHBoxLayout *hdr = new QHBoxLayout;
        const auto &vc = VisualConfigManager::instance().cfg();
        hdr->setSpacing(vc.trafficLightSpacing);
        
        // macOS-style hover glyphs: × − + appear on hover.
        QString closeGlyph = vc.trafficLightHoverGlyphs ? QString::fromUtf8("\xC3\x97") : "";
        QString minGlyph   = vc.trafficLightHoverGlyphs ? QString::fromUtf8("\xE2\x88\x92") : "";
        QString maxGlyph   = vc.trafficLightHoverGlyphs ? "+" : "";
        
        QPushButton *closeBtn = makeTrafficLight("#7a2b25", "#ff5f57", closeGlyph, vc.trafficLightSize);
        QPushButton *minBtn   = makeTrafficLight("#7a5b18", "#febc2e", minGlyph, vc.trafficLightSize);
        QPushButton *maxBtn   = makeTrafficLight("#155c1e", "#28c840", maxGlyph, vc.trafficLightSize);
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;")
            .arg(Sigil::textPrimary().name()));

        if (m_override.trafficSide == "right") {
            hdr->addWidget(titleLabel);
            hdr->addStretch();
            hdr->addWidget(closeBtn);
            hdr->addWidget(minBtn);
            hdr->addWidget(maxBtn);
        } else {
            hdr->addWidget(closeBtn);
            hdr->addWidget(minBtn);
            hdr->addWidget(maxBtn);
            hdr->addSpacing(10);
            hdr->addWidget(titleLabel);
            hdr->addStretch();
        }

        QObject::connect(closeBtn, &QPushButton::clicked, [this]() {
            // Red light: minimize animation, then exit the process.
            // The quit timer MUST match the animation's actual
            // wall-clock duration, not a hardcoded 900ms — on
            // llvmpipe the animation runs at ~20fps, so a 1140ms
            // "duration" takes 2-3x longer visually. 900ms fires
            // mid-animation and the window disappears early.
            shellCloseViaWarp(this);
            const int animMs = qBound(128,
                VisualConfigManager::instance().cfg().warpDurationMs,
                5000);
            QTimer::singleShot(animMs * 3 + 1500, this, []() {
                QCoreApplication::quit();
            });
        });
        QObject::connect(minBtn, &QPushButton::clicked, []() {
            shellMinimizeFocusedWindow();
        });
        QObject::connect(maxBtn, &QPushButton::clicked,
                         [this, preferred]() {
            static QHash<ShellApp *, bool> s_zoomed;
            bool z = s_zoomed.value(this, false);
            qInfo() << "[green] clicked zoomed=" << z
                    << "current=" << geometry();
            if (z) {
                // Exit fullscreen — Qt restores the geometry that
                // was active before showFullScreen(). Reapply our
                // preferred size on the next event-loop tick so
                // the window lands where the shell expects.
                showNormal();
                // 50ms delay lets Sway finish its Wayland resize
                // before we request our custom geometry, preventing
                // the flickering/glitching loop.
                QTimer::singleShot(50, this, [this, preferred]() {
                    snapShellResize(this,
                        shellWindowRect(preferred, QPoint(), true));
                });
            } else {
                // Enter fullscreen natively. Qt + Wayland handle
                // the resize and the compositor natively hides
                // everything else (top bar, dock) because the
                // surface covers the whole screen.
                showFullScreen();
            }
            s_zoomed[this] = !z;
        });

        if (titleOut)  *titleOut  = titleLabel;
        if (maxBtnOut) *maxBtnOut = maxBtn;
        return hdr;
    }

    // Snap the shell window to a target rect.
    //
    // NOTE: we used to animate this via QPropertyAnimation on the
    // "geometry" property. That does not work on Wayland — each
    // animation frame is a resize *request* to the compositor, and
    // the compositor answers with a configure event. At 60 fps the
    // two sides talk over each other and the window visibly
    // oscillates. Direct setGeometry is one request; the compositor
    // ack's it once and we are done. Smooth motion for this case
    // requires an overlay-based approach (grab the pixmap, animate
    // the pixmap, snap the real window) — tracked separately.
    void snapShellResize(QWidget *w, const QRect &target)
    {
        if (!w) return;
        if (w->layout()) {
            w->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        }
        w->setGeometry(target);
    }

    // Read this app's override entry from visual.json. Called at
    // construction. Unknown or empty values fall back to defaults.
    void readOverride() {
        if (m_appId.isEmpty()) return;
        const auto &cfg = VisualConfigManager::instance().cfg();
        QVariant v = cfg.appOverrides.value(m_appId);
        if (!v.canConvert<QVariantMap>()) return;
        QVariantMap m = v.toMap();
        m_override.chromeStyle =
            m.value("chromeStyle", m_override.chromeStyle).toString();
        m_override.trafficSide =
            m.value("trafficSide", m_override.trafficSide).toString();
        m_override.accent      =
            m.value("accent", m_override.accent).toString();
        m_override.radius      =
            m.value("radius", m_override.radius).toInt();
    }

public:
    // The chrome renderer. Used by paintEvent and by the Settings
    // preview so both draw the *same* pixels. Static so the preview
    // can call it without constructing a ShellApp.
    static void paintChrome(QPainter &p, const QRect &r, bool embedded,
                            const AppOverride &ovr)
    {
        p.setRenderHint(QPainter::Antialiasing);
        int radius = ovr.radius >= 0 ? ovr.radius : 14;
        radius = qBound(0, radius, 32);

        QRectF body = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath full;
        full.addRoundedRect(body, radius, radius);
        p.fillPath(full, Sigil::contentBg());

        if (!embedded) {
            QString style = ovr.chromeStyle;
            if (style == "system") style = "glass";

            if (style == "glass") {
                const QPixmap &bg = ThemeManager::instance().blurredBg();
                if (!bg.isNull() && r.height() > 40 && r.width() > 40) {
                    p.save();
                    p.setClipPath(full);
                    constexpr int HDR = 40;
                    QRect srcRect(0, 0, bg.width(),
                        qMax(1, bg.height() * HDR / qMax(1, r.height())));
                    p.drawPixmap(QRect(0, 0, r.width(), HDR), bg, srcRect);
                    p.fillRect(0, 0, r.width(), HDR,
                               QColor(0, 0, 0, 95));
                    p.restore();
                }
            } else if (style == "solid") {
                p.save();
                p.setClipPath(full);
                // Distinct header band — lighter than the body so
                // the boundary is obvious, with a bottom hairline
                // to separate header from content.
                p.fillRect(0, 0, r.width(), 40, QColor(34, 34, 38));
                p.setPen(QPen(QColor(255, 255, 255, 40), 1));
                p.drawLine(0, 40, r.width(), 40);
                p.restore();
            }
            // "flat" = no header strip at all.
        }

        QColor borderCol = Sigil::border();
        if (ovr.accent != "inherit" && !ovr.accent.isEmpty()) {
            QColor a(ovr.accent);
            if (a.isValid()) borderCol = a;
        }
        p.setPen(QPen(borderCol, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(full);
    }

protected:
    void installStandardShortcuts()
    {
        auto add = [this](const QKeySequence &k,
                          std::function<void()> fn) {
            QShortcut *s = new QShortcut(k, this);
            QObject::connect(s, &QShortcut::activated, fn);
        };
        add(QKeySequence("Ctrl+Q"),
            []() { QCoreApplication::quit(); });
        add(QKeySequence("Ctrl+W"),
            [this]() { onCloseWindow(); });
        add(QKeySequence(Qt::Key_Escape),
            [this]() { onEscape(); });
        add(QKeySequence("Meta+M"),
            []() { shellMinimizeFocusedWindow(); });
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintChrome(p, rect(), m_embedded, m_override);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (!m_embedded && e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle())
                if (wh->startSystemMove()) { e->accept(); return; }
        }
        QWidget::mousePressEvent(e);
    }

    Theme m_appTheme;
    bool  m_embedded      = false;
    QSize m_preferredSize;
    QString m_appId;
    AppOverride m_override;
};

#endif // APOKOLIPS_SHELLAPP_H
