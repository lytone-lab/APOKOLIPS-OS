#ifndef APOKOLIPS_SETTINGS_H
#define APOKOLIPS_SETTINGS_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "helpers.h"

#include <QFileDialog>

class SettingsWindow : public QWidget {
public:
    explicit SettingsWindow(QWidget *parent = nullptr) : QWidget(parent) {
        StyleManager::load();   // <-- read saved style so buttons highlight correctly
        setWindowFlags(Qt::FramelessWindowHint);
        setWindowTitle("Apokolips Settings");
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        buildUi();

        // Don't build any page in the constructor — the window must
        // appear first. Page 0 loads on the next event-loop tick,
        // then the rest warm in the background. warmNextPage yields
        // between builds so the UI stays responsive while pages
        // populate in the background.
        m_warmIndex = 0;
        m_warmer = new QTimer(this);
        m_warmer->setInterval(30);
        QObject::connect(m_warmer, &QTimer::timeout,
                         this, &SettingsWindow::warmNextPage);
        QTimer::singleShot(0, this, [this]() { m_warmer->start(); });

        // Apply persisted text scale before choosing the final initial size.
        applyTextScale(VisualConfigManager::instance().cfg().shellTextScale);

        // Calculate the final geometry after the widget tree is complete,
        // before main() shows the window. This prevents a layout-driven
        // second resize after the first paint and keeps the dock unobstructed.
        if (QScreen *s = QApplication::primaryScreen()) {
            QRect usable = s->availableGeometry();
            constexpr int dockExtent = 96; // Dock's fixed thickness
            constexpr int bottomReserve = dockExtent + 14;
            constexpr int sideReserve = dockExtent + 14 + 8;
            const int dockPosition =
                VisualConfigManager::instance().cfg().dockPosition;
            if (dockPosition == 0) {       // Bottom dock
                usable.setHeight(usable.height() - bottomReserve);
            } else if (dockPosition == 1) { // Left dock
                usable.setLeft(usable.left() + sideReserve);
            } else if (dockPosition == 2) { // Right dock
                usable.setWidth(usable.width() - sideReserve);
            }

            if (usable.width() < 1 || usable.height() < 1)
                usable = s->geometry();

            const QSize minSize(qMin(880, usable.width()),
                                qMin(620, usable.height()));
            if (layout()) {
                layout()->setSizeConstraint(QLayout::SetNoConstraint);
                layout()->activate();
            }

            QSize initialSize(qMin(1100, usable.width()),
                              qMin(650, usable.height()));
            initialSize.setWidth(qMax(initialSize.width(), minSize.width()));
            initialSize.setHeight(qMax(initialSize.height(), minSize.height()));

            // Force the initial Wayland surface to be exactly
            // initialSize. Wayland honours set_min_size at the first
            // xdg_toplevel commit; a plain resize() before show() is
            // silently dropped, which caused Settings to appear at a
            // default size and then visibly grow.
            setMinimumSize(initialSize);
            resize(initialSize);
            move(usable.center() - QPoint(initialSize.width() / 2,
                                          initialSize.height() / 2));
            qInfo() << "[settings-geom] ctor set to" << initialSize;

            // Release the tight minimum once the surface is up so the
            // user can still resize freely.
            QTimer::singleShot(0, this, [this, minSize]() {
                setMinimumSize(minSize);
            });
        } else {
            setMinimumSize(880, 620);
            resize(1100, 700);
        }

        // Subscribe AFTER buildUi so restyleAll can see the widgets
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyleAll();
        });
        StyleManager::startPolling(this, [this]() { update(); });
    }

    // ---- Text scale: rewrites every widget's inline font-size ----
    void applyTextScale(double f) {
        if (f <= 0.0) f = 1.0;
        static const QRegularExpression re(
            "font-size:\\s*(\\d+(?:\\.\\d+)?)px");

        for (QWidget *w : findChildren<QWidget *>()) {
            QString css = w->styleSheet();
            if (css.isEmpty() || !css.contains("font-size:")) continue;

            // Snapshot original CSS once, so scaling is stable across calls
            QVariant orig = w->property("_sigilOrigCss");
            if (!orig.isValid()) {
                w->setProperty("_sigilOrigCss", css);
                orig = css;
            }
            QString base = orig.toString();

            QString out;
            int pos = 0;
            auto m = re.match(base);
            while (m.hasMatch()) {
                out += base.mid(pos, m.capturedStart() - pos);
                double px = m.captured(1).toDouble();
                int scaled = qMax(8, int(qRound(px * f)));
                out += QString("font-size: %1px").arg(scaled);
                pos = m.capturedEnd();
                m = re.match(base, pos);
            }
            out += base.mid(pos);
            w->setStyleSheet(out);
        }

        // Also scale the base font (affects labels without explicit size)
        QFont bf = qApp->font();
        bf.setPointSizeF(10.0 * f);
        qApp->setFont(bf);
        update();
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

    void showEvent(QShowEvent *e) override {
        QWidget::showEvent(e);
        // Geometry was finalised in the constructor via setMinimumSize
        // so the surface maps at the correct size on the first frame.
        // No post-show resize — that used to race the constructor and
        // the layout, causing visible small-then-big jumps.
    }

    void changeEvent(QEvent *e) override {
        QWidget::changeEvent(e);
        // Sway may reposition/resize us when a window comes back from
        // scratchpad after genie minimize. Re-apply our dock-aware
        // geometry on every state change so we do not end up covering
        // the dock.
        if (e->type() == QEvent::WindowStateChange
            || e->type() == QEvent::ActivationChange
            || e->type() == QEvent::Show) {
            QTimer::singleShot(0, this, [this]() { reapplyShellGeometry(); });
        }
    }

    void reapplyShellGeometry() {
        if (QScreen *s = QApplication::primaryScreen()) {
            QRect usable = s->availableGeometry();
            constexpr int dockExtent = 96;
            constexpr int bottomReserve = dockExtent + 14;
            constexpr int sideReserve = dockExtent + 14 + 8;
            const int dockPosition = VisualConfigManager::instance()
                                        .cfg().dockPosition;
            if (dockPosition == 0)
                usable.setHeight(usable.height() - bottomReserve);
            else if (dockPosition == 1)
                usable.setLeft(usable.left() + sideReserve);
            else if (dockPosition == 2)
                usable.setWidth(usable.width() - sideReserve);
            if (usable.width() < 1 || usable.height() < 1)
                usable = s->geometry();

            QSize target(qMin(1100, usable.width()),
                         qMin(650, usable.height()));

            // Already correct? Do nothing. This used to fire on every
            // activation / show event and generate a redundant Wayland
            // resize request — visible as a small flicker.
            if (size() == target) return;

            qInfo() << "[settings-geom] reapply"
                    << size() << "->" << target;
            resize(target);
            move(usable.center()
                 - QPoint(target.width() / 2, target.height() / 2));
        }
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // Draw the rounded body directly (not via clip) so the outer
        // antialiased edge blends to the wallpaper, not to black.
        QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath full;
        full.addRoundedRect(body, 14, 14);

        p.fillPath(full, Sigil::contentBg());

        // Sidebar overlay — clipped to rounded corners
        if (m_sidebar) {
            p.save();
            p.setClipPath(full);
            p.fillRect(QRect(0, 0, m_sidebar->width(), height()),
                       Sigil::sidebarBg());
            p.restore();
        }

        // Header strip — clipped to rounded corners
        p.save();
        p.setClipPath(full);
        p.fillRect(QRect(0, 0, width(), 42), Sigil::headerBg());
        p.restore();

        // Separators
        p.setPen(QPen(Sigil::border(), 1));
        p.drawLine(0, 42, width(), 42);
        if (m_sidebar)
            p.drawLine(m_sidebar->width(), 42,
                       m_sidebar->width(), height() - 1);

        // --- AA saturation: stroke the rounded outline with opaque
        // colors so antialiased edge pixels never fade to transparent
        // (which sway composites over black).
        {
            QPen satPen;
            satPen.setWidthF(1.6);
            satPen.setJoinStyle(Qt::RoundJoin);
            satPen.setCapStyle(Qt::RoundCap);
            p.setBrush(Qt::NoBrush);

            // 1) Left column — sidebar color
            if (m_sidebar) {
                satPen.setColor(Sigil::sidebarBg());
                p.setPen(satPen);
                p.save();
                p.setClipRect(QRect(0, 0,
                                    m_sidebar->width(),
                                    height()));
                p.setClipPath(full, Qt::IntersectClip);
                p.drawPath(full);
                p.restore();
            }

            // 2) Top strip — header color
            satPen.setColor(Sigil::headerBg());
            p.setPen(satPen);
            p.save();
            p.setClipRect(QRect(0, 0, width(), 42));
            p.setClipPath(full, Qt::IntersectClip);
            p.drawPath(full);
            p.restore();

            // 3) Right side + bottom — content color
            satPen.setColor(Sigil::contentBg());
            p.setPen(satPen);
            p.save();
            int sx = m_sidebar ? m_sidebar->width() : 0;
            p.setClipRect(QRect(sx, 42,
                                width() - sx, height() - 42));
            p.setClipPath(full, Qt::IntersectClip);
            p.drawPath(full);
            p.restore();
        }

        // Window outline (drawn last so the border stays crisp).
        // Use an INSET path so the 1px pen stays fully inside the
        // opaque body — drawing on `full` directly bled the outer
        // 0.5px of the pen onto transparent corner pixels, which
        // the compositor rendered as a black fringe.
        QRectF borderRect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, 13, 13);
        p.setPen(QPen(Sigil::border(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(borderPath);
    }

private:
    void buildUi() {
        QVBoxLayout *root = new QVBoxLayout(this);
        // Bottom margin = rounded-corner radius. Keeps child
        // widgets from painting their opaque backgrounds over
        // the bottom corners of the rounded body.
        root->setContentsMargins(0, 0, 0, 16);
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
                         [this]() { shellCloseViaGenie(this); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         []() { shellMinimizeFocusedWindow(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            // Cage supports client-side resize; use that for zoom.
            static bool zoomed = false;
            if (zoomed)
                setShellWindowGeometry(this, QSize(920, 620));
            else
                setGeometry(shellSafeArea());
            zoomed = !zoomed;
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
        m_sidebar->setFixedWidth(220);
        // The parent paints the sidebar color inside its rounded window path.
        // Keep this child surface transparent so it cannot square off the
        // parent's lower-left alpha corner.
        m_sidebar->setAutoFillBackground(false);

        QVBoxLayout *sidebarWrap = new QVBoxLayout(m_sidebar);
        sidebarWrap->setContentsMargins(0, 0, 0, 0);
        sidebarWrap->setSpacing(0);

        QScrollArea *sidebarScroll = new QScrollArea;
        sidebarScroll->setWidgetResizable(true);
        sidebarScroll->setFrameShape(QFrame::NoFrame);
        sidebarScroll->setHorizontalScrollBarPolicy(
            Qt::ScrollBarAlwaysOff);
        sidebarScroll->setVerticalScrollBarPolicy(
            Qt::ScrollBarAsNeeded);
        sidebarScroll->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical {"
            "  background: transparent; width: 8px; margin: 2px; }"
            "QScrollBar::handle:vertical {"
            "  background: rgba(255,255,255,50);"
            "  border-radius: 4px; min-height: 24px; }"
            "QScrollBar::handle:vertical:hover {"
            "  background: rgba(255,255,255,80); }"
            "QScrollBar::add-line:vertical,"
            "QScrollBar::sub-line:vertical { height: 0; background: transparent; }"
            "QScrollBar::add-page:vertical,"
            "QScrollBar::sub-page:vertical { background: transparent; }");

        QWidget *sidebarContent = new QWidget;
        sidebarContent->setAutoFillBackground(false);
        sidebarContent->setStyleSheet(
            "QWidget { background: transparent; }");

        QVBoxLayout *sb = new QVBoxLayout(sidebarContent);
        sb->setContentsMargins(10, 12, 10, 12);
        sb->setSpacing(2);

        Sigil::SearchBar *search = new Sigil::SearchBar;
        search->setPlaceholderText("Search");
        sb->addWidget(search);
        sb->addSpacing(10);

        // Filter sidebar rows live as the user types
        QObject::connect(search, &QLineEdit::textChanged,
                         [this](const QString &q) {
            QString query = q.trimmed().toLower();
            for (Sigil::SidebarRow *row : m_sidebarRows) {
                if (!row) continue;
                bool show = query.isEmpty()
                    || row->label().toLower().contains(query);
                row->setVisible(show);
            }
        });

        m_stack = new QStackedWidget;
        m_stack->setAutoFillBackground(false);
        m_stack->setStyleSheet(
            "QStackedWidget { background: transparent; border: none; }");

        addSection(sb, "Appearance", QString::fromUtf8("\xE2\x9C\xA8"),
                   QColor(122, 106, 159), [this]() { return makeAppearancePage(); });
        addSection(sb, "Control Center", QString::fromUtf8("\xE2\x98\xB0"),
                   QColor(90, 122, 158), [this]() { return makeControlCenterPage(); });
        addSection(sb, "General", QString::fromUtf8("\xE2\x9A\x99"),
                   QColor(106, 106, 112), [this]() { return makeGeneralPage(); });
        addSection(sb, "OmniStorage", QString::fromUtf8("\xF0\x9F\x92\xBE"),
                   QColor(74, 122, 122), [this]() { return makeStoragePage(); },
                   Sigil::drawOmniStorage);
        addSection(sb, "Core Matrix", QString::fromUtf8("\xF0\x9F\x94\x97"),
                   QColor(168, 50, 50), [this]() { return makeCoreMatrixPage(); });
        addSection(sb, "Software Update",
                   QString::fromUtf8("\xE2\x86\xBB"),
                   QColor(74, 122, 90), [this]() { return makeSoftwareUpdatePage(); });
        addSection(sb, "Time Machine",
                   QString::fromUtf8("\xF0\x9F\x95\xB0"),
                   QColor(80, 120, 200), [this]() { return makeTimeMachinePage(); });
        addSection(sb, "Accessibility",
                   QString::fromUtf8("\xF0\x9F\xA7\xBF"),
                   QColor(74, 122, 158), [this]() { return makeAccessibilityPage(); });
        addSection(sb, "Privacy & Security",
                   QString::fromUtf8("\xF0\x9F\x9B\xA1"),
                   QColor(168, 50, 50), [this]() { return makePrivacyPage(); });
        addSection(sb, "Search",
                   QString::fromUtf8("\xF0\x9F\x94\x8D"),
                   QColor(80, 120, 200), [this]() { return makeSearchPage(); });

        addSection(sb, "Launcher & Spotlight",

                   QString::fromUtf8("\xF0\x9F\x94\x8D"),

                   QColor(168, 50, 50), [this]() { return makeLauncherSettingsPage(); });
        addSection(sb, "Focus",
                   QString::fromUtf8("\xF0\x9F\x8C\x99"),
                   QColor(122, 106, 159), [this]() { return makeFocusPage(); });
        addSection(sb, "Printers & Scanners",
                   QString::fromUtf8("\xF0\x9F\x96\xA8"),
                   QColor(166, 122, 90), [this]() { return makePrintersPage(); });
        addSection(sb, "Screensaver",
                   QString::fromUtf8("\xF0\x9F\x8C\x8C"),
                   QColor(90, 122, 158), [this]() { return makeScreensaverPage(); });
        addSection(sb, "Wallpaper",  QString::fromUtf8("\xE2\x96\xA3"),
                   QColor(90, 122, 158), [this]() { return makeWallpaperPage(); });
        addSection(sb, "Dock",       QString::fromUtf8("\xE2\x96\xAA"),
                   QColor(166, 122, 90), [this]() { return makeDockPage(); });
        addSection(sb, "Sound",      QString::fromUtf8("\xE2\x99\xAA"),
                   QColor(160, 90, 106), [this]() { return makeSoundPage(); });
        addSection(sb, "Display",    QString::fromUtf8("\xE2\x96\xA3"),
                   QColor(74, 122, 122), [this]() { return makeDisplayPage(); });
        addSection(sb, "Arc Reactor", QString::fromUtf8("\xE2\x9A\xA1"),
                   QColor(74, 122, 90), [this]() { return makeArcReactorPage(); },
                   Sigil::drawArcReactor);
        addSection(sb, "Bluetooth",  QString::fromUtf8("\xE2\x9C\xA6"),
                   QColor(90, 122, 158), [this]() { return makeBluetoothPage(); });
        addSection(sb, "Wi-Fi",      QString::fromUtf8("\xE2\x98\x81"),
                   QColor(90, 122, 158), [this]() { return makeWiFiPage(); });
        addSection(sb, "Network",    QString::fromUtf8("\xE2\x98\x81"),
                   QColor(74, 122, 90), [this]() { return makeNetworkPage(); });
        addSection(sb, "Users & Groups",
                   QString::fromUtf8("\xF0\x9F¥"),
                   QColor(122, 106, 159), [this]() { return makeUsersPage(); });
        addSection(sb, "Lock Screen & Security",
                   QString::fromUtf8("\xF0\x9F"),
                   QColor(90, 122, 158), [this]() { return makeLockScreenPage(); });
        addSection(sb, "Keyboard",
                   QString::fromUtf8("\xE2\x8C\xA8"),
                   QColor(122, 106, 159), [this]() { return makeKeyboardPage(); });
        addSection(sb, "Mouse & Trackpad",
                   QString::fromUtf8("\xF0\x9F\x96\xB1"),
                   QColor(166, 122, 90), [this]() { return makeMousePage(); });
        addSection(sb, "Notifications",
                   QString::fromUtf8("\xF0\x9F\x94\x94"),
                   QColor(200, 100, 60), [this]() { return makeNotificationsPage(); });
        addSection(sb, "Date & Time",
                   QString::fromUtf8("\xF0\x9F\x95\x90"),
                   QColor(80, 120, 200), [this]() { return makeDateTimePage(); });
        addSection(sb, "myMach",
                   QString::fromUtf8("\xF0\x9F\x93\x8A"),
                   QColor(80, 120, 200),
                   [this]() { return makeMyMachPage(); });
        addSection(sb, "About",      QString::fromUtf8("\xE2\x84\xB9"),
                   QColor(106, 106, 112), [this]() { return makeAboutPage(); });

        sb->addStretch();
        sidebarScroll->setWidget(sidebarContent);
        sidebarScroll->setAutoFillBackground(false);
        sidebarScroll->setAttribute(Qt::WA_NoSystemBackground, true);
        sidebarScroll->viewport()->setAutoFillBackground(false);
        sidebarScroll->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
        sidebarScroll->viewport()->setStyleSheet("background: transparent;");
        sidebarWrap->addWidget(sidebarScroll);

        // The parent paints this same content color within its rounded path.
        // Leave the stack transparent so it does not cover the lower-right
        // alpha corner with a rectangular child surface.

        body->addWidget(m_sidebar);
        body->addWidget(m_stack, 1);
        root->addLayout(body, 1);

        restyleAll();
    }

    void addSection(QVBoxLayout *sb, const QString &name,
                    const QString &glyph, const QColor &color,
                    std::function<QWidget*()> pageFactory,
                    std::function<void(QPainter&, QRectF)> customPaint = nullptr)
    {
        Sigil::SidebarRow *row = new Sigil::SidebarRow(
            name, glyph, color, nullptr, customPaint);
        m_sidebarRows.append(row);
        sb->addWidget(row);

        // Wrap every page in a scroll area so content stays reachable
        // even when the window is small.
        QScrollArea *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // The SettingsWindow parent paints the opaque content color inside
        // its rounded surface. Keep the scroll wrapper and page transparent
        // so they cannot cover the rounded lower-right alpha corner.
        scroll->setAutoFillBackground(false);
        scroll->setAttribute(Qt::WA_NoSystemBackground, true);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
        scroll->viewport()->setStyleSheet("background: transparent;");
        scroll->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical {"
            "  background: transparent; width: 10px; margin: 2px; }"
            "QScrollBar::handle:vertical {"
            "  background: rgba(255,255,255,60);"
            "  border-radius: 4px; min-height: 24px; }"
            "QScrollBar::handle:vertical:hover {"
            "  background: rgba(255,255,255,90); }"
            "QScrollBar::handle:vertical:pressed {"
            "  background: rgba(255,255,255,130); }"
            "QScrollBar::add-line:vertical,"
            "QScrollBar::sub-line:vertical { height: 0; background: transparent; }"
            "QScrollBar::add-page:vertical,"
            "QScrollBar::sub-page:vertical { background: transparent; }");

        // Placeholder until the first click — nothing is built yet.
        QWidget *placeholder = new QWidget;
        placeholder->setAutoFillBackground(false);
        scroll->setWidget(placeholder);

        int idx = m_stack->addWidget(scroll);
        if (m_stack->count() == 1) {
            row->setSelected(true);
        }

        m_pageScrolls.append(scroll);
        m_pageFactories.append(pageFactory);

        row->onActivate = [this, idx]() {
            ensurePageBuilt(idx);
            for (Sigil::SidebarRow *r : m_sidebarRows)
                r->setSelected(false);
            m_sidebarRows[idx]->setSelected(true);
            m_stack->setCurrentIndex(idx);
        };
    }

    // Build the page for a given section on demand, cache it, and swap
    // it into the scroll area in place of the placeholder.
    void ensurePageBuilt(int idx) {
        if (idx < 0 || idx >= m_pageFactories.size()) return;
        if (m_pageCache.contains(idx)) return;
        QWidget *page = m_pageFactories[idx]();
        if (!page) return;
        page->setAutoFillBackground(false);
        page->setStyleSheet("QWidget { background: transparent; }");
        QScrollArea *scroll = m_pageScrolls[idx];
        QWidget *old = scroll->takeWidget();
        if (old) old->deleteLater();
        scroll->setWidget(page);
        m_pageCache.insert(idx, page);
    }

    // Background warmer — builds one page every 60 ms while the window
    // is visible so idle time gets used and first click is instant.
    void warmNextPage() {
        if (!isVisible()) return;
        if (m_warmIndex >= m_pageFactories.size()) {
            if (m_warmer) m_warmer->stop();
            return;
        }
        ensurePageBuilt(m_warmIndex);
        ++m_warmIndex;

        // Hand the event loop back after each page so the UI can
        // repaint and respond to clicks between background builds.
        QCoreApplication::processEvents();
    }

    QWidget *makeAppearancePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(20);

        v->addWidget(Sigil::sectionTitle("Appearance"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Transparency card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(64 + 6 * 56);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("CHROME OPACITY"));

            auto addRow = [&](const QString &title, const QString &sub,
                              int val, int lo, int hi,
                              std::function<void(int)> onChange) {
                QWidget *row = new QWidget;
                row->setAutoFillBackground(false);
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(4);

                QHBoxLayout *titleRow = new QHBoxLayout;
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px;"
                    " background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(sub);
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                titleRow->addWidget(t);
                titleRow->addStretch();
                titleRow->addWidget(s);
                rv->addLayout(titleRow);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(lo, hi);
                sl->setValue(val);
                sl->setStyleSheet(QString(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgba(255,255,255,180);"
                    "  border-radius: 2px; }"));
                m_sliders.append(sl);
                // Debounce rapid slider changes. Every tick was
                // calling setAndSave → firing all VisualConfig
                // subscribers → full-scene repaint of top bar, dock,
                // panels. Dragging fired that 30×/second and stuttered.
                // Now the save fires once, 120 ms after the user pauses.
                // Works for both mouse drag and keyboard arrows.
                {
                    QTimer *debounce = new QTimer(sl);
                    debounce->setSingleShot(true);
                    debounce->setInterval(120);
                    QObject::connect(sl, &QSlider::valueChanged,
                                     debounce,
                                     qOverload<>(&QTimer::start));
                    QObject::connect(debounce, &QTimer::timeout,
                                     [sl, onChange]() {
                        onChange(sl->value());
                    });
                }
                rv->addWidget(sl);

                cv->addWidget(row);
            };

            addRow("Blur strength", "Higher = heavier frosted glass",
                   int(vc.blurStrength * 100), 0, 100,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) { c.blurStrength = pct / 100.0; });
            });
            addRow("Top bar", "How opaque the top strip looks",
                   vc.topBarAlpha, 0, 255,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) { c.topBarAlpha = pct; });
            });
            addRow("Dock", "Dock background opacity",
                   vc.dockAlpha, 0, 255,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) { c.dockAlpha = pct; });
            });
            addRow("Panels", "Launcher + Control Center",
                   vc.launcherAlpha, 0, 255,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) {
                        c.launcherAlpha = pct;
                        c.controlCenterAlpha = pct;
                    });
            });
            addRow("Widgets", "Desktop widget cards",
                   vc.widgetCardAlpha, 0, 255,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) { c.widgetCardAlpha = pct; });
            });
            addRow("Finder", "File browser window",
                   vc.finderAlpha, 0, 255,
                   [](int pct) {
                VisualConfigManager::instance().setAndSave(
                    [pct](VisualConfig &c) { c.finderAlpha = pct; });
            });

            v->addWidget(c);
        }

        // ---- UI Style card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(110);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            cv->addWidget(Sigil::groupLabel("UI STYLE"));

            QGridLayout *g = new QGridLayout;
            g->setSpacing(10);
            struct StyleOpt { VisualStyle s; const char *name; };
            const StyleOpt opts[] = {
                { VisualStyle::Glass,         "Glass"         },
                { VisualStyle::Neumorphism,   "Neumorphism"   },
                { VisualStyle::Skeuomorphism, "Skeuomorphism" },
                { VisualStyle::Claymorphism,  "Claymorphism"  }
            };
            int col = 0;
            for (const auto &o : opts) {
                QPushButton *b = new QPushButton(o.name);
                b->setCursor(Qt::PointingHandCursor);
                b->setFixedHeight(46);
                b->setProperty("styleId", static_cast<int>(o.s));
                m_styleButtons.append(b);
                QObject::connect(b, &QPushButton::clicked,
                                 [this, s = o.s]() {
                    StyleManager::set(s);
                    restyleStyleButtons();
                });
                g->addWidget(b, 0, col++);
            }
            cv->addLayout(g);
            v->addWidget(c);
        }

        // ---- Fonts card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("FONTS"));

            // Async: fire fc-list once, fill both dropdowns on finish.
            // Shared loader struct keeps callback + async handler in
            // sync — pointers persist for the card's lifetime.
            struct FontsLoader {
                QStringList uiOptions{ "(Qt default)" };
                QStringList monoOptions{ "(Qt default)" };
                Sigil::Dropdown *uiDD = nullptr;
                Sigil::Dropdown *monoDD = nullptr;
            };
            auto *fl = new FontsLoader;

            // System font
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel("System font");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(
                    "Used across the Apokolips shell and Qt windows");
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    fl->uiOptions, 0,
                    [fl](int i) {
                        if (i < 0 || i >= fl->uiOptions.size()) return;
                        QString fam = (i == 0)
                            ? QString()
                            : fl->uiOptions.at(i);
                        VisualConfigManager::instance().setAndSave(
                            [fam](VisualConfig &c) {
                                c.shellFontFamily = fam; });
                        QFont f = qApp->font();
                        if (!fam.isEmpty()) f.setFamily(fam);
                        qApp->setFont(f);
                    });
                h->addWidget(dd);
                cv->addWidget(row);
                fl->uiDD = dd;
            }

            // Monospace font
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel("Monospace font");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(
                    "Used for code, terminals, and fixed-width text");
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    fl->monoOptions, 0,
                    [fl](int i) {
                        if (i < 0 || i >= fl->monoOptions.size()) return;
                        QString fam = (i == 0)
                            ? QString()
                            : fl->monoOptions.at(i);
                        VisualConfigManager::instance().setAndSave(
                            [fam](VisualConfig &c) {
                                c.shellMonoFontFamily = fam; });
                    });
                h->addWidget(dd);
                cv->addWidget(row);
                fl->monoDD = dd;
            }

            // Fire fc-list async now that both dropdowns exist
            {
                auto *pc = new QProcess(this);
                QObject::connect(pc,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [pc, fl]() {
                        QStringList fams = QString::fromUtf8(
                            pc->readAllStandardOutput())
                            .split('\n', Qt::SkipEmptyParts);
                        pc->deleteLater();
                        fl->uiOptions = { "(Qt default)" };
                        fl->uiOptions += fams;
                        fl->monoOptions = { "(Qt default)" };
                        for (const QString &f : fams) {
                            if (f.contains("Mono", Qt::CaseInsensitive)
                                || f.contains("Code", Qt::CaseInsensitive)
                                || f.contains("Fixed", Qt::CaseInsensitive))
                                fl->monoOptions << f;
                        }
                        auto &vc2 =
                            VisualConfigManager::instance().cfg();
                        if (fl->uiDD) {
                            int idx = 0;
                            if (!vc2.shellFontFamily.isEmpty()) {
                                int i = fl->uiOptions.indexOf(
                                    vc2.shellFontFamily);
                                if (i >= 0) idx = i;
                            }
                            fl->uiDD->setItemsSilent(fl->uiOptions, idx);
                        }
                        if (fl->monoDD) {
                            int idx = 0;
                            if (!vc2.shellMonoFontFamily.isEmpty()) {
                                int i = fl->monoOptions.indexOf(
                                    vc2.shellMonoFontFamily);
                                if (i >= 0) idx = i;
                            }
                            fl->monoDD->setItemsSilent(
                                fl->monoOptions, idx);
                        }
                    });
                pc->start("bash", { "-c",
                    "fc-list : family | tr ',' '\\n' | sed 's/^ *//' "
                    "| sort -u | grep -v '^$'" });
            }

            // Base size slider
            {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Base font size");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(
                    QString("%1 pt").arg(vc.shellBaseFontPt));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(50);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(8, 18);
                sl->setValue(vc.shellBaseFontPt);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [val](int x) {
                    val->setText(QString("%1 pt").arg(x));
                    VisualConfigManager::instance().setAndSave(
                        [x](VisualConfig &c) {
                            c.shellBaseFontPt = x; });
                    QFont f = qApp->font();
                    f.setPointSizeF(double(x));
                    qApp->setFont(f);
                });
                rv->addWidget(sl);
                cv->addWidget(row);
            }

            QLabel *hint = new QLabel(
                "Changes apply live to the Settings window. Other apps "
                "pick them up on next launch.");
            hint->setWordWrap(true);
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        // ---- Reset card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(74);
            QHBoxLayout *cv = new QHBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Reset to defaults");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Wipe every visual tweak back to factory");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            cv->addLayout(left, 1);

            QPushButton *reset = new QPushButton("Reset");
            reset->setCursor(Qt::PointingHandCursor);
            reset->setFixedHeight(32);
            reset->setStyleSheet(
                "QPushButton { background: transparent;"
                "  color: rgb(168, 50, 50);"
                "  border: 1px solid rgb(168, 50, 50);"
                "  border-radius: 8px; padding: 4px 18px;"
                "  font-size: 12px; }"
                "QPushButton:hover { background: rgba(168,50,50,70);"
                "  color: #ffffff; }");
            QObject::connect(reset, &QPushButton::clicked, [this]() {
                VisualConfigManager::instance().resetToDefaults();
                StyleManager::set(VisualStyle::Glass);
                auto &c = VisualConfigManager::instance().cfg();
                for (int i = 0; i < m_sliders.size() && i < 6; ++i) {
                    static const int vals[] = {
                        -1, 0, 0, 0, 0, 0
                    };
                    Q_UNUSED(vals);
                }
                if (m_sliders.size() >= 6) {
                    m_sliders[0]->setValue(int(c.blurStrength * 100));
                    m_sliders[1]->setValue(c.topBarAlpha);
                    m_sliders[2]->setValue(c.dockAlpha);
                    m_sliders[3]->setValue(c.launcherAlpha);
                    m_sliders[4]->setValue(c.widgetCardAlpha);
                    m_sliders[5]->setValue(c.finderAlpha);
                }
                restyleStyleButtons();
            });
            cv->addWidget(reset);
            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeGeneralPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        auto &vc = VisualConfigManager::instance().cfg();

        QStackedWidget *hub = new QStackedWidget;
        hub->setStyleSheet("QStackedWidget { background: transparent; }");

        // ---------- Hub home ----------
        QWidget *home = new QWidget;
        home->setAutoFillBackground(false);
        QVBoxLayout *hv = new QVBoxLayout(home);
        hv->setContentsMargins(0, 0, 0, 0);
        hv->setSpacing(18);

        hv->addWidget(Sigil::sectionTitle("General"));

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(4, 8, 4, 8);
            cv->setSpacing(0);

            auto addNavRow = [&](const QString &title,
                                 const QString &sub,
                                 std::function<void()> onClick) {
                QPushButton *b = new QPushButton;
                b->setCursor(Qt::PointingHandCursor);
                b->setMinimumHeight(60);
                b->setStyleSheet(QString(
                    "QPushButton {"
                    "  background: transparent;"
                    "  border: none;"
                    "  border-radius: 8px;"
                    "  text-align: left;"
                    "  padding: 0;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,20); }"));

                QHBoxLayout *h = new QHBoxLayout(b);
                h->setContentsMargins(18, 10, 18, 10);
                h->setSpacing(14);

                QVBoxLayout *l = new QVBoxLayout;
                l->setContentsMargins(0, 0, 0, 0);
                l->setSpacing(2);
                QLabel *t = new QLabel(title);
                t->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px;"
                    " background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(sub);
                s->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                l->addWidget(t);
                l->addWidget(s);
                h->addLayout(l, 1);

                QLabel *chev = new QLabel(
                    QString::fromUtf8("\xE2\x80\xBA"));
                chev->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                chev->setStyleSheet(QString(
                    "color: %1; font-size: 18px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                h->addWidget(chev);

                QObject::connect(b, &QPushButton::clicked, onClick);
                cv->addWidget(b);
            };

            addNavRow("Startup Items",
                "Apps and services that launch at login",
                [hub]() { hub->setCurrentIndex(1); });
            addNavRow("Language & Region",
                "System language and formats",
                [hub]() { hub->setCurrentIndex(2); });
            addNavRow("Transfer / Reset",
                "Migrate or erase this machine",
                [hub]() { hub->setCurrentIndex(3); });

            hv->addWidget(c);
        }
        hv->addStretch();

        // ---------- Back button header helper ----------
        auto makeBackHeader = [hub, home](const QString &title) {
            QWidget *w = new QWidget;
            QVBoxLayout *v = new QVBoxLayout(w);
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(6);

            QPushButton *back = new QPushButton(
                QString::fromUtf8("\xE2\x80\xB9  General"));
            back->setCursor(Qt::PointingHandCursor);
            back->setFixedHeight(26);
            back->setMinimumWidth(110);
            back->setStyleSheet(QString(
                "QPushButton { background: transparent;"
                "  color: %1; border: none;"
                "  font-size: 12px; padding: 2px 0;"
                "  text-align: left; }"
                "QPushButton:hover { color: %2; }")
                .arg(Sigil::textDim().name(),
                     Sigil::textPrimary().name()));
            QObject::connect(back, &QPushButton::clicked,
                             [hub, home]() {
                hub->setCurrentWidget(home);
            });
            v->addWidget(back, 0, Qt::AlignLeft);
            v->addWidget(Sigil::sectionTitle(title));
            return w;
        };

        // ---------- Startup Items sub-page ----------
        QWidget *startup = new QWidget;
        {
            QVBoxLayout *sv = new QVBoxLayout(startup);
            sv->setContentsMargins(0, 0, 0, 0);
            sv->setSpacing(18);
            sv->addWidget(makeBackHeader("Startup Items"));

            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->addWidget(Sigil::groupLabel("AUTOSTART ENTRIES"));
            hdr->addStretch();
            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             [this]() { refreshStartupItems(); });
            hdr->addWidget(refresh);

            QPushButton *openDir = new QPushButton("Open Folder");
            openDir->setCursor(Qt::PointingHandCursor);
            openDir->setFixedHeight(26);
            openDir->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(openDir, &QPushButton::clicked, []() {
                QString dir = QDir::homePath() + "/.config/autostart";
                QDir().mkpath(dir);
                QProcess::startDetached("xdg-open", { dir });
            });
            hdr->addWidget(openDir);

            QPushButton *addItem = new QPushButton("Add Item…");
            addItem->setCursor(Qt::PointingHandCursor);
            addItem->setFixedHeight(26);
            addItem->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(addItem, &QPushButton::clicked,
                             [this]() {
                bool ok = false;
                QString path = ApokolipsInputDialog::getText(
                    this, "Add Startup Item",
                    "Path to a program to launch at login:",
                    "/usr/bin/", &ok);
                if (!ok || path.isEmpty()) return;
                if (!QFile::exists(path)) return;

                QFileInfo fi(path);
                QString base = fi.fileName();
                if (base.isEmpty()) return;

                QString dir = QDir::homePath() + "/.config/autostart";
                QDir().mkpath(dir);

                QString desktopPath = dir + "/" + base + ".desktop";
                QFile f(desktopPath);
                if (!f.open(QIODevice::WriteOnly
                            | QIODevice::Truncate)) {
                    return;
                }
                QString content =
                    "[Desktop Entry]\n"
                    "Type=Application\n"
                    "Name=" + base + "\n"
                    "Exec=" + path + "\n"
                    "Terminal=false\n"
                    "X-GNOME-Autostart-enabled=true\n";
                f.write(content.toUtf8());
                f.close();
                refreshStartupItems();
            });
            hdr->addWidget(addItem);

            cv->addLayout(hdr);

            m_startupList = new QListWidget;
            m_startupList->setFrameShape(QFrame::NoFrame);
            m_startupList->setContextMenuPolicy(Qt::CustomContextMenu);
            QObject::connect(m_startupList,
                             &QListWidget::customContextMenuRequested,
                             [this](const QPoint &pos) {
                QListWidgetItem *it = m_startupList->itemAt(pos);
                if (!it) return;
                QString fileName = it->data(Qt::UserRole).toString();
                if (fileName.isEmpty()) return;

                QMenu m(this);
                m.setStyleSheet(
                    "QMenu {"
                    "  background: rgb(24, 22, 30);"
                    "  border: 1px solid rgba(255,255,255,30);"
                    "  border-radius: 10px;"
                    "  padding: 6px;"
                    "  color: rgb(240, 237, 232);"
                    "  font-size: 13px;"
                    "}"
                    "QMenu::item {"
                    "  padding: 7px 24px 7px 16px;"
                    "  border-radius: 6px;"
                    "}"
                    "QMenu::item:selected {"
                    "  background: rgba(168, 50, 50, 90);"
                    "}");

                bool enabled = !fileName.endsWith(".disabled");
                QAction *toggle = m.addAction(
                    enabled ? "Disable at login" : "Enable at login");
                m.addSeparator();
                QAction *openDir = m.addAction("Open in Files");

                QAction *chosen = m.exec(
                    m_startupList->mapToGlobal(pos));
                if (chosen == toggle) {
                    QString dirPath = QDir::homePath()
                        + "/.config/autostart";
                    QFile f(dirPath + "/" + fileName);
                    if (enabled) {
                        QString newName = fileName + ".disabled";
                        QFile::rename(f.fileName(),
                                      dirPath + "/" + newName);
                    } else {
                        QString base = fileName;
                        base.remove(QRegularExpression("\\.disabled$"));
                        QFile::rename(f.fileName(),
                                      dirPath + "/" + base);
                    }
                    refreshStartupItems();
                } else if (chosen == openDir) {
                    QProcess::startDetached("xdg-open",
                        { QDir::homePath() + "/.config/autostart" });
                }
            });
            cv->addWidget(m_startupList, 1);
            refreshStartupItems();

            sv->addWidget(c);
            sv->addStretch();
        }

        // ---------- Language & Region sub-page ----------
        QWidget *lang = new QWidget;
        {
            QVBoxLayout *lv = new QVBoxLayout(lang);
            lv->setContentsMargins(0, 0, 0, 0);
            lv->setSpacing(18);
            lv->addWidget(makeBackHeader("Language & Region"));

            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SYSTEM LANGUAGE"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *l = new QVBoxLayout;
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(2);
            QLabel *t = new QLabel("Language");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Applied via localectl set-locale");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            l->addWidget(t);
            l->addWidget(s);
            h->addLayout(l, 1);

            QStringList names = {
                "English (US)  —  en_US.UTF-8",
                "English (UK)  —  en_GB.UTF-8",
                "German        —  de_DE.UTF-8",
                "French        —  fr_FR.UTF-8",
                "Spanish       —  es_ES.UTF-8",
                "Italian       —  it_IT.UTF-8",
                "Russian       —  ru_RU.UTF-8",
                "Japanese      —  ja_JP.UTF-8",
                "Chinese       —  zh_CN.UTF-8",
                "Arabic        —  ar_SA.UTF-8",
                "Swahili       —  sw_KE.UTF-8"
            };
            QStringList codes = {
                "en_US.UTF-8", "en_GB.UTF-8", "de_DE.UTF-8",
                "fr_FR.UTF-8", "es_ES.UTF-8", "it_IT.UTF-8",
                "ru_RU.UTF-8", "ja_JP.UTF-8", "zh_CN.UTF-8",
                "ar_SA.UTF-8", "sw_KE.UTF-8"
            };
            int idx = codes.indexOf(vc.systemLanguage);
            if (idx < 0) idx = 0;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                names, idx,
                [codes](int i) {
                    if (i < 0 || i >= codes.size()) return;
                    QString code = codes[i];
                    VisualConfigManager::instance().setAndSave(
                        [code](VisualConfig &c) {
                            c.systemLanguage = code;
                        });
                    QProcess::startDetached("pkexec", {
                        "localectl", "set-locale",
                        "LANG=" + code });
                });
            h->addWidget(dd);
            cv->addWidget(row);

            QLabel *hint = new QLabel(
                "Changing the system language affects all apps on next "
                "login. Requires admin password.");
            hint->setWordWrap(true);
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            lv->addWidget(c);
            lv->addStretch();
        }

        // ---------- Transfer / Reset sub-page ----------
        QWidget *transfer = new QWidget;
        {
            QVBoxLayout *tv = new QVBoxLayout(transfer);
            tv->setContentsMargins(0, 0, 0, 0);
            tv->setSpacing(18);
            tv->addWidget(makeBackHeader("Transfer / Reset"));

            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("TRANSFER"));

            QLabel *desc = new QLabel(
                "Migration Assistant: transfer user data, settings and "
                "applications from another machine. Currently informational "
                "only — full migration requires an installation media.");
            desc->setWordWrap(true);
            desc->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(desc);

            QPushButton *migrate = new QPushButton(
                "Open Migration Assistant…");
            migrate->setCursor(Qt::PointingHandCursor);
            migrate->setFixedHeight(30);
            migrate->setMinimumWidth(200);
            migrate->setEnabled(false);
            migrate->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,10);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }")
                .arg(Sigil::textDim().name()));
            QHBoxLayout *mh = new QHBoxLayout;
            mh->addWidget(migrate);
            mh->addStretch();
            cv->addLayout(mh);

            tv->addWidget(c);

            // Reset card
            Sigil::Card *r = new Sigil::Card;
            QVBoxLayout *rv = new QVBoxLayout(r);
            rv->setContentsMargins(20, 18, 20, 18);
            rv->setSpacing(14);
            rv->addWidget(Sigil::groupLabel("RESET"));

            QLabel *warn = new QLabel(
                "Erase All Content and Settings: removes user data, "
                "accounts and preferences. Cannot be undone.");
            warn->setWordWrap(true);
            warn->setStyleSheet(QString(
                "color: rgb(255, 120, 120);"
                " font-size: 12px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            rv->addWidget(warn);

            QPushButton *reset = new QPushButton(
                "Erase All Content and Settings…");
            reset->setCursor(Qt::PointingHandCursor);
            reset->setFixedHeight(30);
            reset->setMinimumWidth(240);
            reset->setStyleSheet(
                "QPushButton { background: transparent;"
                "  color: rgb(220, 70, 80);"
                "  border: 1px solid rgb(220, 70, 80);"
                "  border-radius: 8px; padding: 4px 18px;"
                "  font-size: 12px; }"
                "QPushButton:hover { background: rgba(220,70,80,60);"
                "  color: #ffffff; }");
            QObject::connect(reset, &QPushButton::clicked, [this]() {
                QMessageBox box(this);
                box.setWindowTitle("Erase all data?");
                box.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                box.setText("This will remove ALL user data, accounts "
                            "and preferences on this machine.\n\n"
                            "This cannot be undone. Are you absolutely "
                            "sure?");
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Cancel);
                box.adjustSize();
                // Centre on the Settings window
                if (QWidget *top = window()) {
                    QPoint c = top->mapToGlobal(QPoint(
                        (top->width()  - box.width())  / 2,
                        (top->height() - box.height()) / 2));
                    box.move(c);
                }
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }");
                if (box.exec() != QMessageBox::Yes) return;

                bool ok = false;
                QString confirm = ApokolipsInputDialog::getText(
                    this, "Confirm erase",
                    "Type ERASE in capitals to confirm:",
                    "", &ok);
                if (!ok || confirm != "ERASE") return;

                // Educational — do not execute a real erase blindly.
                // Instead open a terminal explaining the manual step.
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--node", "--run",
                    "echo 'Apokolips OS — Reset'; echo; "
                    "echo 'A full system reset must be done from a live '; "
                    "echo 'USB / installer environment. This page cannot '; "
                    "echo 'safely wipe the running system.'; echo; "
                    "echo 'Reboot into the installer to reset.'; "
                    "read -p 'Press Enter to close...'" });
            });
            QHBoxLayout *rh = new QHBoxLayout;
            rh->addWidget(reset);
            rh->addStretch();
            rv->addLayout(rh);

            tv->addWidget(r);
            tv->addStretch();
        }

        hub->addWidget(home);
        hub->addWidget(startup);
        hub->addWidget(lang);
        hub->addWidget(transfer);

        v->addWidget(hub);
        return page;
    }

    void refreshStartupItems() {
        if (!m_startupList) return;
        m_startupList->clear();

        QString dirPath = QDir::homePath() + "/.config/autostart";
        QDir dir(dirPath);
        if (!dir.exists()) {
            QListWidgetItem *it = new QListWidgetItem(
                "No autostart items found (" + dirPath + " does not exist)");
            it->setFlags(Qt::NoItemFlags);
            it->setForeground(QColor(122, 120, 115));
            m_startupList->addItem(it);
            return;
        }

        QStringList files = dir.entryList(
            { "*.desktop", "*.desktop.disabled" },
            QDir::Files, QDir::Name);
        if (files.isEmpty()) {
            QListWidgetItem *it = new QListWidgetItem(
                "No autostart entries — this is normal on a fresh install");
            it->setFlags(Qt::NoItemFlags);
            it->setForeground(QColor(122, 120, 115));
            m_startupList->addItem(it);
            return;
        }

        for (const QString &f : files) {
            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly)) continue;
            QString text = QString::fromUtf8(file.readAll());
            file.close();

            QString name = f;
            name.remove(QRegularExpression("\\.disabled$"));
            bool enabled = !f.endsWith(".disabled");
            for (const QString &line : text.split('\n')) {
                QString t = line.trimmed();
                if (t.startsWith("Name="))
                    name = t.mid(5);
                if (t.startsWith("Hidden=true"))
                    enabled = false;
                if (t.startsWith("X-GNOME-Autostart-enabled=false"))
                    enabled = false;
            }

            QString label = QString("%1    %2")
                .arg(enabled ? "●" : "○", name);
            QListWidgetItem *it = new QListWidgetItem(label);
            it->setData(Qt::UserRole, f);
            it->setForeground(enabled
                ? QColor(240, 237, 232)
                : QColor(140, 140, 145));
            m_startupList->addItem(it);
        }
    }

    QWidget *makeStoragePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("OmniStorage"));

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("DISK"));

            m_storageSummary = new QLabel("Querying…");
            m_storageSummary->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_storageSummary);

            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->addWidget(Sigil::groupLabel("CATEGORIES"));
            hdr->addStretch();
            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             [this]() { refreshStoragePage(); });
            hdr->addWidget(refresh);
            cv->addLayout(hdr);

            m_storageCats = new QListWidget;
            m_storageCats->setFrameShape(QFrame::NoFrame);
            m_storageCats->setMinimumHeight(180);
            cv->addWidget(m_storageCats);

            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("TRASH"));

            m_trashInfo = new QLabel("Querying…");
            m_trashInfo->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(m_trashInfo);

            QHBoxLayout *h = new QHBoxLayout;
            QPushButton *empty = new QPushButton("Empty Trash…");
            empty->setCursor(Qt::PointingHandCursor);
            empty->setFixedHeight(30);
            empty->setMinimumWidth(140);
            empty->setStyleSheet(
                "QPushButton { background: transparent;"
                "  color: rgb(200, 74, 74);"
                "  border: 1px solid rgb(200, 74, 74);"
                "  border-radius: 7px; padding: 4px 14px;"
                "  font-size: 12px; }"
                "QPushButton:hover { background: rgba(200,74,74,60);"
                "  color: #ffffff; }");
            QObject::connect(empty, &QPushButton::clicked,
                             [this]() {
                QMessageBox box(this);
                box.setWindowTitle("Empty Trash");
                box.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                box.setText("Permanently delete all items in the Trash?");
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Cancel);
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }");
                box.adjustSize();
                if (QWidget *top = window()) {
                    QPoint pt = top->mapToGlobal(QPoint(
                        (top->width()  - box.width())  / 2,
                        (top->height() - box.height()) / 2));
                    box.move(pt);
                }
                if (box.exec() != QMessageBox::Yes) return;
                QProcess p;
                p.start("gio", { "trash", "--empty" });
                p.waitForFinished(2000);
                refreshStoragePage();
            });
            h->addWidget(empty);
            h->addStretch();
            cv->addLayout(h);

            v->addWidget(c);
        }

        v->addStretch();
        QTimer::singleShot(100, this, [this]() { refreshStoragePage(); });
        return page;
    }

    void refreshStoragePage() {
        // ---- Disk summary: async df ----
        if (m_storageSummary) {
            m_storageSummary->setText("Querying…");
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput());
                    p->deleteLater();
                    if (!m_storageSummary) return;
                    QStringList lines = out.split('\n', Qt::SkipEmptyParts);
                    quint64 total = 0, used = 0, avail = 0;
                    int pct = 0;
                    if (lines.size() >= 2) {
                        QStringList p2 = lines[1].split(
                            QRegularExpression("\\s+"),
                            Qt::SkipEmptyParts);
                        if (p2.size() >= 4) {
                            total = p2[0].toULongLong();
                            used  = p2[1].toULongLong();
                            avail = p2[2].toULongLong();
                            pct   = p2[3].remove('%').toInt();
                        }
                    }
                    auto fmtGB = [](quint64 bytes) {
                        return QString("%1 GB").arg(
                            bytes / 1073741824.0, 0, 'f', 1);
                    };
                    m_storageSummary->setText(QString(
                        "%1 used of %2     ·     %3 available     ·     %4%")
                        .arg(fmtGB(used), fmtGB(total), fmtGB(avail))
                        .arg(pct));
                });
            p->start("df", { "-B1", "--output=size,used,avail,pcent", "/" });
        }

        // ---- Categories: 7 parallel du calls ----
        if (m_storageCats) {
            m_storageCats->clear();
            QString home = QDir::homePath();
            QStringList labels = {
                "Applications", "Home", "Documents", "Downloads",
                "Music", "Pictures", "Videos"
            };
            QStringList paths = {
                "/usr", home,
                home + "/Documents", home + "/Downloads",
                home + "/Music",     home + "/Pictures",
                home + "/Videos"
            };

            struct CatJob {
                QList<QString> results;
                int done = 0;
                int total = 0;
            };
            auto *job = new CatJob;
            job->total = paths.size();
            job->results.resize(paths.size());

            for (int i = 0; i < paths.size(); ++i) {
                auto *d = new QProcess(this);
                QObject::connect(d,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [this, d, i, job, labels](int, QProcess::ExitStatus) {
                        QString o = QString::fromUtf8(
                            d->readAllStandardOutput());
                        d->deleteLater();
                        quint64 bytes =
                            o.section('\t', 0, 0).trimmed().toULongLong();
                        QString size = bytes > 1073741824
                            ? QString("%1 GB").arg(
                                bytes / 1073741824.0, 0, 'f', 2)
                            : bytes > 1048576
                                ? QString("%1 MB").arg(
                                    bytes / 1048576.0, 0, 'f', 1)
                                : QString("%1 KB").arg(
                                    bytes / 1024.0, 0, 'f', 0);
                        job->results[i] = labels[i]
                            + QString(20 - labels[i].length(), ' ') + size;
                        if (++job->done < job->total) return;

                        if (m_storageCats) {
                            for (const QString &text : job->results) {
                                QListWidgetItem *it =
                                    new QListWidgetItem(text);
                                it->setForeground(QColor(240, 237, 232));
                                m_storageCats->addItem(it);
                            }
                        }
                        delete job;
                    });
                d->start("du", { "-sb", paths[i] });
            }
        }

        // ---- Trash: 2 parallel calls ----
        if (m_trashInfo) {
            m_trashInfo->setText("Querying…");
            struct TrashJob {
                QString size;
                int items = 0;
                int done = 0;
            };
            auto *tj = new TrashJob;

            auto finish = [this, tj]() {
                if (++tj->done < 2) return;
                if (m_trashInfo) {
                    QString sz = tj->size.isEmpty() ? "0" : tj->size;
                    m_trashInfo->setText(QString("%1  ·  %2 item(s)")
                        .arg(sz).arg(tj->items));
                }
                delete tj;
            };

            auto *t = new QProcess(this);
            QObject::connect(t,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [t, tj, finish](int, QProcess::ExitStatus) {
                    tj->items = QString::fromUtf8(
                        t->readAllStandardOutput()).trimmed().toInt();
                    t->deleteLater();
                    finish();
                });
            t->start("bash", { "-c",
                "ls -1 ~/.local/share/Trash/files/ 2>/dev/null | wc -l" });

            auto *s = new QProcess(this);
            QObject::connect(s,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [s, tj, finish](int, QProcess::ExitStatus) {
                    tj->size = QString::fromUtf8(
                        s->readAllStandardOutput()).trimmed();
                    s->deleteLater();
                    finish();
                });
            s->start("bash", { "-c",
                "du -sh ~/.local/share/Trash 2>/dev/null | cut -f1" });
        }
    }

    QWidget *makeCoreMatrixPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Core Matrix"));

        // Description
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(6);

            QLabel *desc = new QLabel(
                "Choose which application opens each kind of file. "
                "Changes take effect immediately.");
            desc->setWordWrap(true);
            desc->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(desc);
            v->addWidget(c);
        }

        // Common file types
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("FILE TYPES"));

            struct Entry {
                const char *label;
                const char *mime;
            };
            const Entry entries[] = {
                { "Text files",     "text/plain"       },
                { "Images (Sigil)", "image/png"        },
                { "Video",          "video/mp4"        },
                { "Audio",          "audio/mpeg"       },
                { "PDF documents",  "application/pdf"  },
                { "Web pages",      "text/html"        },
                { "Archives",       "application/zip"  },
                { "Directories",    "inode/directory"  }
            };

            // Build app registry: display name -> desktop file
            QMap<QString, QString> appRegistry;
            QStringList appDirs = {
                "/usr/share/applications",
                QDir::homePath() + "/.local/share/applications"
            };
            for (const QString &dirPath : appDirs) {
                QDir dir(dirPath);
                if (!dir.exists()) continue;
                for (const QString &f :
                     dir.entryList({ "*.desktop" }, QDir::Files)) {
                    QFile file(dir.filePath(f));
                    if (!file.open(QIODevice::ReadOnly)) continue;
                    QString text = QString::fromUtf8(file.readAll());
                    file.close();
                    QString name;
                    bool hidden = false;
                    for (const QString &line : text.split('\n')) {
                        QString t = line.trimmed();
                        if (t.startsWith("Name=") && name.isEmpty())
                            name = t.mid(5).trimmed();
                        if (t == "NoDisplay=true") hidden = true;
                        if (t == "Hidden=true") hidden = true;
                    }
                    if (name.isEmpty() || hidden) continue;
                    // Only first occurrence (user overrides system)
                    if (!appRegistry.contains(name))
                        appRegistry[name] = f;
                }
            }

            QStringList appNames = appRegistry.keys();
            appNames.sort(Qt::CaseInsensitive);

            // Build every row first with a placeholder dropdown,
            // then fire all 8 queries at once. Each dropdown fills
            // itself in when its query returns. No waiting in line.
            QList<QPair<QString, Sigil::Dropdown *>> pending;
            for (const auto &e : entries) {
                QString mime = QString(e.mime);

                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel(e.label);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *sub = new QLabel(mime);
                sub->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(sub);
                h->addLayout(left, 1);

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    { "(Loading…)" }, 0,
                    [appRegistry, mime](int i) {
                        QStringList keys = appRegistry.keys();
                        keys.sort(Qt::CaseInsensitive);
                        if (i < 0 || i >= keys.size()) return;
                        QString desktopFile = appRegistry[keys[i]];
                        QProcess::startDetached("xdg-mime", {
                            "default", desktopFile, mime });
                    });
                dd->setMinimumWidth(220);
                h->addWidget(dd);
                cv->addWidget(row);
                pending.append({ mime, dd });
            }

            for (const auto &pair : pending) {
                const QString mime = pair.first;
                Sigil::Dropdown *dd = pair.second;
                auto *p = new QProcess(this);
                QObject::connect(p,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [p, dd, appRegistry, appNames]() {
                        QString current = QString::fromUtf8(
                            p->readAllStandardOutput()).trimmed();
                        p->deleteLater();
                        QString currentName;
                        for (auto it = appRegistry.constBegin();
                             it != appRegistry.constEnd(); ++it) {
                            if (it.value() == current) {
                                currentName = it.key();
                                break;
                            }
                        }
                        int currentIdx = 0;
                        if (!currentName.isEmpty()) {
                            int idx = appNames.indexOf(currentName);
                            if (idx >= 0) currentIdx = idx;
                        }
                        if (dd) dd->setItemsSilent(appNames, currentIdx);
                    });
                p->start("xdg-mime", { "query", "default", mime });
            }

            v->addWidget(c);
        }

        // Actions card
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("ACTIONS"));

            QPushButton *reset = new QPushButton("Reset all to system defaults");
            reset->setCursor(Qt::PointingHandCursor);
            reset->setFixedHeight(30);
            reset->setMinimumWidth(220);
            reset->setStyleSheet(QString(
                "QPushButton { background: transparent;"
                "  color: rgb(200, 74, 74);"
                "  border: 1px solid rgb(200, 74, 74);"
                "  border-radius: 7px; padding: 4px 14px;"
                "  font-size: 12px; }"
                "QPushButton:hover { background: rgba(200,74,74,60);"
                "  color: #ffffff; }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(reset, &QPushButton::clicked, [this]() {
                QMessageBox box(this);
                box.setWindowTitle("Reset associations");
                box.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                box.setText("Remove all custom file associations and "
                            "revert to system defaults?");
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Cancel);
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }");
                box.adjustSize();
                if (QWidget *top = window()) {
                    QPoint pt = top->mapToGlobal(QPoint(
                        (top->width()  - box.width())  / 2,
                        (top->height() - box.height()) / 2));
                    box.move(pt);
                }
                if (box.exec() != QMessageBox::Yes) return;

                // Delete the user's mimeapps.list
                QString path = QDir::homePath()
                    + "/.config/mimeapps.list";
                QFile::remove(path);
                QString path2 = QDir::homePath()
                    + "/.local/share/applications/mimeapps.list";
                QFile::remove(path2);
            });

            QHBoxLayout *h = new QHBoxLayout;
            h->addWidget(reset);
            h->addStretch();
            cv->addLayout(h);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeSoftwareUpdatePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Software Update"));

        // ---- System card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SYSTEM"));

            m_updateVersion = new QLabel("Apokolips OS");
            m_updateVersion->setStyleSheet(QString(
                "color: %1; font-size: 20px; font-weight: 300;"
                " background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_updateVersion);

            m_updateChecked = new QLabel("Last checked: never");
            m_updateChecked->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(m_updateChecked);

            QHBoxLayout *h = new QHBoxLayout;
            QPushButton *check = new QPushButton("Check for Updates");
            check->setCursor(Qt::PointingHandCursor);
            check->setFixedHeight(32);
            check->setMinimumWidth(180);
            check->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 8px;"
                "  padding: 4px 18px; font-size: 12px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(check, &QPushButton::clicked, [this]() {
                QMessageBox box(this);
                box.setWindowTitle("Check for Updates");
                box.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                box.setText("Refresh the package list and look for "
                            "available updates? Requires admin password.");
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Yes);
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }");
                box.adjustSize();
                if (QWidget *top = window()) {
                    QPoint pt = top->mapToGlobal(QPoint(
                        (top->width() - box.width()) / 2,
                        (top->height() - box.height()) / 2));
                    box.move(pt);
                }
                if (box.exec() != QMessageBox::Yes) return;
                QProcess::startDetached("pkexec", { "apt", "update" });
                QTimer::singleShot(3000, this, [this]() {
                    refreshSoftwareUpdatePage();
                });
            });
            h->addWidget(check);
            h->addStretch();
            cv->addLayout(h);

            v->addWidget(c);
        }

        // ---- Available updates card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->addWidget(Sigil::groupLabel("AVAILABLE UPDATES"));
            hdr->addStretch();

            QPushButton *install = new QPushButton("Install All");
            install->setCursor(Qt::PointingHandCursor);
            install->setFixedHeight(28);
            install->setMinimumWidth(100);
            install->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(install, &QPushButton::clicked, [this]() {
                QMessageBox box(this);
                box.setWindowTitle("Install updates");
                box.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
                box.setText("Install all available package updates? "
                            "Requires admin password. This may take "
                            "several minutes.");
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Yes);
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }");
                box.adjustSize();
                if (QWidget *top = window()) {
                    QPoint pt = top->mapToGlobal(QPoint(
                        (top->width() - box.width()) / 2,
                        (top->height() - box.height()) / 2));
                    box.move(pt);
                }
                if (box.exec() != QMessageBox::Yes) return;
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--node", "--run",
                    "sudo apt upgrade -y && echo && echo 'Updates "
                    "installed. Press Enter to close.' && read" });
            });
            hdr->addWidget(install);

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(28);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             [this]() { refreshSoftwareUpdatePage(); });
            hdr->addWidget(refresh);
            cv->addLayout(hdr);

            m_updateStatus = new QLabel("Querying…");
            m_updateStatus->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(m_updateStatus);

            m_updateList = new QListWidget;
            m_updateList->setFrameShape(QFrame::NoFrame);
            m_updateList->setMinimumHeight(180);
            cv->addWidget(m_updateList);

            v->addWidget(c);
        }

        // ---- Auto-update card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("AUTOMATIC UPDATES"));

            // Read current state
            QFile f("/etc/apt/apt.conf.d/20auto-upgrades");
            bool autoOn = false;
            if (f.open(QIODevice::ReadOnly)) {
                QString text = QString::fromUtf8(f.readAll());
                autoOn = text.contains("Unattended-Upgrade \"1\"");
            }

            cv->addWidget(Sigil::makeToggleRow(
                "Install updates automatically",
                autoOn ? "Unattended upgrades enabled"
                       : "You install updates manually",
                autoOn,
                [](bool on) {
                    QString content = QString(
                        "APT::Periodic::Update-Package-Lists \"1\";\n"
                        "APT::Periodic::Unattended-Upgrade \"%1\";\n")
                        .arg(on ? "1" : "0");
                    // Fire-and-forget: write to a temp file (no
                    // auth needed), then pkexec cp it into place.
                    // The UI never blocks waiting for the auth
                    // prompt; the user confirms in a separate
                    // window.
                    QString tmp = "/tmp/apokolips-auto-upgrade.conf";
                    QFile f(tmp);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        f.write(content.toUtf8());
                        f.close();
                        QProcess::startDetached("pkexec",
                            { "cp", tmp,
                              "/etc/apt/apt.conf.d/20auto-upgrades" });
                    }
                }));

            v->addWidget(c);
        }

        // ---- History card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("RECENT HISTORY"));

            m_historyList = new QListWidget;
            m_historyList->setFrameShape(QFrame::NoFrame);
            m_historyList->setMinimumHeight(140);
            cv->addWidget(m_historyList);

            v->addWidget(c);
        }

        v->addStretch();
        QTimer::singleShot(100, this,
                           [this]() { refreshSoftwareUpdatePage(); });
        return page;
    }

    void refreshSoftwareUpdatePage() {
        // ---- OS version ----
        if (m_updateVersion) {
            QFile f("/etc/os-release");
            QString pretty = "Apokolips OS";
            if (f.open(QIODevice::ReadOnly)) {
                for (const QString &line :
                     QString::fromUtf8(f.readAll()).split('\n')) {
                    if (line.startsWith("PRETTY_NAME=")) {
                        pretty = line.mid(12).trimmed();
                        pretty.remove('"');
                    }
                }
            }
            m_updateVersion->setText(pretty);
        }

        // ---- Last checked ----
        if (m_updateChecked) {
            QFileInfo fi("/var/lib/apt/periodic/update-success-stamp");
            if (fi.exists()) {
                m_updateChecked->setText(
                    "Last checked: " +
                    fi.lastModified().toString("d MMM yyyy, HH:mm"));
            } else {
                m_updateChecked->setText("Last checked: never");
            }
        }

        // ---- Available updates (async) ----
        if (m_updateList) {
            m_updateList->clear();
            if (m_updateStatus) m_updateStatus->setText("Querying…");
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput());
                    p->deleteLater();
                    if (!m_updateList) return;
                    QStringList lines =
                        out.split('\n', Qt::SkipEmptyParts);

                    if (lines.isEmpty()) {
                        if (m_updateStatus)
                            m_updateStatus->setText(
                                "Everything is up to date ✓");
                        QListWidgetItem *it = new QListWidgetItem(
                            "No updates available");
                        it->setFlags(Qt::NoItemFlags);
                        it->setForeground(QColor(122, 120, 115));
                        m_updateList->addItem(it);
                    } else {
                        if (m_updateStatus)
                            m_updateStatus->setText(
                                QString("%1 update(s) available")
                                    .arg(lines.size()));
                        for (const QString &line : lines) {
                            QString pkg = line.section('/', 0, 0);
                            QString rest = line.section('/', 1);
                            QString ver = rest.section(' ', 0, 0);
                            QString text = pkg
                                + QString(28 - pkg.length(), ' ') + ver;
                            QListWidgetItem *it =
                                new QListWidgetItem(text);
                            it->setForeground(QColor(240, 237, 232));
                            m_updateList->addItem(it);
                        }
                    }
                });
            p->start("bash", { "-c",
                "apt list --upgradable 2>/dev/null | tail -n +2" });
        }

        // ---- History (async) ----
        if (m_historyList) {
            m_historyList->clear();
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput());
                    p->deleteLater();
                    if (!m_historyList) return;

                    QStringList lines =
                        out.split('\n', Qt::SkipEmptyParts);
                    QString curDate;
                    int shown = 0;
                    for (const QString &line : lines) {
                        if (line.startsWith("Start-Date:")) {
                            curDate = line.mid(11).trimmed();
                        } else if (line.startsWith("Commandline:")
                                   && shown < 6) {
                            QString cmd = line.mid(12).trimmed();
                            if (cmd.length() > 90)
                                cmd = cmd.left(87) + "…";
                            QString text = curDate + "  ·  " + cmd;
                            QListWidgetItem *it =
                                new QListWidgetItem(text);
                            it->setForeground(QColor(200, 200, 205));
                            m_historyList->addItem(it);
                            shown++;
                        }
                    }
                    if (shown == 0) {
                        QListWidgetItem *it = new QListWidgetItem(
                            "No history yet");
                        it->setFlags(Qt::NoItemFlags);
                        it->setForeground(QColor(122, 120, 115));
                        m_historyList->addItem(it);
                    }
                });
            p->start("bash", { "-c",
                "tac /var/log/apt/history.log 2>/dev/null | "
                "grep -E '^(Start-Date|Commandline)' | head -20" });
        }
    }

    QWidget *makeTimeMachinePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Time Machine"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Destination card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("DESTINATION"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Backup folder");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *sub = new QLabel(
                vc.backupDestination.isEmpty()
                    ? "Not set — backups are disabled"
                    : vc.backupDestination);
            sub->setWordWrap(true);
            sub->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(sub);
            h->addLayout(left, 1);

            QPushButton *choose = new QPushButton("Choose…");
            choose->setCursor(Qt::PointingHandCursor);
            choose->setFixedHeight(30);
            choose->setMinimumWidth(110);
            choose->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(choose, &QPushButton::clicked,
                             [this, sub]() {
                bool ok = false;
                QString def = QDir::homePath() + "/Backups";
                QString path = ApokolipsInputDialog::getText(
                    this, "Backup destination",
                    "Folder for Time Machine backups:",
                    VisualConfigManager::instance().cfg()
                        .backupDestination.isEmpty()
                            ? def
                            : VisualConfigManager::instance().cfg()
                                  .backupDestination,
                    &ok);
                if (!ok || path.isEmpty()) return;
                if (!QDir(path).exists() && !QDir().mkpath(path)) {
                    qWarning() << "could not create" << path;
                    return;
                }
                VisualConfigManager::instance().setAndSave(
                    [path](VisualConfig &c) {
                        c.backupDestination = path;
                    });
                sub->setText(path);
                applyBackupSchedule();
            });
            h->addWidget(choose);
            cv->addWidget(row);

            v->addWidget(c);
        }

        // ---- Schedule card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SCHEDULE"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Automatic backups");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel("Runs in the background via systemd");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QStringList opts = { "Manual only", "Hourly",
                                 "Daily", "Weekly" };
            QStringList values = { "manual", "hourly",
                                   "daily", "weekly" };
            int idx = values.indexOf(vc.backupSchedule);
            if (idx < 0) idx = 0;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [values, this](int i) {
                    if (i < 0 || i >= values.size()) return;
                    QString v = values[i];
                    VisualConfigManager::instance().setAndSave(
                        [v](VisualConfig &c) {
                            c.backupSchedule = v;
                        });
                    applyBackupSchedule();
                });
            h->addWidget(dd);
            cv->addWidget(row);

            v->addWidget(c);
        }

        // ---- Action card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("BACKUP NOW"));

            m_backupLast = new QLabel("Last backup: never");
            m_backupLast->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(m_backupLast);

            m_backupStatus = new QLabel("");
            m_backupStatus->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(m_backupStatus);

            QHBoxLayout *h = new QHBoxLayout;
            QPushButton *run = new QPushButton("Back Up Now");
            run->setCursor(Qt::PointingHandCursor);
            run->setFixedHeight(32);
            run->setMinimumWidth(160);
            run->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 8px;"
                "  padding: 4px 18px; font-size: 12px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(run, &QPushButton::clicked,
                             [this]() { runBackupNow(); });
            h->addWidget(run);
            h->addStretch();
            cv->addLayout(h);

            v->addWidget(c);
        }

        // ---- History card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->addWidget(Sigil::groupLabel("HISTORY"));
            hdr->addStretch();
            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             [this]() { refreshBackupPage(); });
            hdr->addWidget(refresh);
            cv->addLayout(hdr);

            m_backupHistory = new QListWidget;
            m_backupHistory->setFrameShape(QFrame::NoFrame);
            m_backupHistory->setMinimumHeight(160);
            cv->addWidget(m_backupHistory);

            v->addWidget(c);
        }

        v->addStretch();
        QTimer::singleShot(100, this, [this]() { refreshBackupPage(); });
        return page;
    }

    void runBackupNow() {
        auto &cfg = VisualConfigManager::instance().cfg();
        if (cfg.backupDestination.isEmpty()) {
            if (m_backupStatus)
                m_backupStatus->setText(
                    "No destination set. Click Choose… first.");
            return;
        }
        if (!QDir().mkpath(cfg.backupDestination)) return;

        QString stamp = QDateTime::currentDateTime()
                            .toString("yyyyMMdd-HHmmss");
        QString dest = cfg.backupDestination + "/snapshot-" + stamp;

        if (m_backupStatus)
            m_backupStatus->setText("Backing up… (see terminal for detail)");

        // Real rsync in a foot terminal so the user sees progress
        QString cmd = QString(
            "mkdir -p '%1' && "
            "rsync -a --info=progress2 "
            "--exclude='.cache' "
            "--exclude='.local/share/Trash' "
            "--exclude='snap' "
            "--exclude='Backups' "
            "$HOME/ '%1/' && "
            "echo && echo 'Backup complete.' && "
            "echo 'Press Enter to close.' && read")
            .arg(dest);

        QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            { "--node", "--run", cmd });

        QTimer::singleShot(1500, this, [this]() { refreshBackupPage(); });
    }

    void applyBackupSchedule() {
        auto &cfg = VisualConfigManager::instance().cfg();
        QString unitDir = QDir::homePath() + "/.config/systemd/user";
        QDir().mkpath(unitDir);

        // Remove any existing backup timer
        QString timerPath = unitDir + "/apokolips-backup.timer";
        QString servicePath = unitDir + "/apokolips-backup.service";
        QFile::remove(timerPath);
        QFile::remove(servicePath);

        QProcess::execute("systemctl", { "--user", "daemon-reload" });

        if (cfg.backupSchedule == "manual"
            || cfg.backupDestination.isEmpty()) {
            QProcess::execute("systemctl", { "--user", "stop",
                "apokolips-backup.timer" });
            QProcess::execute("systemctl", { "--user", "disable",
                "apokolips-backup.timer" });
            return;
        }

        QString onCalendar;
        if (cfg.backupSchedule == "hourly")  onCalendar = "hourly";
        else if (cfg.backupSchedule == "daily")  onCalendar = "daily";
        else if (cfg.backupSchedule == "weekly") onCalendar = "weekly";
        else return;

        // Service
        QFile svc(servicePath);
        if (svc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            svc.write(QString(
                "[Unit]\n"
                "Description=Apokolips Time Machine backup\n"
                "\n"
                "[Service]\n"
                "Type=oneshot\n"
                "ExecStart=/usr/bin/bash -c \"mkdir -p '%1/snapshot-$(date +%%Y%%m%%d-%%H%%M%%S)' && "
                "rsync -a --exclude='.cache' --exclude='.local/share/Trash' "
                "--exclude='snap' --exclude='Backups' "
                "$HOME/ '%1/snapshot-$(date +%%Y%%m%%d-%%H%%M%%S)/'\"\n")
                .arg(cfg.backupDestination).toUtf8());
            svc.close();
        }

        // Timer
        QFile tmr(timerPath);
        if (tmr.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tmr.write(QString(
                "[Unit]\n"
                "Description=Apokolips Time Machine schedule\n"
                "\n"
                "[Timer]\n"
                "OnCalendar=%1\n"
                "Persistent=true\n"
                "\n"
                "[Install]\n"
                "WantedBy=timers.target\n")
                .arg(onCalendar).toUtf8());
            tmr.close();
        }

        QProcess::execute("systemctl", { "--user", "daemon-reload" });
        QProcess::execute("systemctl", { "--user", "enable", "--now",
            "apokolips-backup.timer" });
    }

    void refreshBackupPage() {
        auto &cfg = VisualConfigManager::instance().cfg();

        if (m_backupLast) {
            if (cfg.backupDestination.isEmpty()) {
                m_backupLast->setText("Last backup: never");
            } else {
                QDir d(cfg.backupDestination);
                QStringList snaps = d.entryList(
                    { "snapshot-*" }, QDir::Dirs, QDir::Name);
                if (snaps.isEmpty()) {
                    m_backupLast->setText("Last backup: never");
                } else {
                    QString latest = snaps.last();
                    QFileInfo fi(d.filePath(latest));
                    m_backupLast->setText(
                        "Last backup: " +
                        fi.lastModified().toString("d MMM yyyy, HH:mm"));
                }
            }
        }

        if (m_backupHistory) {
            m_backupHistory->clear();
            if (cfg.backupDestination.isEmpty()) {
                QListWidgetItem *it = new QListWidgetItem(
                    "No destination set");
                it->setFlags(Qt::NoItemFlags);
                it->setForeground(QColor(122, 120, 115));
                m_backupHistory->addItem(it);
                return;
            }

            QDir d(cfg.backupDestination);
            QStringList snaps = d.entryList(
                { "snapshot-*" }, QDir::Dirs, QDir::Name);
            if (snaps.isEmpty()) {
                QListWidgetItem *it = new QListWidgetItem(
                    "No backups yet");
                it->setFlags(Qt::NoItemFlags);
                it->setForeground(QColor(122, 120, 115));
                m_backupHistory->addItem(it);
                return;
            }

            // Show most recent first, cap at 12
            int shown = 0;
            for (int i = snaps.size() - 1; i >= 0 && shown < 12; --i) {
                QFileInfo fi(d.filePath(snaps[i]));
                QString text = QString("%1   ·   %2")
                    .arg(fi.lastModified().toString("d MMM yyyy HH:mm"),
                         snaps[i]);
                QListWidgetItem *it = new QListWidgetItem(text);
                it->setForeground(QColor(240, 237, 232));
                m_backupHistory->addItem(it);
                shown++;
            }
        }
    }

    QWidget *makeAccessibilityPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Accessibility"));

        auto &vc = VisualConfigManager::instance().cfg();

        // Helper: read/write gsettings
        // Prefetch every accessibility key at once, in parallel.
        // One wait instead of nine — the page opens much faster,
        // and the shell stays responsive while the answers arrive.
        QHash<QString, QString> gsCache;
        {
            QProcess prefetch;
            QString script = R"SCRIPT(
                for spec in \
                  "org.gnome.desktop.a11y.keyboard stickykeys-enable" \
                  "org.gnome.desktop.a11y.keyboard slowkeys-enable" \
                  "org.gnome.desktop.a11y.keyboard bouncekeys-enable" \
                  "org.gnome.desktop.a11y.keyboard togglekeys-enable" \
                  "org.gnome.desktop.a11y.keyboard mousekeys-enable" \
                  "org.gnome.desktop.a11y.keyboard slowkeys-delay" \
                  "org.gnome.desktop.a11y.keyboard bouncekeys-delay" \
                  "org.gnome.desktop.a11y.mouse dwell-click-enabled" \
                  "org.gnome.desktop.a11y always-show-text-caret"
                do
                    schema=${spec%% *}
                    key=${spec##* }
                    ( val=$(gsettings get "$schema" "$key" 2>/dev/null)
                      printf '%s %s = %s\n' "$schema" "$key" "$val" ) &
                done
                wait
            )SCRIPT";
            prefetch.start("bash", { "-c", script });
            prefetch.waitForFinished(1500);
            QString out = QString::fromUtf8(
                prefetch.readAllStandardOutput());
            for (const QString &line :
                 out.split('\n', Qt::SkipEmptyParts)) {
                int eq = line.indexOf('=');
                if (eq < 0) continue;
                QString k = line.left(eq).trimmed();
                QString v = line.mid(eq + 1).trimmed();
                gsCache[k] = v;
            }
        }

        auto getGS = [&gsCache](const QString &schema,
                                const QString &key) -> QString {
            return gsCache.value(schema + " " + key);
        };
        auto setGS = [](const QString &schema, const QString &key,
                        const QString &val) {
            QProcess::startDetached("gsettings",
                { "set", schema, key, val });
        };

        // ---- Vision card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("VISION"));

            // Text size slider — 0.8x to 1.6x, applied to our shell
            {
                double factor = vc.shellTextScale;
                if (factor <= 0) factor = 1.0;

                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Text size");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(
                    QString("%1x").arg(factor, 0, 'f', 2));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(60);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(80, 160);
                sl->setValue(int(factor * 100));
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [this, val](int x) {
                    double f = x / 100.0;
                    val->setText(QString("%1x").arg(f, 0, 'f', 2));
                    VisualConfigManager::instance().setAndSave(
                        [f](VisualConfig &c) { c.shellTextScale = f; });
                    applyTextScale(f);
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.interface",
                          "text-scaling-factor",
                          QString::number(f, 'f', 2) });
                });
                rv->addWidget(sl);

                QLabel *sub = new QLabel(
                    "Scales the Apokolips shell and GTK apps");
                sub->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                rv->addWidget(sub);
                cv->addWidget(row);
            }

            // High contrast toggle
            cv->addWidget(Sigil::makeToggleRow(
                "High contrast",
                "Increase contrast for GTK and system apps",
                vc.shellHighContrast,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.shellHighContrast = on; });
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.interface",
                          "high-contrast",
                          on ? "true" : "false" });
                }));

            // Reduce motion toggle
            cv->addWidget(Sigil::makeToggleRow(
                "Reduce motion",
                "Minimize animations across the shell and apps",
                vc.shellReduceMotion,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.shellReduceMotion = on; });
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.interface",
                          "enable-animations",
                          on ? "false" : "true" });
                }));

            // Cursor size
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel("Cursor size");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel("Larger pointer for easier targeting");
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                QStringList opts = { "Small (16)", "Medium (24)",
                                     "Large (36)", "Huge (48)" };
                QList<int> vals = { 16, 24, 36, 48 };
                int idx = vals.indexOf(vc.cursorSize);
                if (idx < 0) idx = 1;

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    opts, idx,
                    [vals](int i) {
                        if (i < 0 || i >= vals.size()) return;
                        int sz = vals[i];
                        VisualConfigManager::instance().setAndSave(
                            [sz](VisualConfig &c) { c.cursorSize = sz; });
                        QProcess::startDetached("gsettings",
                            { "set", "org.gnome.desktop.interface",
                              "cursor-size", QString::number(sz) });
                    });
                h->addWidget(dd);
                cv->addWidget(row);
            }

            // Visual alerts
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel("Visual alerts");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(
                    "Flash the screen instead of playing a sound");
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                QStringList opts = { "Off", "Flash screen", "Flash window" };
                QStringList values = { "off", "flash", "window" };
                int idx = values.indexOf(vc.visualAlerts);
                if (idx < 0) idx = 0;

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    opts, idx,
                    [values](int i) {
                        if (i < 0 || i >= values.size()) return;
                        QString v = values[i];
                        VisualConfigManager::instance().setAndSave(
                            [v](VisualConfig &c) { c.visualAlerts = v; });
                        QProcess::startDetached("gsettings",
                            { "set",
                              "org.gnome.desktop.wm.preferences",
                              "visual-bell-type", v });
                        QProcess::startDetached("gsettings",
                            { "set", "org.gnome.desktop.wm.preferences",
                              "visual-bell",
                              v == "off" ? "false" : "true" });
                    });
                h->addWidget(dd);
                cv->addWidget(row);
            }

            // Notification timeout
            {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Notification timeout");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(
                    QString("%1 s").arg(vc.notifTimeoutSec));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(60);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(1, 60);
                sl->setValue(vc.notifTimeoutSec);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [val](int x) {
                    val->setText(QString("%1 s").arg(x));
                    VisualConfigManager::instance().setAndSave(
                        [x](VisualConfig &c) { c.notifTimeoutSec = x; });
                });
                rv->addWidget(sl);
                cv->addWidget(row);
            }

            v->addWidget(c);
        }

        // ---- Keyboard accessibility card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("KEYBOARD"));

            auto gsBool = [&](const QString &schema, const QString &key,
                              bool def) {
                QString v = getGS(schema, key);
                if (v == "true")  return true;
                if (v == "false") return false;
                return def;
            };

            cv->addWidget(Sigil::makeToggleRow(
                "Sticky keys",
                "Press modifier keys one at a time (Shift, Ctrl, Alt)",
                gsBool("org.gnome.desktop.a11y.keyboard",
                       "stickykeys-enable", false),
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "stickykeys-enable",
                          on ? "true" : "false" });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Slow keys",
                "Require keys to be held longer before they register",
                gsBool("org.gnome.desktop.a11y.keyboard",
                       "slowkeys-enable", false),
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "slowkeys-enable",
                          on ? "true" : "false" });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Bounce keys",
                "Ignore rapid duplicate key presses",
                gsBool("org.gnome.desktop.a11y.keyboard",
                       "bouncekeys-enable", false),
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "bouncekeys-enable",
                          on ? "true" : "false" });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Toggle keys",
                "Beep when Caps Lock, Num Lock or Scroll Lock toggles",
                gsBool("org.gnome.desktop.a11y.keyboard",
                       "togglekeys-enable", false),
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "togglekeys-enable",
                          on ? "true" : "false" });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Mouse keys",
                "Control the pointer with the numeric keypad",
                gsBool("org.gnome.desktop.a11y.keyboard",
                       "mousekeys-enable", false),
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "mousekeys-enable",
                          on ? "true" : "false" });
                }));

            {
                int delay = getGS("org.gnome.desktop.a11y.keyboard",
                                  "slowkeys-delay").toInt();
                if (delay <= 0) delay = 300;

                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Slow keys delay");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(QString("%1 ms").arg(delay));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(70);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(100, 1000);
                sl->setValue(delay);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [val](int x) {
                    val->setText(QString("%1 ms").arg(x));
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "slowkeys-delay", QString::number(x) });
                });
                rv->addWidget(sl);
                cv->addWidget(row);
            }

            {
                int delay = getGS("org.gnome.desktop.a11y.keyboard",
                                  "bouncekeys-delay").toInt();
                if (delay <= 0) delay = 300;

                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Bounce keys delay");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(QString("%1 ms").arg(delay));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(70);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(100, 1000);
                sl->setValue(delay);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [val](int x) {
                    val->setText(QString("%1 ms").arg(x));
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.keyboard",
                          "bouncekeys-delay", QString::number(x) });
                });
                rv->addWidget(sl);
                cv->addWidget(row);
            }

            v->addWidget(c);
        }

        // ---- Mouse accessibility card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("MOUSE"));

            cv->addWidget(Sigil::makeToggleRow(
                "Dwell click",
                "Click by hovering the pointer instead of pressing",
                getGS("org.gnome.desktop.a11y.mouse",
                      "dwell-click-enabled") == "true",
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y.mouse",
                          "dwell-click-enabled",
                          on ? "true" : "false" });
                }));

            v->addWidget(c);
        }

        // ---- Caret card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("CARET & FOCUS"));

            cv->addWidget(Sigil::makeToggleRow(
                "Always show text caret",
                "Keep the blinking cursor visible at all times",
                getGS("org.gnome.desktop.a11y",
                      "always-show-text-caret") == "true",
                [](bool on) {
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.desktop.a11y",
                          "always-show-text-caret",
                          on ? "true" : "false" });
                }));

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makePrivacyPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Privacy & Security"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Statement card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(8);

            QLabel *head = new QLabel(
                "Apokolips OS ships with zero telemetry by default.");
            head->setWordWrap(true);
            head->setStyleSheet(QString(
                "color: %1; font-size: 14px; font-weight: 500;"
                " background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(head);

            QLabel *sub = new QLabel(
                "No analytics, no usage reporting, no crash upload, no "
                "background phone-home. This is a feature, not a default "
                "we later flip. Any reporting on this machine is opt-in "
                "only and can be turned off below.");
            sub->setWordWrap(true);
            sub->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(sub);
            v->addWidget(c);
        }

        // ---- System Security card ---- (async: 3 parallel status queries)
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("SYSTEM SECURITY"));

            auto makeSecRow = [cv](const QString &name) -> QLabel * {
                QHBoxLayout *row = new QHBoxLayout;
                QLabel *l = new QLabel(name);
                l->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *v = new QLabel("Querying…");
                v->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                row->addWidget(l);
                row->addStretch();
                row->addWidget(v);
                cv->addLayout(row);
                return v;
            };

            QLabel *v1 = makeSecRow("AppArmor");
            QLabel *v2 = makeSecRow("Firewall (ufw)");
            QLabel *v3 = makeSecRow("SSH server");

            // AppArmor async
            {
                auto *p = new QProcess(this);
                QObject::connect(p,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [p, v1](int, QProcess::ExitStatus) {
                        bool on = QString::fromUtf8(
                            p->readAllStandardOutput()).trimmed()
                            .startsWith("Y");
                        p->deleteLater();
                        if (!v1) return;
                        v1->setText(on ? "Enabled" : "Disabled");
                        v1->setStyleSheet(QString(
                            "color: %1; font-size: 12px;"
                            " background: transparent;")
                            .arg(on ? "rgb(120, 200, 120)"
                                    : "rgb(200, 120, 120)"));
                    });
                p->start("bash", { "-c",
                    "cat /sys/module/apparmor/parameters/enabled "
                    "2>/dev/null || echo 'N'" });
            }

            // Firewall async
            {
                auto *p = new QProcess(this);
                QObject::connect(p,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [p, v2](int, QProcess::ExitStatus) {
                        QString s = QString::fromUtf8(
                            p->readAllStandardOutput()).trimmed();
                        p->deleteLater();
                        if (!v2) return;
                        bool on = (s == "active");
                        v2->setText(on ? "Active" : "Inactive");
                        v2->setStyleSheet(QString(
                            "color: %1; font-size: 12px;"
                            " background: transparent;")
                            .arg(on ? "rgb(120, 200, 120)"
                                    : "rgb(200, 160, 100)"));
                    });
                p->start("bash", { "-c",
                    "systemctl is-active ufw 2>/dev/null "
                    "|| echo inactive" });
            }

            // SSH async
            {
                auto *p = new QProcess(this);
                QObject::connect(p,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [p, v3](int, QProcess::ExitStatus) {
                        bool on = QString::fromUtf8(
                            p->readAllStandardOutput()).trimmed()
                            == "active";
                        p->deleteLater();
                        if (!v3) return;
                        v3->setText(on ? "Running" : "Stopped");
                        v3->setStyleSheet(QString(
                            "color: %1; font-size: 12px;"
                            " background: transparent;")
                            .arg(on ? "rgb(200, 160, 100)"
                                    : "rgb(120, 200, 120)"));
                    });
                p->start("systemctl", { "is-active", "ssh" });
            }

            QLabel *hint = new QLabel(
                "Manage firewall and SSH in Lock Screen & Security.");
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        // ---- Location card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("LOCATION"));

            cv->addWidget(Sigil::makeToggleRow(
                "Location services",
                "Allow apps to determine your physical location",
                vc.locationEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.locationEnabled = on; });
                    QProcess::startDetached("gsettings",
                        { "set", "org.gnome.system.location",
                          "enabled", on ? "true" : "false" });
                    // Also start/stop geoclue.service — the actual
                    // location-lookup daemon
                    QProcess::startDetached("pkexec",
                        { "systemctl",
                          on ? "start" : "stop",
                          "geoclue.service" });
                }));

            v->addWidget(c);
        }

        // ---- Telemetry card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("ANALYTICS"));

            cv->addWidget(Sigil::makeToggleRow(
                "Send diagnostic reports",
                "Ubuntu report — off by default. Enabling allows anonymous "
                "system info to be sent after installs.",
                vc.telemetryEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.telemetryEnabled = on; });
                    if (on) {
                        QProcess::startDetached("ubuntu-report",
                            { "send" });
                    }
                }));

            QLabel *popularity = new QLabel(
                "Popularity contest (popcon): not installed on this machine.");
            popularity->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            popularity->setWordWrap(true);
            cv->addWidget(popularity);

            v->addWidget(c);
        }

        // ---- Autostart card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("AUTOSTART (SYSTEM)"));

            QListWidget *list = new QListWidget;
            list->setFrameShape(QFrame::NoFrame);
            list->setMinimumHeight(160);

            QDir sysDir("/etc/xdg/autostart");
            for (const QString &f :
                 sysDir.entryList({ "*.desktop" }, QDir::Files,
                                  QDir::Name)) {
                QFile file(sysDir.filePath(f));
                QString name = f;
                if (file.open(QIODevice::ReadOnly)) {
                    for (const QString &line :
                         QString::fromUtf8(file.readAll()).split('\n')) {
                        if (line.startsWith("Name=")) {
                            name = line.mid(5).trimmed();
                            break;
                        }
                    }
                }
                list->addItem(name + "   (" + f + ")");
            }
            if (list->count() == 0) {
                list->addItem("No system autostart entries");
            }
            cv->addWidget(list);

            QLabel *hint = new QLabel(
                "User autostart entries are managed in General → Startup Items.");
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

        QWidget *makeLauncherSettingsPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Launcher & Spotlight"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Modules card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("MODULES"));

            cv->addWidget(Sigil::makeToggleRow(
                "Launcher",
                "Grid of every installed app (Super+Space)",
                vc.launcherEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.launcherEnabled = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Spotlight search",
                "Type to find apps, files, calculations",
                vc.spotlightEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.spotlightEnabled = on; });
                }));

            v->addWidget(c);
        }

        // ---- Merge card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("BEHAVIOUR"));

            cv->addWidget(Sigil::makeToggleRow(
                "Add a search field to the Launcher",
                "Type inside the Launcher to filter apps in place",
                vc.launcherHasSearch,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) {
                            c.launcherHasSearch = on; });
                }));

            v->addWidget(c);
        }

        // ---- Spotlight size card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("SPOTLIGHT SIZE"));

            auto addSlider = [&](const QString &title,
                                 const QString &sub,
                                 int value, int lo, int hi,
                                 const QString &suffix,
                                 std::function<void(int)> onChange) {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(QString::number(value) + suffix);
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(70);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(lo, hi);
                sl->setValue(value);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [val, suffix, onChange](int x) {
                    val->setText(QString::number(x) + suffix);
                    onChange(x);
                });
                rv->addWidget(sl);
                QLabel *subLbl = new QLabel(sub);
                subLbl->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                rv->addWidget(subLbl);
                cv->addWidget(row);
            };

            addSlider("Width", "Smaller values give a compact bar",
                      vc.spotlightBarWidth, 400, 1400, " px",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.spotlightBarWidth = x; });
            });

            addSlider("Height", "Shorter values hide preview rows",
                      vc.spotlightBarHeight, 60, 700, " px",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.spotlightBarHeight = x; });
            });

            v->addWidget(c);
        }

        // ---- Launcher icon size card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("LAUNCHER ICON SIZE"));

            QWidget *row = new QWidget;
            QVBoxLayout *rv = new QVBoxLayout(row);
            rv->setContentsMargins(0, 0, 0, 0);
            rv->setSpacing(6);

            QHBoxLayout *top = new QHBoxLayout;
            QLabel *t = new QLabel("Icon size");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *val = new QLabel(QString("%1 px").arg(vc.launcherIconSize));
            val->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            val->setMinimumWidth(70);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            top->addWidget(t);
            top->addStretch();
            top->addWidget(val);
            rv->addLayout(top);

            QSlider *sl = new QSlider(Qt::Horizontal);
            sl->setRange(36, 80);
            sl->setValue(vc.launcherIconSize);
            sl->setStyleSheet(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgb(168,50,50);"
                "  border-radius: 2px; }");
            m_sliders.append(sl);
            QObject::connect(sl, &QSlider::valueChanged,
                             [val](int x) {
                val->setText(QString("%1 px").arg(x));
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.launcherIconSize = x; });
            });
            rv->addWidget(sl);
            cv->addWidget(row);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeSearchPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Search"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Sources card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SEARCH SOURCES"));

            cv->addWidget(Sigil::makeToggleRow(
                "Calculator",
                "Type math like 8*4 or 120/3 to get an inline answer",
                vc.spotCalcEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.spotCalcEnabled = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Applications",
                "Find and launch installed apps by name",
                vc.spotAppsEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.spotAppsEnabled = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Files & folders",
                "Match paths and home-directory items",
                vc.spotFilesEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.spotFilesEnabled = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Web search",
                "Send queries to the default browser (coming in V2)",
                vc.spotWebEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.spotWebEnabled = on; });
                }));

            v->addWidget(c);
        }

        // ---- Result count card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("RESULTS"));

            QWidget *row = new QWidget;
            QVBoxLayout *rv = new QVBoxLayout(row);
            rv->setContentsMargins(0, 0, 0, 0);
            rv->setSpacing(6);

            QHBoxLayout *top = new QHBoxLayout;
            QLabel *t = new QLabel("Maximum results shown");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *val = new QLabel(QString::number(vc.spotMaxResults));
            val->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            val->setMinimumWidth(40);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            top->addWidget(t);
            top->addStretch();
            top->addWidget(val);
            rv->addLayout(top);

            QSlider *sl = new QSlider(Qt::Horizontal);
            sl->setRange(3, 20);
            sl->setValue(vc.spotMaxResults);
            sl->setStyleSheet(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgb(168,50,50);"
                "  border-radius: 2px; }");
            m_sliders.append(sl);
            QObject::connect(sl, &QSlider::valueChanged,
                             [val](int x) {
                val->setText(QString::number(x));
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.spotMaxResults = x; });
            });
            rv->addWidget(sl);
            cv->addWidget(row);

            v->addWidget(c);
        }

        // ---- Shortcut card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SHORTCUT"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Open Spotlight");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Bound in ~/.config/sway/config (Super+Space)");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QLabel *keycap = new QLabel("Super + Space");
            keycap->setStyleSheet(QString(
                "color: %1; font-size: 12px;"
                " background: rgba(255,255,255,20);"
                " border-radius: 6px; padding: 6px 14px;")
                .arg(Sigil::textPrimary().name()));
            h->addWidget(keycap);
            cv->addWidget(row);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeFocusPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Focus"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Current mode card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(16);
            cv->addWidget(Sigil::groupLabel("MODE"));

            m_focusModeStatus = new QLabel;
            m_focusModeStatus->setStyleSheet(QString(
                "color: %1; font-size: 22px; font-weight: 300;"
                " background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_focusModeStatus);

            // 4 buttons: Off, Work, Sleep, Personal
            QHBoxLayout *row = new QHBoxLayout;
            row->setSpacing(10);

            struct ModeBtn { const char *id; const char *label; };
            const ModeBtn modes[] = {
                { "off",      "Off" },
                { "work",     "Work" },
                { "sleep",    "Sleep" },
                { "personal", "Personal" }
            };

            m_focusModeButtons.clear();
            for (const auto &m : modes) {
                QPushButton *b = new QPushButton(m.label);
                b->setCursor(Qt::PointingHandCursor);
                b->setFixedHeight(36);
                b->setMinimumWidth(90);
                b->setProperty("modeId", QString(m.id));

                QObject::connect(b, &QPushButton::clicked,
                                 [this, b]() {
                    QString id = b->property("modeId").toString();
                    VisualConfigManager::instance().setAndSave(
                        [id](VisualConfig &c) { c.focusMode = id; });
                    VisualConfigManager::instance().setAndSave(
                        [id](VisualConfig &c) {
                            c.dndEnabled = (id != "off");
                        });
                    refreshFocusModeButtons();
                });

                m_focusModeButtons.append(b);
                row->addWidget(b);
            }
            row->addStretch();
            cv->addLayout(row);

            refreshFocusModeButtons();

            v->addWidget(c);
        }

        // ---- Policy card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("NOTIFICATIONS"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("When Focus is on");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "What happens to incoming notifications");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QStringList opts = { "Allow all", "Priority only",
                                 "Mute everything" };
            QStringList vals = { "all", "priority", "mute" };
            int idx = vals.indexOf(vc.focusPolicy);
            if (idx < 0) idx = 1;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [vals](int i) {
                    if (i < 0 || i >= vals.size()) return;
                    QString v = vals[i];
                    VisualConfigManager::instance().setAndSave(
                        [v](VisualConfig &c) { c.focusPolicy = v; });
                });
            h->addWidget(dd);
            cv->addWidget(row);

            QLabel *hint = new QLabel(
                "Priority only lets notifications from the allowed apps "
                "listed below through. Mute everything silences all.");
            hint->setWordWrap(true);
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        // ---- Schedule card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SCHEDULE"));

            cv->addWidget(Sigil::makeToggleRow(
                "Enable schedule",
                "Turn Focus on automatically during a time window",
                vc.focusScheduleOn,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.focusScheduleOn = on; });
                }));

            // Start / End hour dropdowns
            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Time window");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "From start to end, 24-hour clock");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QStringList hours;
            for (int i = 0; i < 24; ++i)
                hours << QString("%1:00").arg(i, 2, 10, QChar('0'));

            Sigil::Dropdown *startDD = new Sigil::Dropdown(
                hours, vc.focusStartHour,
                [](int i) {
                    VisualConfigManager::instance().setAndSave(
                        [i](VisualConfig &c) { c.focusStartHour = i; });
                });
            Sigil::Dropdown *endDD = new Sigil::Dropdown(
                hours, vc.focusEndHour,
                [](int i) {
                    VisualConfigManager::instance().setAndSave(
                        [i](VisualConfig &c) { c.focusEndHour = i; });
                });

            h->addWidget(startDD);
            QLabel *toLbl = new QLabel("to");
            toLbl->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            h->addWidget(toLbl);
            h->addWidget(endDD);
            cv->addWidget(row);

            v->addWidget(c);
        }

        // ---- Allowed apps card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("ALLOWED APPS"));

            QLabel *hint = new QLabel(
                "These apps can break through Focus when policy is "
                "Priority only.");
            hint->setWordWrap(true);
            hint->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(hint);

            // List installed apps (from .desktop files) with checkboxes
            QListWidget *list = new QListWidget;
            list->setFrameShape(QFrame::NoFrame);
            list->setMinimumHeight(180);

            QMap<QString, QString> appMap;
            QDir appDir("/usr/share/applications");
            for (const QString &f :
                 appDir.entryList({ "*.desktop" }, QDir::Files)) {
                QFile file(appDir.filePath(f));
                if (!file.open(QIODevice::ReadOnly)) continue;
                QString name;
                for (const QString &line :
                     QString::fromUtf8(file.readAll()).split('\n')) {
                    if (line.startsWith("Name=") && name.isEmpty()) {
                        name = line.mid(5).trimmed();
                    }
                }
                if (name.isEmpty() || appMap.contains(name)) continue;
                appMap[name] = f;
            }

            for (auto it = appMap.constBegin();
                 it != appMap.constEnd(); ++it) {
                QListWidgetItem *item = new QListWidgetItem(it.key());
                item->setData(Qt::UserRole, it.value());
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                bool checked =
                    vc.focusAllowedApps.contains(it.key());
                item->setCheckState(checked ? Qt::Checked
                                            : Qt::Unchecked);
                list->addItem(item);
            }

            QObject::connect(list, &QListWidget::itemChanged,
                             [](QListWidgetItem *item) {
                auto &c = VisualConfigManager::instance().cfg();
                QStringList allowed = c.focusAllowedApps;
                QString name = item->text();
                bool want = (item->checkState() == Qt::Checked);
                if (want && !allowed.contains(name)) allowed << name;
                if (!want) allowed.removeAll(name);
                VisualConfigManager::instance().setAndSave(
                    [allowed](VisualConfig &c) {
                        c.focusAllowedApps = allowed;
                    });
            });

            cv->addWidget(list);
            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeScreensaverPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Screensaver"));

        auto &vc = VisualConfigManager::instance().cfg();

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("SCREENSAVER"));

            cv->addWidget(Sigil::makeToggleRow(
                "Enable screensaver",
                "Show a soft overlay before the screen turns off",
                vc.screensaverEnabled,
                [this](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) {
                            c.screensaverEnabled = on; });
                    applySwayidle();
                }));

            {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel("Start after idle");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(
                    QString("%1 min").arg(vc.screensaverTimeoutMin));
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(70);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(1, 60);
                sl->setValue(vc.screensaverTimeoutMin);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);
                QObject::connect(sl, &QSlider::valueChanged,
                                 [this, val](int x) {
                    val->setText(QString("%1 min").arg(x));
                    VisualConfigManager::instance().setAndSave(
                        [x](VisualConfig &c) {
                            c.screensaverTimeoutMin = x; });
                    applySwayidle();
                });
                rv->addWidget(sl);
                cv->addWidget(row);
            }

            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("STYLE"));

            QStringList opts = { "Dim desktop", "Clock",
                                 "Image slideshow", "Video" };
            QStringList values = { "dim", "clock",
                                   "slideshow", "video" };
            int idx = values.indexOf(vc.screensaverStyle);
            if (idx < 0) idx = 1;

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);
            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Style");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel("What to show when idle");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [values](int i) {
                    if (i < 0 || i >= values.size()) return;
                    QString v = values[i];
                    VisualConfigManager::instance().setAndSave(
                        [v](VisualConfig &c) {
                            c.screensaverStyle = v; });
                });
            h->addWidget(dd);
            cv->addWidget(row);

            QWidget *row2 = new QWidget;
            QHBoxLayout *h2 = new QHBoxLayout(row2);
            h2->setContentsMargins(0, 6, 0, 6);
            h2->setSpacing(14);
            QVBoxLayout *l2 = new QVBoxLayout;
            l2->setContentsMargins(0, 0, 0, 0);
            l2->setSpacing(2);
            QLabel *t2 = new QLabel("Image folder");
            t2->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s2 = new QLabel(
                vc.screensaverImageDir.isEmpty()
                    ? "Not set — slideshow falls back to dim"
                    : vc.screensaverImageDir);
            s2->setWordWrap(true);
            s2->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            l2->addWidget(t2);
            l2->addWidget(s2);
            h2->addLayout(l2, 1);

            QPushButton *chooseDir = new QPushButton("Choose…");
            chooseDir->setCursor(Qt::PointingHandCursor);
            chooseDir->setFixedHeight(30);
            chooseDir->setMinimumWidth(110);
            chooseDir->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(chooseDir, &QPushButton::clicked,
                             [this, s2]() {
                bool ok = false;
                QString path = ApokolipsInputDialog::getText(
                    this, "Image folder",
                    "Folder with images for the slideshow:",
                    QDir::homePath() + "/Pictures", &ok);
                if (!ok || path.isEmpty()) return;
                if (!QDir(path).exists()) return;
                VisualConfigManager::instance().setAndSave(
                    [path](VisualConfig &c) {
                        c.screensaverImageDir = path; });
                s2->setText(path);
            });
            h2->addWidget(chooseDir);
            cv->addWidget(row2);

            QWidget *row3 = new QWidget;
            QHBoxLayout *h3 = new QHBoxLayout(row3);
            h3->setContentsMargins(0, 6, 0, 6);
            h3->setSpacing(14);
            QVBoxLayout *l3 = new QVBoxLayout;
            l3->setContentsMargins(0, 0, 0, 0);
            l3->setSpacing(2);
            QLabel *t3 = new QLabel("Video file");
            t3->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s3 = new QLabel(
                vc.screensaverVideoPath.isEmpty()
                    ? "Not set — video falls back to clock"
                    : vc.screensaverVideoPath);
            s3->setWordWrap(true);
            s3->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            l3->addWidget(t3);
            l3->addWidget(s3);
            h3->addLayout(l3, 1);

            QPushButton *chooseVid = new QPushButton("Choose…");
            chooseVid->setCursor(Qt::PointingHandCursor);
            chooseVid->setFixedHeight(30);
            chooseVid->setMinimumWidth(110);
            chooseVid->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(chooseVid, &QPushButton::clicked,
                             [this, s3]() {
                bool ok = false;
                QString path = ApokolipsInputDialog::getText(
                    this, "Video file",
                    "Full path to a video file (mp4 / webm / mkv):",
                    QDir::homePath() + "/Videos", &ok);
                if (!ok || path.isEmpty()) return;
                if (!QFile::exists(path)) return;
                VisualConfigManager::instance().setAndSave(
                    [path](VisualConfig &c) {
                        c.screensaverVideoPath = path; });
                s3->setText(path);
            });
            h3->addWidget(chooseVid);
            cv->addWidget(row3);

            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("TEST"));

            QPushButton *test = new QPushButton("Preview Screensaver");
            test->setCursor(Qt::PointingHandCursor);
            test->setFixedHeight(32);
            test->setMinimumWidth(200);
            test->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 8px;"
                "  padding: 4px 18px; font-size: 12px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(test, &QPushButton::clicked, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--screensaver" });
            });
            QHBoxLayout *h = new QHBoxLayout;
            h->addWidget(test);
            h->addStretch();
            cv->addLayout(h);

            QLabel *hint = new QLabel(
                "Click any key or move the mouse to dismiss.");
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makePrintersPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Printers & Scanners"));

        // ---- Default printer card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("DEFAULT"));

            m_printerDefault = new QLabel("No system default destination");
            m_printerDefault->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_printerDefault);

            v->addWidget(c);
        }

        // ---- Printers card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->addWidget(Sigil::groupLabel("PRINTERS"));
            hdr->addStretch();

            QPushButton *add = new QPushButton("Add Printer…");
            add->setCursor(Qt::PointingHandCursor);
            add->setFixedHeight(28);
            add->setMinimumWidth(120);
            add->setStyleSheet(
                "QPushButton { background: rgb(168, 50, 50);"
                "  color: #ffffff; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px;"
                "  font-weight: 600; }"
                "QPushButton:hover { background: rgb(200, 74, 74); }");
            QObject::connect(add, &QPushButton::clicked, [this]() {
                // CUPS web UI handles add — real, standard, safe.
                QProcess::startDetached("xdg-open",
                    { "http://localhost:631/admin" });
            });
            hdr->addWidget(add);

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(28);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             [this]() { refreshPrintersPage(); });
            hdr->addWidget(refresh);
            cv->addLayout(hdr);

            m_printerList = new QListWidget;
            m_printerList->setFrameShape(QFrame::NoFrame);
            m_printerList->setMinimumHeight(200);
            m_printerList->setContextMenuPolicy(Qt::CustomContextMenu);
            QObject::connect(m_printerList,
                             &QListWidget::customContextMenuRequested,
                             [this](const QPoint &pos) {
                QListWidgetItem *it = m_printerList->itemAt(pos);
                if (!it) return;
                QString name = it->data(Qt::UserRole).toString();
                if (name.isEmpty()) return;

                QMenu m(this);
                m.setStyleSheet(
                    "QMenu {"
                    "  background: rgb(24, 22, 30);"
                    "  border: 1px solid rgba(255,255,255,30);"
                    "  border-radius: 10px;"
                    "  padding: 6px;"
                    "  color: rgb(240, 237, 232);"
                    "  font-size: 13px;"
                    "}"
                    "QMenu::item {"
                    "  padding: 7px 24px 7px 16px;"
                    "  border-radius: 6px;"
                    "}"
                    "QMenu::item:selected {"
                    "  background: rgba(168, 50, 50, 90);"
                    "}");

                QAction *testPage = m.addAction("Print test page");
                QAction *setDefault = m.addAction("Set as default");
                m.addSeparator();
                QAction *remove = m.addAction("Remove printer…");

                QAction *chosen = m.exec(
                    m_printerList->mapToGlobal(pos));
                if (chosen == testPage) {
                    QProcess::startDetached("lp", { "-d", name,
                        "/usr/share/cups/data/testprint" });
                } else if (chosen == setDefault) {
                    QProcess::startDetached("pkexec", { "lpadmin",
                        "-d", name });
                    QTimer::singleShot(1500, this,
                        [this]() { refreshPrintersPage(); });
                } else if (chosen == remove) {
                    QMessageBox box(this);
                    box.setWindowTitle("Remove printer");
                    box.setWindowFlags(Qt::Dialog
                                       | Qt::FramelessWindowHint);
                    box.setText(QString("Remove printer \"%1\"?").arg(name));
                    box.setStandardButtons(QMessageBox::Cancel
                                           | QMessageBox::Yes);
                    box.setDefaultButton(QMessageBox::Cancel);
                    box.setStyleSheet(
                        "QMessageBox { background: rgb(24, 22, 30); }"
                        "QMessageBox QLabel { color: rgb(240, 237, 232);"
                        "  font-size: 13px; }"
                        "QPushButton {"
                        "  background: rgba(255,255,255,20);"
                        "  color: rgb(240, 237, 232);"
                        "  border: none; border-radius: 7px;"
                        "  padding: 6px 18px; min-width: 80px;"
                        "}"
                        "QPushButton:hover { background: rgba(255,255,255,45); }");
                    box.adjustSize();
                    if (QWidget *top = window()) {
                        QPoint pt = top->mapToGlobal(QPoint(
                            (top->width() - box.width()) / 2,
                            (top->height() - box.height()) / 2));
                        box.move(pt);
                    }
                    if (box.exec() != QMessageBox::Yes) return;
                    QProcess::startDetached("pkexec",
                        { "lpadmin", "-x", name });
                    QTimer::singleShot(1500, this,
                        [this]() { refreshPrintersPage(); });
                }
            });
            cv->addWidget(m_printerList);
            refreshPrintersPage();

            QLabel *hint = new QLabel(
                "Right-click a printer for test page, default, and "
                "remove actions. New printers are added via the CUPS "
                "web interface.");
            hint->setWordWrap(true);
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    void refreshPrintersPage() {
        if (m_printerList) {
            m_printerList->clear();
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput());
                    p->deleteLater();
                    if (!m_printerList) return;

                    QStringList lines =
                        out.split('\n', Qt::SkipEmptyParts);
                    if (lines.isEmpty()) {
                        QListWidgetItem *it = new QListWidgetItem(
                            "No printers configured");
                        it->setFlags(Qt::NoItemFlags);
                        it->setForeground(QColor(122, 120, 115));
                        m_printerList->addItem(it);
                    } else {
                        for (const QString &line : lines) {
                            QString text = line.trimmed();
                            QString name;
                            if (text.startsWith("printer ")) {
                                name = text.section(' ', 1, 1);
                            }
                            QString label = text;
                            if (label.length() > 90)
                                label = label.left(87) + "…";
                            QListWidgetItem *it =
                                new QListWidgetItem(label);
                            it->setData(Qt::UserRole, name);
                            it->setForeground(QColor(240, 237, 232));
                            m_printerList->addItem(it);
                        }
                    }
                });
            p->start("lpstat", { "-p" });
        }

        if (m_printerDefault) {
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput()).trimmed();
                    p->deleteLater();
                    if (!m_printerDefault) return;
                    if (out.isEmpty()
                        || out.contains("no system default"))
                        m_printerDefault->setText(
                            "No system default destination");
                    else {
                        QString name = out;
                        if (name.startsWith(
                                "system default destination: "))
                            name = name.mid(28);
                        m_printerDefault->setText(name);
                    }
                });
            p->start("lpstat", { "-d" });
        }
    }

    void refreshFocusModeButtons() {
        auto &c = VisualConfigManager::instance().cfg();

        QString label;
        if (c.focusMode == "work")            label = "Work";
        else if (c.focusMode == "sleep")      label = "Sleep";
        else if (c.focusMode == "personal")   label = "Personal";
        else                                  label = "Off";
        if (m_focusModeStatus) m_focusModeStatus->setText(label);

        for (QPushButton *b : m_focusModeButtons) {
            if (!b) continue;
            QString id = b->property("modeId").toString();
            bool selected = (id == c.focusMode);

            b->setStyleSheet(QString(
                "QPushButton {"
                "  background: %1;"
                "  color: %2;"
                "  border: none;"
                "  border-radius: 8px;"
                "  padding: 4px 18px;"
                "  font-size: 12px;"
                "  font-weight: %3;"
                "}"
                "QPushButton:hover { background: %4; }")
                .arg(selected ? "rgb(168, 50, 50)"
                              : "rgba(255,255,255,20)",
                     selected ? "#ffffff"
                              : Sigil::textPrimary().name(),
                     selected ? "600" : "400",
                     selected ? "rgb(200, 74, 74)"
                              : "rgba(255,255,255,45)"));
        }
    }

    QWidget *makeControlCenterPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Control Center"));

        auto &vc = VisualConfigManager::instance().cfg();

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("MODULES"));

            auto addToggle = [&](const QString &title,
                                 const QString &sub,
                                 bool initial,
                                 std::function<void(bool)> onChange) {
                cv->addWidget(Sigil::makeToggleRow(title, sub,
                                                   initial, onChange));
            };

            addToggle("Wi-Fi",
                "Show connection status and quick toggle",
                vc.ccShowWiFi,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowWiFi = on; });
                });

            addToggle("Bluetooth",
                "Show device status",
                vc.ccShowBluetooth,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowBluetooth = on; });
                });

            addToggle("Now Playing",
                "Media card with play / pause / skip",
                vc.ccShowMedia,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowMedia = on; });
                });

            addToggle("Quick toggles",
                "Focus, Battery, Alerts, Display row",
                vc.ccShowToggles,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowToggles = on; });
                });

            addToggle("Sound",
                "Master volume slider",
                vc.ccShowSound,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowSound = on; });
                });

            addToggle("Display",
                "Brightness slider",
                vc.ccShowDisplay,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.ccShowDisplay = on; });
                });

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeWallpaperPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(20);

        v->addWidget(Sigil::sectionTitle("Wallpaper"));

        Sigil::Card *c = new Sigil::Card;
        c->setMinimumHeight(210);
        QVBoxLayout *cv = new QVBoxLayout(c);
        cv->setContentsMargins(20, 16, 20, 18);
        cv->setSpacing(14);

        cv->addWidget(Sigil::groupLabel("CHOOSE A WALLPAPER"));

        QGridLayout *g = new QGridLayout;
        g->setSpacing(10);

        auto &wpCfg = VisualConfigManager::instance().cfg();
        // thumbName: filename inside assets/wallpapers/thumbs/
        //            (empty = no thumbnail, text-only tile)
        struct WP {
            const char *id;
            const char *name;
            bool        dev;
            const char *thumbName;
        };
        const WP wps[] = {
            { "ruby",       "Magnific",       false, "magnific.jpg"       },
            { "starfield",  "Starfield",      false, "starfield.jpg"      },
            { "aurora",     "Aurora",         false, "aurora.jpg"         },
            { "goldengate", "Golden Gate",    false, "goldengate.jpg"     },
            { "everest",    "Mount Everest",  false, "mount-everest.jpg"  },
            { "neonjinx",   "Neon Jinx",      false, "neonjinx.jpg"       },
            { "gif",        "GIF…",           false, nullptr              },
            { "custom",     "Pick Image…",    false, nullptr              },
            { "apocalypse", "Apocalypse",     true,  "apocalypse.jpg"     },
            { "mob",        "Mob",            true,  "mob.jpg"            },
            { "vander",     "Vander Warwick", true,  "vander.jpg"         }
        };
        auto resolveThumb = [](const QString &rel) -> QString {
            if (rel.isEmpty()) return {};
            QStringList cands = {
                QDir::homePath() + "/apokolips-shell/" + rel,
                "/home/lytone/apokolips-shell/" + rel,
                rel,
                "/usr/share/apokolips/wallpapers/"
                    + QFileInfo(rel).fileName()
            };
            for (const QString &c : cands)
                if (QFile::exists(c)) return c;
            return {};
        };
        int col = 0, row = 0;
        for (const auto &wp : wps) {
            // Dev wallpapers stay hidden until the user has picked
            // one from the desktop right-click menu at least once.
            if (wp.dev
                && !wpCfg.discoveredWallpapers.contains(
                       QString::fromUtf8(wp.id))) {
                continue;
            }

            QPushButton *btn = new QPushButton;
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(46);
            btn->setProperty("wpId", wp.id);

            // Resolve thumbnail path from the explicit filename.
            // Empty thumbName = text-only tile.
            QString thumb;
            if (wp.thumbName && *wp.thumbName) {
                thumb = resolveThumb(
                    QString("assets/wallpapers/thumbs/%1")
                        .arg(QString::fromUtf8(wp.thumbName)));
            }
            // "Pick Image…" tile shows the user's chosen image
            if (QString(wp.id) == "custom"
                && !wpCfg.customWallpaperPath.isEmpty()
                && QFile::exists(wpCfg.customWallpaperPath))
                thumb = wpCfg.customWallpaperPath;

            if (!thumb.isEmpty()) {
                // Use border-image (Qt understands it and stretches)
                // instead of background-image + background-size
                // (which Qt does not understand — see journal:
                // "Unknown property background-size"). Qt caches
                // border-image per file path, so loading is once.
                btn->setStyleSheet(QString(
                    "QPushButton {"
                    "  border-image: url(%1) 0 0 0 0 stretch stretch;"
                    "  border: 1px solid rgba(255,255,255,55);"
                    "  border-radius: 8px;"
                    "  padding: 0; }"
                    "QPushButton:hover {"
                    "  border: 1px solid rgba(255,255,255,140); }"
                    "QPushButton[selected=\"true\"] {"
                    "  border: 2px solid #ffffff; }")
                    .arg(thumb));
                btn->update();

                QVBoxLayout *bl = new QVBoxLayout(btn);
                bl->setContentsMargins(8, 8, 8, 6);
                bl->addStretch();
                QLabel *lbl = new QLabel(wp.name, btn);
                lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                lbl->setStyleSheet(
                    "color: white;"
                    "background: rgba(0,0,0,90);"
                    "border-radius: 3px;"
                    "padding: 2px 6px;"
                    "font-size: 11px;"
                    "font-weight: 500;");
                bl->addWidget(lbl, 0, Qt::AlignLeft | Qt::AlignBottom);
            } else {
                btn->setText(wp.name);
                btn->setStyleSheet(
                    "QPushButton {"
                    "  background: rgba(255,255,255,15);"
                    "  color: white;"
                    "  border: 1px solid rgba(255,255,255,40);"
                    "  border-radius: 8px;"
                    "  font-size: 12px; }"
                    "QPushButton:hover {"
                    "  background: rgba(255,255,255,30); }"
                    "QPushButton[selected=\"true\"] {"
                    "  border: 2px solid #ffffff;"
                    "  background: rgba(255,255,255,25); }");
            }

            m_wpButtons.append(btn);
            QObject::connect(btn, &QPushButton::clicked,
                             [this, id = QString(wp.id)]() {
                if (id == "custom") {
                    SigilFileDialog dlg(this, "Pick Wallpaper Image",
                                        QDir::homePath() + "/Pictures",
                                        "Images (*.jpg *.jpeg *.png *.webp *.bmp)");
                    if (dlg.exec() != QDialog::Accepted) return;
                    QString path = dlg.selectedFiles().value(0);
                    if (path.isEmpty()) return;

                    // ---- Check image size. Small images look soft
                    // when stretched to the screen. Offer to sharpen.
                    QImage test(path);
                    const int srcW = test.width();
                    const int srcH = test.height();
                    const int screenW = QApplication::primaryScreen()
                        ? QApplication::primaryScreen()
                              ->geometry().width()
                        : 1920;

                    if (srcW > 0 && srcW < 2560) {
                        auto &cfg =
                            VisualConfigManager::instance().cfg();
                        QString prior = cfg.upscaleDecisions
                                            .value(path).toString();

                        if (prior.isEmpty()) {
                            bool remember = false;
                            int ch = SigilDialog::ask(this,
                                "Make wallpaper sharper?",
                                QString("This image is %1 \xC3\x97 %2. "
                                        "It will look soft on your "
                                        "%3-wide screen. Sharpen it?")
                                    .arg(srcW).arg(srcH).arg(screenW),
                                { "Quick (about 10 seconds)",
                                  "Leave as-is" },
                                "Don't ask again for this image",
                                &remember);
                            prior = (ch == 0) ? "quick" : "skip";
                            if (remember) {
                                QString p = path, d = prior;
                                VisualConfigManager::instance()
                                    .setAndSave([p, d](VisualConfig &c) {
                                        c.upscaleDecisions[p] = d;
                                    });
                            }
                        }

                        if (prior == "quick") {
                            QString cacheDir = QDir::homePath()
                                + "/.cache/apokolips/wallpapers";
                            QDir().mkpath(cacheDir);
                            QString hash = QString::number(
                                qHash(path), 16);
                            QString cached = cacheDir + "/"
                                + hash + ".jpg";
                            if (!QFile::exists(cached)) {
                                QImage img(path);
                                if (!img.isNull()) {
                                    QImage big = img.scaledToWidth(
                                        3840,
                                        Qt::SmoothTransformation);
                                    if (big.save(cached, "JPEG", 92)) {
                                        qInfo() << "[wallpaper] "
                                                   "quick-upscaled to"
                                                << big.width()
                                                << "x" << big.height()
                                                << "->" << cached;
                                    }
                                }
                            }
                            if (QFile::exists(cached)) path = cached;
                        }
                    }

                    VisualConfigManager::instance().setAndSave(
                        [path](VisualConfig &c) {
                            c.customWallpaperPath = path;
                        });
                    qInfo() << "[wallpaper] custom picked:" << path;
                }
                if (id == "gif") {
                    bool ok = false;
                    QString path = ApokolipsInputDialog::getText(
                        this, "GIF Wallpaper",
                        "Full path to .gif file:",
                        QDir::homePath() + "/", &ok);
                    if (!ok || path.isEmpty()) return;
                    if (!QFile::exists(path)) return;
                    WallpaperConfig::saveGifPath(path);
                }
                WallpaperConfig::saveId(id);
                Wallpaper *wp = WallpaperConfig::makeById(id);
                Theme newT = wp->theme();
                delete wp;
                m_theme = newT;
                ThemeManager::instance().setTheme(newT);
                updateWallpaperHighlight();
                restyleAll();
                update();
                for (QWidget *child : findChildren<QWidget *>())
                    child->update();
            });
            g->addWidget(btn, row, col);
            if (++col == 3) { col = 0; ++row; }
        }
        cv->addLayout(g);
        v->addWidget(c);
        v->addStretch();
        return page;
    }

    QWidget *makeDockPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Desktop & Dock"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ============================================================
        // Card: DOCK
        // ============================================================
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);

            cv->addWidget(Sigil::groupLabel("DOCK"));

            // Two side-by-side sliders
            QHBoxLayout *slidersRow = new QHBoxLayout;
            slidersRow->setSpacing(24);

            auto makeSliderCol = [&](const QString &label,
                                     int val, int lo, int hi,
                                     std::function<void(int)> onChange)
            {
                QWidget *col = new QWidget;
                QVBoxLayout *cl = new QVBoxLayout(col);
                cl->setContentsMargins(0, 0, 0, 0);
                cl->setSpacing(6);

                QLabel *l = new QLabel(label);
                l->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                cl->addWidget(l);

                QSlider *s = new QSlider(Qt::Horizontal);
                s->setRange(lo, hi);
                s->setValue(val);
                s->setStyleSheet(QString(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgba(255,255,255,180);"
                    "  border-radius: 2px; }"));
                m_sliders.append(s);
                QObject::connect(s, &QSlider::valueChanged, onChange);
                cl->addWidget(s);

                QHBoxLayout *ends = new QHBoxLayout;
                QLabel *loL = new QLabel("Small");
                QLabel *hiL = new QLabel("Large");
                QString endCss = QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name());
                loL->setStyleSheet(endCss);
                hiL->setStyleSheet(endCss);
                ends->addWidget(loL);
                ends->addStretch();
                ends->addWidget(hiL);
                cl->addLayout(ends);

                return col;
            };

            slidersRow->addWidget(makeSliderCol(
                "Size", vc.dockIconSize, 32, 90,
                [](int sz) {
                    VisualConfigManager::instance().setAndSave(
                        [sz](VisualConfig &c) { c.dockIconSize = sz; });
                }), 1);

            slidersRow->addWidget(makeSliderCol(
                "Magnification", int(vc.dockMagnifyMax * 100), 0, 100,
                [](int pct) {
                    VisualConfigManager::instance().setAndSave(
                        [pct](VisualConfig &c) {
                            c.dockMagnifyMax = pct / 100.0;
                        });
                }), 1);

            cv->addLayout(slidersRow);

            // Disabled dropdown rows — Coming Soon
            auto addDisabledDropdown = [&](const QString &title,
                                           const QString &value,
                                           const QString &subtitle)
            {
                QWidget *row = new QWidget;
                row->setAutoFillBackground(false);
                row->setEnabled(false);
                row->setStyleSheet("QWidget { opacity: 130; }");
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                left->addWidget(t);
                QLabel *s = new QLabel(subtitle);
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(s);
                h->addLayout(left, 1);

                QLabel *val = new QLabel(value);
                val->setStyleSheet(QString(
                    "color: %1; font-size: 13px;"
                    " background: rgba(255,255,255,15);"
                    " border-radius: 6px;"
                    " padding: 6px 12px;")
                    .arg(Sigil::textDim().name()));
                h->addWidget(val);

                cv->addWidget(row);
            };

            // Position on screen — real Dropdown
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel("Position on screen");
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel("Where the dock sits");
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    { "Bottom", "Left", "Right" }, vc.dockPosition,
                    [](int idx) {
                        VisualConfigManager::instance().setAndSave(
                            [idx](VisualConfig &c) {
                                c.dockPosition = idx;
                            });
                    });
                h->addWidget(dd);

                cv->addWidget(row);
            }
            // Forward minimize animation style — persisted by name.
            {
                QWidget *row = new QWidget;
                row->setAutoFillBackground(false);
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *title = new QLabel("Minimize windows using");
                title->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *subtitle = new QLabel(
                    "Choose the animation used when minimizing");
                subtitle->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(title);
                left->addWidget(subtitle);
                h->addLayout(left, 1);

                QStringList labels = { "Genie", "Scale", "Suck" };
                QStringList values = { "genie", "scale", "suck" };
                int curIdx = values.indexOf(vc.minimizeStyle);
                if (curIdx < 0) curIdx = 0;

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    labels, curIdx,
                    [values](int idx) {
                        if (idx < 0 || idx >= values.size()) return;
                        QString chosen = values[idx];
                        VisualConfigManager::instance().setAndSave(
                            [chosen](VisualConfig &c) {
                                c.minimizeStyle = chosen;
                            });
                    });
                h->addWidget(dd);
                cv->addWidget(row);
            }
            addDisabledDropdown("Double-click a window's title bar to",
                "Zoom", "ⓘ Coming soon");

            // Minimize into app icon toggle (disabled for now)
            QWidget *minRow = Sigil::makeToggleRow(
                "Minimize windows into application icon",
                "ⓘ Coming soon", false, nullptr);
            minRow->setEnabled(false);
            cv->addWidget(minRow);

            v->addWidget(c);
        }

        // ============================================================
        // Card: DESKTOP
        // ============================================================
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("DESKTOP"));

            cv->addWidget(Sigil::makeToggleRow(
                "Show desktop widgets",
                "Clock, CPU / RAM, weather and calendar on the right",
                vc.widgetsVisible,
                [root = this](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.widgetsVisible = on; });
                    // Find the DesktopWidgets in the shell and toggle it
                    if (QWidget *r = qobject_cast<QWidget *>(root->window())) {
                        for (QWidget *w : r->findChildren<QWidget *>()) {
                            if (w->objectName() == "desktopWidgets") {
                                w->setVisible(on);
                                if (on) w->raise();
                            }
                        }
                    }
                }));

            v->addWidget(c);
        }

        // ============================================================
        // Card: FINDER OPACITY (moved here from Appearance)
        // ============================================================
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("FINDER"));

            QLabel *l = new QLabel("Finder window opacity");
            l->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(l);

            QSlider *s = new QSlider(Qt::Horizontal);
            s->setRange(0, 255);
            s->setValue(vc.finderAlpha);
            s->setStyleSheet(QString(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgba(255,255,255,180);"
                "  border-radius: 2px; }"));
            m_sliders.append(s);
            QObject::connect(s, &QSlider::valueChanged, [](int v) {
                VisualConfigManager::instance().setAndSave(
                    [v](VisualConfig &c) { c.finderAlpha = v; });
            });
            cv->addWidget(s);

            v->addWidget(c);
        }

        v->addWidget(makeGenieCard());

        v->addStretch();
        return page;
    }

    QWidget *makeGenieCard() {
        // Rendered as a section inside makeDockPage.
        QWidget *wrap = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(wrap);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(18);

        auto &vc = VisualConfigManager::instance().cfg();

        // Enable toggle
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("MINIMIZE — GENIE"));
            cv->addWidget(Sigil::makeToggleRow(
                "Enable Genie effect",
                "Windows pour into the dock when minimized",
                vc.genieEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.genieEnabled = on; });
                }));
            v->addWidget(c);
        }

        // Shape sliders card
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("GENIE SHAPE"));

            auto addSlider = [&](const QString &title, const QString &sub,
                                 int value, int lo, int hi,
                                 const QString &suffix,
                                 std::function<void(int)> onChange) {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(QString::number(value) + suffix);
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(48);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(lo, hi);
                sl->setValue(value);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);

                QObject::connect(sl, &QSlider::valueChanged,
                                 [val, suffix, onChange](int x) {
                    val->setText(QString::number(x) + suffix);
                    onChange(x);
                });
                rv->addWidget(sl);

                QLabel *subLbl = new QLabel(sub);
                subLbl->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                rv->addWidget(subLbl);

                cv->addWidget(row);
            };

            addSlider("Duration",
                      "How long the animation takes, in milliseconds",
                      vc.genieDurationMs, 120, 1500, " ms",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.genieDurationMs = x; });
            });
            addSlider("Strips",
                      "Higher = smoother warp, more CPU",
                      vc.genieStrips, 40, 320, "",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.genieStrips = x; });
            });
            addSlider("Stagger",
                      "Per-strip delay — higher makes the pour more dramatic",
                      int(vc.genieStagger * 100), 0, 80, "%",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.genieStagger = x / 100.0; });
            });
            addSlider("Curve",
                      "How aggressively the funnel narrows (×10)",
                      int(vc.genieCurve * 10), 3, 20, "",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.genieCurve = x / 10.0; });
            });
            addSlider("Fade start",
                      "When opacity begins dropping (×100)",
                      int(vc.genieFadeStart * 100), 50, 100, "%",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.genieFadeStart = x / 100.0; });
            });

            v->addWidget(c);
        }

        return wrap;
    }

    QWidget *makeSoundPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(20);

        v->addWidget(Sigil::sectionTitle("Sound"));

        // Volume card
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(96);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(10);
            cv->addWidget(Sigil::groupLabel("MASTER VOLUME"));

            QHBoxLayout *row = new QHBoxLayout;
            QLabel *t = new QLabel("Output level");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            row->addWidget(t);
            row->addStretch();

            QSlider *vol = new QSlider(Qt::Horizontal);
            vol->setRange(0, 150);
            vol->setValue(SysControl::audioGet());
            vol->setMinimumWidth(280);
            vol->setStyleSheet(QString(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgba(255,255,255,180);"
                "  border-radius: 2px; }"));
            QObject::connect(vol, &QSlider::valueChanged,
                             [](int val) { SysControl::audioSet(val); });
            row->addWidget(vol);
            cv->addLayout(row);
            v->addWidget(c);
        }

        // Devices card
        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(240);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("OUTPUT DEVICES"));
            header->addStretch();
            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             this, &SettingsWindow::refreshSoundDevices);
            header->addWidget(refresh);
            cv->addLayout(header);

            m_soundList = new QListWidget;
            m_soundList->setFrameShape(QFrame::NoFrame);
            cv->addWidget(m_soundList, 1);
            refreshSoundDevices();
            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeDisplayPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(20);

        v->addWidget(Sigil::sectionTitle("Display"));

        // Current output card
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(86);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(8);
            cv->addWidget(Sigil::groupLabel("ACTIVE OUTPUT"));

            m_displayInfo = new QLabel("Querying…");
            m_displayInfo->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_displayInfo);
            v->addWidget(c);
        }

        // Resolution list card
        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(280);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("RESOLUTION"));
            header->addStretch();

            QPushButton *apply = new QPushButton("Apply");
            apply->setCursor(Qt::PointingHandCursor);
            apply->setFixedHeight(26);
            apply->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(apply, &QPushButton::clicked, [this]() {
                QListWidgetItem *it = m_resList->currentItem();
                if (!it) return;
                QString res = it->data(Qt::UserRole).toString();
                QString out = it->data(Qt::UserRole + 1).toString();
                QProcess::startDetached("swaymsg",
                    { "output", out, "mode", res });
            });
            header->addWidget(apply);

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             this, &SettingsWindow::refreshDisplayPage);
            header->addWidget(refresh);

            cv->addLayout(header);

            m_resList = new QListWidget;
            m_resList->setFrameShape(QFrame::NoFrame);
            cv->addWidget(m_resList, 1);
            v->addWidget(c);
        }

        v->addStretch();
        QTimer::singleShot(100, this, &SettingsWindow::refreshDisplayPage);
        return page;
    }

    static void launchGtklock() {
        auto &cfg = VisualConfigManager::instance().cfg();

        QStringList args;

        // Pin GTK theme — avoids a slow theme scan on every lock
        args << "-g" << "Adwaita";

        // Load our Sigil stylesheet if it exists
        QString cssPath = QDir::homePath() + "/.config/gtklock/style.css";
        if (QFile::exists(cssPath))
            args << "-s" << cssPath;

        // Time / date format
        if (cfg.lockShowClock) {
            args << "-t" << (cfg.clock24Hour ? "%H:%M" : "%I:%M %p");
        } else {
            args << "-t" << " ";
        }
        if (cfg.lockShowDate) {
            args << "-D" << "%A, %d %B";
        } else {
            args << "-D" << " ";
        }

        // Wallpaper background
        if (!cfg.lockWallpaper.isEmpty()
            && QFile::exists(cfg.lockWallpaper)) {
            args << "-b" << cfg.lockWallpaper;
        }

        // Idle: hide the form until mouse/key moves
        args << "-H" << "-T" << "4";

        // Attach stdin (gtklock may need it via --daemonize)
        QProcess::startDetached("gtklock", args);
    }

    void applySwayidle() {
        auto &c = VisualConfigManager::instance().cfg();

        // Kill any existing swayidle we spawned
        QProcess::execute("pkill", { "-f", "swayidle -w" });

        // Nothing to do if no timeout is set
        if (c.lockTimeoutMin == 0
            && c.screenOffTimeoutMin == 0
            && c.sleepTimeoutMin == 0)
            return;

        QStringList args = { "-w" };

        // Screensaver first — soft visual layer
        if (c.screensaverEnabled && c.screensaverTimeoutMin > 0) {
            int sec = c.screensaverTimeoutMin * 60;
            QString ssCmd =
                "exec /home/lytone/apokolips-shell/build/"
                "apokolips-shell --screensaver";
            QString ssKill =
                "pkill -f -- '--screensaver'; pkill -x mpv";
            args << QString::number(sec) << ssCmd
                 << "resume" << ssKill;
        }

        // Lock — hard password gate
        if (c.lockTimeoutMin > 0) {
            int sec = c.lockTimeoutMin * 60;
            args << QString::number(sec)
                 << QString("exec /home/lytone/apokolips-shell/"
                            "build/apokolips-shell --lock");
        }

        // Screen off — optionally with lock
        if (c.screenOffTimeoutMin > 0) {
            int sec = c.screenOffTimeoutMin * 60;
            QString offCmd = "swaymsg output \"*\" dpms off";
            args << QString::number(sec)
                 << offCmd
                 << "resume swaymsg output \"*\" dpms on";
        }

        // Sleep — always locks first (before-sleep event)
        if (c.sleepTimeoutMin > 0) {
            int sec = c.sleepTimeoutMin * 60;
            args << QString::number(sec)
                 << "systemctl suspend";
        }

        // Lock before sleep regardless of idle timeouts
        if (c.lockOnSleep) {
            args << "before-sleep"
                 << QString("exec /home/lytone/apokolips-shell/"
                            "build/apokolips-shell --lock");
        }

        QProcess::startDetached("swayidle", args);
    }

    QWidget *makeArcReactorPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Arc Reactor"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Battery status card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("BATTERY"));

            // Async: placeholder, fill on upower finish.
            QVBoxLayout *body = new QVBoxLayout;
            body->setContentsMargins(0, 0, 0, 0);
            body->setSpacing(14);
            cv->addLayout(body);

            QLabel *placeholder = new QLabel("Querying…");
            placeholder->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            body->addWidget(placeholder);

            auto *q = new QProcess(this);
            QObject::connect(q,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [q, body, placeholder](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        q->readAllStandardOutput());
                    q->deleteLater();
                    body->removeWidget(placeholder);
                    placeholder->deleteLater();

                    bool present = out.contains("present:") &&
                        !out.contains("present:             no");

                    if (!present) {
                        QLabel *head = new QLabel("No battery detected");
                        head->setStyleSheet(QString(
                            "color: %1; font-size: 13px;"
                            " background: transparent;")
                            .arg(Sigil::textPrimary().name()));
                        QLabel *sub = new QLabel(
                            "This machine has no battery. Plug in a "
                            "laptop-style power source, or ignore if "
                            "this is a desktop.");
                        sub->setWordWrap(true);
                        sub->setStyleSheet(QString(
                            "color: %1; font-size: 11px;"
                            " background: transparent;")
                            .arg(Sigil::textDim().name()));
                        body->addWidget(head);
                        body->addWidget(sub);
                        return;
                    }

                    auto getField = [&out](const QString &key) -> QString {
                        for (const QString &line : out.split('\n')) {
                            QString t = line.trimmed();
                            if (t.startsWith(key + ":")) {
                                return t.section(':', 1).trimmed();
                            }
                        }
                        return {};
                    };

                    QString pct  = getField("percentage");
                    QString st   = getField("state");
                    QString rate = getField("energy-rate");
                    QString full = getField("energy-full");
                    QString cap  = getField("capacity");

                    QHBoxLayout *bigRow = new QHBoxLayout;
                    QLabel *big = new QLabel(pct);
                    big->setStyleSheet(QString(
                        "color: %1; font-size: 34px; font-weight: 300;"
                        " background: transparent; letter-spacing: 1px;")
                        .arg(Sigil::textPrimary().name()));
                    bigRow->addWidget(big);
                    bigRow->addStretch();
                    QLabel *stLbl = new QLabel(st);
                    stLbl->setStyleSheet(QString(
                        "color: %1; font-size: 13px;"
                        " background: transparent;")
                        .arg(Sigil::textSecondary().name()));
                    bigRow->addWidget(stLbl, 0, Qt::AlignVCenter);
                    body->addLayout(bigRow);

                    auto addDetail =
                        [body](const QString &k, const QString &val) {
                            if (val.isEmpty()) return;
                            QHBoxLayout *h = new QHBoxLayout;
                            QLabel *l = new QLabel(k);
                            l->setStyleSheet(QString(
                                "color: %1; font-size: 12px;"
                                " background: transparent;")
                                .arg(Sigil::textDim().name()));
                            QLabel *r = new QLabel(val);
                            r->setStyleSheet(QString(
                                "color: %1; font-size: 12px;"
                                " background: transparent;")
                                .arg(Sigil::textSecondary().name()));
                            h->addWidget(l);
                            h->addStretch();
                            h->addWidget(r);
                            body->addLayout(h);
                        };
                    addDetail("Power draw",   rate);
                    addDetail("Total energy", full);
                    addDetail("Health",       cap);
                });
            q->start("upower", { "-i",
                "/org/freedesktop/UPower/devices/battery_BAT0" });
            v->addWidget(c);
        }

        // ---- Power saving card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("POWER SAVING"));

            cv->addWidget(Sigil::makeToggleRow(
                "Low power mode",
                "Reduce CPU performance to save battery",
                vc.lowPowerMode,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.lowPowerMode = on; });
                    // Apply CPU governor via cpupower
                    QProcess::startDetached("pkexec", {
                        "cpupower", "frequency-set", "-g",
                        on ? "powersave" : "ondemand" });
                }));

            v->addWidget(c);
        }

        // ---- Timeouts card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("TIMEOUTS"));

            QStringList opts = { "Never", "5 min", "15 min", "30 min",
                                 "1 hour", "2 hours", "4 hours" };
            QList<int> vals = { 0, 5, 15, 30, 60, 120, 240 };

            auto makeTimeoutRow = [&](const QString &title,
                                      const QString &sub,
                                      int currentMin,
                                      std::function<void(int)> onChange)
            {
                QWidget *row = new QWidget;
                QHBoxLayout *h = new QHBoxLayout(row);
                h->setContentsMargins(0, 6, 0, 6);
                h->setSpacing(14);

                QVBoxLayout *left = new QVBoxLayout;
                left->setContentsMargins(0, 0, 0, 0);
                left->setSpacing(2);
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *s = new QLabel(sub);
                s->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                left->addWidget(t);
                left->addWidget(s);
                h->addLayout(left, 1);

                int idx = vals.indexOf(currentMin);
                if (idx < 0) idx = 0;

                Sigil::Dropdown *dd = new Sigil::Dropdown(
                    opts, idx,
                    [onChange, vals](int i) {
                        if (i >= 0 && i < vals.size())
                            onChange(vals[i]);
                    });
                h->addWidget(dd);
                cv->addWidget(row);
            };

            makeTimeoutRow("Turn off screen after",
                           "Display sleeps when idle",
                           vc.screenOffTimeoutMin,
                           [this](int m) {
                VisualConfigManager::instance().setAndSave(
                    [m](VisualConfig &c) { c.screenOffTimeoutMin = m; });
                applySwayidle();
            });

            makeTimeoutRow("Sleep after",
                           "System suspends when idle",
                           vc.sleepTimeoutMin,
                           [this](int m) {
                VisualConfigManager::instance().setAndSave(
                    [m](VisualConfig &c) { c.sleepTimeoutMin = m; });
                applySwayidle();
            });

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeBluetoothPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);
        v->addWidget(Sigil::sectionTitle("Bluetooth"));

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("STATUS"));

            // Async: placeholder, chain list → show → toggle
            QLabel *btPlaceholder = new QLabel("Querying…");
            btPlaceholder->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(btPlaceholder);

            auto *q = new QProcess(this);
            QObject::connect(q,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [this, q, cv, btPlaceholder](int, QProcess::ExitStatus) {
                    bool hasAdapter = !q->readAllStandardOutput()
                        .trimmed().isEmpty();
                    q->deleteLater();
                    cv->removeWidget(btPlaceholder);
                    btPlaceholder->deleteLater();

                    if (!hasAdapter) {
                        QLabel *head = new QLabel(
                            "No Bluetooth hardware detected");
                        head->setStyleSheet(QString(
                            "color: %1; font-size: 13px;"
                            " background: transparent;")
                            .arg(Sigil::textPrimary().name()));
                        QLabel *sub = new QLabel(
                            "This machine has no Bluetooth adapter, "
                            "or the service is not running. Plug in "
                            "a USB Bluetooth adapter and restart "
                            "bluetooth.service, then reopen this "
                            "page.");
                        sub->setWordWrap(true);
                        sub->setStyleSheet(QString(
                            "color: %1; font-size: 11px;"
                            " background: transparent;")
                            .arg(Sigil::textDim().name()));
                        cv->addWidget(head);
                        cv->addWidget(sub);
                        return;
                    }

                    auto *pw = new QProcess(this);
                    QObject::connect(pw,
                        QOverload<int, QProcess::ExitStatus>::of(
                            &QProcess::finished),
                        [pw, cv](int, QProcess::ExitStatus) {
                            QString out = QString::fromUtf8(
                                pw->readAllStandardOutput());
                            pw->deleteLater();
                            bool powered = out.contains("Powered: yes");
                            cv->addWidget(Sigil::makeToggleRow(
                                "Bluetooth",
                                "Power on to scan and pair devices",
                                powered,
                                [](bool on) {
                                    QProcess::startDetached(
                                        "bluetoothctl",
                                        { "power",
                                          on ? "on" : "off" });
                                }));
                        });
                    pw->start("bluetoothctl", { "show" });
                });
            q->start("bluetoothctl", { "list" });
            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(260);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("DEVICES"));
            header->addStretch();

            QPushButton *scan = new QPushButton("Scan");
            scan->setCursor(Qt::PointingHandCursor);
            scan->setFixedHeight(26);
            scan->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(scan, &QPushButton::clicked, []() {
                QProcess::startDetached("bluetoothctl", { "scan", "on" });
            });
            header->addWidget(scan);

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                [this]() { refreshBluetoothPage(); });
            header->addWidget(refresh);
            cv->addLayout(header);

            m_btList = new QListWidget;
            m_btList->setFrameShape(QFrame::NoFrame);
            cv->addWidget(m_btList, 1);
            refreshBluetoothPage();
            v->addWidget(c);
        }
        v->addStretch();
        return page;
    }

    void refreshBluetoothPage() {
        if (!m_btList) return;
        m_btList->clear();
        auto *q = new QProcess(this);
        QObject::connect(q,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, q](int, QProcess::ExitStatus) {
                QString out = QString::fromUtf8(
                    q->readAllStandardOutput()).trimmed();
                q->deleteLater();
                if (!m_btList) return;
                if (out.isEmpty()) {
                    QListWidgetItem *it = new QListWidgetItem(
                        "No paired devices");
                    it->setFlags(Qt::NoItemFlags);
                    it->setForeground(QColor(122, 120, 115));
                    m_btList->addItem(it);
                    return;
                }
                const QStringList lines =
                    out.split('\n', Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    QString name = line.section(' ', 2);
                    QString mac  = line.section(' ', 1, 1);
                    if (name.isEmpty()) name = mac;
                    m_btList->addItem(name + "   (" + mac + ")");
                }
            });
        q->start("bluetoothctl", { "devices", "Paired" });
    }

    QWidget *makeWiFiPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Wi-Fi"));

        // ---- Radio card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("WI-FI"));

            // Async: fire both nmcli calls in parallel, populate the
            // card in a shared finish handler. No waitForFinished.
            QLabel *placeholder = new QLabel("Querying…");
            placeholder->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(placeholder);

            struct WifiAsync { QString general; bool radio = false; int done = 0; };
            auto *st = new WifiAsync;

            auto finish = [cv, placeholder, st]() {
                if (++st->done < 2) return;
                cv->removeWidget(placeholder);
                placeholder->deleteLater();
                if (!st->general.isEmpty()) {
                    cv->addWidget(Sigil::makeToggleRow(
                        "Wi-Fi",
                        st->radio ? "On" : "Off",
                        st->radio,
                        [](bool on) {
                            QProcess::startDetached("nmcli",
                                { "radio", "wifi", on ? "on" : "off" });
                        }));
                } else {
                    QLabel *head = new QLabel(
                        "No wireless adapter detected");
                    head->setStyleSheet(QString(
                        "color: %1; font-size: 13px; background: transparent;")
                        .arg(Sigil::textPrimary().name()));
                    QLabel *sub = new QLabel(
                        "This machine has no Wi-Fi hardware. Wireless networks "
                        "will appear here once an adapter is present. Ethernet "
                        "settings are under Network.");
                    sub->setWordWrap(true);
                    sub->setStyleSheet(QString(
                        "color: %1; font-size: 11px; background: transparent;")
                        .arg(Sigil::textDim().name()));
                    cv->addWidget(head);
                    cv->addWidget(sub);
                }
                delete st;
            };

            auto *pq = new QProcess(this);
            QObject::connect(pq,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [pq, st, finish](int, QProcess::ExitStatus) {
                    st->general = QString::fromUtf8(
                        pq->readAllStandardOutput()).trimmed();
                    pq->deleteLater();
                    finish();
                });
            pq->start("nmcli", { "-t", "-f", "WIFI", "general" });

            auto *pr = new QProcess(this);
            QObject::connect(pr,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [pr, st, finish](int, QProcess::ExitStatus) {
                    st->radio = QString::fromUtf8(
                        pr->readAllStandardOutput()).trimmed().contains("enabled");
                    pr->deleteLater();
                    finish();
                });
            pr->start("nmcli", { "radio", "wifi" });
            v->addWidget(c);
        }

        // ---- Networks card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(280);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("NETWORKS"));
            header->addStretch();

            QPushButton *scan = new QPushButton("Scan");
            scan->setCursor(Qt::PointingHandCursor);
            scan->setFixedHeight(26);
            scan->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(scan, &QPushButton::clicked,
                [this]() { refreshWiFiPage(); });
            header->addWidget(scan);

            cv->addLayout(header);

            m_wifiStatus = new QLabel("Querying…");
            m_wifiStatus->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(m_wifiStatus);

            m_wifiPageList = new QListWidget;
            m_wifiPageList->setFrameShape(QFrame::NoFrame);
            cv->addWidget(m_wifiPageList, 1);
            refreshWiFiPage();
            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    void refreshWiFiPage() {
        if (!m_wifiPageList) return;
        m_wifiPageList->clear();
        if (m_wifiStatus) m_wifiStatus->setText("Querying…");

        auto *q = new QProcess(this);
        QObject::connect(q,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, q](int, QProcess::ExitStatus) {
                QString out = QString::fromUtf8(
                    q->readAllStandardOutput()).trimmed();
                q->deleteLater();
                if (!m_wifiPageList) return;

                if (out.isEmpty()) {
                    if (m_wifiStatus)
                        m_wifiStatus->setText(
                            "No wireless adapter — Wi-Fi list unavailable.");
                    QListWidgetItem *it = new QListWidgetItem(
                        "No networks (no adapter)");
                    it->setFlags(Qt::NoItemFlags);
                    it->setForeground(QColor(122, 120, 115));
                    m_wifiPageList->addItem(it);
                    return;
                }

                if (m_wifiStatus)
                    m_wifiStatus->setText(
                        QString("%1 network(s) found").arg(
                            out.split('\n', Qt::SkipEmptyParts).size()));

                const QStringList lines =
                    out.split('\n', Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    QStringList parts = line.split(':');
                    if (parts.size() < 4) continue;
                    bool inUse = parts[0].trimmed() == "*";
                    QString ssid = parts[1];
                    QString sig  = parts[2];
                    QString sec  = parts[3];
                    QString text = (inUse ? "●  " : "    ") + ssid
                                 + "   " + sig + "%   " + sec;
                    QListWidgetItem *it = new QListWidgetItem(text);
                    it->setData(Qt::UserRole, ssid);
                    m_wifiPageList->addItem(it);
                }
            });
        q->start("nmcli", { "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                            "device", "wifi", "list" });
    }

    QWidget *makeNetworkPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Network"));

        // ---- Status card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(8);
            cv->addWidget(Sigil::groupLabel("CURRENT CONNECTION"));

            m_networkInfo = new QLabel("Querying…");
            m_networkInfo->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_networkInfo);
            v->addWidget(c);
        }

        // ---- Hint card pointing to Wi-Fi ----
        {
            Sigil::Card *c = new Sigil::Card;
            QHBoxLayout *h = new QHBoxLayout(c);
            h->setContentsMargins(20, 16, 20, 16);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Wireless networks");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Wi-Fi is handled on its own page under Settings → Wi-Fi.");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QPushButton *open = new QPushButton("Open Wi-Fi");
            open->setCursor(Qt::PointingHandCursor);
            open->setFixedHeight(28);
            open->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(open, &QPushButton::clicked, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--settings" });
            });
            h->addWidget(open);

            v->addWidget(c);
        }

        // ---- Ethernet card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(180);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 16, 20, 16);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("ETHERNET"));
            header->addStretch();

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                             this, &SettingsWindow::refreshNetworkPage);
            header->addWidget(refresh);

            cv->addLayout(header);

            m_networkList = new QListWidget;
            m_networkList->setFrameShape(QFrame::NoFrame);
            cv->addWidget(m_networkList, 1);

            v->addWidget(c);
        }

        v->addStretch();
        QTimer::singleShot(100, this, &SettingsWindow::refreshNetworkPage);
        return page;
    }

    void refreshSoundDevices() {
        if (!m_soundList) return;
        m_soundList->clear();

        auto *p = new QProcess(this);
        QObject::connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, p](int, QProcess::ExitStatus) {
                QString out = QString::fromUtf8(
                    p->readAllStandardOutput());
                p->deleteLater();
                if (!m_soundList) return;

                QStringList lines = out.split('\n');
                bool inSinks = false;
                for (const QString &line : lines) {
                    QString trimmed = line.trimmed();
                    if (trimmed.startsWith("Sinks:")) {
                        inSinks = true; continue;
                    }
                    if (trimmed.startsWith("Sources:")) {
                        inSinks = false; continue;
                    }
                    if (!inSinks) continue;
                    if (trimmed.contains('.')) {
                        QString name =
                            trimmed.section('.', 1).trimmed();
                        name.remove(QRegularExpression(
                            "\\[[^]]*\\]"));
                        name = name.trimmed();
                        if (name.isEmpty()) continue;
                        QListWidgetItem *it =
                            new QListWidgetItem("  " + name);
                        m_soundList->addItem(it);
                    }
                }
                if (m_soundList->count() == 0) {
                    m_soundList->addItem("  (No devices reported)");
                }
            });
        p->start("wpctl", { "status" });
    }

    void refreshDisplayPage() {
        if (!m_displayInfo || !m_resList) return;

        auto *p = new QProcess(this);
        QObject::connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, p](int, QProcess::ExitStatus) {
                QJsonDocument doc = QJsonDocument::fromJson(
                    p->readAllStandardOutput());
                p->deleteLater();
                if (!m_displayInfo || !m_resList) return;

                if (!doc.isArray()) {
                    m_displayInfo->setText("(Cannot query display)");
                    return;
                }

                m_resList->clear();
                for (const QJsonValue &v : doc.array()) {
                    QJsonObject o = v.toObject();
                    if (!o.value("active").toBool()) continue;
                    QString name = o.value("name").toString();
                    QJsonObject cur =
                        o.value("current_mode").toObject();
                    int curW = cur.value("width").toInt();
                    int curH = cur.value("height").toInt();
                    m_displayInfo->setText(
                        QString("%1 — %2×%3")
                            .arg(name).arg(curW).arg(curH));

                    for (const QJsonValue &mv :
                         o.value("modes").toArray()) {
                        QJsonObject m = mv.toObject();
                        int w = m.value("width").toInt();
                        int h = m.value("height").toInt();
                        int hz = int(m.value("refresh").toDouble() + 0.5);
                        QString line = QString("  %1×%2  (%3 Hz)")
                            .arg(w).arg(h).arg(hz);
                        QListWidgetItem *it =
                            new QListWidgetItem(line);
                        it->setData(Qt::UserRole,
                            QString("%1x%2@%3Hz").arg(w).arg(h).arg(hz));
                        it->setData(Qt::UserRole + 1, name);
                        if (w == curW && h == curH)
                            it->setSelected(true);
                        m_resList->addItem(it);
                    }
                }
            });
        p->start("swaymsg", { "-t", "get_outputs", "-r" });
    }

    void refreshNetworkPage() {
        // Non-blocking. Every nmcli call fires on a fresh QProcess
        // and parses in its own finished handler. The event loop
        // never waits on the network — UI stays responsive, values
        // populate when nmcli answers (usually < 200 ms).

        // ---- Current connection summary ----
        if (m_networkInfo) {
            auto *p = new QProcess(this);
            QObject::connect(p,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [this, p](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        p->readAllStandardOutput());
                    QString summary;
                    for (const QString &line : out.split('\n')) {
                        QStringList parts =
                            line.trimmed().split(':');
                        if (parts.size() < 3) continue;
                        if (parts[2].startsWith("connected")) {
                            if (!summary.isEmpty())
                                summary += "   ·   ";
                            summary += parts[1] + " (" +
                                       parts[0] + ")";
                        }
                    }
                    if (summary.isEmpty()) summary = "Offline";
                    if (m_networkInfo)
                        m_networkInfo->setText(summary);
                    p->deleteLater();
                });
            p->start("nmcli", { "-t", "-f",
                                "DEVICE,TYPE,STATE", "device" });
        }

        // ---- Ethernet / VPN / Bridge device list ----
        if (m_networkList) {
            m_networkList->clear();
            auto *q = new QProcess(this);
            QObject::connect(q,
                QOverload<int, QProcess::ExitStatus>::of(
                    &QProcess::finished),
                [this, q](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(
                        q->readAllStandardOutput());
                    bool anyAdded = false;
                    for (const QString &line : out.split('\n')) {
                        QString t = line.trimmed();
                        if (t.isEmpty()) continue;
                        QStringList parts = t.split(':');
                        if (parts.size() < 4) continue;
                        const QString dev  = parts[0];
                        const QString type = parts[1];
                        const QString st   = parts[2];
                        const QString conn = parts[3];
                        if (type == "wifi" ||
                            type == "loopback") continue;
                        if (type == "ethernet" &&
                            dev.startsWith("enp") &&
                            st == "unavailable") continue;

                        QString text = dev + "   " + type +
                                       "   " + st;
                        if (!conn.isEmpty())
                            text += "   (" + conn + ")";

                        if (!m_networkList) break;
                        QListWidgetItem *it =
                            new QListWidgetItem(text);
                        if (st.startsWith("connected"))
                            it->setForeground(
                                QColor(240, 237, 232));
                        else
                            it->setForeground(
                                QColor(176, 173, 168));
                        m_networkList->addItem(it);
                        anyAdded = true;
                    }
                    if (!anyAdded && m_networkList) {
                        QListWidgetItem *it = new QListWidgetItem(
                            "No wired, VPN or bridge interfaces "
                            "detected");
                        it->setFlags(Qt::NoItemFlags);
                        it->setForeground(
                            QColor(122, 120, 115));
                        m_networkList->addItem(it);
                    }
                    q->deleteLater();
                });
            q->start("nmcli", { "-t", "-f",
                                "DEVICE,TYPE,STATE,CONNECTION",
                                "device" });
        }
    }

    QWidget *makeDateTimePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Date & Time"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Current time card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setFixedHeight(110);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(8);

            cv->addWidget(Sigil::groupLabel("CURRENT"));

            QLabel *big = new QLabel;
            big->setStyleSheet(QString(
                "color: %1; font-size: 32px; font-weight: 300;"
                " background: transparent; letter-spacing: 1px;")
                .arg(Sigil::textPrimary().name()));

            QLabel *sub = new QLabel;
            sub->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textSecondary().name()));

            QTimer *tick = new QTimer(c);
            tick->setInterval(500);
            QObject::connect(tick, &QTimer::timeout, [big, sub]() {
                auto &cfg = VisualConfigManager::instance().cfg();
                QDateTime now = QDateTime::currentDateTime();
                QString fmt = cfg.clock24Hour ? "HH:mm" : "hh:mm AP";
                if (cfg.clockShowSeconds)
                    fmt = cfg.clock24Hour ? "HH:mm:ss" : "hh:mm:ss AP";
                big->setText(now.toString(fmt));
                sub->setText(now.toString("dddd, d MMMM yyyy"));
            });
            tick->start();
            QDateTime now = QDateTime::currentDateTime();
            big->setText(now.toString(vc.clock24Hour ? "HH:mm" : "hh:mm AP"));
            sub->setText(now.toString("dddd, d MMMM yyyy"));

            cv->addWidget(big);
            cv->addWidget(sub);
            v->addWidget(c);
        }

        // ---- Format card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("FORMAT"));

            cv->addWidget(Sigil::makeToggleRow(
                "Use 24-hour clock",
                "Off uses 12-hour with AM/PM",
                vc.clock24Hour,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.clock24Hour = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Show seconds in clock",
                "Top bar will display HH:MM:SS",
                vc.clockShowSeconds,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.clockShowSeconds = on; });
                }));

            v->addWidget(c);
        }

        // ---- System card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("SYSTEM"));

            QWidget *tzRow = new QWidget;
            QHBoxLayout *tzH = new QHBoxLayout(tzRow);
            tzH->setContentsMargins(0, 6, 0, 6);
            tzH->setSpacing(14);

            QVBoxLayout *tzLeft = new QVBoxLayout;
            tzLeft->setContentsMargins(0, 0, 0, 0);
            tzLeft->setSpacing(2);
            QLabel *tzTitle = new QLabel("Time zone");
            tzTitle->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *tzSub = new QLabel("Read from /etc/timezone");
            tzSub->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            tzLeft->addWidget(tzTitle);
            tzLeft->addWidget(tzSub);
            tzH->addLayout(tzLeft, 1);

            QString tzValue = "Unknown";
            QFile tzf("/etc/timezone");
            if (tzf.open(QIODevice::ReadOnly))
                tzValue = QString::fromUtf8(tzf.readAll()).trimmed();
            QLabel *tzVal = new QLabel(tzValue);
            tzVal->setStyleSheet(QString(
                "color: %1; font-size: 13px;"
                " background: rgba(255,255,255,15);"
                " border-radius: 6px;"
                " padding: 6px 12px;")
                .arg(Sigil::textPrimary().name()));
            tzH->addWidget(tzVal);

            QPushButton *setTz = new QPushButton("Change…");
            setTz->setCursor(Qt::PointingHandCursor);
            setTz->setFixedHeight(30);
            setTz->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  padding: 4px 14px; font-size: 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,40); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(setTz, &QPushButton::clicked, [this]() {
                bool ok = false;
                QString tz = ApokolipsInputDialog::getText(
                    this, "Time zone",
                    "Enter time zone (e.g. Africa/Kampala):",
                    "Africa/Kampala", &ok);
                if (!ok || tz.isEmpty()) return;
                if (!QFile::exists("/usr/share/zoneinfo/" + tz)) return;
                QProcess::execute("ln",
                    { "-sf", "/usr/share/zoneinfo/" + tz,
                      "/etc/localtime" });
                QFile tzf2("/etc/timezone");
                if (tzf2.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    tzf2.write(tz.toUtf8());
            });
            tzH->addWidget(setTz);

            cv->addWidget(tzRow);

            // Async: build with placeholder, fill on timedatectl finish
            QWidget *ntpRow = Sigil::makeToggleRow(
                "Automatic date & time",
                "Sync from network time servers (NTP)",
                false,
                [](bool on) {
                    QProcess::startDetached("timedatectl", {
                        "set-ntp", on ? "true" : "false" });
                });
            cv->addWidget(ntpRow);

            {
                auto *tc = new QProcess(this);
                QObject::connect(tc,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [tc, ntpRow](int, QProcess::ExitStatus) {
                        bool on = QString::fromUtf8(
                            tc->readAllStandardOutput()).trimmed()
                            == "yes";
                        tc->deleteLater();
                        if (auto *t = Sigil::toggleIn(ntpRow))
                            t->setOnSilent(on);
                    });
                tc->start("timedatectl",
                    { "show", "-p", "NTP", "--value" });
            }

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeKeyboardPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Keyboard"));

        auto &vc = VisualConfigManager::instance().cfg();

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("LAYOUT"));

            struct Layout { const char *code; const char *name; };
            const Layout layouts[] = {
                { "us", "English (US)" },
                { "gb", "English (UK)" },
                { "de", "German" },
                { "fr", "French" },
                { "es", "Spanish" },
                { "it", "Italian" },
                { "ru", "Russian" },
                { "jp", "Japanese" },
                { "cn", "Chinese" },
                { "ara", "Arabic" },
                { "sw", "Swahili" },
                { "ke", "Kenyan" }
            };

            QStringList names;
            QStringList codes;
            int currentIdx = 0;
            for (const auto &l : layouts) {
                names << QString(l.name);
                codes << QString(l.code);
                if (QString(l.code) == vc.keyboardLayout)
                    currentIdx = names.size() - 1;
            }

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Layout");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Keyboard layout applied live via swaymsg");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                names, currentIdx,
                [codes](int i) {
                    if (i < 0 || i >= codes.size()) return;
                    QString code = codes[i];
                    VisualConfigManager::instance().setAndSave(
                        [code](VisualConfig &c) { c.keyboardLayout = code; });
                    QProcess::startDetached("swaymsg", {
                        "input", "type:keyboard", "xkb_layout", code });
                });
            h->addWidget(dd);
            cv->addWidget(row);
            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("TYPING"));

            auto addSlider = [&](const QString &title,
                                 const QString &sub,
                                 int value, int lo, int hi,
                                 const QString &suffix,
                                 std::function<void(int)> onChange) {
                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 0, 0, 0);
                rv->setSpacing(6);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *val = new QLabel(QString::number(value) + suffix);
                val->setStyleSheet(QString(
                    "color: %1; font-size: 12px; background: transparent;")
                    .arg(Sigil::textSecondary().name()));
                val->setMinimumWidth(60);
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                top->addWidget(t);
                top->addStretch();
                top->addWidget(val);
                rv->addLayout(top);

                QSlider *sl = new QSlider(Qt::Horizontal);
                sl->setRange(lo, hi);
                sl->setValue(value);
                sl->setStyleSheet(
                    "QSlider::groove:horizontal { height: 4px;"
                    "  background: rgba(255,255,255,25);"
                    "  border-radius: 2px; }"
                    "QSlider::handle:horizontal {"
                    "  background: #ffffff; width: 14px; height: 14px;"
                    "  margin: -5px 0; border-radius: 7px; }"
                    "QSlider::sub-page:horizontal {"
                    "  background: rgb(168,50,50);"
                    "  border-radius: 2px; }");
                m_sliders.append(sl);

                QObject::connect(sl, &QSlider::valueChanged,
                                 [val, suffix, onChange](int x) {
                    val->setText(QString::number(x) + suffix);
                    onChange(x);
                });
                rv->addWidget(sl);

                QLabel *subLbl = new QLabel(sub);
                subLbl->setStyleSheet(QString(
                    "color: %1; font-size: 11px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                rv->addWidget(subLbl);
                cv->addWidget(row);
            };

            addSlider("Repeat delay",
                      "How long you hold a key before it repeats",
                      vc.keyboardRepeatDelay, 100, 1000, " ms",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.keyboardRepeatDelay = x; });
                QProcess::startDetached("swaymsg", {
                    "input", "type:keyboard", "repeat_delay",
                    QString::number(x) });
            });

            addSlider("Repeat rate",
                      "How fast the key repeats while held",
                      vc.keyboardRepeatRate, 1, 60, " /s",
                      [](int x) {
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.keyboardRepeatRate = x; });
                QProcess::startDetached("swaymsg", {
                    "input", "type:keyboard", "repeat_rate",
                    QString::number(x) });
            });

            v->addWidget(c);
        }

        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("CAPS LOCK"));

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Caps Lock behavior");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel("What the Caps Lock key does");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QStringList opts = { "Normal caps lock",
                                 "Act as Escape",
                                 "Disabled" };
            QStringList values = { "default", "escape", "disabled" };
            int idx = values.indexOf(vc.keyboardCapsLock);
            if (idx < 0) idx = 0;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [values](int i) {
                    if (i < 0 || i >= values.size()) return;
                    QString val = values[i];
                    VisualConfigManager::instance().setAndSave(
                        [val](VisualConfig &c) {
                            c.keyboardCapsLock = val;
                        });
                    QString xkbOpt;
                    if (val == "escape")   xkbOpt = "caps:escape";
                    else if (val == "disabled") xkbOpt = "caps:none";
                    else xkbOpt = "";
                    QProcess::startDetached("swaymsg", {
                        "input", "type:keyboard",
                        "xkb_options", xkbOpt });
                });
            h->addWidget(dd);
            cv->addWidget(row);
            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeMousePage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Mouse & Trackpad"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Mouse card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(18);
            cv->addWidget(Sigil::groupLabel("MOUSE"));

            // Pointer speed slider
            QWidget *row = new QWidget;
            QVBoxLayout *rv = new QVBoxLayout(row);
            rv->setContentsMargins(0, 0, 0, 0);
            rv->setSpacing(6);

            QHBoxLayout *top = new QHBoxLayout;
            QLabel *t = new QLabel("Pointer speed");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *val = new QLabel(
                vc.mouseAccel < 0
                    ? QString("Slow (%1)").arg(vc.mouseAccel)
                    : (vc.mouseAccel > 0
                        ? QString("Fast (+%1)").arg(vc.mouseAccel)
                        : QString("Normal")));
            val->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            val->setMinimumWidth(80);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            top->addWidget(t);
            top->addStretch();
            top->addWidget(val);
            rv->addLayout(top);

            QSlider *sl = new QSlider(Qt::Horizontal);
            sl->setRange(-10, 10);
            sl->setValue(vc.mouseAccel);
            sl->setStyleSheet(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgb(168,50,50);"
                "  border-radius: 2px; }");
            m_sliders.append(sl);

            QObject::connect(sl, &QSlider::valueChanged,
                             [val](int x) {
                val->setText(x < 0 ? QString("Slow (%1)").arg(x)
                                   : x > 0
                                        ? QString("Fast (+%1)").arg(x)
                                        : QString("Normal"));
                VisualConfigManager::instance().setAndSave(
                    [x](VisualConfig &c) { c.mouseAccel = x; });
                // sway's pointer accel runs -1.0 .. 1.0
                double a = x / 10.0;
                QProcess::startDetached("swaymsg", {
                    "input", "type:pointer", "accel_profile", "adaptive",
                    "accel_speed",
                    QString::number(a, 'f', 2) });
            });
            rv->addWidget(sl);

            QLabel *subLbl = new QLabel(
                "Adjusts acceleration curve for pointing devices");
            subLbl->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            rv->addWidget(subLbl);
            cv->addWidget(row);

            v->addWidget(c);
        }

        // ---- Trackpad card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("TRACKPAD"));

            auto makeRow = [&](const QString &title, const QString &sub,
                               bool on, std::function<void(bool)> onChange) {
                return Sigil::makeToggleRow(title, sub, on, onChange);
            };

            cv->addWidget(makeRow(
                "Natural scrolling",
                "Content moves with your fingers",
                vc.touchpadNatural,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.touchpadNatural = on; });
                    QProcess::startDetached("swaymsg", {
                        "input", "type:touchpad", "natural_scroll",
                        on ? "enabled" : "disabled" });
                }));

            cv->addWidget(makeRow(
                "Tap to click",
                "Tap the trackpad without pressing",
                vc.touchpadTap,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.touchpadTap = on; });
                    QProcess::startDetached("swaymsg", {
                        "input", "type:touchpad", "tap",
                        on ? "enabled" : "disabled" });
                }));

            cv->addWidget(makeRow(
                "Two-finger scrolling",
                "Scroll with two fingers instead of one",
                vc.touchpadTwoFinger,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.touchpadTwoFinger = on; });
                    QProcess::startDetached("swaymsg", {
                        "input", "type:touchpad",
                        "scroll_method",
                        on ? "two_finger" : "edge" });
                }));

            cv->addWidget(makeRow(
                "Disable while typing",
                "Ignore taps while the keyboard is in use",
                vc.touchpadDisableWhileTyping,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) {
                            c.touchpadDisableWhileTyping = on;
                        });
                    QProcess::startDetached("swaymsg", {
                        "input", "type:touchpad",
                        "dwt",
                        on ? "enabled" : "disabled" });
                }));

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeLockScreenPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Lock Screen & Security"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Lock Screen card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("LOCK SCREEN"));

            // Lock now button
            QWidget *nowRow = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(nowRow);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Lock screen now");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "Immediately lock the screen (requires password to unlock)");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QPushButton *lock = new QPushButton("Lock");
            lock->setCursor(Qt::PointingHandCursor);
            lock->setFixedHeight(30);
            lock->setMinimumWidth(80);
            lock->setStyleSheet(QString(
                "QPushButton { background: rgba(168,50,50,200);"
                "  color: #ffffff; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(200,74,74,220); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(lock, &QPushButton::clicked,
                             []() { launchGtklock(); });
            h->addWidget(lock);

            cv->addWidget(nowRow);

            // Lock timeout dropdown
            QWidget *timeoutRow = new QWidget;
            QHBoxLayout *th = new QHBoxLayout(timeoutRow);
            th->setContentsMargins(0, 6, 0, 6);
            th->setSpacing(14);

            QVBoxLayout *tl = new QVBoxLayout;
            tl->setContentsMargins(0, 0, 0, 0);
            tl->setSpacing(2);
            QLabel *tt = new QLabel("Lock after idle");
            tt->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *ts = new QLabel(
                "Auto-lock when the screen has been idle");
            ts->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            tl->addWidget(tt);
            tl->addWidget(ts);
            th->addLayout(tl, 1);

            QStringList opts = { "Never", "1 min", "5 min", "10 min",
                                 "15 min", "30 min", "1 hour" };
            QList<int> vals = { 0, 1, 5, 10, 15, 30, 60 };
            int idx = vals.indexOf(vc.lockTimeoutMin);
            if (idx < 0) idx = 0;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [this, vals](int i) {
                    if (i < 0 || i >= vals.size()) return;
                    int m = vals[i];
                    VisualConfigManager::instance().setAndSave(
                        [m](VisualConfig &c) { c.lockTimeoutMin = m; });
                    applySwayidle();
                });
            th->addWidget(dd);

            cv->addWidget(timeoutRow);

            v->addWidget(c);
        }

        // ---- Appearance card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("APPEARANCE"));

            cv->addWidget(Sigil::makeToggleRow(
                "Show clock",
                "Large time on the lock screen",
                vc.lockShowClock,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.lockShowClock = on; });
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Show date",
                "Full date below the clock",
                vc.lockShowDate,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.lockShowDate = on; });
                }));

            // Require password after grace
            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            h->setSpacing(14);

            QVBoxLayout *left = new QVBoxLayout;
            left->setContentsMargins(0, 0, 0, 0);
            left->setSpacing(2);
            QLabel *t = new QLabel("Require password");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel(
                "How long after locking before a password is needed");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            left->addWidget(t);
            left->addWidget(s);
            h->addLayout(left, 1);

            QStringList opts = { "Immediately", "5 seconds",
                                 "10 seconds", "30 seconds" };
            QList<int> vals = { 0, 5, 10, 30 };
            int idx = vals.indexOf(vc.lockGraceSeconds);
            if (idx < 0) idx = 0;

            Sigil::Dropdown *dd = new Sigil::Dropdown(
                opts, idx,
                [vals](int i) {
                    if (i < 0 || i >= vals.size()) return;
                    int g = vals[i];
                    VisualConfigManager::instance().setAndSave(
                        [g](VisualConfig &c) { c.lockGraceSeconds = g; });
                });
            h->addWidget(dd);
            cv->addWidget(row);

            // Wallpaper row
            QWidget *wpRow = new QWidget;
            QHBoxLayout *wh = new QHBoxLayout(wpRow);
            wh->setContentsMargins(0, 6, 0, 6);
            wh->setSpacing(14);

            QVBoxLayout *wl = new QVBoxLayout;
            wl->setContentsMargins(0, 0, 0, 0);
            wl->setSpacing(2);
            QLabel *wt = new QLabel("Background");
            wt->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *ws = new QLabel(
                vc.lockWallpaper.isEmpty()
                    ? "Solid graphite (default)"
                    : vc.lockWallpaper);
            ws->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            ws->setWordWrap(true);
            wl->addWidget(wt);
            wl->addWidget(ws);
            wh->addLayout(wl, 1);

            QPushButton *choose = new QPushButton("Choose image…");
            choose->setCursor(Qt::PointingHandCursor);
            choose->setFixedHeight(30);
            choose->setMinimumWidth(120);
            choose->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(choose, &QPushButton::clicked, [this, ws]() {
                bool ok = false;
                QString path = ApokolipsInputDialog::getText(
                    this, "Lock screen wallpaper",
                    "Full path to image (empty = solid graphite):",
                    QDir::homePath() + "/Pictures/", &ok);
                if (!ok) return;
                if (!path.isEmpty() && !QFile::exists(path)) return;
                VisualConfigManager::instance().setAndSave(
                    [path](VisualConfig &c) { c.lockWallpaper = path; });
                ws->setText(path.isEmpty()
                    ? "Solid graphite (default)" : path);
            });
            wh->addWidget(choose);
            cv->addWidget(wpRow);

            v->addWidget(c);
        }

        // ---- Lock triggers card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("LOCK TRIGGERS"));

            cv->addWidget(Sigil::makeToggleRow(
                "Lock before sleep",
                "Lock the screen before the system suspends",
                vc.lockOnSleep,
                [this](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.lockOnSleep = on; });
                    applySwayidle();
                }));

            cv->addWidget(Sigil::makeToggleRow(
                "Lock when screen turns off",
                "Lock screen when display goes to sleep",
                vc.lockOnScreenOff,
                [this](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.lockOnScreenOff = on; });
                    applySwayidle();
                }));

            QLabel *hint = new QLabel(
                "Press Super+L to lock at any time");
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(hint);

            v->addWidget(c);
        }

        // ---- SSH card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("REMOTE ACCESS"));

            // Async: build with placeholder, fill on systemctl finish.
            // Subtitle is now neutral — the toggle state shows on/off.
            QWidget *sshRow = Sigil::makeToggleRow(
                "SSH server",
                "Allow remote login over SSH",
                false,
                [](bool on) {
                    QProcess::startDetached("pkexec",
                        { "systemctl", on ? "start" : "stop", "ssh" });
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.sshEnabled = on; });
                });
            cv->addWidget(sshRow);

            {
                auto *q = new QProcess(this);
                QObject::connect(q,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [q, sshRow](int, QProcess::ExitStatus) {
                        bool on = QString::fromUtf8(
                            q->readAllStandardOutput()).trimmed()
                            == "active";
                        q->deleteLater();
                        if (auto *t = Sigil::toggleIn(sshRow))
                            t->setOnSilent(on);
                    });
                q->start("systemctl", { "is-active", "ssh" });
            }

            v->addWidget(c);
        }

        // ---- Firewall card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("FIREWALL"));

            // Async: placeholder, fill on ufw finish.
            QLabel *status = new QLabel("Querying…");
            status->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(status);

            QWidget *row = new QWidget;
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);

            QPushButton *enable = new QPushButton("Querying…");
            enable->setCursor(Qt::PointingHandCursor);
            enable->setFixedHeight(30);
            enable->setMinimumWidth(150);
            enable->setEnabled(false);
            enable->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }"
                "QPushButton:disabled { color: %2; }")
                .arg(Sigil::textPrimary().name(),
                     Sigil::textDim().name()));
            h->addWidget(enable);
            h->addStretch();
            cv->addWidget(row);

            // Button determines current state from its own text
            QObject::connect(enable, &QPushButton::clicked, [enable]() {
                bool currentlyActive =
                    enable->text().contains("Disable");
                QProcess::startDetached("pkexec",
                    { "ufw",
                      currentlyActive ? "disable" : "--force",
                      "enable" });
            });

            {
                auto *q = new QProcess(this);
                QObject::connect(q,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [q, status, enable](int, QProcess::ExitStatus) {
                        QString out = QString::fromUtf8(
                            q->readAllStandardOutput());
                        q->deleteLater();
                        bool active = out.contains("Status: active");
                        if (status) {
                            status->setText(
                                active ? "Active — firewall is "
                                         "protecting this machine"
                                       : "Inactive — incoming "
                                         "connections are not filtered");
                            status->setStyleSheet(QString(
                                "color: %1; font-size: 12px;"
                                " background: transparent;")
                                .arg(Sigil::textSecondary().name()));
                        }
                        if (enable) {
                            enable->setText(active
                                ? "Disable Firewall"
                                : "Enable Firewall");
                            enable->setEnabled(true);
                        }
                    });
                q->start("ufw", { "status" });
            }

            QLabel *hint = new QLabel(
                "Enabling/disabling requires admin password. "
                "Takes effect immediately.");
            hint->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            hint->setWordWrap(true);
            cv->addWidget(hint);

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeUsersPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Users & Groups"));

        // ---- Current user card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(10);
            cv->addWidget(Sigil::groupLabel("CURRENT USER"));

            QString user = qgetenv("USER");
            if (user.isEmpty())
                user = qgetenv("LOGNAME");

            m_userInfo = new QLabel(user);
            m_userInfo->setStyleSheet(QString(
                "color: %1; font-size: 20px; font-weight: 300;"
                " background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(m_userInfo);

            // Groups for this user (async)
            QLabel *grp = new QLabel("Groups:  …");
            grp->setWordWrap(true);
            grp->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            cv->addWidget(grp);

            {
                auto *q = new QProcess(this);
                QString u = user;
                QObject::connect(q,
                    QOverload<int, QProcess::ExitStatus>::of(
                        &QProcess::finished),
                    [q, grp](int, QProcess::ExitStatus) {
                        QString g = QString::fromUtf8(
                            q->readAllStandardOutput()).trimmed();
                        q->deleteLater();
                        if (grp) grp->setText("Groups:  " + g);
                    });
                q->start("id", { "-nG", u });
            }

            // Change password button
            QPushButton *chpw = new QPushButton("Change password…");
            chpw->setCursor(Qt::PointingHandCursor);
            chpw->setFixedHeight(30);
            chpw->setMinimumWidth(160);
            chpw->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 7px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(chpw, &QPushButton::clicked, [user]() {
                // Launch foot for password change — real terminal
                // password prompts can't be faked in a Qt dialog.
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--node", "--run",
                    QString("echo 'Changing password for %1'; passwd")
                        .arg(user) });
            });
            QHBoxLayout *btn = new QHBoxLayout;
            btn->addWidget(chpw);
            btn->addStretch();
            cv->addLayout(btn);

            v->addWidget(c);
        }

        // ---- All users card ----
        {
            Sigil::Card *c = new Sigil::Card;
            c->setMinimumHeight(280);
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);

            QHBoxLayout *header = new QHBoxLayout;
            header->addWidget(Sigil::groupLabel("ALL USERS"));
            header->addStretch();

            QPushButton *refresh = new QPushButton("Refresh");
            refresh->setCursor(Qt::PointingHandCursor);
            refresh->setFixedHeight(26);
            refresh->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,20);"
                "  color: %1; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(255,255,255,45); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(refresh, &QPushButton::clicked,
                [this]() { refreshUsersPage(); });
            header->addWidget(refresh);

            QPushButton *add = new QPushButton("Add user…");
            add->setCursor(Qt::PointingHandCursor);
            add->setFixedHeight(26);
            add->setStyleSheet(QString(
                "QPushButton { background: rgba(168,50,50,200);"
                "  color: #ffffff; border: none; border-radius: 6px;"
                "  font-size: 11px; padding: 2px 12px; }"
                "QPushButton:hover { background: rgba(200,74,74,220); }")
                .arg(Sigil::textPrimary().name()));
            QObject::connect(add, &QPushButton::clicked, [this]() {
                bool ok = false;
                QString name = ApokolipsInputDialog::getText(
                    this, "Add user",
                    "Username (lowercase, no spaces):",
                    "", &ok);
                if (!ok || name.isEmpty()) return;
                // sanitize
                for (const QChar &ch : name) {
                    if (!ch.isLetterOrNumber() && ch != '_' && ch != '-') {
                        return;
                    }
                }
                // Real add — pkexec will prompt for admin password
                QProcess::startDetached("pkexec", {
                    "useradd", "-m", "-s", "/bin/bash", name });
                QTimer::singleShot(1500, this, [this]() {
                    refreshUsersPage();
                });
            });
            header->addWidget(add);

            cv->addLayout(header);

            m_usersList = new QListWidget;
            m_usersList->setFrameShape(QFrame::NoFrame);
            m_usersList->setContextMenuPolicy(Qt::CustomContextMenu);
            QObject::connect(m_usersList,
                             &QListWidget::customContextMenuRequested,
                             [this](const QPoint &pos) {
                QListWidgetItem *it = m_usersList->itemAt(pos);
                if (!it) return;
                QString name = it->data(Qt::UserRole).toString();
                if (name.isEmpty()) return;

                // Safety rails
                if (name == "root") return;
                if (name == qgetenv("USER")) return;

                QMenu m(this);
                m.setStyleSheet(
                    "QMenu {"
                    "  background: rgb(24, 22, 30);"
                    "  border: 1px solid rgba(255,255,255,30);"
                    "  border-radius: 10px;"
                    "  padding: 6px;"
                    "  color: rgb(240, 237, 232);"
                    "  font-size: 13px;"
                    "}"
                    "QMenu::item {"
                    "  padding: 7px 24px 7px 16px;"
                    "  border-radius: 6px;"
                    "}"
                    "QMenu::item:selected {"
                    "  background: rgba(200, 74, 74, 90);"
                    "}");

                QAction *del = m.addAction(
                    QString("Delete user \"%1\"…").arg(name));
                QAction *chosen = m.exec(
                    m_usersList->mapToGlobal(pos));
                if (chosen != del) return;

                // Confirmation dialog
                QMessageBox box(this);
                box.setWindowTitle("Delete user");
                box.setText(QString("Delete user \"%1\" and their home "
                                    "directory? This cannot be undone.")
                            .arg(name));
                box.setStandardButtons(QMessageBox::Cancel |
                                       QMessageBox::Yes);
                box.setDefaultButton(QMessageBox::Cancel);
                box.setStyleSheet(
                    "QMessageBox { background: rgb(24, 22, 30); }"
                    "QMessageBox QLabel { color: rgb(240, 237, 232);"
                    "  font-size: 13px; }"
                    "QPushButton {"
                    "  background: rgba(255,255,255,20);"
                    "  color: rgb(240, 237, 232);"
                    "  border: none; border-radius: 7px;"
                    "  padding: 6px 18px; min-width: 80px;"
                    "}"
                    "QPushButton:hover { background: rgba(255,255,255,45); }"
                    "QPushButton:default {"
                    "  background: rgba(168, 50, 50, 200);"
                    "  color: #ffffff;"
                    "}");

                if (box.exec() != QMessageBox::Yes) return;

                // Real delete via pkexec
                QProcess::startDetached("pkexec",
                    { "userdel", "-r", name });
                QTimer::singleShot(1500, this, [this]() {
                    refreshUsersPage();
                });
            });
            cv->addWidget(m_usersList, 1);
            refreshUsersPage();

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    void refreshUsersPage() {
        if (!m_usersList) return;
        m_usersList->clear();

        auto *q = new QProcess(this);
        QObject::connect(q,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, q](int, QProcess::ExitStatus) {
                QString out = QString::fromUtf8(
                    q->readAllStandardOutput());
                q->deleteLater();
                if (!m_usersList) return;

                bool anyAdded = false;
                for (const QString &line :
                     out.split('\n', Qt::SkipEmptyParts)) {
                    QStringList parts = line.split(':');
                    if (parts.size() < 7) continue;
                    const QString name = parts[0];
                    const QString uid  = parts[2];
                    const QString gecos= parts[4];
                    const QString home = parts[5];
                    const QString shell= parts[6];

                    bool isUid = false;
                    int uidInt = uid.toInt(&isUid);
                    if (!isUid) continue;

                    // Human users only
                    if (uidInt < 1000 && uidInt != 0) continue;
                    if (shell.endsWith("/nologin")
                        || shell.endsWith("/false"))
                        continue;

                    QString display = gecos.isEmpty() ? name : gecos;
                    QString text = QString(
                        "%1   ·   %2   ·   uid %3   ·   %4")
                        .arg(name, display, uid, home);

                    QListWidgetItem *it = new QListWidgetItem(text);
                    it->setData(Qt::UserRole, name);
                    if (uidInt == 0) {
                        it->setForeground(QColor(200, 150, 80));
                    } else {
                        it->setForeground(QColor(240, 237, 232));
                    }
                    m_usersList->addItem(it);
                    anyAdded = true;
                }

                if (!anyAdded) {
                    QListWidgetItem *it = new QListWidgetItem(
                        "No human users found");
                    it->setFlags(Qt::NoItemFlags);
                    it->setForeground(QColor(122, 120, 115));
                    m_usersList->addItem(it);
                }
            });
        q->start("getent", { "passwd" });
    }

    QWidget *makeMyMachPage() {
    QWidget *page = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setContentsMargins(40, 30, 40, 40);
    v->setSpacing(20);
    v->addWidget(Sigil::sectionTitle("myMach"));
    {
        QLabel *sub = new QLabel("System monitor appearance");
        sub->setStyleSheet(QString(
            "color: %1; font-size: 12px;"
            " background: transparent;")
            .arg(Sigil::textDim().name()));
        v->addWidget(sub);
    }

    // ---- GRAPH STYLE card ----
    {
        Sigil::Card *c = new Sigil::Card;
        QVBoxLayout *cv = new QVBoxLayout(c);
        cv->setContentsMargins(20, 18, 20, 18);
        cv->setSpacing(14);
        cv->addWidget(Sigil::groupLabel("GRAPH STYLE"));
        {
            QLabel *h = new QLabel(
                "How the CPU and RAM sparklines are drawn");
            h->setStyleSheet(QString(
                "color: %1; font-size: 11px;"
                " background: transparent;")
                .arg(Sigil::textDim().name()));
            cv->addWidget(h);
        }

        // The buttons and their values — re-applied on every click so
        // the selected one is highlighted, the rest are not.
        auto *btns = new QList<QPair<QPushButton *, QString>>;

        auto applyStyle = [](QPushButton *b, bool on) {
            b->setStyleSheet(QString(
                "QPushButton {"
                " background: %1;"
                " color: %2;"
                " border: 1px solid %3;"
                " border-radius: 8px;"
                " padding: 4px 18px;"
                " font-size: 12px;"
                " font-weight: %4; }"
                "QPushButton:hover { background: %5; }")
                .arg(on ? QString("rgba(80,120,200,80)")
                        : QString("rgba(255,255,255,18)"),
                     on ? QString("#ffffff")
                        : Sigil::textPrimary().name(),
                     on ? QString("rgba(80,120,200,200)")
                        : QString("rgba(255,255,255,25)"),
                     on ? "600" : "400",
                     on ? QString("rgba(80,120,200,120)")
                        : QString("rgba(255,255,255,38)")));
        };

        const QString current =
            VisualConfigManager::instance().cfg().mymachGraphStyle;

        QHBoxLayout *row = new QHBoxLayout;
        row->setSpacing(10);
        struct Opt { const char *label; const char *value; };
        const Opt opts[] = {
            { "Line",   "line"   },
            { "Bar",    "bar"    },
            { "Area",   "area"   },
            { "Radial", "radial" },
        };
        for (const auto &o : opts) {
            QPushButton *b = new QPushButton(o.label);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(34);
            b->setMinimumWidth(80);
            applyStyle(b, current == o.value);
            btns->append({ b, QString(o.value) });

            const QString value = o.value;
            QObject::connect(
                b, &QPushButton::clicked,
                [value, btns, applyStyle]() {
                    VisualConfigManager::instance().setAndSave(
                        [value](VisualConfig &c) {
                            c.mymachGraphStyle = value;
                        });
                    for (const auto &pair : *btns) {
                        applyStyle(pair.first,
                                   pair.second == value);
                    }
                });
            row->addWidget(b);
        }
        row->addStretch();
        cv->addLayout(row);
        v->addWidget(c);
    }

    v->addStretch();
    return page;
}

    QWidget *makeNotificationsPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        v->addWidget(Sigil::sectionTitle("Notifications"));

        auto &vc = VisualConfigManager::instance().cfg();

        // ---- Notifications card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("NOTIFICATIONS"));

            cv->addWidget(Sigil::makeToggleRow(
                "Do Not Disturb",
                "Mute banners. Panel shows a Muted badge.",
                vc.dndEnabled,
                [](bool on) {
                    VisualConfigManager::instance().setAndSave(
                        [on](VisualConfig &c) { c.dndEnabled = on; });
                }));

            // Banner duration slider
            QWidget *durRow = new QWidget;
            QVBoxLayout *dl = new QVBoxLayout(durRow);
            dl->setContentsMargins(0, 8, 0, 8);
            dl->setSpacing(6);

            QHBoxLayout *top = new QHBoxLayout;
            QLabel *t = new QLabel("Banner duration");
            t->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            QLabel *s = new QLabel("How long a banner stays visible");
            s->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            top->addWidget(t);
            top->addStretch();
            top->addWidget(s);
            dl->addLayout(top);

            QSlider *sl = new QSlider(Qt::Horizontal);
            sl->setRange(1, 30);   // seconds
            sl->setValue(vc.notificationBannerMs / 1000);
            sl->setStyleSheet(QString(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 14px; height: 14px;"
                "  margin: -5px 0; border-radius: 7px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgba(255,255,255,180);"
                "  border-radius: 2px; }"));
            m_sliders.append(sl);

            QLabel *durValue = new QLabel(QString("%1 s").arg(sl->value()));
            durValue->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            durValue->setMinimumWidth(40);
            durValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            QHBoxLayout *slRow = new QHBoxLayout;
            slRow->addWidget(sl, 1);
            slRow->addWidget(durValue);
            dl->addLayout(slRow);

            QObject::connect(sl, &QSlider::valueChanged,
                             [durValue](int val) {
                durValue->setText(QString("%1 s").arg(val));
                VisualConfigManager::instance().setAndSave(
                    [val](VisualConfig &c) {
                        c.notificationBannerMs = val * 1000;
                    });
            });

            cv->addWidget(durRow);

            v->addWidget(c);
        }

        // ---- Coming Soon card ----
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);

            cv->addWidget(Sigil::groupLabel("MORE — COMING SOON"));

            auto disabledRow = [&](const QString &title,
                                   const QString &sub) {
                QWidget *row = new QWidget;
                row->setEnabled(false);
                row->setStyleSheet("QWidget { }");
                QVBoxLayout *rl = new QVBoxLayout(row);
                rl->setContentsMargins(0, 6, 0, 6);
                rl->setSpacing(2);
                QLabel *t = new QLabel(title);
                t->setStyleSheet(QString(
                    "color: %1; font-size: 13px; background: transparent;")
                    .arg(Sigil::textDim().name()));
                QLabel *s = new QLabel(sub);
                s->setStyleSheet(QString(
                    "color: rgb(90, 88, 84); font-size: 11px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                rl->addWidget(t);
                rl->addWidget(s);
                cv->addWidget(row);
            };

            disabledRow("Sound",
                "ⓘ Coming soon — needs notification audio pipeline");
            disabledRow("Show on lock screen",
                "ⓘ Coming soon — needs custom lock screen");

            v->addWidget(c);
        }

        v->addStretch();
        return page;
    }

    QWidget *makeAboutPage() {
        QWidget *page = new QWidget;
        page->setAutoFillBackground(false);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(20);

        v->addWidget(Sigil::sectionTitle("About"));

        Sigil::Card *c = new Sigil::Card;
        c->setFixedHeight(150);
        QVBoxLayout *cv = new QVBoxLayout(c);
        cv->setContentsMargins(20, 18, 20, 18);
        cv->setSpacing(10);

        cv->addWidget(Sigil::groupLabel("SYSTEM"));

        m_aboutLines.clear();
        auto addRow = [&](const QString &label, const QString &value) {
            QHBoxLayout *r = new QHBoxLayout;
            QLabel *l = new QLabel(label);
            l->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textSecondary().name()));
            QLabel *val = new QLabel(value);
            val->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            m_aboutLines.append(val);
            r->addWidget(l);
            r->addStretch();
            r->addWidget(val);
            cv->addLayout(r);
        };

        addRow("Name", "Apokolips OS");
        addRow("Version", "0.1 — build " __DATE__);
        addRow("Toolkit", "C++17 / Qt6");
        addRow("Source", "github.com/lytone-lab/APOKOLIPS-OS");

        v->addWidget(c);
        v->addStretch();
        return page;
    }

    QLabel *sectionHeader(const QString &text) {
        QLabel *l = Sigil::sectionTitle(text);
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
            // Force style re-evaluation so [selected="true"] repaints
            b->style()->unpolish(b);
            b->style()->polish(b);
            b->update();
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

        if (m_sidebar) {
            m_sidebar->setStyleSheet(QString(
                "QWidget { background: rgb(%1, %2, %3); }")
                .arg(Sigil::sidebarBg().red())
                .arg(Sigil::sidebarBg().green())
                .arg(Sigil::sidebarBg().blue()));
        }
        if (m_stack) {
            m_stack->setStyleSheet(QString(
                "QStackedWidget { background: rgb(%1, %2, %3); }")
                .arg(Sigil::contentBg().red())
                .arg(Sigil::contentBg().green())
                .arg(Sigil::contentBg().blue()));
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
        auto styleList = [&](QListWidget *lw) {
            if (!lw) return;
            lw->setStyleSheet(QString(
                "QListWidget { background: rgba(255,255,255,12);"
                "  border: none; border-radius: 10px; padding: 6px;"
                "  color: %1; font-size: 13px; }"
                "QListWidget::item { padding: 8px 10px; border-radius: 6px; }"
                "QListWidget::item:hover { background: rgba(255,255,255,25); }"
                "QListWidget::item:selected { background: %2; color: %3; }")
                .arg(t.textPrimary.name(),
                     rgba(t.accentSoft),
                     t.textPrimary.name()));
        };
        styleList(m_soundList);
        styleList(m_resList);
        styleList(m_wifiList);

        for (QPushButton *b : m_dockPageButtons) {
            b->setStyleSheet(QString(
                "QPushButton { background: %1; color: %2;"
                "  border: 1px solid %3; border-radius: 8px;"
                "  font-size: 12px; padding: 4px 14px; }"
                "QPushButton:hover { background: %4; }")
                .arg(rgba(t.chromeBg.lighter(120)),
                     t.textPrimary.name(),
                     rgba(t.chromeBorder),
                     rgba(t.accentSoft)));
        }
        if (m_displayInfo) {
            m_displayInfo->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;"
                " padding: 4px 0;").arg(t.textSecondary.name()));
        }
        if (m_networkInfo) {
            m_networkInfo->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;"
                " padding: 4px 0;").arg(t.textSecondary.name()));
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
    QList<QScrollArea*> m_pageScrolls;
    QList<std::function<QWidget*()>> m_pageFactories;
    QHash<int, QWidget*> m_pageCache;
    QTimer *m_warmer = nullptr;
    int     m_warmIndex = 0;
    QStackedWidget *m_stack = nullptr;
    QListWidget    *m_soundList    = nullptr;
    QListWidget    *m_btList       = nullptr;
    QListWidget    *m_wifiPageList = nullptr;
    QLabel         *m_wifiStatus   = nullptr;
    QListWidget    *m_networkList  = nullptr;
    QListWidget    *m_usersList    = nullptr;
    QListWidget    *m_startupList  = nullptr;
    QLabel         *m_updateVersion = nullptr;
    QLabel         *m_updateChecked = nullptr;
    QListWidget    *m_updateList   = nullptr;
    QListWidget    *m_historyList  = nullptr;
    QLabel         *m_updateStatus = nullptr;
    QLabel         *m_backupLast   = nullptr;
    QLabel         *m_backupStatus = nullptr;
    QListWidget    *m_backupHistory= nullptr;
    QList<QPushButton *> m_focusModeButtons;
    QLabel         *m_focusModeStatus = nullptr;
    QListWidget    *m_printerList     = nullptr;
    QLabel         *m_printerDefault  = nullptr;
    QLabel         *m_storageSummary = nullptr;
    QListWidget    *m_storageCats  = nullptr;
    QLabel         *m_trashInfo    = nullptr;
    QLabel         *m_userInfo     = nullptr;
    QListWidget    *m_resList      = nullptr;
    QListWidget    *m_wifiList     = nullptr;
    QLabel         *m_displayInfo  = nullptr;
    QLabel         *m_networkInfo  = nullptr;
    QList<QPushButton *> m_dockPageButtons;
    QList<QPushButton *> m_sidebarButtons;
    QList<Sigil::SidebarRow *> m_sidebarRows;
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

#endif // APOKOLIPS_SETTINGS_H
