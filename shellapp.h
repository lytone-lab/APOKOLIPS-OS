#ifndef APOKOLIPS_SHELLAPP_H
#define APOKOLIPS_SHELLAPP_H

// ShellApp — the base class every Apokolips app inherits.
// Owns: window setup, traffic lights, keyboard shortcuts,
// theme subscription, standard paint, drag-to-move.
// Subclasses override onThemeChanged() for custom restyling.

#include "shell.h"
#include "theme.h"
#include "style.h"
#include "shared_fwd.h"

#include <QShortcut>
#include <QHash>
#include <QCoreApplication>
#include <QPointer>

class ShellApp : public QWidget {
public:
    explicit ShellApp(QWidget *parent = nullptr) : QWidget(parent) {}

    // Call from subclass ctor. Sets window flags, subscribes to
    // theme, installs Ctrl+Q / Ctrl+W / Escape / Super+M.
    void setupShellApp(const QString &title,
                       const QSize   &preferred,
                       bool           embedded)
    {
        m_embedded      = embedded;
        m_preferredSize = preferred;

        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        if (embedded) {
            setWindowFlags(Qt::Widget);
        } else {
            setWindowTitle(title);
            setWindowFlags(Qt::FramelessWindowHint);
            setShellWindowGeometry(this, preferred);
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
                                  int size = 13)
    {
        QPushButton *b = new QPushButton;
        b->setFixedSize(size, size);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString(
            "QPushButton { background: %1;"
            "  border: 1px solid rgba(0,0,0,60);"
            "  border-radius: %2px; }"
            "QPushButton:hover { background: %3; }")
            .arg(idle, QString::number(size / 2), hover));
        return b;
    }

    // Builds the header with three traffic lights pre-wired:
    //   close -> shellCloseViaGenie(this)
    //   min   -> shellMinimizeFocusedWindow()
    //   max   -> toggle preferred size / fullscreen
    // Returns the hbox; caller adds its own widgets after.
    QHBoxLayout *buildStandardHeader(const QString &title,
                                     const QSize   &preferred,
                                     QLabel       **titleOut = nullptr,
                                     QPushButton  **maxBtnOut = nullptr)
    {
        QHBoxLayout *hdr = new QHBoxLayout;
        QPushButton *closeBtn = makeTrafficLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn   = makeTrafficLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn   = makeTrafficLight("#155c1e", "#28c840");
        hdr->addWidget(closeBtn);
        hdr->addWidget(minBtn);
        hdr->addWidget(maxBtn);
        hdr->addSpacing(10);

        QLabel *titleLabel = new QLabel(title);
        titleLabel->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;")
            .arg(Sigil::textPrimary().name()));
        hdr->addWidget(titleLabel);
        hdr->addStretch();

        QObject::connect(closeBtn, &QPushButton::clicked, [this]() {
            // Red light: minimize animation, then exit the process.
            // Without the quit, the app stays alive as a hidden
            // QWidget and the dock's running dot stays lit forever.
            shellCloseViaGenie(this);
            QTimer::singleShot(900, this, []() {
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
            QRect target = z
                ? shellWindowRect(preferred, QPoint(), true)
                : shellSafeArea();
            qInfo() << "[green] clicked zoomed=" << z
                    << "current=" << geometry()
                    << "target=" << target;
            snapShellResize(this, target);
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
        p.setRenderHint(QPainter::Antialiasing);
        QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath full;
        full.addRoundedRect(body, 14, 14);
        p.fillPath(full, Sigil::contentBg());
        p.setPen(QPen(Sigil::border(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(full);
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
};

#endif // APOKOLIPS_SHELLAPP_H
