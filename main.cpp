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
#include <QProcess>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWindow>
#include <QMenu>

static const QColor BG_TOP    (26, 6, 14);
static const QColor BG_MID    (58, 12, 28);
static const QColor BG_BOTTOM (12, 2, 6);
static const QColor GLOW_HI   (210, 40, 80, 130);
static const QColor GLOW_MID  (150, 20, 60, 60);
static const QColor BAR_BG    (18, 2, 8);
static const QColor BAR_LINE  (255, 90, 130);

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
        header->setStyleSheet(
            "QWidget { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 rgb(80, 14, 36), stop:1 rgb(48, 8, 22)); }"
        );
        QHBoxLayout *hLayout = new QHBoxLayout(header);
        hLayout->setContentsMargins(14, 0, 14, 0);
        hLayout->setSpacing(10);

        QLabel *dot = new QLabel(QString::fromUtf8("\xE2\x97\x86"));
        dot->setStyleSheet("color: #ff7096; font-size: 14px;"
                           " background: transparent;");
        hLayout->addWidget(dot);

        QLabel *title = new QLabel(appName);
        title->setStyleSheet("color: #ffe1e8; font-size: 14px;"
                             " font-weight: 600; background: transparent;");
        hLayout->addWidget(title);
        hLayout->addStretch();

        QPushButton *close = new QPushButton;
        close->setFixedSize(13, 13);
        close->setCursor(Qt::PointingHandCursor);
        close->setStyleSheet(
            "QPushButton { background: #7a2b25;"
            "  border: 1px solid rgba(0,0,0,60); border-radius: 6px; }"
            "QPushButton:hover { background: #ff5f57; }"
        );
        QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
        hLayout->addWidget(close);

        layout->addWidget(header);

        QWidget *body = new QWidget;
        body->setAttribute(Qt::WA_StyledBackground, true);
        body->setStyleSheet("QWidget { background: rgb(24, 6, 14); }");
        QVBoxLayout *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setAlignment(Qt::AlignCenter);

        QLabel *big = new QLabel(appName);
        big->setAlignment(Qt::AlignCenter);
        big->setStyleSheet(
            "color: #ffe1e8; font-size: 28px; font-weight: 300;"
            " letter-spacing: 4px; background: transparent;"
        );
        bodyLayout->addWidget(big);

        QLabel *sub = new QLabel("running under Apokolips Shell");
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet(
            "color: #e07090; font-size: 11px; letter-spacing: 3px;"
            " margin-top: 8px; background: transparent;"
        );
        bodyLayout->addWidget(sub);

        layout->addWidget(body, 1);
    }
};

// =========================================================
// App launcher overlay — NO graphics effects, manual paint
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
    }

    void toggle() {
        if (isVisible() && m_opacity > 0.5) hideLauncher();
        else showLauncher();
    }

    void showLauncher() {
        raise();
        show();
        setFocus();
        m_fadeTarget = 1.0;
        m_animTimer->start();
    }

    void hideLauncher() {
        m_fadeTarget = 0.0;
        m_animTimer->start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Dimmed backdrop
        QColor backdrop(10, 2, 6);
        backdrop.setAlphaF(0.92 * m_opacity);
        p.fillRect(rect(), backdrop);

        // Radial accent behind the panel
        QRadialGradient glow(width() * 0.5, height() * 0.35,
                             width() * 0.5);
        QColor g1(210, 40, 80);  g1.setAlphaF(0.30 * m_opacity);
        QColor g2(80, 10, 30);   g2.setAlphaF(0.0);
        glow.setColorAt(0.0, g1);
        glow.setColorAt(1.0, g2);
        p.fillRect(rect(), glow);

        // Panel rect (fixed size, centered horizontally, near top)
        QRect panelRect((width() - PANEL_W) / 2,
                        120, PANEL_W, PANEL_H);

        // Soft outer glow (manual shadow approximation)
        for (int i = 6; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.06 * m_opacity);
            p.setPen(Qt::NoPen);
            p.setBrush(sh);
            p.drawRoundedRect(panelRect.adjusted(-i*2, -i*2 + 6, i*2, i*2 + 6),
                              22 + i, 22 + i);
        }

        // Panel body
        QColor panel(30, 8, 18);
        panel.setAlphaF(0.97 * m_opacity);
        p.setBrush(panel);
        QColor border(230, 70, 110);
        border.setAlphaF(0.75 * m_opacity);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(panelRect, 22, 22);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            hideLauncher();
            e->accept();
        } else {
            QWidget::keyPressEvent(e);
        }
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

        QLabel *title = new QLabel("SEARCH");
        title->setStyleSheet(
            "color: #ff8aa8; font-size: 11px; letter-spacing: 4px;"
            " background: transparent;"
        );
        pl->addWidget(title);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search apps, files, settings...");
        m_search->setStyleSheet(
            "QLineEdit {"
            "  background: rgba(60, 14, 30, 220);"
            "  border: 1px solid rgba(230, 70, 110, 180);"
            "  border-radius: 12px;"
            "  padding: 12px 16px;"
            "  color: #ffe1e8;"
            "  font-size: 15px;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid rgba(255, 90, 130, 240);"
            "  background: rgba(70, 18, 36, 230);"
            "}"
        );
        pl->addWidget(m_search);

        QGridLayout *grid = new QGridLayout;
        grid->setSpacing(14);

        struct App { const char *icon; const char *name; };
        const App apps[] = {
            { "\xE2\x8C\x98", "Finder"   },
            { "\xE2\x8C\xA5", "Settings" },
            { "\xE2\x9C\xA6", "Photos"   },
            { "\xE2\x96\xB6", "Music"    },
            { "\xE2\x99\xAB", "Notes"    },
            { "\xE2\x9C\x89", "Mail"     },
            { "\xE2\x9A\x99", "Terminal" },
            { "\xE2\x9A\xA1", "Power"    }
        };

        int col = 0, row = 0, idx = 0;
        for (const auto &a : apps) {
            QWidget *tile = makeTile(QString::fromUtf8(a.icon), a.name, idx);
            grid->addWidget(tile, row, col);
            if (++col == 4) { col = 0; ++row; }
            ++idx;
        }

        pl->addLayout(grid);
        outer->addWidget(panel, 0, Qt::AlignHCenter);
        outer->addStretch();
    }

    QWidget *makeTile(const QString &icon, const QString &name, int index) {
        QPushButton *tile = new QPushButton;
        tile->setFixedSize(136, 116);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setStyleSheet(
            "QPushButton {"
            "  background: rgba(255, 140, 170, 30);"
            "  border: 1px solid rgba(255, 160, 190, 60);"
            "  border-radius: 16px;"
            "  color: #ffe1e8;"
            "  padding-bottom: 10px;"
            "}"
            "QPushButton:hover {"
            "  background: rgba(255, 90, 130, 90);"
            "  border: 1px solid rgba(255, 130, 170, 180);"
            "}"
            "QPushButton:pressed {"
            "  background: rgba(255, 60, 110, 140);"
            "}"
        );

        QVBoxLayout *v = new QVBoxLayout(tile);
        v->setContentsMargins(8, 14, 8, 10);
        v->setSpacing(6);

        QLabel *ic = new QLabel(icon);
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet("font-size: 38px; color: #ffb0c6;"
                          " background: transparent;");
        ic->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        v->addWidget(ic);

        QLabel *nm = new QLabel(name);
        nm->setAlignment(Qt::AlignCenter);
        nm->setStyleSheet("font-size: 12px; color: #ffe1e8;"
                          " background: transparent;");
        nm->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        v->addWidget(nm);

        QString appName = name;
        int offset = index;
        QObject::connect(tile, &QPushButton::clicked,
                         [this, appName, offset]() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--demo", appName, QString::number(offset) }
            );
            hideLauncher();
        });

        return tile;
    }

    QLineEdit *m_search = nullptr;
    QTimer *m_animTimer = nullptr;
    qreal m_opacity = 0.0;
    qreal m_fadeTarget = 0.0;
};

// =========================================================
// Background
// =========================================================
class DesktopBackground : public QWidget {
public:
    explicit DesktopBackground(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setLauncher(AppLauncher *l) {
        m_launcher = l;
        l->setParent(this);
        l->setGeometry(rect());
        l->raise();
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_launcher) {
            m_launcher->setGeometry(rect());
            m_launcher->raise();
        }
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QLinearGradient base(0, 0, 0, height());
        base.setColorAt(0.0, BG_TOP);
        base.setColorAt(0.55, BG_MID);
        base.setColorAt(1.0, BG_BOTTOM);
        p.fillRect(rect(), base);

        QRadialGradient glow(width() * 0.78, height() * 0.14,
                             width() * 0.55);
        glow.setColorAt(0.0, GLOW_HI);
        glow.setColorAt(0.4, GLOW_MID);
        glow.setColorAt(1.0, QColor(80, 10, 30, 0));
        p.fillRect(rect(), glow);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 220, 230, 60));
        for (int i = 0; i < 60; ++i) {
            int sx = (i * 137) % width();
            int sy = (i * 89) % (height() / 2);
            p.drawEllipse(QPoint(sx, sy), 1, 1);
        }

        const int TOP_H = 34;
        p.fillRect(QRect(0, 0, width(), TOP_H), BAR_BG);
        p.setPen(QPen(BAR_LINE, 2));
        p.drawLine(0, TOP_H - 1, width(), TOP_H - 1);
    }

private:
    AppLauncher *m_launcher = nullptr;
};

// =========================================================
// Top bar (draggable)
// =========================================================
class TopBar : public QWidget {
public:
    explicit TopBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(34);
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

static QString menuStyle()
{
    return
        "QMenu {"
        "  background: rgb(24, 6, 14);"
        "  border: 1px solid rgb(210, 55, 95);"
        "  border-radius: 10px;"
        "  padding: 6px;"
        "  color: #ffe1e8;"
        "  font-size: 13px;"
        "}"
        "QMenu::item {"
        "  padding: 7px 22px 7px 16px;"
        "  border-radius: 6px;"
        "  background: transparent;"
        "}"
        "QMenu::item:selected {"
        "  background: rgb(120, 20, 50);"
        "  color: #ffffff;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: rgba(210, 55, 95, 120);"
        "  margin: 6px 10px;"
        "}";
}

// =========================================================
// Shell
// =========================================================
static int runShell(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Apokolips Shell");

    QMainWindow window;
    window.setWindowFlags(Qt::FramelessWindowHint);
    window.setWindowTitle("Apokolips OS");

    DesktopBackground *root = new DesktopBackground;
    QVBoxLayout *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    TopBar *topBar = new TopBar;
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(14, 0, 18, 0);
    topLayout->setSpacing(10);

    auto makeLight = [](const QString &hoverColor, const QString &idleColor) {
        QPushButton *b = new QPushButton;
        b->setFixedSize(13, 13);
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
    QObject::connect(btnMax, &QPushButton::clicked, [&window, &app, isScreenSized]() {
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

    auto styledLabel = [](const QString &t, const QString &color,
                          int size = 13, int weight = -1) {
        QLabel *l = new QLabel(t);
        QString css = QString("color: %1; font-size: %2px; background: transparent;")
                        .arg(color).arg(size);
        if (weight > 0) css += QString("font-weight: %1;").arg(weight);
        l->setStyleSheet(css);
        return l;
    };

    // Launcher created first so the diamond button can toggle it
    AppLauncher *launcher = new AppLauncher;
    root->setLauncher(launcher);

    QPushButton *btnDiamond = new QPushButton(QString::fromUtf8("\xE2\x97\x86"));
    btnDiamond->setCursor(Qt::PointingHandCursor);
    btnDiamond->setFlat(true);
    btnDiamond->setStyleSheet(
        "QPushButton { color: #ff5c80; background: transparent;"
        "  border: none; font-size: 15px; padding: 2px 4px; }"
        "QPushButton:hover { color: #ff8aa8;"
        "  background: rgba(210, 55, 95, 90); border-radius: 5px; }"
        "QPushButton:pressed { background: rgba(210, 55, 95, 150); }"
    );
    topLayout->addWidget(btnDiamond);
    QObject::connect(btnDiamond, &QPushButton::clicked, [launcher]() {
        launcher->toggle();
    });

    topLayout->addWidget(styledLabel("Apokolips", "#ffe1e8", 13, 600));
    topLayout->addSpacing(10);

    auto makeMenuButton = [&](const QString &label, QMenu *menu) {
        QPushButton *b = new QPushButton(label);
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        b->setStyleSheet(
            "QPushButton {"
            "  color: #e8b4c3; background: transparent; border: none;"
            "  padding: 4px 10px; font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  color: #ffffff; background: rgba(210, 55, 95, 90);"
            "  border-radius: 5px;"
            "}"
            "QPushButton:pressed { background: rgba(210, 55, 95, 150); }"
        );
        menu->setStyleSheet(menuStyle());
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

    topLayout->addWidget(styledLabel(QString::fromUtf8("\xE2\x97\x8F"),
                                     "#ff7096", 12));
    topLayout->addWidget(styledLabel(QString::fromUtf8("\xE2\x96\xAE"),
                                     "#ff7096", 14));

    QLabel *clock = new QLabel;
    clock->setStyleSheet("color: #ffe1e8; font-weight: 500;"
                         " background: transparent; font-size: 13px;");
    clock->setMinimumWidth(90);
    clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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

    QWidget *dock = new QWidget;
    QHBoxLayout *dockLayout = new QHBoxLayout(dock);
    dockLayout->setContentsMargins(16, 10, 16, 10);
    dockLayout->setSpacing(12);
    dock->setObjectName("dockRoot");
    dock->setAttribute(Qt::WA_StyledBackground, true);
    dock->setFixedHeight(72);
    dock->setStyleSheet(
        "QWidget#dockRoot {"
        "  background: rgba(44, 8, 20, 230);"
        "  border: 1px solid rgba(210, 55, 95, 200);"
        "  border-radius: 20px;"
        "}"
    );

    struct DockApp { const char *icon; const char *name; };
    const DockApp apps[] = {
        { "\xE2\x8C\x98", "Finder"   },
        { "\xE2\x8C\xA5", "Settings" },
        { "\xE2\x9C\xA6", "Photos"   },
        { "\xE2\x96\xB6", "Music"    },
        { "\xE2\x99\xAB", "Notes"    },
        { "\xE2\x9C\x89", "Mail"     },
        { "\xE2\x9A\x99", "Terminal" },
        { "\xE2\x9A\xA1", "Power"    }
    };

    int i = 0;
    for (const auto &a : apps) {
        QPushButton *b = new QPushButton(QString::fromUtf8(a.icon));
        b->setFixedSize(50, 50);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(a.name);
        b->setStyleSheet(
            "QPushButton {"
            "  background: rgba(255, 140, 170, 40);"
            "  border: 1px solid rgba(255, 160, 190, 70);"
            "  border-radius: 13px;"
            "  color: #ffe1e8;"
            "  font-size: 22px;"
            "}"
            "QPushButton:hover {"
            "  background: rgba(255, 90, 130, 90);"
            "  border: 1px solid rgba(255, 120, 160, 160);"
            "}"
            "QPushButton:pressed {"
            "  background: rgba(255, 60, 110, 140);"
            "}"
        );

        QString appName = a.name;
        int offset = i;
        QObject::connect(b, &QPushButton::clicked, [appName, offset]() {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--demo", appName, QString::number(offset) }
            );
        });

        dockLayout->addWidget(b);
        ++i;
    }

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

    return app.exec();
}

int main(int argc, char *argv[])
{
    if (argc >= 3 && QString(argv[1]) == "--demo") {
        QApplication app(argc, argv);
        int offset = (argc >= 4) ? QString(argv[3]).toInt() : 0;
        DemoWindow w(QString(argv[2]), offset);
        w.show();
        return app.exec();
    }
    return runShell(argc, argv);
}
