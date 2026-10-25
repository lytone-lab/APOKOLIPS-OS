#ifndef APOKOLIPS_CHROME_H
#define APOKOLIPS_CHROME_H

#include "shell.h"
#include <QFileIconProvider>
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWindow>
#include <QTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QIcon>
#include <QFileSystemWatcher>
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "helpers.h"
#include "registry.h"
#include "weather.h"
#include "globals.h"
#include "theme_bridge.h"

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
            for (QWidget *c : m_history) c->deleteLater();
            m_history.clear();
            if (m_historyHeader) m_historyHeader->setVisible(false);
            for (QWidget *c : m_cards) c->deleteLater();
            m_cards.clear();
            buildCards();
            updateThemeOnHeader();
        });

        // Poll /tmp/apokolips-notify/ for files dropped by any app
        // via ApoNotify::send(). Each is a JSON object with
        // {app, title, body}. Focus mode gates display.
        QDir().mkpath(QDir::tempPath() + "/apokolips-notify");
        m_notifyTimer = new QTimer(this);
        m_notifyTimer->setInterval(400);
        QObject::connect(m_notifyTimer, &QTimer::timeout, this, [this]() {
            QDir d(QDir::tempPath() + "/apokolips-notify");
            if (!d.exists()) return;
            const QStringList files = d.entryList(
                {"*.json"}, QDir::Files, QDir::Time);
            for (const QString &fn : files) {
                const QString fp = d.filePath(fn);
                QFile f(fp);
                if (!f.open(QIODevice::ReadOnly)) { QFile::remove(fp); continue; }
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                f.close();
                QFile::remove(fp);
                if (!doc.isObject()) continue;
                QJsonObject o = doc.object();
                const QString app   = o.value("app").toString("Apokolips");
                const QString title = o.value("title").toString();
                const QString body  = o.value("body").toString();

                const auto &cfg = VisualConfigManager::instance().cfg();
                bool allow = true;
                if (cfg.dndEnabled) {
                    allow = false;
                    if (cfg.focusPolicy == "all") allow = true;
                    else if (cfg.focusPolicy == "priority") {
                        for (const auto &a : cfg.focusAllowedApps) {
                            if (a.compare(app, Qt::CaseInsensitive) == 0) {
                                allow = true; break;
                            }
                        }
                    }
                    // mute → allow stays false
                }
                qInfo() << "[notify] recv" << app << "|" << title
                        << "| focusGate=" << (cfg.dndEnabled ? "on" : "off")
                        << " policy=" << cfg.focusPolicy
                        << " allow=" << allow;
                if (allow) flash(app, title, body);
            }
        });
        m_notifyTimer->start();
    }

    void toggle() { isVisible() ? hideCenter() : showCenter(); }

    void showCenter() { raise(); show(); m_target = 1.0; m_animTimer->start(); }
    void hideCenter() { m_target = 0.0; m_animTimer->start(); }

    void flash(const QString &app, const QString &title,
               const QString &body, int ms = 6000)
    {
        if (!m_cardsLayout) return;
        const bool weOpened = !isVisible();

        QString time = QTime::currentTime().toString("HH:mm");
        QWidget *card = makeNote(app, title, body, time, m_theme.accent);
        m_cardsLayout->addWidget(card);
        m_cards.append(card);
        if (m_emptyLabel) m_emptyLabel->setVisible(false);

        if (weOpened) showCenter();

        QPointer<QWidget> guard(card);
        if (VisualConfigManager::instance().cfg().notifAutoDismiss) {
            qInfo() << "[notif] auto-dismiss scheduled in" << ms << "ms";
            QTimer::singleShot(ms, this, [this, guard, weOpened]() {
                qInfo() << "[notif] auto-dismiss fired (guard alive ="
                        << (guard ? "yes" : "no") << ")";
                if (!guard) return;
                m_cards.removeAll(guard);
                if (m_cardsLayout) m_cardsLayout->removeWidget(guard);
                guard->deleteLater();
                if (m_emptyLabel) m_emptyLabel->setVisible(m_cards.isEmpty());
                if (weOpened && m_cards.isEmpty()) hideCenter();
            });
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Deeper, softer shadow — wider ring cascade
        p.setPen(Qt::NoPen);
        for (int i = 20; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.022 * m_slide * (i / 20.0));
            p.setBrush(sh);
            p.drawRoundedRect(rect().adjusted(-i - 3, -i, 0, i),
                              16 + i, 16 + i);
        }

        QRectF ncRect(rect());
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (cfg.notifStyle == 0) { // Glass
            StyleRenderer::drawPanel(p, ncRect, 0, m_theme,
                                     int(230 * m_slide));

            QLinearGradient glass(0, 0, 0, height());
            glass.setColorAt(0.00, QColor(255, 255, 255, int(14 * m_slide)));
            glass.setColorAt(0.55, QColor(255, 255, 255, 0));
            glass.setColorAt(1.00, QColor(0, 0, 0, int(22 * m_slide)));
            p.fillRect(rect(), glass);

            QLinearGradient edgeGrad(0, 0, 6, 0);
            edgeGrad.setColorAt(0.0, QColor(255, 255, 255, int(90 * m_slide)));
            edgeGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
            p.fillRect(QRect(0, 0, 6, height()), edgeGrad);

            QColor edge = m_theme.chromeBorder;
            edge.setAlpha(int(230 * m_slide));
            p.setPen(QPen(edge, 1));
            p.drawLine(0, 0, 0, height());
        } else { // Solid
            p.fillRect(rect(), m_theme.chromeBg);
            QColor edge = m_theme.chromeBorder;
            edge.setAlpha(int(230 * m_slide));
            p.setPen(QPen(edge, 1));
            p.drawRect(rect().adjusted(0, 0, -1, -1));
        }
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
            const auto &cfg = VisualConfigManager::instance().cfg();
            bool isLeft = (cfg.notifPosition == 1);
            int x = isLeft ? -offX : par->width() - width() + offX;
            move(x, 0);
            setFixedHeight(par->height());
        }
        update();
    }

    QWidget *makeNote(const QString &app, const QString &title,
                      const QString &body, const QString &time,
                      const QColor &accent)
    {
        QWidget *card = new QWidget;
        card->setObjectName("notifCard");
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setAttribute(Qt::WA_Hover, true);
        const auto &cfg = VisualConfigManager::instance().cfg();
        QColor cardTop = m_theme.chromeBg.lighter(120);
        QColor cardBot = m_theme.chromeBg.lighter(105);
        QColor hoverBg = m_theme.chromeBg.lighter(135);
        if (cfg.notifStyle == 1) { // Solid
            cardTop = m_theme.chromeBg;
            cardBot = m_theme.chromeBg;
            hoverBg = m_theme.chromeBg.lighter(115);
        }
        QColor inner   = m_theme.chromeBorder;
        inner.setAlpha(70);
        // Scope the stylesheet to the card widget itself
        // (#notifCard), not to every QWidget descendant — that
        // was causing each QLabel inside the card to draw its
        // own 1px box.
        card->setStyleSheet(QString(
            "QWidget#notifCard {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
            "    stop:0 %1, stop:1 %2);"
            "  border: 1px solid %3;"
            "  border-left: 3px solid %4;"
            "  border-radius: 14px;"
            "}"
            "QWidget#notifCard:hover {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
            "    stop:0 %5, stop:1 %1);"
            "}").arg(cardTop.name(),
                     cardBot.name(),
                     rgba(inner),
                     rgba(accent),
                     hoverBg.name()));

        QVBoxLayout *v = new QVBoxLayout(card);
        v->setContentsMargins(16, 14, 16, 14);
        v->setSpacing(5);

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

        auto &cfg = VisualConfigManager::instance().cfg();

        // If DND is on AND policy is mute, show nothing.
        if (cfg.dndEnabled && cfg.focusPolicy == "mute") {
            return;
        }

        // Helper — decides if a source app is allowed through.
        auto allowed = [&](const QString &source) {
            if (!cfg.dndEnabled) return true;
            if (cfg.focusPolicy == "all") return true;
            // priority: only apps listed in focusAllowedApps
            for (const QString &app : cfg.focusAllowedApps) {
                if (app.compare(source, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        };

        if (allowed("MUSIC"))
            m_cards.append(makeNote("MUSIC", "Now Playing",
                "Apokolips Anthem - track 1 of 12", "now", a1));
        if (allowed("MAIL"))
            m_cards.append(makeNote("MAIL", "3 new messages",
                "From: build system, VS Code, GitHub", "2m", a2));
        if (allowed("SYSTEM"))
            m_cards.append(makeNote("SYSTEM", "Update available",
                "Ubuntu 26.04 security patch ready to install", "12m",
                a3));
        if (allowed("APOKOLIPS"))
            m_cards.append(makeNote("APOKOLIPS", "Shell loaded",
                "All modules online. Welcome back.", "just now", a4));

        if (m_cardsLayout) {
            for (QWidget *c : m_cards) {
                m_cardsLayout->addWidget(c);
                // Auto-hide after configured banner duration.
                // A repeating 200ms check picks up live config changes.
                QTimer *hidetimer = new QTimer(c);
                hidetimer->setInterval(200);
                QDateTime *shownAt = new QDateTime(QDateTime::currentDateTime());
                QObject::connect(hidetimer, &QTimer::timeout,
                                 [this, c, shownAt, hidetimer]() {
                    int dur = VisualConfigManager::instance().cfg()
                              .notificationBannerMs;
                    if (shownAt->msecsTo(QDateTime::currentDateTime()) < dur)
                        return;
                    hidetimer->stop();
                    delete shownAt;
                    hidetimer->deleteLater();
                    if (!m_cards.contains(c)) return;
                    m_cards.removeAll(c);
                    if (m_cardsLayout) m_cardsLayout->removeWidget(c);
                    QColor dim = m_theme.textDim;
                    c->setStyleSheet(QString(
                        "QWidget {"
                        "  background: %1;"
                        "  border: none;"
                        "  border-left: 3px solid %2;"
                        "  border-radius: 12px;"
                        "}").arg(rgba(m_theme.chromeBg.darker(120)),
                                 rgba(dim)));
                    if (m_historyLayout) {
                        m_history.prepend(c);
                        m_historyLayout->insertWidget(0, c);
                        while (m_history.size() > 6) {
                            QWidget *old = m_history.takeLast();
                            m_historyLayout->removeWidget(old);
                            old->deleteLater();
                        }
                    }
                    if (m_historyHeader) m_historyHeader->setVisible(true);
                    if (m_emptyLabel)
                        m_emptyLabel->setVisible(m_cards.isEmpty());
                });
                hidetimer->start();
            }
        }
    }

    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(14, 44, 14, 14);
        outer->setSpacing(14);

        // Header row: label + Clear All button
        QHBoxLayout *hdr = new QHBoxLayout;
        hdr->setContentsMargins(0, 0, 0, 0);
        hdr->setSpacing(8);

        QLabel *title = new QLabel("NOTIFICATIONS");
        title->setStyleSheet(QString(
            "color: %1; font-size: 11px; letter-spacing: 2px;"
            " font-weight: 600;"
            " background: transparent;").arg(m_theme.accent.name()));
        hdr->addWidget(title);
        hdr->addStretch();

        m_clearAllBtn = new QPushButton("Clear All");
        m_clearAllBtn->setCursor(Qt::PointingHandCursor);
        m_clearAllBtn->setFlat(true);
        QObject::connect(m_clearAllBtn, &QPushButton::clicked,
                         this, &NotificationCenter::clearAll);
        hdr->addWidget(m_clearAllBtn);
        outer->addLayout(hdr);

        // Active cards
        QWidget *holder = new QWidget;
        holder->setAttribute(Qt::WA_TranslucentBackground, true);
        m_cardsLayout = new QVBoxLayout(holder);
        m_cardsLayout->setContentsMargins(0, 0, 0, 0);
        m_cardsLayout->setSpacing(14);
        outer->addWidget(holder);
        outer->addStretch(1);
        outer->addStretch(1); // Always stack from top

        // Empty state
        m_emptyLabel = new QLabel("No new notifications");
        m_emptyLabel->setAlignment(Qt::AlignCenter);
        m_emptyLabel->setVisible(false);
        outer->addWidget(m_emptyLabel);

        // History section (hidden when empty)
        m_historyHeader = new QLabel("RECENT");
        m_historyHeader->setVisible(false);
        outer->addWidget(m_historyHeader);

        m_historyHolder = new QWidget;
        m_historyHolder->setAttribute(Qt::WA_TranslucentBackground, true);
        m_historyLayout = new QVBoxLayout(m_historyHolder);
        m_historyLayout->setContentsMargins(0, 0, 0, 0);
        m_historyLayout->setSpacing(10);
        outer->addWidget(m_historyHolder);

        outer->addStretch();

        buildCards();
    }

    void clearAll() {
        // Move active cards to history (dim them)
        for (QWidget *c : m_cards) {
            m_cardsLayout->removeWidget(c);
            m_history.prepend(c);
            m_historyLayout->insertWidget(0, c);
            c->setStyleSheet(QString(
                "QWidget {"
                "  background: %1;"
                "  border: none;"
                "  border-left: 3px solid %2;"
                "  border-radius: 12px;"
                "}").arg(rgba(m_theme.chromeBg.darker(120)),
                         rgba(m_theme.textDim)));
        }
        m_cards.clear();

        // Limit history to 6
        while (m_history.size() > 6) {
            QWidget *old = m_history.takeLast();
            m_historyLayout->removeWidget(old);
            old->deleteLater();
        }

        m_historyHeader->setVisible(!m_history.isEmpty());
        m_emptyLabel->setVisible(m_cards.isEmpty());
    }

    void updateThemeOnHeader() {
        if (!m_clearAllBtn) return;
        m_clearAllBtn->setStyleSheet(QString(
            "QPushButton { color: %1; background: transparent;"
            "  border: none; font-size: 11px; padding: 3px 8px; }"
            "QPushButton:hover { color: %2;"
            "  background: rgba(255,255,255,30); border-radius: 6px; }")
            .arg(m_theme.textSecondary.name(),
                 m_theme.textPrimary.name()));
        if (m_historyHeader) {
            m_historyHeader->setStyleSheet(QString(
                "color: %1; font-size: 10px; letter-spacing: 4px;"
                " background: transparent; margin-top: 8px;")
                .arg(m_theme.textDim.name()));
        }
        if (m_emptyLabel) {
            m_emptyLabel->setStyleSheet(QString(
                "color: %1; font-size: 12px; background: transparent;"
                " padding: 20px 0;").arg(m_theme.textDim.name()));
        }
    }

    QTimer *m_animTimer = nullptr;
    qreal m_slide = 0.0;
    qreal m_target = 0.0;
    Theme m_theme;
    QVBoxLayout *m_cardsLayout = nullptr;
    QList<QWidget *> m_cards;
    QTimer *m_notifyTimer = nullptr;

    QPushButton *m_clearAllBtn = nullptr;
    QLabel      *m_historyHeader = nullptr;
    QVBoxLayout *m_historyLayout = nullptr;
    QWidget     *m_historyHolder = nullptr;
    QLabel      *m_emptyLabel = nullptr;
    QList<QWidget *> m_history;
};

// Launches an external app via its .desktop file. Parses Exec= and
// injects Wayland env vars (WAYLAND_DISPLAY, SAL_USE_VCLPLUGIN).
// Used by both DockIcon::launchApp and AppLauncher tile click so
// external apps launch identically from every entry point.
static void launchExternalDesktop(const QString &desktopBase) {
    const QString path = "/usr/share/applications/"
                         + desktopBase + ".desktop";
    QFile df(path);
    if (!df.open(QIODevice::ReadOnly)) {
        qWarning() << "[launch] cannot open" << path;
        return;
    }
    QTextStream ts(&df);
    QString line, execCmd;
    while (ts.readLineInto(&line)) {
        if (line.startsWith("Exec=")) { execCmd = line.mid(5); break; }
    }
    df.close();

    execCmd.remove(QRegularExpression(" ?%[UFufick]"));
    execCmd = execCmd.trimmed();
    if (execCmd.isEmpty()) {
        qWarning() << "[launch] empty Exec for" << desktopBase;
        return;
    }
    QStringList parts = QProcess::splitCommand(execCmd);
    if (parts.isEmpty()) return;
    const QString program = parts.takeFirst();
    qInfo() << "[launch]" << desktopBase << "->" << program << parts;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.value("WAYLAND_DISPLAY").isEmpty()) {
        QString runtime = env.value("XDG_RUNTIME_DIR");
        if (runtime.isEmpty()) runtime = "/run/user/1000";
        QDir rd(runtime);
        for (const QString &f : rd.entryList({"wayland-*"},
                                             QDir::Files | QDir::System)) {
            if (f.endsWith(".lock")) continue;
            env.insert("WAYLAND_DISPLAY", f);
            break;
        }
    }
    if (env.value("XDG_RUNTIME_DIR").isEmpty())
        env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
    if (env.value("XDG_SESSION_TYPE").isEmpty())
        env.insert("XDG_SESSION_TYPE", "wayland");
    if (env.value("SAL_USE_VCLPLUGIN").isEmpty())
        env.insert("SAL_USE_VCLPLUGIN", "qt6");
    
    // Per-toolkit theming (GTK, Qt5/Qt6 via qt5ct/qt6ct): see theme_bridge.h
    applyThemeEnv(env, desktopBase);
    
    // Chromium / Electron family: dark-mode flags (merged, never duplicated)
    themeChromiumFlags(program, parts);
    
    if (env.value("DISPLAY").isEmpty()) {
        if (QFile::exists("/tmp/.X11-unix/X0"))
            env.insert("DISPLAY", ":0");
        else if (QFile::exists("/tmp/.X11-unix/X1"))
            env.insert("DISPLAY", ":1");
    }

    qInfo() << "[launch] env:"
            << "WAYLAND_DISPLAY=" << env.value("WAYLAND_DISPLAY")
            << "DISPLAY=" << env.value("DISPLAY")
            << "SAL_USE_VCLPLUGIN=" << env.value("SAL_USE_VCLPLUGIN");

    QProcess *proc = new QProcess;
    proc->setProgram(program);
    proc->setArguments(parts);
    proc->setProcessEnvironment(env);
    proc->startDetached();
    delete proc;
}

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

        // Live preview — Settings writes visual.json from another
        // process, so watch the file directly.
        {
            QString cfgPath = QStandardPaths::writableLocation(
                                  QStandardPaths::ConfigLocation);
            if (cfgPath.isEmpty())
                cfgPath = QDir::homePath() + "/.config";
            cfgPath += "/apokolips/visual.json";

            QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
            if (QFile::exists(cfgPath)) watcher->addPath(cfgPath);
            QObject::connect(watcher, &QFileSystemWatcher::fileChanged,
                             this, [this, cfgPath, watcher]() {
                VisualConfigManager::instance().load();
                // rebuildTiles tears down and recreates the tiles.
                // restyleTiles then applies the theme stylesheet —
                // without it new tiles render with Qt's default
                // (light) look. repositionPanel updates geometry for
                // a changed column count or anchor.
                rebuildTiles();
                restyleTiles();
                repositionPanel();
                update();
                if (!watcher->files().contains(cfgPath)
                    && QFile::exists(cfgPath))
                    watcher->addPath(cfgPath);
            });
        }
    }

    void toggle() {
        auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.launcherEnabled) return;
        (isVisible() && m_opacity > 0.5) ? hideLauncher() : showLauncher();
    }
    void showLauncher() {
        // Apply the "Add search field to the Launcher" setting every
        // time the panel opens. If launcherHasSearch is off, hide both
        // the "SEARCH" title and the QLineEdit so the panel collapses
        // into a clean icon grid.
        const auto &vc = VisualConfigManager::instance().cfg();
        if (m_titleLabel) m_titleLabel->setVisible(vc.launcherHasSearch);
        if (m_search) m_search->setVisible(vc.launcherHasSearch);
        raise(); show(); setFocus(); m_fadeTarget = 1.0; m_animTimer->start();
    }
    void hideLauncher() {
        if (m_search) m_search->clear();
        m_fadeTarget = 0.0;
        m_animTimer->start();
    }

    void rebuildTiles() {
        // Hide panel, tear down grid, rebuild fresh from registry.
        // Simplest reliable path: hide + emit a "rebuild requested"
        // signal that the outer widget handles by rebuilding buildUi().
        // Here we do a direct rebuild: destroy the entire tile grid
        // by deleting all tiles + column layouts, then re-add.
        for (QPushButton *t : m_tiles) t->deleteLater();
        m_tiles.clear();
        m_nameLabels.clear();
        m_iconLabels.clear();

        // Destroy existing column layouts
        for (QVBoxLayout *col : m_gridColumns) {
            QLayoutItem *it;
            while ((it = col->takeAt(0)) != nullptr) {
                if (QWidget *w = it->widget()) w->deleteLater();
                delete it;
            }
            delete col;
        }
        m_gridColumns.clear();

        // Find the parent HBox that held the columns
        QHBoxLayout *gridRow = m_gridRow;
        if (!gridRow) return;
        // Clear stale items from gridRow
        QLayoutItem *gi;
        while ((gi = gridRow->takeAt(0)) != nullptr) {
            if (QLayout *l = gi->layout()) delete l;
            if (QWidget *w = gi->widget()) w->deleteLater();
            delete gi;
        }

        // Build fresh columns — same COLS value used to create the
        // columns and to place the tiles. Must never diverge.
        const int COLS = qBound(3, VisualConfigManager::instance()
                                     .cfg().launcherColumns, 8);
        for (int c = 0; c < COLS; ++c) {
            QVBoxLayout *colLay = new QVBoxLayout;
            colLay->setContentsMargins(0, 0, 0, 0);
            colLay->setSpacing(14);
            colLay->setAlignment(Qt::AlignTop);
            m_gridColumns.append(colLay);
            gridRow->addLayout(colLay);
        }

        // Populate
        QVector<AppEntry> visible;
        for (const auto &e : appRegistry())
            if (QString::fromUtf8(e.id) != "launchpad"
                && isInLauncher(QString::fromUtf8(e.id)))
                visible.append(e);

        for (int i = 0; i < visible.size(); ++i) {
            QWidget *tile = makeTile(QString::fromUtf8(visible[i].glyph),
                                     visible[i].label, i);
            m_gridColumns[i % COLS]->addWidget(
                tile, 0, Qt::AlignTop | Qt::AlignHCenter);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (m_panelRect.isNull()) return;
        const auto &vc = VisualConfigManager::instance().cfg();
        const QRect panelRect = m_panelRect;

        // ---- Soft edge: many thin rings = smooth fade ----
        if (m_opacity > 0.35 || m_fadeTarget >= m_opacity) {
            p.setPen(Qt::NoPen);
            for (int i = 14; i >= 1; --i) {
                QColor sh(0, 0, 0);
                sh.setAlphaF(0.012 * m_opacity);
                p.setBrush(sh);
                p.drawRoundedRect(panelRect.adjusted(-i, -i + 3, i, i + 3),
                                  22 + i, 22 + i);
            }
        }

        // ---- Wallpaper blur through the panel ----
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull() && vc.launcherBlur > 0) {
            QPainterPath panelPath;
            panelPath.addRoundedRect(panelRect, 22, 22);
            p.save();
            p.setClipPath(panelPath);
            p.setOpacity((vc.launcherBlur / 100.0) * m_opacity);
            p.drawPixmap(panelRect, bg, panelRect);
            p.restore();
        }

        // ---- Tint so tiles stay legible ----
        StyleRenderer::drawPanel(p, QRectF(panelRect), 22, m_theme,
                                 int(vc.launcherAlpha * m_opacity));
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { hideLauncher(); e->accept(); }
        else QWidget::keyPressEvent(e);
    }

private:
    static constexpr int PANEL_W = 720;
    static constexpr int PANEL_H = 480;

    void tickFade() {
        // Faster fade: 4 frames for open, 3 for close (~64/48 ms)
        if (m_opacity < m_fadeTarget) {
            m_opacity += 0.28;
            if (m_opacity >= m_fadeTarget) m_opacity = m_fadeTarget;
        } else if (m_opacity > m_fadeTarget) {
            m_opacity -= 0.35;
            if (m_opacity <= m_fadeTarget) m_opacity = m_fadeTarget;
            // Bail out early while closing — no point drawing the
            // last few nearly-invisible frames.
            if (m_opacity <= 0.05 && m_fadeTarget == 0.0) {
                m_opacity = 0.0;
                m_animTimer->stop();
                hide();
                update();
                return;
            }
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
                "  color: %2;"
                "  font-size: 15px;"
                "}"
                "QLineEdit:focus { border: 1px solid %3; }")
                .arg(rgba(m_theme.chromeBg.lighter(120)),
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
        QString textColor = m_theme.textPrimary.name();

        // Keep the app button and hit area, but remove its visible tile.
        QString css = QString(
            "QPushButton { background: transparent; border: none; color: %1; }"
            "QPushButton:hover { background: transparent; border: none; }"
            "QPushButton:pressed { background: transparent; border: none; }")
            .arg(textColor);

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
        // The panel is a direct child of this widget and is
        // positioned absolutely by repositionPanel(). No layout —
        // a layout would fight the anchor-move.
        m_panel = new QWidget(this);
        m_panel->setAttribute(Qt::WA_TranslucentBackground, true);
        m_panel->setAttribute(Qt::WA_NoSystemBackground, true);

        QVBoxLayout *pl = new QVBoxLayout(m_panel);
        pl->setContentsMargins(28, 26, 28, 30);
        pl->setSpacing(18);

        m_titleLabel = new QLabel("SEARCH");
        pl->addWidget(m_titleLabel);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search apps, files, settings...");
        pl->addWidget(m_search);
        QObject::connect(m_search, &QLineEdit::textChanged,
                         this, [this](const QString &q) {
            filterTiles(q);
        });

        m_tilesLayout = new QGridLayout;
        m_tilesLayout->setSpacing(14);
        m_tilesLayout->setContentsMargins(20, 20, 20, 20);

        QVector<AppEntry> visible;
        for (const auto &e : appRegistry())
            if (QString::fromUtf8(e.id) != "launchpad"
                && isInLauncher(QString::fromUtf8(e.id)))
                visible.append(e);

        // COLS is read once and used by both the create loop and
        // the placement loop. They must never use different values.
        const int COLS = qBound(3, VisualConfigManager::instance()
                                     .cfg().launcherColumns, 8);

        QList<QVBoxLayout *> cols;
        QHBoxLayout *gridRow = new QHBoxLayout;
        gridRow->setContentsMargins(0, 0, 0, 0);
        gridRow->setSpacing(14);
        for (int c = 0; c < COLS; ++c) {
            QVBoxLayout *colLay = new QVBoxLayout;
            colLay->setContentsMargins(0, 0, 0, 0);
            colLay->setSpacing(14);
            colLay->setAlignment(Qt::AlignTop);
            cols.append(colLay);
            gridRow->addLayout(colLay);
        }

        const int appCount = visible.size();
        for (int i = 0; i < appCount; ++i) {
            QWidget *tile = makeTile(QString::fromUtf8(visible[i].glyph),
                                     visible[i].label, i);
            cols[i % COLS]->addWidget(tile, 0,
                                      Qt::AlignTop | Qt::AlignHCenter);
        }
        m_gridColumns = cols;
        m_gridRow = gridRow;

        QWidget *scrollContent = new QWidget;
        scrollContent->setAttribute(Qt::WA_TranslucentBackground, true);
        scrollContent->setAttribute(Qt::WA_NoSystemBackground, true);
        scrollContent->setStyleSheet("background: transparent;");
        QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
        scrollLayout->setContentsMargins(0, 0, 0, 0);
        scrollLayout->setSpacing(18);
        // Stop Qt from shrinking the grid rows when the panel is short
        scrollLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        scrollLayout->addLayout(gridRow);

        // File-results section — populated by searchFiles() when
        // the user types a query. Hidden by default. Sits below the
        // app grid so both app matches and file matches are visible.
        m_fileResultsBox = new QWidget;
        m_fileResultsBox->setAttribute(Qt::WA_TranslucentBackground, true);
        m_fileResultsLayout = new QVBoxLayout(m_fileResultsBox);
        m_fileResultsLayout->setContentsMargins(20, 8, 20, 8);
        m_fileResultsLayout->setSpacing(4);
        m_fileResultsBox->setVisible(false);
        scrollLayout->addWidget(m_fileResultsBox);
        scrollLayout->addStretch();

        QScrollArea *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAttribute(Qt::WA_TranslucentBackground, true);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        scroll->viewport()->setStyleSheet("background: transparent;");
        scroll->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
            "QScrollBar::handle:vertical { background: rgba(255,255,255,40); border-radius: 4px; min-height: 20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }"
        );
        scroll->setWidget(scrollContent);
        // Prevent QScrollArea from squishing the content horizontally
        const auto &vc2 = VisualConfigManager::instance().cfg();
        const int cCols = qBound(3, vc2.launcherColumns, 8);
        const int cTileW = vc2.launcherIconSize + 88;
        scrollContent->setMinimumWidth(cCols * cTileW + (cCols - 1) * 14 + 40);
        pl->addWidget(scroll, 1);

        repositionPanel();
    }

    // Panel width grows with the column count and icon size so the
    // grid never overflows the panel.
    int computedPanelW() const {
        const auto &vc = VisualConfigManager::instance().cfg();
        const int cols = qBound(3, vc.launcherColumns, 8);
        const int tileW = vc.launcherIconSize + 88;
        const int contentW = cols * tileW + (cols - 1) * 14;
        // Account for margins (56), scrollbar (~10), and grid inner margins (40).
        return contentW + 140;
    }

    // Height grows with app count so the launcher never squeezes.
    // Capped at 90% of screen height as a safety net (installing 50
    // apps would push tiles offscreen without a scroll area, which
    // is Variant C — deferred until needed).
    int computedPanelH() const {
        const auto &vc = VisualConfigManager::instance().cfg();
        const int cols = qBound(3, vc.launcherColumns, 8);
        const int tileH = vc.launcherIconSize + 58;
        const int appCount = qMax(1, m_tiles.size());
        const int rows = (appCount + cols - 1) / cols;
        const int gridH = rows * tileH + (rows - 1) * 14;
        const int chromeH = 40 + 20 + 26 + 30 + 20;  // search+header+margins
        const int natural = gridH + chromeH;
        int maxH = 900;
        if (QScreen *s = QGuiApplication::primaryScreen())
            maxH = int(s->geometry().height() * 0.85);
        return qBound(300, natural, maxH);
    }

    // Position the panel per the 3x3 anchor grid. 0=TL, 1=TC,
    // 2=TR, 3=ML, 4=MC, 5=MR, 6=BL, 7=BC, 8=BR.
    void repositionPanel() {
        if (!m_panel) return;
        const auto &vc = VisualConfigManager::instance().cfg();
        const int panelW = computedPanelW();
        const int panelH = computedPanelH();
        int px, py;
        switch (vc.launcherAnchor % 3) {
            case 0:  px = 40; break;
            case 2:  px = width() - panelW - 40; break;
            default: px = (width() - panelW) / 2;
        }
        switch (vc.launcherAnchor / 3) {
            case 0:  py = 120; break;
            case 2:  py = height() - panelH - 120; break;
            default: py = (height() - panelH) / 2;
        }
        // py can go negative if panelH is very large and anchor is
        // bottom — clamp so the panel never escapes the top edge.
        if (py < 40) py = 40;
        m_panel->setGeometry(px, py, panelW, panelH);
        m_panelRect = QRect(px, py, panelW, panelH);
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        repositionPanel();
    }

    void filterTiles(const QString &query) {
        const QString q = query.trimmed();
        for (QPushButton *tile : m_tiles) {
            if (!tile) continue;
            if (q.isEmpty()) {
                tile->setVisible(true);
                continue;
            }
            const QString appName = tile->property("appName").toString();
            const bool matches = appName.contains(q, Qt::CaseInsensitive);
            tile->setVisible(matches);
        }
        searchFiles(q);
    }

    // Scoped file search — searches Documents, Downloads, Desktop,
    // Pictures, Music, Videos recursively. Skips noise directories
    // (node_modules, .git, build, __pycache__) even when found inside
    // a scoped folder. Caps at 20 results. Fast on VM because scope
    // is small; on real hardware this is instant.
    void searchFiles(const QString &query) {
        if (!m_fileResultsLayout) return;

        // Tear down old results
        for (QPushButton *row : m_fileResultsList) {
            if (row) row->deleteLater();
        }
        m_fileResultsList.clear();

        const QString q = query.trimmed();
        if (q.length() < 2) {
            if (m_fileResultsBox) m_fileResultsBox->setVisible(false);
            return;
        }

        const QString home = QDir::homePath();
        const QStringList scopes = {
            home + "/Documents",
            home + "/Downloads",
            home + "/Desktop",
            home + "/Pictures",
            home + "/Music",
            home + "/Videos",
        };
        const QStringList excludeDirs = {
            "/node_modules/", "/.git/", "/build/",
            "/__pycache__/", "/.cache/",
        };

        int count = 0;
        const int MAX_RESULTS = 20;

        for (const QString &scope : scopes) {
            if (count >= MAX_RESULTS) break;
            QDir scopeDir(scope);
            if (!scopeDir.exists()) continue;

            QDirIterator it(scope,
                QDir::Files | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories);
            while (it.hasNext() && count < MAX_RESULTS) {
                it.next();
                const QString path = it.filePath();

                bool skip = false;
                for (const QString &ex : excludeDirs) {
                    if (path.contains(ex)) { skip = true; break; }
                }
                if (skip) continue;

                QFileInfo fi(path);
                if (!fi.fileName().contains(q, Qt::CaseInsensitive))
                    continue;

                QPushButton *row = new QPushButton;
                row->setCursor(Qt::PointingHandCursor);
                row->setFixedHeight(34);
                row->setStyleSheet(
                    "QPushButton {"
                    "  text-align: left;"
                    "  padding: 4px 12px;"
                    "  color: " + Sigil::textPrimary().name() + ";"
                    "  background: transparent;"
                    "  border: none;"
                    "  border-radius: 6px;"
                    "  font-size: 12px; }"
                    "QPushButton:hover {"
                    "  background: " + rgba(Sigil::rowHover()) + "; }");
                row->setText("  ð  " + fi.fileName()
                             + "   —   " + fi.absolutePath().left(60));

                const QString filePath = path;
                QObject::connect(row, &QPushButton::clicked,
                                 [this, filePath]() {
                    QProcess::startDetached("xdg-open", { filePath });
                    if (parentWidget())
                        this->hideLauncher();
                });

                m_fileResultsLayout->addWidget(row);
                m_fileResultsList.append(row);
                ++count;
            }
        }

        if (m_fileResultsBox) m_fileResultsBox->setVisible(count > 0);
        repositionPanel();
    }

    QWidget *makeTile(const QString &icon, const QString &name, int index) {
        const int iconSize =
            VisualConfigManager::instance().cfg().launcherIconSize;
        const int tileW = iconSize + 88;
        const int tileH = iconSize + 58;
        const int iconWidgetSize = iconSize + 12;

        QPushButton *tile = new QPushButton;
        tile->setFixedSize(tileW, tileH);
        tile->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setProperty("appName", name);

        QVBoxLayout *v = new QVBoxLayout(tile);
        v->setContentsMargins(4, 6, 4, 8);
        v->setSpacing(4);
        v->setAlignment(Qt::AlignCenter);

        // Custom vector icon widget (replaces old emoji label)
        class TileIcon : public QWidget {
        public:
            TileIcon(const QString &iconName, QWidget *parent = nullptr,
                     int size = 64)
                : QWidget(parent), m_name(iconName) {
                setFixedSize(size, size);
                setAttribute(Qt::WA_TransparentForMouseEvents, true);
                ThemeManager::instance().subscribe([this](const Theme &t) {
                    m_color = t.textPrimary;
                    update();
                });
            }
            void setHovered(bool hovered) {
                if (m_hovered == hovered) return;
                m_hovered = hovered;
                update();
            }
        protected:
            void paintEvent(QPaintEvent *) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                QRectF artRect(rect());
                if (!m_hovered) {
                    int adj = int(width() * 0.09);
                    artRect.adjust(adj, adj, -adj, -adj);
                }
                // Bug #2 fix (2026-10-17): route ALL icons through
                // Icons::drawFor uniformly. drawGlyph now has its own
                // appTile() background, so the launcher tile matches
                // the dock icon and the rest of the app grid.
                Icons::drawFor(m_name, p, artRect, m_color);
            }

            // Glyph logo: dashed crop frame, 3 oxblood corners,
            // bottom-right replaced by a filled caret
            static void drawGlyphMark(QPainter &p, const QRectF &r) {
                QRectF frame = r.adjusted(r.width() * 0.16,
                                          r.height() * 0.16,
                                          -r.width() * 0.16,
                                          -r.height() * 0.16);

                QPen bone(QColor(240, 237, 232, 210), 1.3);
                bone.setStyle(Qt::DashLine);
                bone.setDashPattern({3, 3});
                p.setPen(bone);
                p.setBrush(Qt::NoBrush);
                p.drawRect(frame);

                QColor ox(168, 50, 50);
                QPen corner(ox, 2.6);
                corner.setCapStyle(Qt::RoundCap);
                p.setPen(corner);
                qreal L = frame.width() * 0.34;

                p.drawLine(frame.topLeft(),
                           frame.topLeft() + QPointF(L, 0));
                p.drawLine(frame.topLeft(),
                           frame.topLeft() + QPointF(0, L));

                p.drawLine(frame.topRight(),
                           frame.topRight() + QPointF(-L, 0));
                p.drawLine(frame.topRight(),
                           frame.topRight() + QPointF(0, L));

                p.drawLine(frame.bottomLeft(),
                           frame.bottomLeft() + QPointF(L, 0));
                p.drawLine(frame.bottomLeft(),
                           frame.bottomLeft() + QPointF(0, -L));

                QPointF br = frame.bottomRight();
                QPolygonF caret;
                caret << br + QPointF(-L * 0.85, 0)
                      << br
                      << br + QPointF(0, -L * 0.85);
                p.setPen(Qt::NoPen);
                p.setBrush(ox);
                p.drawPolygon(caret);
            }
        private:
            QString m_name;
            QColor  m_color = QColor(230, 230, 240);
            bool m_hovered = false;
        };

        TileIcon *ic = new TileIcon(name, nullptr, iconWidgetSize);

        // Track hover on the button (the icon itself ignores mouse events).
        class TileHoverFilter final : public QObject {
        public:
            TileHoverFilter(TileIcon *icon, QObject *parent)
                : QObject(parent), m_icon(icon) {}
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Enter)
                    m_icon->setHovered(true);
                else if (event->type() == QEvent::Leave)
                    m_icon->setHovered(false);
                return false;
            }
        private:
            TileIcon *m_icon;
        };
        tile->installEventFilter(new TileHoverFilter(ic, tile));
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
            // External apps (glyph = "ext:<desktop-base>")
            for (const auto &e : appRegistry()) {
                if (appName == QLatin1String(e.label)) {
                    QString g = QString::fromUtf8(e.glyph);
                    if (g.startsWith("ext:")) {
                        launchExternalDesktop(g.mid(4));
                        hideLauncher();
                        return;
                    }
                    break;
                }
            }
            // Launchpad must never spawn a new shell process. If a
            // Launchpad tile somehow ends up in the grid, toggle the
            // existing launcher instead. Closes the orphan-shell /
            // half-screen-tile bug at the source.
            if (appName == "Launchpad") {
                hideLauncher();
                if (ShellRegistry::launcherToggle())
                    ShellRegistry::launcherToggle()();
                return;
            }
            QStringList args;
            if (appName == "Settings")
                args = { "--settings" };
            else if (appName == "Finder")
                args = { "--files" };
            else if (appName == "Glyph")
                args = { "--glyph" };
            else if (appName == "BoomTube")
                args = { "--boomtube" };
            else if (appName == "myMach")
                args = { "--mymach" };
            else if (appName == "Aperture") {
                // Open the image viewer with no file — user picks one
                // For now launch with a default sample if one exists.
                QString home = QDir::homePath();
                QString sample = home + "/Pictures/test-gradient.png";
                if (QFile::exists(sample))
                    args = { "--image", sample };
                else
                    args = { "--files", home + "/Pictures" };
            }
            else if (appName == "Node") {
                args = { "--node" };
            }
            else if (appName == "Notes") {
                args = { "--notes" };
            }
            else if (appName == "Calculator") {
                args = { "--calc" };
            }
            else if (appName == "Mosaic") {
                args = { "--mosaic" };
            }
            else {
                // Unknown tile — do NOT spawn a bare shell. A bare
                // spawn inherits app_id=apokolips-shell and gets
                // tiled next to the real shell, splitting the screen.
                // Log and bail.
                qWarning() << "[launcher] no click handler for tile:"
                           << appName << "— refusing to spawn";
                hideLauncher();
                return;
            }
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                args);
            hideLauncher();
        });

        // ---- Right-click guard: swallow non-left press/release so
        //      QPushButton never fires clicked() on right-click, but
        //      the context menu event still reaches our handler. ----
        class RightClickGuard : public QObject {
        public:
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::MouseButtonPress
                    || e->type() == QEvent::MouseButtonRelease) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    if (me->button() != Qt::LeftButton) return true;
                }
                return false;
            }
        };
        tile->installEventFilter(new RightClickGuard);

        // ---- Right-click menu — Add to / Remove from Dock / Desktop ----
        tile->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(tile, &QPushButton::customContextMenuRequested,
                         [this, appName, tile](const QPoint &pos) {
            QString id = idForLabel(appName);
            if (id.isEmpty()) return;

            bool inDock    = isInDock(id);
            bool onDesktop = isOnDesktop(id);

            QMenu menu;
            menu.setStyleSheet(
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

            QAction *dockAct = menu.addAction(
                inDock ? "Remove from Dock" : "Add to Dock");
            QAction *deskAct = menu.addAction(
                onDesktop ? "Remove from thePool" : "Add to thePool");

            QWidget *from = tile;
            QAction *chosen = menu.exec(
                from->mapToGlobal(pos));
            if (chosen == dockAct) {
                setInDock(id, !inDock);
            } else if (chosen == deskAct) {
                setOnDesktop(id, !onDesktop);
            }
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
    QWidget *m_fileResultsBox = nullptr;
    QVBoxLayout *m_fileResultsLayout = nullptr;
    QList<QPushButton *> m_fileResultsList;
    QList<QLabel *> m_iconLabels;
    QList<QLabel *> m_nameLabels;
    QList<QVBoxLayout *> m_gridColumns;
    QHBoxLayout *m_gridRow = nullptr;
    QWidget *m_panel = nullptr;
    QRect m_panelRect;
};

// ----------------------------------------------------------------
// DockPreviewPopup — Windows 11 style window thumbnails shown
// above a dock icon on hover. Queries sway for every window of
// the app, captures a screenshot of each, shows them side by
// side. Click a thumbnail → focus that window. Move off → closes.
// ----------------------------------------------------------------
class DockPreviewPopup : public QWidget {
public:
    DockPreviewPopup(const QString &appId,
                     const QString &titleMatch,
                     QWidget *anchorIcon,
                     QWidget *parentRoot)
        : QWidget(parentRoot),
          m_appId(appId), m_titleMatch(titleMatch),
          m_anchor(anchorIcon)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_DeleteOnClose, true);

        m_theme = ThemeManager::instance().current();

        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(8, 8, 8, 8);
        outer->setSpacing(0);

        Sigil::Card *card = new Sigil::Card;
        outer->addWidget(card);

        m_row = new QHBoxLayout(card);
        m_row->setContentsMargins(12, 12, 12, 12);
        m_row->setSpacing(10);

        m_closeTimer = new QTimer(this);
        m_closeTimer->setSingleShot(true);
        QObject::connect(m_closeTimer, &QTimer::timeout,
                         this, [this]() { close(); });

        QTimer::singleShot(0, this, [this]() { loadWindows(); });
    }

    void keepAlive() { if (m_closeTimer) m_closeTimer->stop(); }
    void scheduleClose(int ms = 200) {
        if (m_closeTimer) m_closeTimer->start(ms);
    }

protected:
    void enterEvent(QEnterEvent *) override { keepAlive(); }
    void leaveEvent(QEvent *)      override { scheduleClose(300); }

    void paintEvent(QPaintEvent *) override {
        // Let the Sigil::Card render — nothing to add on top.
    }

private:
    void loadWindows() {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        p.setProcessEnvironment(env);
        p.start("swaymsg", { "-t", "get_tree", "-r" });
        if (!p.waitForFinished(600)) { close(); return; }
        QJsonDocument doc = QJsonDocument::fromJson(
            p.readAllStandardOutput());
        if (!doc.isObject()) { close(); return; }

        QList<QJsonObject> windows;
        collectWindows(doc.object(), windows);
        qInfo() << "[preview] appId=" << m_appId
                << "title=" << m_titleMatch
                << "windows=" << windows.size();

        if (windows.isEmpty()) { close(); return; }

        for (const QJsonObject &w : windows) addCard(w);
        adjustSize();
        positionAboveAnchor();
        show();
        raise();
    }

    void collectWindows(const QJsonObject &node,
                        QList<QJsonObject> &out) {
        const QString type = node.value("type").toString();
        if (type == "con" || type == "floating_con") {
            const QString app = node.value("app_id").toString();
            const QString title = node.value("name").toString();
            if (title.isEmpty()) {
                for (const QJsonValue &v : node.value("nodes").toArray())
                    collectWindows(v.toObject(), out);
                for (const QJsonValue &v :
                     node.value("floating_nodes").toArray())
                    collectWindows(v.toObject(), out);
                return;
            }
            // Settings launches as `apokolips-shell --settings` so
            // its sway app_id is "apokolips-shell" not
            // "apokolips-settings". When a title filter is set,
            // that wins. Without one, fall back to appId.
            bool match = false;
            if (!m_titleMatch.isEmpty()) {
                match = title.contains(m_titleMatch,
                                       Qt::CaseInsensitive);
            } else if (!m_appId.isEmpty()) {
                match = (app == m_appId);
            }
            if (match) out.append(node);
        }
        for (const QJsonValue &v : node.value("nodes").toArray())
            collectWindows(v.toObject(), out);
        for (const QJsonValue &v :
             node.value("floating_nodes").toArray())
            collectWindows(v.toObject(), out);
    }

    void addCard(const QJsonObject &w) {
        const int conId = w.value("id").toInt();
        const QString title = w.value("name").toString();
        QJsonObject r = w.value("rect").toObject();
        const QRect rect(r.value("x").toInt(), r.value("y").toInt(),
                         r.value("width").toInt(),
                         r.value("height").toInt());

        QWidget *cardW = new QWidget;
        cardW->setFixedSize(240, 170);
        cardW->setAttribute(Qt::WA_StyledBackground, true);
        cardW->setStyleSheet(QString(
            "QWidget { background: %1;"
            "  border: 1px solid rgba(255,255,255,55);"
            "  border-radius: 10px; }"
            "QWidget:hover { border: 1px solid rgba(255,255,255,150); }")
            .arg(rgba(m_theme.chromeBg.lighter(115))));

        QVBoxLayout *v = new QVBoxLayout(cardW);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(6);

        QLabel *thumb = new QLabel;
        thumb->setFixedSize(224, 126);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(
            "QLabel { background: rgba(0,0,0,60);"
            "  border-radius: 6px; color: rgba(255,255,255,90);"
            "  font-size: 11px; }");
        thumb->setText("Loading…");
        v->addWidget(thumb);

        QLabel *titleLbl = new QLabel;
        titleLbl->setFixedHeight(20);
        titleLbl->setStyleSheet(QString(
            "QLabel { color: %1; font-size: 11px;"
            "  background: transparent; border: none; }")
            .arg(m_theme.textPrimary.name()));
        QFontMetrics fm(titleLbl->font());
        titleLbl->setText(fm.elidedText(title, Qt::ElideMiddle, 220));
        v->addWidget(titleLbl);

        cardW->setCursor(Qt::PointingHandCursor);
        cardW->setProperty("conId", conId);
        cardW->installEventFilter(this);
        m_row->addWidget(cardW);

        captureThumbnail(conId, rect, thumb);
    }

    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease) {
            QWidget *w = qobject_cast<QWidget *>(obj);
            if (w) {
                int conId = w->property("conId").toInt();
                if (conId > 0) {
                    QProcess::startDetached("swaymsg",
                        { QString("[con_id=%1]").arg(conId), "focus" });
                    QTimer::singleShot(80, this, [this]() { close(); });
                    return true;
                }
            }
        }
        if (e->type() == QEvent::Enter) keepAlive();
        if (e->type() == QEvent::Leave) scheduleClose(300);
        return QWidget::eventFilter(obj, e);
    }

    void captureThumbnail(int conId, const QRect &rect, QLabel *label) {
        auto *p = new QProcess(this);
        QString tmpPath = QDir::tempPath()
            + QString("/apokolips-preview-%1.png").arg(conId);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        p->setProcessEnvironment(env);
        p->start("grim", {
            "-g", QString("%1,%2 %3x%4")
                .arg(rect.x()).arg(rect.y())
                .arg(rect.width()).arg(rect.height()),
            "-t", "png", tmpPath });

        // Guard: the popup (and its child widgets) can be destroyed
        // while the process is still running. If the label is dead
        // by the time the signal fires, skip. Same for the process
        // — during destruction its own ~QProcess emits finished(),
        // and touching the object from there crashes.
        QPointer<QLabel>   labelGuard(label);
        QPointer<QProcess> procGuard(p);
        QObject::connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [procGuard, labelGuard, tmpPath](int code,
                                             QProcess::ExitStatus) {
                if (!labelGuard) {
                    // Popup is gone. Clean up the temp file and
                    // do not touch anything else.
                    QFile::remove(tmpPath);
                    return;
                }
                if (procGuard) procGuard->deleteLater();
                if (code != 0) {
                    labelGuard->setText("(no preview)");
                    return;
                }
                QPixmap pm(tmpPath);
                QFile::remove(tmpPath);
                if (pm.isNull()) return;
                QPixmap scaled = pm.scaled(labelGuard->size(),
                    Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
                int sx = (scaled.width()  - labelGuard->width())  / 2;
                int sy = (scaled.height() - labelGuard->height()) / 2;
                labelGuard->setPixmap(scaled.copy(sx, sy,
                    labelGuard->width(), labelGuard->height()));
            });
    }

    void positionAboveAnchor() {
        if (!m_anchor) return;
        QWidget *root = parentWidget();
        if (!root) return;
        QPoint anchorInRoot = m_anchor->mapTo(root,
            QPoint(m_anchor->width() / 2, 0));
        int x = anchorInRoot.x() - width() / 2;
        int y = anchorInRoot.y() - height() - 12;
        x = qBound(8, x, root->width() - width() - 8);
        y = qMax(8, y);
        move(x, y);
    }

    QString            m_appId;
    QString            m_titleMatch;
    QPointer<QWidget>  m_anchor;
    QHBoxLayout       *m_row = nullptr;
    Theme              m_theme;
    QTimer            *m_closeTimer = nullptr;
};

class DockIcon : public QWidget {
public:
    DockIcon(const QString &icon, const QString &name, int index,
             const QString &swayId = QString(), bool isDynamic = false,
             QWidget *parent = nullptr)
        : QWidget(parent), m_icon(icon), m_name(name), m_index(index),
          m_swayId(swayId), m_isDynamic(isDynamic)
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

    QString m_swayId;
    bool m_isDynamic = false;
    bool m_active = false;

    void setTarget(qreal t)  { m_target = t; }
    void setCurrent(qreal c) {
        if (qAbs(c - m_current) > 0.002) { m_current = c; update(); }
    }

    // Bounce — called when a window minimizes to this app
    void bounce() {
        m_bounceFrames = 0;
        if (!m_bounceTimer) {
            m_bounceTimer = new QTimer(this);
            m_bounceTimer->setInterval(16);
            QObject::connect(m_bounceTimer, &QTimer::timeout,
                             this, [this]() {
                m_bounceFrames++;
                // Spring: up then settle back, 22 frames (~350ms)
                double t = m_bounceFrames / 22.0;
                if (t >= 1.0) {
                    m_bounceOffset = 0;
                    m_bounceTimer->stop();
                } else {
                    // Sine arc that peaks at t=0.3, decays
                    double arc = std::sin(t * 3.14159) * std::exp(-t * 2.0);
                    m_bounceOffset = -int(arc * 14);
                }
                update();
            });
        }
        m_bounceTimer->start();
    }
    qreal current() const    { return m_current; }

    void setRunning(bool r) {
        if (m_running != r) { m_running = r; update(); }
    }
    bool isRunning() const { return m_running; }
    QString appName() const { return m_name; }
    QString swayId() const { return m_swayId; }
    bool isDynamic() const { return m_isDynamic; }
    void setActive(bool a) { if (m_active != a) { m_active = a; update(); } }
    qint64  pid() const { return m_pid; }
    void setPid(qint64 p) { m_pid = p; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal baseSize =
            VisualConfigManager::instance().cfg().dockIconSize;
        int visual = int(baseSize * m_current);
        int x = (width()  - visual) / 2;

        // Hover amount — recomputed here so we can lift the icon.
        const qreal boost0 = (m_current > 1.0)
            ? qBound(0.0, (m_current - 1.0) / 0.42, 1.0)
            : 0.0;
        const int liftPx = int(boost0 * 3.0);

        int y = (height() - visual) / 2 - 4 + m_bounceOffset - liftPx;

        // Icon is full-bleed — no wrapper margin. Chip is drawn at
        // the same bounds and same radius (0.22). At rest it's
        // completely covered by the icon. On hover, it grows outward
        // a few px and its alpha ramps up, giving a soft dark halo.
        const qreal baseRadius = visual * 0.22;
        QRectF iconRect(x, y, visual, visual);

        // Hover amount 0..1
        const qreal boost = (m_current > 1.0)
            ? qBound(0.0, (m_current - 1.0) / 0.42, 1.0)
            : 0.0;

        // Chip — invisible at rest, halo on hover.
        const qreal pad = boost * 3.0;
        QRectF chipRect = iconRect.adjusted(-pad, -pad, pad, pad);
        const int chipAlpha = int(boost * 160);
        if (chipAlpha > 0) {
            QPainterPath chipPath;
            chipPath.addRoundedRect(chipRect, baseRadius + pad,
                                    baseRadius + pad);
            p.fillPath(chipPath,
                       QColor(0, 0, 0, chipAlpha));
        }

        // Bloom ring — soft white glow right around the icon on
        // hover. Alpha follows the boost so it fades in smoothly.
        if (boost > 0.05) {
            QPainterPath ringPath;
            QRectF ringRect = chipRect.adjusted(-1, -1, 1, 1);
            ringPath.addRoundedRect(ringRect, baseRadius + pad + 1,
                                    baseRadius + pad + 1);
            QColor bloom(255, 255, 255, int(boost * 90));
            p.setPen(QPen(bloom, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPath(ringPath);
        }

        // App icon on top.
        Icons::drawFor(m_name, p, iconRect, m_theme.textPrimary);

        // Running indicator dot under the icon
        if (m_running || m_active) {
            p.setPen(Qt::NoPen);
            if (m_active) {
                p.setBrush(Qt::white);
                p.drawEllipse(QPoint(width() / 2, height() - 6), 4, 4);
            } else {
                p.setBrush(m_theme.accent);
                p.drawEllipse(QPoint(width() / 2, height() - 6), 3, 3);
            }
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
        // External apps (launched via .desktop file) — delegate to gio
        // so we don't maintain 15 launch paths for apps we didn't write.
        // The .desktop file handles Wayland integration, StartupWMClass,
        // MIME types, etc.
        QString extBase = Icons::externalDesktopFor(m_name);
        if (!extBase.isEmpty()) {
            // Parse Exec= from the .desktop file and run directly.
            // More reliable than `gio launch` which can fail silently
            // on non-executable .desktop files or apps needing PATH
            // resolution (LibreOffice, Epiphany).
            const QString path = "/usr/share/applications/"
                                 + extBase + ".desktop";
            QFile df(path);
            if (!df.open(QIODevice::ReadOnly)) {
                qWarning() << "[launch] cannot open" << path;
                return;
            }
            QTextStream ts(&df);
            QString line, execCmd;
            while (ts.readLineInto(&line)) {
                if (line.startsWith("Exec=")) {
                    execCmd = line.mid(5);
                    break;
                }
            }
            df.close();

            // Strip desktop-entry placeholders (%U %F %u %f %i %c %k)
            execCmd.remove(QRegularExpression(" ?%[UFufick]"));
            execCmd = execCmd.trimmed();
            if (execCmd.isEmpty()) {
                qWarning() << "[launch] empty Exec for" << extBase;
                return;
            }

            QStringList parts = QProcess::splitCommand(execCmd);
            if (parts.isEmpty()) {
                qWarning() << "[launch] failed to parse Exec:" << execCmd;
                return;
            }
            const QString program = parts.takeFirst();
            qInfo() << "[launch]" << extBase << "→" << program << parts;

            // Inject Wayland display env vars. The shell runs under
            // systemd --user and doesn't inherit these from sway, so
            // GUI apps launched from here fall back to X11 and fail
            // with "Can't open display".
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            if (env.value("WAYLAND_DISPLAY").isEmpty()) {
                QString runtime = env.value("XDG_RUNTIME_DIR");
                if (runtime.isEmpty()) runtime = "/run/user/1000";
                QDir rd(runtime);
                for (const QString &f : rd.entryList({"wayland-*"},
                                                     QDir::Files | QDir::System)) {
                    if (f.endsWith(".lock")) continue;
                    env.insert("WAYLAND_DISPLAY", f);
                    break;
                }
            }
            if (env.value("XDG_RUNTIME_DIR").isEmpty())
                env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
            if (env.value("XDG_SESSION_TYPE").isEmpty())
                env.insert("XDG_SESSION_TYPE", "wayland");

            // LibreOffice defaults to X11 backend without SAL_USE_VCLPLUGIN.
            // Force Qt6 VCL so it uses Wayland natively.
            if (env.value("SAL_USE_VCLPLUGIN").isEmpty())
                env.insert("SAL_USE_VCLPLUGIN", "qt6");

            // Per-toolkit theming (see theme_bridge.h)
            applyThemeEnv(env, extBase);
            themeChromiumFlags(program, parts);

            // XWayland fallback: if DISPLAY isn't set, try :0 so any
            // remaining X11 app still works when XWayland is active.
            if (env.value("DISPLAY").isEmpty()) {
                if (QFile::exists("/tmp/.X11-unix/X0"))
                    env.insert("DISPLAY", ":0");
                else if (QFile::exists("/tmp/.X11-unix/X1"))
                    env.insert("DISPLAY", ":1");
            }

            qInfo() << "[launch] env:"
                    << "WAYLAND_DISPLAY=" << env.value("WAYLAND_DISPLAY")
                    << "DISPLAY=" << env.value("DISPLAY")
                    << "SAL_USE_VCLPLUGIN=" << env.value("SAL_USE_VCLPLUGIN");

            QProcess *proc = new QProcess;
            proc->setProgram(program);
            proc->setArguments(parts);
            proc->setProcessEnvironment(env);
            proc->startDetached();
            delete proc;
            return;
        }

        // Clear this app's notification badge when launched
        BadgeRegistry::clear(m_name);
        update();

        // Restore a matching Warp-minimized window instead of relaunching it.
        if (restoreWarpForDock(m_name))
            return;

        // If the app is already running, focus its window instead of
        // spawning a duplicate. Use swaymsg to raise the matching app_id.
        QString matchId;
        if (m_name == "Finder")         matchId = "apokolips-files";
        else if (m_name == "Settings")  matchId = "apokolips-settings";
        // Both Finder and Settings now have their own app_ids
        // (setDesktopFileName in their dispatch blocks). Matching
        // "apokolips-shell" was matching the bar itself — latent
        // bug fixed across Sessions 3a and 3b.
        else if (m_name == "BoomTube")  matchId = "apokolips-boombube";
        else if (m_name == "Aperture")  matchId = "apokolips-aperture";
        else if (m_name == "Node")      matchId = "apokolips-node";
        else if (m_name == "myMach")    matchId = "apokolips-mymach";
        else if (m_name == "Mosaic")    matchId = "apokolips-mosaic";
        else if (m_name == "Notes")     matchId = "apokolips-notes";
        else if (m_name == "Mail")      matchId = "apokolips-mail";
        else if (m_name == "Files")     matchId = "org.gnome.Nautilus";

        if (!matchId.isEmpty()) {
            // SWAYSOCK is NOT inherited by the systemd-launched shell
            // (verified 2026-10-17: /proc/<pid>/environ has no SWAYSOCK).
            // pollRunning() works because it injects SWAYSOCK explicitly;
            // do the same here. Without this, `swaymsg focus` errors out
            // (exit != 0) and every dock click falls through to spawn —
            // which is why "dock click on running app" produced duplicates
            // before 2026-10-17, masked by the (now removed) IPC probe.
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            const QString sock = discoverSwaySock();
            if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);

            // Bring a scratchpad-hidden window back first — "focus" alone
            // on a scratchpad window can report success without actually
            // surfacing it, which looked like "nothing happens" on click.
            QProcess scratch;
            scratch.setProcessEnvironment(env);
            scratch.start("swaymsg",
                { QString("[app_id=%1]").arg(matchId), "scratchpad", "show" });
            scratch.waitForFinished(500);

            QProcess sway;
            sway.setProcessEnvironment(env);
            sway.start("swaymsg",
                { QString("[app_id=%1]").arg(matchId), "focus" });
            sway.waitForFinished(500);
            // If it exited 0, a window was focused — bail out
            if (sway.exitStatus() == QProcess::NormalExit
                && sway.exitCode() == 0) {
                return;
            }
        }

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
        } else if (m_name == "Aperture") {
            program = QCoreApplication::applicationFilePath();
            QString sample = QDir::homePath()
                + "/Pictures/test-gradient.png";
            if (QFile::exists(sample))
                args = { "--image", sample };
            else
                args = { "--image",
                         QDir::homePath() + "/Pictures" };
        } else if (m_name == "myMach") {
            program = QCoreApplication::applicationFilePath();
            args = { "--mymach" };
        } else if (m_name == "Mosaic") {
            program = QCoreApplication::applicationFilePath();
            args = { "--mosaic" };
        } else if (m_name == "BoomTube") {
            program = QCoreApplication::applicationFilePath();
            args = { "--boomtube" };
        } else if (m_name == "Node") {
            program = QCoreApplication::applicationFilePath();
            args = { "--node" };
        } else if (m_name == "Calculator") {
            program = QCoreApplication::applicationFilePath();
            args = { "--calc" };
        } else if (m_name == "Notes") {
            program = QCoreApplication::applicationFilePath();
            args = { "--notes" };
        } else if (m_name == "Mail") {
            program = QCoreApplication::applicationFilePath();
            args = { "--mail" };
        } else {
            program = QCoreApplication::applicationFilePath();
            args = { "--demo", m_name, QString::number(m_index) };
        }

        // Double-click debounce (2026-10-17). If we spawned for
        // this icon <500ms ago, assume the user double-clicked
        // while the first window was still booting, and skip the
        // second spawn. Focus above already tried and failed (no
        // window yet), so returning here is the correct no-op.
        {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastSpawnMs > 0 && now - m_lastSpawnMs < 500) {
                qDebug() << "[dock] suppressing rapid re-spawn for"
                         << m_name << "(+" << (now - m_lastSpawnMs)
                         << "ms)";
                return;
            }
            m_lastSpawnMs = now;
        }

        qDebug() << "[dock] startDetached" << program << args;
        // (--from-dock removed: fresh launches no longer animate,
        //  minimised windows are restored via the reverse-warp path)

        qint64 launchedPid = 0;
        const bool ok = QProcess::startDetached(
            program, args, QString(), &launchedPid);
        setPid(ok ? launchedPid : 0);
        setRunning(ok && launchedPid > 0);
        qDebug() << "[dock] result:" << ok << "pid:" << m_pid;
    }

public:
    // Model A multi-window helpers (Session 1b, 2026-10-17).
    // supportsMultiWindow gates the "New Window" / "Close Window"
    // menu items — extended per session as each app goes multi-window.
    static bool supportsMultiWindow(const QString &name) {
        return name == "Notes"
            || name == "Calculator"
            || name == "Aperture"
            || name == "BoomTube"
            || name == "myMach"
            || name == "Node"
            || name == "Mosaic"
            || name == "Mail"
            || name == "Finder"
            || name == "Settings";
    }

    static QStringList launchArgsFor(const QString &name) {
        if (name == "Notes")      return {"--notes"};
        if (name == "Calculator") return {"--calc"};
        if (name == "Finder")     return {"--files"};
        if (name == "BoomTube")   return {"--boomtube"};
        if (name == "myMach")     return {"--mymach"};
        if (name == "Node")       return {"--node"};
        if (name == "Settings")   return {"--settings"};
        if (name == "Mosaic")     return {"--mosaic"};
        if (name == "Mail")       return {"--mail"};
        if (name == "Aperture") {
            QString sample = QDir::homePath() + "/Pictures/test-gradient.png";
            if (QFile::exists(sample))
                return {"--image", sample};
            return {"--image", QDir::homePath() + "/Pictures"};
        }
        return {};
    }

    QString appIdForThis() const {
        if (m_name == "Notes")      return "apokolips-notes";
        if (m_name == "Calculator") return "apokolips-calculator";
        if (m_name == "BoomTube")   return "apokolips-boombube";
        if (m_name == "Aperture")   return "apokolips-aperture";
        if (m_name == "Node")       return "apokolips-node";
        if (m_name == "myMach")     return "apokolips-mymach";
        if (m_name == "Mosaic")     return "apokolips-mosaic";
        if (m_name == "Mail")       return "apokolips-mail";
        if (m_name == "Finder")     return "apokolips-files";
        if (m_name == "Settings")   return "apokolips-settings";
        return QString();
    }

    void swayRun(const QStringList &args) {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        p.setProcessEnvironment(env);
        p.start("swaymsg", args);
        p.waitForFinished(500);
    }

    // swaymsg [app_id=X] kill only kills the FIRST match, not all.
    // Loop until no more matches (exit != 0).
    void swayKillAll(const QString &appId) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        for (int i = 0; i < 30; ++i) {
            QProcess p;
            p.setProcessEnvironment(env);
            p.start("swaymsg", {QString("[app_id=%1]").arg(appId), "kill"});
            p.waitForFinished(300);
            if (p.exitCode() != 0) break;
        }
    }

    void handleClick(Qt::MouseButton btn, QPoint globalPos) {
        qDebug() << "[dockicon] handleClick" << m_name << "btn=" << btn;
        if (btn == Qt::LeftButton) {
            launchApp();
        } else if (btn == Qt::RightButton) {
            // Build right-click menu (ApoMenu — Category 7, A test site)
            ApoMenu m(this);

            if (m_running) {
                const bool multiWin = supportsMultiWindow(m_name);
                const QString appId = multiWin ? appIdForThis() : QString();

                if (multiWin && !appId.isEmpty()) {
                    QAction *newAct = m.addAction(
                        QString("New %1 Window").arg(m_name));
                    QObject::connect(newAct, &QAction::triggered,
                                     [name = m_name]() {
                        QProcess::startDetached(
                            QCoreApplication::applicationFilePath(),
                            launchArgsFor(name));
                    });

                    QAction *closeAct = m.addAction(
                        QString("Close %1 Window").arg(m_name));
                    QObject::connect(closeAct, &QAction::triggered,
                                     [this, appId]() {
                        swayRun({QString("[app_id=%1]").arg(appId),
                                 "focus"});
                        swayRun({"kill"});
                    });
                    m.addSeparator();
                }

                const QString quitLabel = multiWin
                    ? QString("Quit %1 (all windows)").arg(m_name)
                    : QString("Quit %1").arg(m_name);
                QAction *quitAct = m.addAction(quitLabel);
                QObject::connect(quitAct, &QAction::triggered,
                                 [this, multiWin, appId]() {
                    if (multiWin && !appId.isEmpty())
                        swayKillAll(appId);
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

            // Don't offer to remove Finder / Launchpad / Power /
            // Settings — core shell entries.
            const bool removable =
                m_name != "Finder"
                && m_name != "Launchpad"
                && m_name != "Settings";

            if (removable) {
                m.addSeparator();
                QString id = idForLabel(m_name);
                QAction *removeAct = m.addAction("Remove from Dock");
                QObject::connect(removeAct, &QAction::triggered,
                                 [id]() {
                    if (!id.isEmpty()) setInDock(id, false);
                });
            }

            m.exec(globalPos);
        } else if (btn == Qt::MiddleButton) {
            // Model A middle-click (2026-10-17): quit ALL windows
            // of this app. Uses swayKillAll() (Session 1b) which
            // loops swaymsg [app_id=X] kill until no match remains.
            // Fallback to kill -TERM m_pid for apps without a
            // mapped app_id (e.g. if Mail ever lands in dock).
            if (m_running) {
                const QString appId = appIdForThis();
                if (!appId.isEmpty()) {
                    swayKillAll(appId);
                } else if (m_pid > 0) {
                    QProcess::execute("kill",
                        {"-TERM", QString::number(m_pid)});
                }
                m_running = false;
                m_pid = 0;
                update();
            }
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        handleClick(e->button(), e->globalPosition().toPoint());
        e->accept();
    }

    void __unused_original_mousePress(QMouseEvent *e) {
        if (e->button() == Qt::LeftButton) {
            launchApp();
        } else if (e->button() == Qt::RightButton) {
            const Theme &t = ThemeManager::instance().current();
            ApoMenu m(this);
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
    // 2026-10-17: debounce double-click spawn. Under Model A there
    // is no IPC probe to silence a second launch, so a double-click
    // while the app is still booting would spawn two windows. Track
    // the last spawn time; if a second click lands within 500ms,
    // suppress. Per-icon, so clicking myMach then Notes still works.
    qint64  m_lastSpawnMs = 0;

    // Bounce animation
    QTimer *m_bounceTimer = nullptr;
    int     m_bounceFrames = 0;
    int     m_bounceOffset = 0;
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

        setAttribute(Qt::WA_NoSystemBackground, true);
        setMouseTracking(true);

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(18, 4, 18, 4);
        m_layout->setSpacing(4);

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
            tick();
            checkAutoHide();
        });
        m_timer->start();
        // (dock container swallows clicks — do NOT override mousePressEvent)

        // Separate timer to poll running state — cheaper than every frame
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(200);
        QObject::connect(m_pollTimer, &QTimer::timeout,
                         this, [this]() { pollRunning(); });
        m_pollTimer->start();

        m_opacityEffect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(m_opacityEffect);
        m_fadeAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
        m_fadeAnim->setDuration(250);
        m_fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);
        QObject::connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            if (m_fadeAnim->endValue().toDouble() == 0.0) hide();
        });
    }

    void pollRunning() {
        // Dot state = "is there a window for this app in sway's
        // tree?" Not "is a process alive?" — after a shell reload,
        // the app processes persist even though their windows are
        // gone. Sway's tree is the source of truth for what's on
        // screen, so it drives the dots.
        QProcess sway;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        sway.setProcessEnvironment(env);
        sway.start("swaymsg", { "-t", "get_tree" });
        if (!sway.waitForFinished(400)) {
            static int n = 0;
            if (n++ % 20 == 0)
                qInfo() << "[dots] swaymsg timeout";
            return;
        }

        const QByteArray raw = sway.readAllStandardOutput();
        const QString tree = QString::fromUtf8(raw);
        QSet<QString> liveAppIds;
        QRegularExpression re("\"app_id\"\\s*:\\s*\"([^\"]+)\"");
        auto it = re.globalMatch(tree);
        while (it.hasNext()) liveAppIds.insert(it.next().captured(1));

        QString focusedAppId;
        QString focusedName;
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject()) {
            findFocusedAppInfo(doc.object(), focusedAppId, focusedName);
        }

        static int dbg = 0;
        if (dbg++ % 10 == 0) {
            qInfo() << "[dots] tree bytes=" << raw.size()
                    << " matches=" << liveAppIds.size()
                    << " ids=" << liveAppIds.values()
                    << " icons=" << m_icons.size();
        }

        for (DockIcon *di : m_icons) {
            const QString appId = appIdForDockName(di->appName());
            QStringList candidateIds;
            if (!appId.isEmpty()) candidateIds.append(appId);
            candidateIds.append(Icons::allAppIdsForDockName(di->appName()));

            bool alive = false;
            for (const QString &cid : candidateIds) {
                if (liveAppIds.contains(cid)) { alive = true; break; }
            }
            // Shell chrome apps share app_id=apokolips-shell; also
            // match by title if the dock entry is one of ours.
            if (!alive) {
                const QString title = titleForDockName(di->appName());
                if (!title.isEmpty()) {
                    const QString q = "\"name\"\\s*:\\s*\""
                        + QRegularExpression::escape(title) + "\"";
                    if (QRegularExpression(q).match(tree).hasMatch())
                        alive = true;
                }
            }
            di->setRunning(alive);

            bool active = false;
            if (!focusedAppId.isEmpty()) {
                if (focusedAppId != "apokolips-shell") {
                    QStringList candidateIds = Icons::allAppIdsForDockName(di->appName());
                    if (di->swayId() == focusedAppId || appIdForDockName(di->appName()) == focusedAppId || candidateIds.contains(focusedAppId)) {
                        active = true;
                    }
                } else {
                    QString expectedTitle = titleForDockName(di->appName());
                    if (!expectedTitle.isEmpty() && focusedName.contains(expectedTitle)) {
                        active = true;
                    }
                }
            }
            di->setActive(active);
        }

        // --- Dynamic Dock Injection & Removal ---
        bool dockChanged = false;
        QSet<QString> activeDynamicIds;

        // 1. Remove closed dynamic apps
        for (int i = m_icons.size() - 1; i >= 0; --i) {
            DockIcon *di = m_icons[i];
            if (di->isDynamic()) {
                if (!liveAppIds.contains(di->swayId())) {
                    qInfo() << "[dock] removed dynamic" << di->swayId();
                    m_layout->removeWidget(di);
                    di->deleteLater();
                    m_icons.removeAt(i);
                    dockChanged = true;
                } else {
                    activeDynamicIds.insert(di->swayId());
                }
            }
        }

        // 2. Add newly opened apps
        for (const QString &id : liveAppIds) {
            if (id == "apokolips-shell" || id == "apokolips-animator" || id == "apokolips-dialog" || id == "apokolips-genie" ||
                id == "apokolips-snap" || id == "apokolips-glyph" || id == "apokolips-lock" ||
                id == "apokolips-clipboard" || id == "apokolips-notify") {
                continue; // Skip shell internal overlays
            }
            if (activeDynamicIds.contains(id)) continue;

            // Check if statically in the dock
            bool inDock = false;
            for (DockIcon *di : m_icons) {
                if (!di->isDynamic()) {
                    if (appIdForDockName(di->appName()) == id || Icons::allAppIdsForDockName(di->appName()).contains(id)) {
                        inDock = true;
                        break;
                    }
                }
            }
            if (inDock) continue;

            // Resolve label and icon
            QString label = id;
            QString icon = "\xE2\x9A\x99"; // Gear fallback
            QString lookup = id;
            if (lookup.startsWith("apokolips-")) lookup = lookup.mid(10);

            for (const auto &e : appRegistry()) {
                if (QString::fromUtf8(e.id) == lookup) {
                    label = QString::fromUtf8(e.label);
                    icon = QString::fromUtf8(e.glyph);
                    break;
                }
            }

            qInfo() << "[dock] added dynamic" << id;
            addIcon(icon, label, m_icons.size(), id, true);
            dockChanged = true;
        }

        if (dockChanged) {
            m_layout->activate();
            updateGeometry();
            update();
        }
    }

    static void findFocusedAppInfo(const QJsonObject &node, QString &appId, QString &name) {
        if (node.value("focused").toBool() && (node.value("type").toString() == "con" || node.value("type").toString() == "floating_con")) {
            appId = node.value("app_id").toString();
            name = node.value("name").toString();
            return;
        }
        for (const QJsonValue &v : node.value("nodes").toArray()) {
            findFocusedAppInfo(v.toObject(), appId, name);
            if (!appId.isEmpty() || !name.isEmpty()) return;
        }
        for (const QJsonValue &v : node.value("floating_nodes").toArray()) {
            findFocusedAppInfo(v.toObject(), appId, name);
            if (!appId.isEmpty() || !name.isEmpty()) return;
        }
    }

    // Map a dock app name to its sway app_id.
    static QString appIdForDockName(const QString &name) {
        if (name == "Finder")     return "apokolips-files";
        if (name == "Notes")      return "apokolips-notes";
        if (name == "Calculator") return "apokolips-calculator";
        if (name == "Settings")   return "apokolips-settings";
        if (name == "Aperture")   return "apokolips-aperture";
        if (name == "BoomTube")   return "apokolips-boombube";
        if (name == "myMach")     return "apokolips-mymach";
        if (name == "Node")       return "apokolips-node";
        // Added 2026-10-17 (Session 2b fix): these were missing,
        // so pollRunning() never set m_running for them. Symptom:
        // right-click always showed "Open X" even when the app was
        // running, so the New/Close/Quit-all items never appeared.
        if (name == "Mosaic")     return "apokolips-mosaic";
        if (name == "Mail")       return "apokolips-mail";
        return QString();
    }

    // Fallback title match for apps whose app_id may vary.
    static QString titleForDockName(const QString &name) {
        if (name == "Finder")     return "Files";
        if (name == "Settings")   return "Apokolips Settings";
        return QString();
    }

    void addIcon(const QString &icon, const QString &name, int index,
                 const QString &swayId = QString(), bool isDynamic = false) {
        DockIcon *di = new DockIcon(icon, name, index, swayId, isDynamic, this);
        m_icons.append(di);
        m_layout->addWidget(di);
    }

    // Tear down and rebuild from the registry / config overrides.
    void rebuildIcons() {
        for (DockIcon *di : m_icons) {
            m_layout->removeWidget(di);
            di->deleteLater();
        }
        m_icons.clear();
        int idx = 0;
        for (const auto &e : appRegistry()) {
            if (!isInDock(QString::fromUtf8(e.id))) continue;
            addIcon(QString::fromUtf8(e.glyph),
                    QString::fromUtf8(e.label), idx++);
        }
        m_layout->activate();
        updateGeometry();
        update();
    }

    void setOrientation(bool vertical) {
        if (m_vertical == vertical) return;
        m_vertical = vertical;
        m_layout->setDirection(vertical ? QBoxLayout::TopToBottom
                                        : QBoxLayout::LeftToRight);
        if (vertical) {
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            setFixedWidth(96);
        } else {
            setMinimumWidth(0);
            setMaximumWidth(QWIDGETSIZE_MAX);
            setFixedHeight(96);
        }
        updateGeometry();
        update();
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

        // Inner highlight — one faint stroke inside the dock so the
        // panel reads as a distinct surface, not just a blurry blob.
        // Radius 18 nests inside the outer 20.
        p.save();
        p.setClipPath(path);
        p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect().adjusted(2, 2, -2, -2)), 18, 18);
        p.restore();
    }

    void mousePressEvent(QMouseEvent *e) override {
        qDebug() << "[dock] press at" << e->position()
                 << "btn" << e->button();
        for (QWidget *child : findChildren<QWidget *>()) {
            if (auto *di = dynamic_cast<DockIcon *>(child)) {
                QPoint local = di->mapFrom(this, e->position().toPoint());
                if (di->rect().contains(local)) {
                    di->handleClick(e->button(),
                                    e->globalPosition().toPoint());
                    e->accept();
                    return;
                }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        m_cursorX  = e->position().x();
        m_cursorY  = e->position().y();
        m_hasCursor = true;
        if (!m_debugOnce) {
            qDebug() << "[dock] first mouseMove at x=" << m_cursorX
                     << "y=" << e->position().y();
            m_debugOnce = true;
        }

        // ---- Preview popup hover detection ----
        DockIcon *hitIcon = nullptr;
        const QPoint pos = e->position().toPoint();
        for (DockIcon *di : m_icons) {
            if (di->geometry().contains(pos)) { hitIcon = di; break; }
        }
        const QString hitName = hitIcon ? hitIcon->appName() : QString();
        if (hitName != m_hoveredAppName) {
            m_hoveredAppName = hitName;

            if (!m_previewDelay) {
                m_previewDelay = new QTimer(this);
                m_previewDelay->setSingleShot(true);
                QObject::connect(m_previewDelay, &QTimer::timeout,
                                 this, [this]() {
                    if (m_hoveredAppName.isEmpty()) return;
                    DockIcon *di = nullptr;
                    for (DockIcon *ic : m_icons) {
                        if (ic->appName() == m_hoveredAppName) {
                            di = ic; break;
                        }
                    }
                    if (!di || !di->isRunning()) return;
                    if (m_previewPopup) {
                        m_previewPopup->close();
                        m_previewPopup = nullptr;
                    }
                    const QString appId = appIdForDockName(
                        m_hoveredAppName);
                    const QString titleMatch = titleForDockName(
                        m_hoveredAppName);
                    m_previewPopup = new DockPreviewPopup(
                        appId, titleMatch, di, window());
                });
            }
            m_previewDelay->stop();
            if (m_previewPopup) {
                m_previewPopup->close();
                m_previewPopup = nullptr;
            }
            if (!hitName.isEmpty()) m_previewDelay->start(500);
        }
    }
    void leaveEvent(QEvent *) override {
        m_hasCursor = false;
        m_hoveredAppName.clear();
        if (m_previewDelay) m_previewDelay->stop();
        if (m_previewPopup) m_previewPopup->scheduleClose(300);
    }

private:
    QString                     m_hoveredAppName;
    QTimer                     *m_previewDelay = nullptr;
    QPointer<DockPreviewPopup>  m_previewPopup;

    void tick() {
        const auto &vc = VisualConfigManager::instance().cfg();
        const qreal sigma    = vc.dockSigma;
        const qreal maxBoost = vc.dockMagnifyMax;
        // Axis depends on dock orientation — X for horizontal,
        // Y for vertical. Same magnify math, different coordinate.
        qreal cursor = m_hasCursor
            ? (m_vertical ? m_cursorY : m_cursorX)
            : -10000.0;

        bool anyMoving = false;
        for (DockIcon *di : m_icons) {
            qreal cx = m_vertical
                ? di->geometry().center().y()
                : di->geometry().center().x();
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

    void checkAutoHide() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.dockVisible) {
            if (isVisible()) hide();
            return;
        }
        if (!cfg.dockAutoHide) {
            if (!isVisible() || m_opacityEffect->opacity() < 1.0) {
                show();
                m_fadeAnim->stop();
                m_fadeAnim->setStartValue(m_opacityEffect->opacity());
                m_fadeAnim->setEndValue(1.0);
                m_fadeAnim->start();
            }
            return;
        }

        const QPoint gp = QCursor::pos();
        QScreen *scr = QApplication::screenAt(gp);
        if (!scr) scr = QApplication::primaryScreen();
        const QRect scrRect = scr ? scr->geometry() : QRect(0, 0, 1, 1);

        // 8px reveal zone at the screen edge where the dock lives
        const int EDGE = 8;
        bool nearEdge = false;
        const QPoint dockGlobal = mapToGlobal(QPoint(0, 0));
        if (m_vertical) {
            if (dockGlobal.x() < scrRect.center().x())
                nearEdge = gp.x() < scrRect.left() + EDGE;
            else
                nearEdge = gp.x() > scrRect.right() - EDGE;
        } else {
            nearEdge = gp.y() > scrRect.bottom() - EDGE;
        }

        if (!isVisible()) {
            if (nearEdge) { 
                show(); 
                raise(); 
                m_lastHoverMs = 0; 
                m_fadeAnim->stop();
                m_fadeAnim->setStartValue(m_opacityEffect->opacity());
                m_fadeAnim->setEndValue(1.0);
                m_fadeAnim->start();
            }
            return;
        }

        const QRect dockRect(dockGlobal, size());
        const bool inDock = dockRect.contains(gp);

        if (inDock || nearEdge) {
            m_lastHoverMs = 0;
            if (m_opacityEffect->opacity() < 1.0 && m_fadeAnim->endValue().toDouble() != 1.0) {
                m_fadeAnim->stop();
                m_fadeAnim->setStartValue(m_opacityEffect->opacity());
                m_fadeAnim->setEndValue(1.0);
                m_fadeAnim->start();
            }
        } else {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastHoverMs == 0) m_lastHoverMs = now;
            if (now - m_lastHoverMs >= cfg.dockAutoHideDelayMs) {
                if (m_opacityEffect->opacity() > 0.0 && m_fadeAnim->endValue().toDouble() != 0.0) {
                    m_fadeAnim->stop();
                    m_fadeAnim->setStartValue(m_opacityEffect->opacity());
                    m_fadeAnim->setEndValue(0.0);
                    m_fadeAnim->start();
                }
                m_lastHoverMs = 0;
            }
        }
    }

    QList<DockIcon *> m_icons;
    QBoxLayout  *m_layout = nullptr;
    bool         m_vertical = false;
    QTimer *m_timer = nullptr;
    QTimer *m_pollTimer = nullptr;
    QGraphicsOpacityEffect *m_opacityEffect = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
    qint64 m_lastHoverMs = 0;
    qreal m_cursorX = 0;
    qreal m_cursorY = 0;
    bool  m_hasCursor = false;
    bool  m_debugOnce = false;
    Theme m_theme;
};

class HUDWidget : public QWidget {
public:
    explicit HUDWidget(QWidget *parent = nullptr) : QWidget(parent) {
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(4);
    }
    void setActions(const QList<QPair<QString, std::function<void()>>> &actions) {
        QLayoutItem *item;
        while ((item = m_layout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        for (const auto &act : actions) {
            QPushButton *b = new QPushButton(act.first);
            b->setCursor(Qt::PointingHandCursor);
            b->setFlat(true);
            b->setStyleSheet(
                "QPushButton { color: rgba(255,255,255,220); background: rgba(0,0,0,60);"
                "  border: 1px solid rgba(255,255,255,30); padding: 2px 10px;"
                "  font-size: 12px; border-radius: 6px; }"
                "QPushButton:hover { background: rgba(255,255,255,60); color: #fff; }");
            QObject::connect(b, &QPushButton::clicked, act.second);
            m_layout->addWidget(b);
        }
    }
private:
    QHBoxLayout *m_layout;
};

class TopBar : public QWidget {
public:
    explicit TopBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(VisualConfigManager::instance().cfg().topBarHeight);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setCursor(Qt::OpenHandCursor);
    }
    void setBrandColor(const QColor &c) {
        m_brandColor = c;
        update();
    }
    bool isMouseOver() const { return m_hovered; }

    void refreshFromConfig() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        setFixedHeight(cfg.topBarHeight);
        if (auto *w = findChild<QWidget*>("topBarClock")) w->setVisible(cfg.topBarShowClock);
        if (auto *w = findChild<QWidget*>("topBarStats")) w->setVisible(cfg.topBarShowStats);
        if (auto *w = findChild<QWidget*>("topBarBell"))  w->setVisible(cfg.topBarShowBell);
        if (auto *w = findChild<QWidget*>("topBarBrand")) w->setVisible(cfg.topBarShowBrand);
        if (auto *w = findChild<QWidget*>("topBarOmega")) w->setVisible(cfg.topBarShowOmega);
        update();
    }

protected:
    void enterEvent(QEnterEvent *e) override {
        m_hovered = true;
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override {
        m_hovered = false;
        QWidget::leaveEvent(e);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // No app focused → leave the bar transparent so the
        // wallpaper shows through, exactly like before HUD.
        if (!m_brandColor.isValid()) return;
        p.fillRect(rect(), m_brandColor);
        p.setPen(QColor(255, 255, 255, 30));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }
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
    QColor m_brandColor;
    bool m_hovered = false;
};

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
    void showCC() {
        refreshFocusBtn();
        applyVisibility();
        raise(); show(); m_target = 1.0; m_animTimer->start();
    }
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
            if (label == "Focus") {
                m_focusBtn = b;
                m_focusLbl = lb;
            }
            return b;
        };

        h->addWidget(mk(QString::fromUtf8("\xE2\x9A\xA1"), "Battery"));
        h->addWidget(mk(QString::fromUtf8("\xE2\x98\xBE"), "Focus"));
        h->addWidget(mk(QString::fromUtf8("\xF0\x9F\x94\x94"), "Alerts"));
        h->addWidget(mk(QString::fromUtf8("\xF0\x9F\x96\xA5"), "Display"));

        if (m_focusBtn) {
            QObject::connect(m_focusBtn, &QPushButton::clicked, [this]() {
                auto &vc = VisualConfigManager::instance().cfg();
                bool nowOn = !vc.dndEnabled;
                VisualConfigManager::instance().setAndSave(
                    [nowOn](VisualConfig &c) { c.dndEnabled = nowOn; });
                refreshFocusBtn();
            });
        }

        return row;
    }

    void refreshFocusBtn() {
        if (!m_focusBtn) return;
        bool on = VisualConfigManager::instance().cfg().dndEnabled;
        QColor bg = on ? m_theme.accent
                       : m_theme.chromeBg.lighter(130);
        m_focusBtn->setStyleSheet(QString(
            "QPushButton { background: %1; border: none;"
            "  border-radius: 14px; color: %2;"
            "  font-size: 10px; padding-top: 4px; }"
            "QPushButton:hover { background: %3; }")
            .arg(rgba(bg),
                 on ? "#ffffff" : m_theme.textPrimary.name(),
                 rgba(on ? m_theme.accent.lighter(120)
                         : m_theme.accentSoft)));
        if (m_focusLbl) {
            m_focusLbl->setText(on ? "Do Not Disturb" : "Focus");
            m_focusLbl->setStyleSheet(QString(
                "font-size: 10px; color: %1; background: transparent;")
                .arg(on ? "#ffffff" : m_theme.textPrimary.name()));
        }
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

        m_wiFiCard    = makeCard("WI-FI", makeWiFiRow());
        m_btCard      = makeCard("BLUETOOTH", makeBTRow());
        m_mediaCard   = makeCard("NOW PLAYING", makeMediaRow());
        m_togglesCard = makeTogglesRow();
        m_soundCard   = makeCard("SOUND",
            makeSliderRow("Output", SysControl::audioGet(),
                [](int v) { SysControl::audioSet(v); }));
        m_displayCard = makeCard("DISPLAY",
            makeSliderRow("Brightness", SysControl::brightnessGet(),
                [](int v) { SysControl::brightnessSet(v); }));

        outer->addWidget(m_wiFiCard);
        outer->addWidget(m_btCard);
        outer->addWidget(m_mediaCard);
        outer->addWidget(m_togglesCard);
        outer->addWidget(m_soundCard);
        outer->addWidget(m_displayCard);
        outer->addStretch();

        applyVisibility();
    }

    void applyVisibility() {
        auto &c = VisualConfigManager::instance().cfg();
        if (m_wiFiCard)    m_wiFiCard->setVisible(c.ccShowWiFi);
        if (m_btCard)      m_btCard->setVisible(c.ccShowBluetooth);
        if (m_mediaCard)   m_mediaCard->setVisible(c.ccShowMedia);
        if (m_togglesCard) m_togglesCard->setVisible(c.ccShowToggles);
        if (m_soundCard)   m_soundCard->setVisible(c.ccShowSound);
        if (m_displayCard) m_displayCard->setVisible(c.ccShowDisplay);
    }

    QTimer *m_animTimer = nullptr;
    qreal m_slide = 0.0;
    qreal m_target = 0.0;
    Theme m_theme;
    QPushButton *m_focusBtn  = nullptr;
    QLabel      *m_focusLbl  = nullptr;
    QWidget     *m_wiFiCard    = nullptr;
    QWidget     *m_btCard      = nullptr;
    QWidget     *m_mediaCard   = nullptr;
    QWidget     *m_togglesCard = nullptr;
    QWidget     *m_soundCard   = nullptr;
    QWidget     *m_displayCard = nullptr;
};

class ClipList : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<QMimeData*(QListWidgetItem*)> mimeFor;
protected:
    QMimeData *mimeData(const QList<QListWidgetItem*> &items)
        const override {
        if (items.isEmpty() || !mimeFor) return nullptr;
        return mimeFor(items.first());
    }
};

class ClipboardPanel : public QWidget {
public:
    ClipboardPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFixedSize(380, 480);
        hide();

        m_storeDir = QDir::homePath()
                     + "/.local/share/apokolips/clipboard";
        QDir().mkpath(m_storeDir);
        m_storeFile = m_storeDir + "/history.json";

        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(14, 12, 14, 14);
        outer->setSpacing(10);

        // ---- Header: traffic lights + title ----
        QHBoxLayout *hdr = new QHBoxLayout;
        hdr->setContentsMargins(0, 0, 0, 0);
        hdr->setSpacing(6);
        hdr->addWidget(makeDot("#7a2b25", "#ff5f57",
                               QString::fromUtf8("\xC3\x97"),
                               [this]() { hidePanel(); }));
        hdr->addWidget(makeDot("#7a5b18", "#febc2e",
                               QString::fromUtf8("\xE2\x88\x92"),
                               [this]() { animateMinimize(); }));
        hdr->addWidget(makeDot("#155c1e", "#28c840", "+", nullptr));
        hdr->addSpacing(10);
        QLabel *title = new QLabel("Clipboard");
        title->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 14px; font-weight: 600;"
            "background: transparent;");
        hdr->addWidget(title);
        hdr->addStretch();
        outer->addLayout(hdr);

        // ---- Search ----
        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search clipboard\xE2\x80\xA6");
        m_search->setFixedHeight(30);
        m_search->setStyleSheet(
            "QLineEdit {"
            "  background: rgba(255,255,255,20);"
            "  border: none;"
            "  border-radius: 8px;"
            "  color: rgb(240, 237, 232);"
            "  font-size: 12px;"
            "  padding: 0 10px;"
            "}"
            "QLineEdit:focus { background: rgba(255,255,255,35); }");
        outer->addWidget(m_search);
        QObject::connect(m_search, &QLineEdit::textChanged,
                         [this](const QString &s) { applyFilter(s); });

        // ---- List ----
        m_list = new ClipList;
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->setDragEnabled(true);
        m_list->setDragDropMode(QAbstractItemView::DragOnly);
        m_list->setStyleSheet(
            "QListWidget {"
            "  background: rgba(255,255,255,12);"
            "  border-radius: 10px;"
            "  color: rgb(240, 237, 232);"
            "  font-size: 12px;"
            "  padding: 4px;"
            "}"
            "QListWidget::item { padding: 2px; border-radius: 8px; }"
            "QListWidget::item:hover { background: rgba(255,255,255,25); }"
            "QListWidget::item:selected {"
            "  background: rgba(168, 50, 80, 100);"
            "}");
        m_list->mimeFor = [this](QListWidgetItem *it) -> QMimeData* {
            if (!it) return nullptr;
            Entry e = entryAt(it);
            QMimeData *m = new QMimeData;
            if ((e.type == "image" || e.type == "file")
                && !e.path.isEmpty()) {
                m->setUrls({ QUrl::fromLocalFile(e.path) });
                m->setData(ApoDrag::kMimeFile, e.path.toUtf8());
            } else if (!e.text.isEmpty()) {
                m->setText(e.text);
            } else {
                delete m; return nullptr;
            }
            return m;
        };
        outer->addWidget(m_list, 1);

        QObject::connect(m_list, &QListWidget::itemClicked,
                         this, [this](QListWidgetItem *it) {
            if (it) copyEntryToClipboard(entryAt(it));
        });

        m_search->installEventFilter(this);

        // ---- Poll ----
        m_poll = new QTimer(this);
        m_poll->setInterval(700);
        QObject::connect(m_poll, &QTimer::timeout,
                         this, &ClipboardPanel::pollClipboard);
        m_poll->start();

        loadStore();
        rebuildList();
        pollClipboard();
    }

    void toggle() { isVisible() ? hidePanel() : showPanel(); }

    void showPanel() {
        if (!m_positioned) {
            QWidget *top = window();
            if (top) {
                QPoint c = top->mapToGlobal(QPoint(
                    (top->width() - width()) / 2,
                    (top->height() - height()) / 2));
                move(c);
            }
            m_positioned = true;
        }
        raise();
        show();
        m_search->clear();
        m_search->setFocus();
        rebuildList();
    }

    void hidePanel() { hide(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().adjusted(0, 0, -1, -1);

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
        if (!bg.isNull() && vc.blurStrength > 0.001 && window()) {
            p.save();
            p.setClipPath(path);
            QPoint pos = mapTo(window(), QPoint(0, 0));
            p.setOpacity(vc.blurStrength);
            p.drawPixmap(r, bg, QRect(pos, size()));
            p.restore();
        }

        p.fillPath(path, QColor(24, 22, 28, 235));
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            hidePanel(); e->accept(); return;
        }
        if (e->key() == Qt::Key_Down) {
            if (m_list->count() > 0) {
                m_list->setCurrentRow(0);
                m_list->setFocus();
            }
            e->accept(); return;
        }
        QWidget::keyPressEvent(e);
    }

    bool eventFilter(QObject *o, QEvent *ev) override {
        if (o == m_search && ev->type() == QEvent::KeyPress) {
            QKeyEvent *k = static_cast<QKeyEvent *>(ev);
            if (k->key() == Qt::Key_Return
                || k->key() == Qt::Key_Enter) {
                QListWidgetItem *it = m_list->currentItem();
                if (!it && m_list->count() > 0)
                    it = m_list->item(0);
                if (it) {
                    copyEntryToClipboard(entryAt(it));
                    return true;
                }
            }
            if (k->key() == Qt::Key_Down) {
                m_list->setFocus();
                if (m_list->count() > 0)
                    m_list->setCurrentRow(0);
                return true;
            }
            if (k->key() == Qt::Key_Escape) {
                hidePanel(); return true;
            }
        }
        return QWidget::eventFilter(o, ev);
    }

private:
    struct Entry {
        QString type;   // text | link | image | file
        QString text;   // content for text/link
        QString path;   // local file for image/file
        QString preview;
        qint64  ts = 0;
        bool    pinned = false;
    };

    QList<Entry>  m_entries;
    QString       m_storeDir;
    QString       m_storeFile;
    ClipList     *m_list = nullptr;
    QLineEdit    *m_search = nullptr;
    QTimer       *m_poll = nullptr;
    QString       m_lastSig;

    QPushButton *makeDot(const QString &idle,
                         const QString &accent,
                         const QString &glyph,
                         std::function<void()> onClick) {
        QPushButton *b = new QPushButton(glyph, this);
        b->setFixedSize(12, 12);
        b->setCursor(onClick ? Qt::PointingHandCursor
                             : Qt::ArrowCursor);
        b->setStyleSheet(QString(
            "QPushButton {"
            "  background: %1;"
            "  border: none;"
            "  border-radius: 6px;"
            "  color: transparent;"
            "  font-size: 9px; font-weight: 900;"
            "  padding: 0;"
            "}"
            "QPushButton:hover { background: %2;"
            "  color: rgba(60,25,25,220); }")
            .arg(idle, accent));
        if (onClick)
            QObject::connect(b, &QPushButton::clicked, this, onClick);
        else
            b->setEnabled(false);
        return b;
    }

    void loadStore() {
        QFile f(m_storeFile);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!d.isArray()) return;
        for (const QJsonValue &v : d.array()) {
            QJsonObject o = v.toObject();
            Entry e;
            e.type    = o.value("type").toString("text");
            e.text    = o.value("text").toString();
            e.path    = o.value("path").toString();
            e.preview = o.value("preview").toString();
            e.ts      = (qint64)o.value("ts").toDouble();
            e.pinned  = o.value("pinned").toBool(false);
            if (!e.path.isEmpty() && !QFile::exists(e.path))
                continue;
            m_entries.append(e);
        }
    }

    void saveStore() {
        QJsonArray arr;
        for (const Entry &e : m_entries) {
            QJsonObject o;
            o["type"]    = e.type;
            o["text"]    = e.text;
            o["path"]    = e.path;
            o["preview"] = e.preview;
            o["ts"]      = double(e.ts);
            o["pinned"]  = e.pinned;
            arr.append(o);
        }
        QFile f(m_storeFile);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(arr).toJson());
    }

    void rebuildList() {
        m_list->clear();
        QList<Entry> pinned, rest;
        for (const Entry &e : m_entries)
            (e.pinned ? pinned : rest).append(e);
        std::sort(rest.begin(), rest.end(),
                  [](const Entry &a, const Entry &b) {
                      return a.ts > b.ts;
                  });
        QList<Entry> ordered = pinned + rest;
        for (const Entry &e : ordered) addRow(e);
        applyFilter(m_search->text());
    }

    void addRow(const Entry &e) {
        QListWidgetItem *it = new QListWidgetItem(m_list);
        it->setSizeHint(QSize(0, 44));
        it->setData(Qt::UserRole,     e.type);
        it->setData(Qt::UserRole + 1, e.text);
        it->setData(Qt::UserRole + 2, e.path);
        it->setData(Qt::UserRole + 3, e.preview);
        it->setData(Qt::UserRole + 4, e.pinned);
        it->setData(Qt::UserRole + 5, e.ts);

        QWidget *row = new QWidget;
        row->setAttribute(Qt::WA_TranslucentBackground, true);
        QHBoxLayout *h = new QHBoxLayout(row);
        h->setContentsMargins(8, 4, 6, 4);
        h->setSpacing(8);

        QLabel *chip = new QLabel(typeLetter(e.type));
        chip->setFixedSize(34, 20);
        chip->setAlignment(Qt::AlignCenter);
        chip->setStyleSheet(
            "QLabel {"
            "  background: rgba(168, 50, 80, 190);"
            "  color: #ffffff;"
            "  border-radius: 5px;"
            "  font-size: 9px; font-weight: 700;"
            "}");
        h->addWidget(chip);

        QLabel *prev = new QLabel(e.preview);
        prev->setStyleSheet(
            "QLabel { color: rgb(240, 237, 232);"
            "  background: transparent; font-size: 12px; }");
        prev->setSizePolicy(QSizePolicy::Expanding,
                            QSizePolicy::Preferred);
        h->addWidget(prev, 1);

        QPushButton *pin = new QPushButton(
            e.pinned
            ? QString::fromUtf8("\xE2\x98\x85")
            : QString::fromUtf8("\xE2\x98\x86"));
        pin->setFixedSize(24, 24);
        pin->setCursor(Qt::PointingHandCursor);
        pin->setFlat(true);
        pin->setStyleSheet(
            "QPushButton {"
            "  background: transparent; border: none;"
            "  color: rgb(200, 197, 192); font-size: 14px;"
            "}"
            "QPushButton:hover {"
            "  background: rgba(255,255,255,20);"
            "  border-radius: 6px; }");
        QString type = e.type, path = e.path, text = e.text;
        qint64 ts = e.ts;
        QObject::connect(pin, &QPushButton::clicked,
                         [this, type, path, text, ts]() {
            for (auto &x : m_entries) {
                if (x.type == type && x.path == path
                    && x.text == text && x.ts == ts) {
                    x.pinned = !x.pinned;
                    break;
                }
            }
            saveStore();
            rebuildList();
        });
        h->addWidget(pin);

        m_list->setItemWidget(it, row);
    }

    QString typeLetter(const QString &t) const {
        if (t == "image") return "IMG";
        if (t == "file")  return "FILE";
        if (t == "link")  return "LINK";
        return "TXT";
    }

    Entry entryAt(QListWidgetItem *it) const {
        Entry e;
        if (!it) return e;
        e.type    = it->data(Qt::UserRole).toString();
        e.text    = it->data(Qt::UserRole + 1).toString();
        e.path    = it->data(Qt::UserRole + 2).toString();
        e.preview = it->data(Qt::UserRole + 3).toString();
        e.pinned  = it->data(Qt::UserRole + 4).toBool();
        e.ts      = it->data(Qt::UserRole + 5).toLongLong();
        return e;
    }

    void applyFilter(const QString &s) {
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            QString prev = it->data(Qt::UserRole + 3).toString();
            QString txt  = it->data(Qt::UserRole + 1).toString();
            QString path = it->data(Qt::UserRole + 2).toString();
            bool keep = s.isEmpty()
                || prev.contains(s, Qt::CaseInsensitive)
                || txt.contains(s, Qt::CaseInsensitive)
                || path.contains(s, Qt::CaseInsensitive);
            it->setHidden(!keep);
        }
    }

    void copyEntryToClipboard(const Entry &e) {
        if (e.type == "image" && !e.path.isEmpty()) {
            QProcess::startDetached("bash", { "-c",
                QString("wl-copy -t image/png < %1")
                    .arg(e.path) });
        } else if (e.type == "file" && !e.path.isEmpty()) {
            QProcess::startDetached("wl-copy",
                { "-t", "text/uri-list",
                  "file://" + e.path });
        } else {
            QProcess::startDetached("wl-copy", { e.text });
        }
        hidePanel();
    }

    QByteArray wlPaste(const QString &mimetype) {
        QProcess p;
        if (mimetype.isEmpty())
            p.start("wl-paste", { "-n" });
        else
            p.start("wl-paste", { "-t", mimetype });
        p.waitForFinished(400);
        return p.readAllStandardOutput();
    }

    void pollClipboard() {
        Entry e;
        bool filled = false;

        // 1) image
        QByteArray img = wlPaste("image/png");
        if (!img.isEmpty()) {
            QByteArray hash = QCryptographicHash::hash(
                img, QCryptographicHash::Md5);
            QString sig = "img:" + hash.toHex();
            if (sig == m_lastSig) return;
            m_lastSig = sig;
            QString fname = m_storeDir + "/"
                + QString::number(
                    QDateTime::currentMSecsSinceEpoch())
                + ".png";
            QFile f(fname);
            if (!f.open(QIODevice::WriteOnly)) return;
            f.write(img);
            f.close();
            e.type    = "image";
            e.path    = fname;
            e.preview = "Image  \xC2\xB7  "
                + QString::number(img.size() / 1024) + " KB";
            filled = true;
        }

        // 2) uri-list (files)
        if (!filled) {
            QByteArray uri = wlPaste("text/uri-list");
            if (!uri.isEmpty()) {
                QString raw = QString::fromUtf8(uri);
                QString first = raw.split(
                    QRegularExpression("[\\r\\n]"),
                    Qt::SkipEmptyParts).value(0);
                if (first.startsWith("file://")) {
                    QString path = QUrl(first).toLocalFile();
                    if (!path.isEmpty()
                        && QFile::exists(path)) {
                        QString sig = "file:" + path;
                        if (sig == m_lastSig) return;
                        m_lastSig = sig;
                        e.type = "file";
                        e.path = path;
                        e.preview = QFileInfo(path).fileName();
                        filled = true;
                    }
                }
            }
        }

        // 3) text
        if (!filled) {
            QByteArray raw = wlPaste("");
            if (raw.isEmpty()) return;
            QString text = QString::fromUtf8(raw);
            QString sig = "text:" + text.left(200);
            if (sig == m_lastSig) return;
            m_lastSig = sig;
            QString preview = text;
            preview.replace("\n", " \xE2\x8F\x8E ");
            if (preview.length() > 80)
                preview = preview.left(77)
                          + QString::fromUtf8("\xE2\x80\xA6");
            bool isLink = text.startsWith("http://")
                       || text.startsWith("https://");
            e.type    = isLink ? "link" : "text";
            e.text    = text;
            e.preview = preview;
        }

        e.ts = QDateTime::currentMSecsSinceEpoch();

        for (const Entry &x : m_entries) {
            if (x.type == e.type && x.text == e.text
                && x.path == e.path) return;
        }

        m_entries.prepend(e);
        while (m_entries.size() > 100) {
            Entry old = m_entries.takeLast();
            if (old.type == "image" && !old.path.isEmpty()
                && old.path.startsWith(m_storeDir)) {
                QFile::remove(old.path);
            }
        }
        saveStore();
        if (isVisible()) rebuildList();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton
            && e->pos().y() < m_headerHeight) {
            m_dragging = true;
            m_dragOffset = e->globalPos()
                           - mapToGlobal(QPoint(0, 0));
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging) {
            QPoint newTL = e->globalPos() - m_dragOffset;
            if (QWidget *par = parentWidget())
                newTL = par->mapFromGlobal(newTL);
            move(newTL);
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_dragging) {
            m_dragging = false;
            setCursor(Qt::ArrowCursor);
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

    void animateMinimize() {
        // Capture what's on screen at the panel's location so the
        // overlay animates the real pixels, not a transparent frame.
        QString png = QDir::tempPath() + "/apokolips-clip-panel-"
                      + QString::number(
                            QDateTime::currentMSecsSinceEpoch())
                      + ".png";
        QPoint gp = mapToGlobal(QPoint(0, 0));
        QProcess grim;
        grim.start("grim", { "-g",
            QString("%1,%2 %3x%4")
                .arg(gp.x()).arg(gp.y())
                .arg(width()).arg(height()),
            png });
        grim.waitForFinished(600);

        // Read the user's chosen minimize style from visual.json.
        // Accept every style the shell accepts — warp, scale, suck,
        // thanos. Keep in sync with visualconfig.h validation.
        QString style = "warp";
        {
            QFile f(QDir::homePath()
                    + "/.config/apokolips/visual.json");
            if (f.open(QIODevice::ReadOnly)) {
                QJsonObject o = QJsonDocument::fromJson(
                    f.readAll()).object();
                QString s = o.value("minimizeStyle").toString();
                if (s == "scale" || s == "suck"
                    || s == "warp" || s == "thanos")
                    style = s;
            }
        }

        if (!QFile::exists(png)) { hidePanel(); return; }

        QScreen *scr = screen();
        int tx = scr ? scr->geometry().width() / 2 : 960;
        int ty = 0;   // top of screen

        const QString readyName =
            "apokolips-clip-ready-"
            + QString::number(QCoreApplication::applicationPid())
            + "-"
            + QString::number(
                QDateTime::currentMSecsSinceEpoch() % 100000);

        QLocalServer::removeServer(readyName);
        // Parent to `this` so the server dies with the panel if
        // nothing else cleans it up.
        QLocalServer *readyServer = new QLocalServer(this);
        readyServer->listen(readyName);

        // Guard: safe to test after deleteLater() has run.
        QPointer<QLocalServer> srvGuard(readyServer);
        // Idempotency flag shared by the two completion paths.
        QSharedPointer<bool> done = QSharedPointer<bool>::create(false);

        auto finishOnce = [this, srvGuard, png, done]() {
            if (*done) return;
            *done = true;
            if (srvGuard) {
                srvGuard->close();
                srvGuard->deleteLater();
            }
            hidePanel();
            QFile::remove(png);
        };

        QObject::connect(readyServer, &QLocalServer::newConnection,
                         this, [readyServer, finishOnce]() {
            QLocalSocket *conn =
                readyServer->nextPendingConnection();
            if (conn) {
                conn->readAll();
                conn->deleteLater();
            }
            finishOnce();
        });

        QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            { "--" + style, png,
              QString::number(gp.x()),
              QString::number(gp.y()),
              QString::number(width()),
              QString::number(height()),
              QString::number(tx),
              QString::number(ty),
              "--ms", "500",
              "--ready-sock", readyName });

        // Safety net: if the overlay never pings (crash, missing
        // style binary, etc.), hide anyway after 1 s.
        QTimer::singleShot(1000, this, [finishOnce]() { finishOnce(); });
    }

private:
    int           m_headerHeight = 40;
    bool          m_dragging = false;
    QPoint        m_dragOffset;
    bool          m_positioned = false;
};

// ---- RenameDialog: macOS-style rename sheet for desktop files/folders ----
class RenameDialog : public QDialog {
public:
    // Returns the new name, or an empty string if cancelled.
    static QString ask(const QString &dir, const QString &current,
                       bool isFolder)
    {
        RenameDialog d(dir, current, isFolder);
        return d.exec() == QDialog::Accepted
                   ? d.m_edit->text().trimmed() : QString();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        p.fillPath(path, QColor(30, 28, 36, 246));
        p.setPen(QPen(QColor(255, 255, 255, 36), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = windowHandle())
                if (wh->startSystemMove()) { e->accept(); return; }
        }
        QDialog::mousePressEvent(e);
    }
    void showEvent(QShowEvent *e) override {
        QDialog::showEvent(e);
        QTimer::singleShot(0, this, [this]() {
            m_edit->setFocus();
            m_edit->setSelection(0, m_selLen);
        });
    }
    void accept() override {
        if (m_ok->isEnabled()) QDialog::accept();
    }

private:
    RenameDialog(const QString &dir, const QString &current, bool isFolder)
        : QDialog(nullptr, Qt::Dialog | Qt::FramelessWindowHint),
          m_dir(dir), m_current(current)
    {
        setWindowTitle("Rename");   // the sway float rule matches this title
        setAttribute(Qt::WA_TranslucentBackground, true);
        setModal(true);
        setFixedSize(380, 272);

        const int dot = current.lastIndexOf('.');
        m_selLen = (!isFolder && dot > 0) ? dot : int(current.size());

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(26, 24, 26, 22);
        root->setSpacing(0);

        auto *ico = new QLabel;
        ico->setPixmap(glyph(isFolder));
        ico->setAlignment(Qt::AlignHCenter);
        ico->setStyleSheet("background: transparent;");
        root->addWidget(ico);
        root->addSpacing(10);

        const QString shown = QFontMetrics(font()).elidedText(
            current, Qt::ElideMiddle, 280);
        auto *title = new QLabel(QString("Rename \u201C%1\u201D").arg(shown));
        title->setAlignment(Qt::AlignHCenter);
        title->setStyleSheet("color: rgb(240,237,232); font-size: 14px;"
                             " font-weight: 600; background: transparent;");
        root->addWidget(title);
        root->addSpacing(4);

        auto *sub = new QLabel(isFolder ? "Enter a new name for this folder."
                                        : "Enter a new name for this file.");
        sub->setAlignment(Qt::AlignHCenter);
        sub->setStyleSheet("color: rgba(240,237,232,150); font-size: 12px;"
                           " background: transparent;");
        root->addWidget(sub);
        root->addSpacing(14);

        m_edit = new QLineEdit(current);
        m_edit->setFixedHeight(34);
        m_edit->setStyleSheet(
            "QLineEdit { background: rgba(0,0,0,70);"
            " border: 1px solid rgba(255,255,255,36); border-radius: 8px;"
            " padding: 0 10px; color: rgb(240,237,232); font-size: 13px;"
            " selection-background-color: rgba(168,50,50,190);"
            " selection-color: white; }"
            "QLineEdit:focus { border: 1px solid rgba(200,70,70,220); }");
        root->addWidget(m_edit);
        root->addSpacing(6);

        m_hint = new QLabel;
        m_hint->setFixedHeight(16);
        m_hint->setStyleSheet("color: rgb(235,110,110); font-size: 11px;"
                              " background: transparent;");
        root->addWidget(m_hint);
        root->addStretch();

        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        row->addStretch();
        m_cancel = new QPushButton("Cancel");
        m_ok     = new QPushButton("Rename");
        for (QPushButton *b : {m_cancel, m_ok}) {
            b->setFixedHeight(32);
            b->setMinimumWidth(90);
            b->setCursor(Qt::PointingHandCursor);
        }
        m_cancel->setAutoDefault(false);
        m_ok->setAutoDefault(true);
        m_ok->setDefault(true);
        m_cancel->setStyleSheet(
            "QPushButton { background: rgba(255,255,255,22);"
            " color: rgb(240,237,232);"
            " border: 1px solid rgba(255,255,255,30); border-radius: 8px;"
            " padding: 0 16px; font-size: 13px; }"
            "QPushButton:hover { background: rgba(255,255,255,36); }");
        m_ok->setStyleSheet(
            "QPushButton { background: rgb(168,50,50); color: white;"
            " border: 1px solid rgba(255,255,255,40); border-radius: 8px;"
            " padding: 0 16px; font-size: 13px; font-weight: 600; }"
            "QPushButton:hover { background: rgb(190,62,62); }"
            "QPushButton:disabled { background: rgba(168,50,50,70);"
            " color: rgba(255,255,255,110); }");
        row->addWidget(m_cancel);
        row->addWidget(m_ok);
        root->addLayout(row);

        QObject::connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        QObject::connect(m_ok, &QPushButton::clicked, this, &QDialog::accept);
        QObject::connect(m_edit, &QLineEdit::returnPressed, this,
                         [this]() { accept(); });
        QObject::connect(m_edit, &QLineEdit::textChanged, this,
                         [this]() { validate(); });
        validate();
    }

    void validate() {
        const QString n = m_edit->text().trimmed();
        QString msg;
        if (n.isEmpty())
            msg = "Name can\u2019t be empty.";
        else if (n.contains('/'))
            msg = "Names can\u2019t contain \u201C/\u201D.";
        else if (n == "." || n == "..")
            msg = "That name isn\u2019t allowed.";
        else if (n != m_current && QFileInfo::exists(QDir(m_dir).filePath(n)))
            msg = "\u201C" + n + "\u201D already exists here.";
        m_hint->setText(msg);
        m_ok->setEnabled(msg.isEmpty() && n != m_current);
    }

    static QPixmap glyph(bool folder) {
        const int S = 56;
        QPixmap pm(S, S);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath t;
        t.addRoundedRect(QRectF(0, 0, S, S), S * 0.22, S * 0.22);
        QLinearGradient g(0, 0, 0, S);
        if (folder) {
            g.setColorAt(0.0, QColor(118, 174, 248));
            g.setColorAt(1.0, QColor(54, 108, 212));
        } else {
            g.setColorAt(0.0, QColor(178, 186, 202));
            g.setColorAt(1.0, QColor(118, 128, 150));
        }
        p.fillPath(t, g);
        p.setPen(Qt::NoPen);
        if (folder) {
            p.setBrush(QColor(250, 252, 255));
            p.drawRoundedRect(QRectF(S * 0.20, S * 0.25, S * 0.28, S * 0.16), 3, 3);
            p.drawRoundedRect(QRectF(S * 0.20, S * 0.34, S * 0.60, S * 0.40), 4, 4);
        } else {
            p.setBrush(QColor(252, 252, 252));
            p.drawRoundedRect(QRectF(S * 0.27, S * 0.17, S * 0.46, S * 0.58), 3, 3);
        }
        return pm;
    }

    QString      m_dir, m_current;
    int          m_selLen = 0;
    QLineEdit   *m_edit   = nullptr;
    QLabel      *m_hint   = nullptr;
    QPushButton *m_ok     = nullptr;
    QPushButton *m_cancel = nullptr;
};

class PoolIcon : public QWidget {
public:
    PoolIcon(const QString &id, const QString &label,
                std::function<void(QPainter&, QRectF)> paint,
                std::function<void()> onActivate,
                QWidget *parent = nullptr)
        : QWidget(parent), m_id(id), m_label(label),
          m_paint(paint), m_onActivate(onActivate)
    {
        setFixedSize(96, 108);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);

        setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(this, &QWidget::customContextMenuRequested,
                         this, [this](const QPoint &pos) {
            // ---- desktop file/folder menu ----
            if (m_id.startsWith("file:")) {
                const QString path = m_id.mid(5);
                const auto openIt = m_onActivate;
                const auto tidy   = m_onTidyUp;
                int choice = 0;
                {
                    // Menu lives only inside this scope, so it is gone
                    // before any dialog opens (the desktop can rebuild
                    // and delete this icon while a dialog is up).
                    ApoMenu fm(this);
                    fm.setStyleSheet(
                        "QMenu { background: rgb(24, 22, 30);"
                        " border: 1px solid rgba(255,255,255,30);"
                        " border-radius: 10px; padding: 6px;"
                        " color: rgb(240, 237, 232); font-size: 13px; }"
                        "QMenu::item { padding: 7px 24px 7px 16px;"
                        " border-radius: 6px; }"
                        "QMenu::item:selected { background: rgba(168, 50, 50, 90); }");
                    QAction *aOpen   = fm.addAction("Open");
                    fm.addSeparator();
                    QAction *aRename = fm.addAction("Rename\u2026");
                    QAction *aTrash  = fm.addAction("Move to Trash");
                    fm.addSeparator();
                    QAction *aCopy   = fm.addAction("Copy Path");
                    fm.addSeparator();
                    QAction *aTidy   = fm.addAction("Tidy Up");
                    QAction *ch = fm.exec(mapToGlobal(pos));
                    if (ch == aOpen) choice = 1;
                    else if (ch == aRename) choice = 2;
                    else if (ch == aTrash) choice = 3;
                    else if (ch == aCopy) choice = 4;
                    else if (ch == aTidy) choice = 5;
                }
                if (choice == 1) { if (openIt) openIt(); }
                else if (choice == 2) {
                    const QFileInfo info(path);
                    const QString name = RenameDialog::ask(
                        info.absolutePath(), info.fileName(), info.isDir());
                    if (!name.isEmpty() && name != info.fileName()) {
                        const QString dest = info.absoluteDir().filePath(name);
                        if (!QDir().rename(path, dest))
                            QMessageBox::warning(nullptr, "Rename",
                                "Couldn't rename \"" + info.fileName() + "\".");
                    }
                }
                else if (choice == 3) {
                    if (!QFile::moveToTrash(path))
                        QMessageBox::warning(nullptr, "Move to Trash",
                            "Couldn't move it to the Trash.");
                }
                else if (choice == 4) QGuiApplication::clipboard()->setText(path);
                else if (choice == 5) { if (tidy) tidy(); }
                return;
            }

            ApoMenu m(this);
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

            const QString id = m_id;
            const bool inDock = isInDock(id);

            QAction *rmDesk = m.addAction("Remove from thePool");
            QAction *dockAct = m.addAction(
                inDock ? "Remove from Dock" : "Add to Dock");
            m.addSeparator();
            QAction *tidyAct = m.addAction("tidyUp");

            QAction *chosen = m.exec(mapToGlobal(pos));
            if (chosen == rmDesk) setOnDesktop(id, false);
            else if (chosen == dockAct) setInDock(id, !inDock);
            else if (chosen == tidyAct && m_onTidyUp) m_onTidyUp();
        });
    }

    QString id() const { return m_id; }

    // ---- The Pool — framework API ----
    // targetPos: where the icon is trying to be. Normally equals
    //   the icon's settled cell. When displaced, the canvas sets a
    //   new value and the tick animates toward it.
    // homeCell:  the icon's original grid slot. Used by Foam mode
    //   to know where to return to.
    QPoint targetPos() const   { return m_targetPos; }
    void   setTargetPos(const QPoint &p) { m_targetPos = p; }

    QPoint homeCell() const    { return m_homeCell; }
    void   setHomeCell(const QPoint &p) { m_homeCell = p; }

    bool isAnimating() const   { return pos() != m_targetPos; }

    // Velocity-based motion toward target. Called from the
    // canvas tick (33 Hz). Each icon has a start delay
    // (m_staggerLeft) so displacement reads as a wave, not a
    // synchronised hop. As it approaches, a small overshoot
    // happens once, then settles — the water "give".
    //
    // Tunables from config: poolSpeed (multiplier),
    // poolStaggerMs (initial delay), poolOvershootPct.
    bool stepTowardTarget() {
        if (m_staggerLeft > 0) { --m_staggerLeft; return false; }

        const auto &cfg = VisualConfigManager::instance().cfg();
        const double speed = qBound(0.5, cfg.poolSpeed, 2.0);

        // Spring/damper tuned for liquid glide, not spring bounce.
        //   accel  — low, so it eases into motion
        //   damp   — high retention, so it coasts
        //   cap    — no teleporting on big displacements
        const double accel = 0.055 * speed;
        const double damp  = 0.90;          // was 0.72 — long glide
        const double cap   = 14.0;          // px/tick soft speed limit

        QPointF toTgt(m_targetPos.x() - pos().x(),
                      m_targetPos.y() - pos().y());
        const double dist = std::hypot(toTgt.x(), toTgt.y());

        // Velocity increment — a spring pulling toward the target.
        QPointF force = toTgt * accel;
        m_vel += force;
        m_vel *= damp;

        // Cap magnitude.
        double vmag = std::hypot(m_vel.x(), m_vel.y());
        if (vmag > cap) {
            m_vel *= cap / vmag;
            vmag = cap;
        }

        // Overshoot: once per arrival. Fires earlier now (25 px)
        // so the give happens with room left to coast back.
        const int overPct = qBound(0, cfg.poolOvershootPct, 30);
        if (!m_overshot && overPct > 0
            && dist > 3 && dist < 25 && vmag < 2.5) {
            double k = 1.0 + overPct / 100.0;
            m_vel.setX(toTgt.x() * k * 0.18);
            m_vel.setY(toTgt.y() * k * 0.18);
            m_overshot = true;
        }

        // Snap only when nearly motionless AND almost at target.
        // Tighter than before, so the last 2 px glide instead of
        // jumping.
        if (vmag < 0.12 && dist < 0.6) {
            move(m_targetPos);
            m_vel = QPointF(0, 0);
            m_overshot = false;
            return true;
        }

        // Apply — round only at the last step so sub-pixel motion
        // accumulates smoothly.
        m_accumX += m_vel.x();
        m_accumY += m_vel.y();
        int dx = int(m_accumX);
        int dy = int(m_accumY);
        m_accumX -= dx;
        m_accumY -= dy;
        if (dx != 0 || dy != 0)
            move(pos().x() + dx, pos().y() + dy);
        return true;
    }

    // Called by the canvas when a new displacement is computed.
    // Resets the wave-stagger so this icon starts moving a
    // fraction of a beat later than its neighbour.
    void armStagger(int extraMs) {
        const auto &cfg = VisualConfigManager::instance().cfg();
        int base = qBound(0, cfg.poolStaggerMs, 200);
        m_staggerLeft = (base + extraMs) / 30;   // 33 Hz ticks
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF tile((width() - 64) / 2.0, 6, 64, 64);

        // Chip at same bounds / same radius as the icon. At rest,
        // fully covered. On hover, grows 3px and darkens.
        const qreal baseRadius = tile.width() * 0.22;
        const qreal pad = m_hover ? 3.0 : 0.0;
        QRectF chipRect = tile.adjusted(-pad, -pad, pad, pad);
        const int chipAlpha = m_hover ? 160 : 0;

        if (chipAlpha > 0) {
            QPainterPath chipPath;
            chipPath.addRoundedRect(chipRect,
                                    baseRadius + pad,
                                    baseRadius + pad);
            p.fillPath(chipPath, QColor(0, 0, 0, chipAlpha));
        }

        // App icon on top, no margin.
        if (m_paint) {
            m_paint(p, tile);
        }

        // Label
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        QColor labelCol(240, 237, 232, m_hover ? 255 : 220);
        QPainterPath bg;
        QRectF labelRect(4, 74, width() - 8, 22);
        bg.addRoundedRect(labelRect, 5, 5);
        p.fillPath(bg, QColor(0, 0, 0, m_hover ? 120 : 70));
        p.setPen(labelCol);
        p.drawText(QRect(0, 74, width(), 30),
                   Qt::AlignHCenter | Qt::AlignTop,
                   m_label);
    }

    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *)      override { m_hover = false; update(); }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = false;
            m_pressPos = e->globalPosition().toPoint();
            m_dragStart = pos();
            e->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        QPoint delta = e->globalPosition().toPoint() - m_pressPos;
        if (!m_dragging && delta.manhattanLength() > 6)
            m_dragging = true;
        if (m_dragging) {
            move(m_dragStart + delta);
            if (onMoved) onMoved(m_id, pos());
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return;
        if (!m_dragging && m_onActivate) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastActivateMs > 0
                && now - m_lastActivateMs < 500) {
                qDebug() << "[pool] suppressing rapid re-activate"
                         << m_id << "(+" << (now - m_lastActivateMs)
                         << "ms)";
            } else {
                m_lastActivateMs = now;
                m_onActivate();
            }
        }
        m_dragging = false;
        // Phase 2: settle at current pixel position. Phase 3 will
        // replace this with "nearest free grid cell away from any
        // intersecting widget."
        m_targetPos = pos();
    }

public:
    std::function<void(const QString&, const QPoint&)> onMoved;
    std::function<void()>                              m_onTidyUp;

private:
    QString m_id;
    QString m_label;
    std::function<void(QPainter&, QRectF)> m_paint;
    std::function<void()> m_onActivate;
    bool m_hover = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    QPoint m_dragStart;
    // 2026-10-17: debounce double-click activate on desktop icons.
    // Same reason as DockIcon — no IPC probe in Model A, so a
    // double-click while the app is booting would spawn twice.
    qint64 m_lastActivateMs = 0;
    QPoint  m_targetPos;       // The Pool — settle target
    QPoint  m_homeCell;        // The Pool — original grid slot
    QPointF m_vel{0, 0};       // velocity (px per tick)
    double  m_accumX = 0.0;    // sub-pixel accumulator
    double  m_accumY = 0.0;
    bool    m_overshot = false;
    int     m_staggerLeft = 0; // ticks to wait before moving
};

class PoolCanvas : public QWidget {
public:
    PoolCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("thePool");
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        // Canvas does NOT block clicks in empty areas; only icons
        // respond. Children receive events normally.
        setMouseTracking(true);

        setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(this, &QWidget::customContextMenuRequested,
                         this, [this](const QPoint &pos) {
            showCanvasMenu(pos);
        });

        // The Pool — settle tick. 33 Hz. Every step, each icon that
        // is animating moves a fraction toward its target. Idle
        // frames cost nothing because stepTowardTarget() returns
        // false when the icon is already at its target.
        m_tick = new QTimer(this);
        m_tick->setInterval(30);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { stepPool(); });
        m_tick->start();
    }

    // Swallow presses on empty canvas so they don't reach the
    // wallpaper layer underneath, but do NOT consume them if they
    // land on a child (Qt handles that automatically).
    void mousePressEvent(QMouseEvent *e) override {
        // Right-click should open our canvas context menu; left-click
        // on empty space goes through to the wallpaper layer.
        if (e->button() == Qt::RightButton) { e->accept(); return; }
        e->ignore();
    }

    void addIcon(const QString &id, const QString &label,
                 std::function<void(QPainter&, QRectF)> paint,
                 std::function<void()> onActivate,
                 const QPoint &pos)
    {
        PoolIcon *icon = new PoolIcon(id, label, paint,
                                             onActivate, this);
        icon->move(pos);
        icon->setHomeCell(pos);
        icon->setTargetPos(pos);
        icon->show();
        icon->raise();
        m_icons.append(icon);

        icon->onMoved = [this](const QString &iconId, const QPoint &p) {
            if (m_onMoved) m_onMoved(iconId, p);
        };
    }

    std::function<void(const QString&, const QPoint&)> m_onMoved;

    QList<PoolIcon*> icons() const { return m_icons; }

    // Wallpaper switching hook — set by PoolBackground at construction.
    // Lets the canvas menu call into the background's setWallpaper without
    // needing a class-order forward reference.
    static std::function<void(const QString &)> &wallpaperHook() {
        static std::function<void(const QString &)> fn;
        return fn;
    }

    void showCanvasMenu(const QPoint &pos) {
        ApoMenu m(this);
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

        // ---- New ----
        QAction *aNewFolder = m.addAction("New Folder");
        QAction *aNewFile   = m.addAction("New Text File");
        m.addSeparator();

        // ---- Open ----
        QAction *aOpenTerminal = m.addAction("Open Terminal");
        QAction *aOpenFiles    = m.addAction("Open File Manager");
        QAction *aOpenHome     = m.addAction("Open Home Folder");
        m.addSeparator();

        // ---- Wallpaper submenu ----
        QMenu *wallSub = m.addMenu("Change Wallpaper");
        struct WP { const char *id; const char *name; };
        const WP wps[] = {
            { "ruby",       "Magnific"             },
            { "starfield",  "Starfield"            },
            { "aurora",     "Aurora"               },
            { "goldengate", "Golden Gate"          },
            { "everest",    "Mount Everest"        },
            { "apocalypse", "Apocalypse"           },
            { "neonjinx",   "Neon Jinx"            },
            { "mob",        "Mob (dev)"            },
            { "vander",     "Vander Warwick (dev)" }
        };
        for (const auto &w : wps) {
            QAction *wa = wallSub->addAction(w.name);
            QString id = QString::fromUtf8(w.id);
            QObject::connect(wa, &QAction::triggered, [id]() {
                auto &hook = PoolCanvas::wallpaperHook();
                if (hook) hook(id);
            });
        }
        m.addSeparator();

        // ---- Desktop ----
        QAction *tidy = m.addAction("Tidy Up");
        QAction *refresh = m.addAction("Refresh Icons");
        m.addSeparator();

        // ---- System ----
        QAction *aSettings = m.addAction("System Settings…");
        QAction *aAbout    = m.addAction("About Apokolips");

        QAction *chosen = m.exec(mapToGlobal(pos));
        if (chosen == tidy) tidyUp();
        else if (chosen == refresh) rebuildIcons();
        else if (chosen == aNewFolder) {
            QString base = QDir::homePath() + "/Desktop";
            QDir().mkpath(base);
            QString path = base + "/New Folder";
            int n = 1;
            while (QDir(path).exists())
                path = base + QString("/New Folder %1").arg(++n);
            QDir().mkpath(path);
            rebuildIcons();
        }
        else if (chosen == aNewFile) {
            QString base = QDir::homePath() + "/Desktop";
            QDir().mkpath(base);
            QString path = base + "/New Text File.txt";
            int n = 1;
            while (QFile::exists(path))
                path = base + QString("/New Text File %1.txt").arg(++n);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.close();
                rebuildIcons();
            }
        }
        else if (chosen == aOpenTerminal) {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--node" });
        }
        else if (chosen == aOpenFiles) {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--files" });
        }
        else if (chosen == aOpenHome) {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--files", QDir::homePath() });
        }
        else if (chosen == aSettings) {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--settings" });
        }
        else if (chosen == aAbout) {
            QMessageBox::about(this, "About Apokolips",
                "<b>Apokolips OS</b><br>"
                "macOS polish. Windows compatibility. "
                "Linux foundation.");
        }
    }

    void tidyUp() {
        VisualConfigManager::instance().setAndSave(
            [](VisualConfig &c) { c.poolIconPos.clear(); });
        rebuildIcons();
    }

    void rebuildIcons() {
        for (PoolIcon *ic : m_icons) ic->deleteLater();
        m_icons.clear();

        // Which apps are on the desktop
        QStringList ids;
        auto &cfg = VisualConfigManager::instance().cfg();
        if (cfg.poolApps.isEmpty()) ids = { "mymach" };
        else                          ids = cfg.poolApps;

        // Default grid — right column fills top-down, then wraps to
        // the next column leftward when the column runs out of space.
        // Uses the CANVAS geometry (not screen) so the layout is
        // correct even when the shell is on a smaller workspace,
        // with the dock on a side, or after a resize. Falls back to
        // screen size only when the canvas hasn't been laid out yet.
        int cW = width();
        int cH = height();
        if (cW <= 0 || cH <= 0) {
            if (QScreen *s = QApplication::primaryScreen()) {
                cW = s->availableGeometry().width();
                cH = s->availableGeometry().height();
            } else {
                cW = 1200; cH = 800;
            }
        }
        const int iconH    = 116;   // slot step
        const int iconW    = 116;
        const int startX   = cW - 130;
        const int startY   = 40;
        const int usableH  = cH - startY - 110;   // leave room for dock
        const int rowsMax  = qMax(1, usableH / iconH);

        int slot = 0;
        for (const QString &id : ids) {
            if (id.isEmpty()) continue;

            int col = slot / rowsMax;
            int row = slot % rowsMax;
            int defX = startX - col * iconW;
            int defY = startY + row * iconH;

            QPoint pos;
            if (cfg.poolIconPos.contains(id)) {
                QVariantMap mp = cfg.poolIconPos[id].toMap();
                pos = QPoint(mp.value("x", defX).toInt(),
                             mp.value("y", defY).toInt());
            } else {
                pos = QPoint(defX, defY);
            }

            QString label = labelForId(id);
            // Route every desktop icon through the same renderer the
            // dock uses — one source of truth for appTile colors,
            // carve, and rim.
            std::function<void(QPainter&, QRectF)> paint =
                [id, label](QPainter &p, QRectF r) {
                    Icons::drawFor(label, p, r,
                                   QColor(240, 237, 232));
                };

            QString capId = id;
            desktopIconsLaunchArg(capId);
            int before = m_icons.size();
            addIcon(id, label, paint,
                [capId]() {
                    QStringList args = desktopLaunchArgs(capId);
                    if (args.isEmpty()) {
                        // Unknown id or explicit refusal. Do NOT
                        // spawn a bare shell — it would inherit
                        // app_id=apokolips-shell and get tiled.
                        qWarning()
                            << "[desktop] click: no args for"
                            << capId << "\u2014 ignoring";
                        return;
                    }
                    QProcess::startDetached(
                        QCoreApplication::applicationFilePath(),
                        args);
                }, pos);
            if (m_icons.size() > before) {
                PoolIcon *added = m_icons.last();
                added->m_onTidyUp = [this]() { tidyUp(); };
            }
            slot++;
        }

        // --- Desktop files and folders (obey The Pool rules) ---
        QDir desktopDir(QDir::homePath() + "/Desktop");
        if (desktopDir.exists()) {
            QIcon::setThemeSearchPaths({
                "/usr/share/icons",
                "/usr/local/share/icons",
                QDir::homePath() + "/.icons",
                QDir::homePath() + "/.local/share/icons"
            });
            QIcon::setThemeName("Adwaita");

            QFileIconProvider iconProvider;
            QFileInfoList entries = desktopDir.entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &fi : entries) {
                QString fpath = fi.absoluteFilePath();
                QString key = "file:" + fpath;

                int col = slot / rowsMax;
                int row = slot % rowsMax;
                int defX = startX - col * iconW;
                int defY = startY + row * iconH;
                QPoint pos;
                if (cfg.poolIconPos.contains(key)) {
                    QVariantMap mp = cfg.poolIconPos[key].toMap();
                    pos = QPoint(mp.value("x", defX).toInt(),
                                 mp.value("y", defY).toInt());
                } else {
                    pos = QPoint(defX, defY);
                }

                QString label = fi.fileName();
                QIcon sysIcon = iconProvider.icon(fi);
                const bool isFolder = fi.isDir();
                const QString ext = fi.suffix().left(4).toUpper();
                std::function<void(QPainter&, QRectF)> fpaint =
                    [isFolder, ext](QPainter &p, QRectF r) {
                        p.setRenderHint(QPainter::Antialiasing, true);
                        const qreal w = r.width(), h = r.height();

                        // The whole tile IS the icon (same squircle as the
                        // app tiles), not an icon sitting inside a tile.
                        QPainterPath tile;
                        tile.addRoundedRect(r, w * 0.22, w * 0.22);

                        QLinearGradient g(r.topLeft(), r.bottomLeft());
                        if (isFolder) {
                            g.setColorAt(0.0, QColor(118, 174, 248));
                            g.setColorAt(1.0, QColor(54, 108, 212));
                        } else {
                            g.setColorAt(0.0, QColor(178, 186, 202));
                            g.setColorAt(1.0, QColor(118, 128, 150));
                        }
                        p.fillPath(tile, g);
                        p.setPen(QPen(QColor(255, 255, 255, 50), 1));
                        p.setBrush(Qt::NoBrush);
                        p.drawPath(tile);

                        p.setPen(Qt::NoPen);
                        if (isFolder) {
                            p.setBrush(QColor(250, 252, 255));
                            p.drawRoundedRect(QRectF(r.left() + w * 0.20, r.top() + h * 0.25, w * 0.28, h * 0.16), 3, 3);
                            p.drawRoundedRect(QRectF(r.left() + w * 0.20, r.top() + h * 0.34, w * 0.60, h * 0.40), 4, 4);
                        } else {
                            QRectF pg(r.left() + w * 0.27, r.top() + h * 0.17, w * 0.46, h * 0.58);
                            const qreal fold = w * 0.13;
                            QPainterPath page;
                            page.moveTo(pg.left(), pg.top());
                            page.lineTo(pg.right() - fold, pg.top());
                            page.lineTo(pg.right(), pg.top() + fold);
                            page.lineTo(pg.right(), pg.bottom());
                            page.lineTo(pg.left(), pg.bottom());
                            page.closeSubpath();
                            p.setBrush(QColor(252, 252, 252));
                            p.drawPath(page);

                            QPainterPath tri;
                            tri.moveTo(pg.right() - fold, pg.top());
                            tri.lineTo(pg.right() - fold, pg.top() + fold);
                            tri.lineTo(pg.right(), pg.top() + fold);
                            tri.closeSubpath();
                            p.setBrush(QColor(205, 208, 216));
                            p.drawPath(tri);

                            p.setPen(QPen(QColor(150, 156, 170), 1.4, Qt::SolidLine, Qt::RoundCap));
                            for (int i = 0; i < 3; ++i) {
                                const qreal y = pg.top() + h * 0.28 + i * h * 0.09;
                                p.drawLine(QPointF(pg.left() + w * 0.07, y),
                                           QPointF(pg.right() - w * 0.07, y));
                            }

                            if (!ext.isEmpty()) {
                                QFont f = p.font();
                                f.setPixelSize(qMax(7, int(h * 0.14)));
                                f.setBold(true);
                                p.setFont(f);
                                p.setPen(QColor(255, 255, 255, 235));
                                p.drawText(QRectF(r.left(), r.top() + h * 0.77, w, h * 0.19),
                                           Qt::AlignCenter, ext);
                            }
                        }
                    };

                int before = m_icons.size();
                bool isDir = fi.isDir();
                addIcon(key, label, fpaint, [fpath, isDir]() {
                    if (isDir) {
                        // Folders: open in our own Finder
                        QProcess::startDetached(
                            QCoreApplication::applicationFilePath(),
                            {"--files", fpath});
                    } else {
                        // Files: launch via xdg-open with Wayland env
                        QProcessEnvironment env =
                            QProcessEnvironment::systemEnvironment();
                        if (env.value("WAYLAND_DISPLAY").isEmpty()) {
                            QDir rd(env.value("XDG_RUNTIME_DIR",
                                              "/run/user/1000"));
                            for (const QString &f :
                                 rd.entryList({"wayland-*"},
                                              QDir::Files | QDir::System)) {
                                if (!f.endsWith(".lock")) {
                                    env.insert("WAYLAND_DISPLAY", f);
                                    break;
                                }
                            }
                        }
                        if (env.value("XDG_RUNTIME_DIR").isEmpty())
                            env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
                        if (env.value("XDG_SESSION_TYPE").isEmpty())
                            env.insert("XDG_SESSION_TYPE", "wayland");
                        if (env.value("GDK_BACKEND").isEmpty())
                            env.insert("GDK_BACKEND", "wayland");

                        QProcess *proc = new QProcess;
                        proc->setProcessEnvironment(env);
                        proc->startDetached("xdg-open", {fpath});
                        delete proc;
                    }
                }, pos);
                if (m_icons.size() > before) {
                    m_icons.last()->m_onTidyUp = [this]() { tidyUp(); };
                }
                slot++;
            }
        }

        // --- Watch ~/Desktop for live changes ---
        if (findChildren<QFileSystemWatcher*>().isEmpty()) {
            auto *watcher = new QFileSystemWatcher(this);
            if (desktopDir.exists())
                watcher->addPath(desktopDir.absolutePath());
            QObject::connect(watcher,
                &QFileSystemWatcher::directoryChanged,
                this, [this](const QString &) {
                    QTimer::singleShot(150, this, [this]() { rebuildIcons(); });
                });
        }

        update();
    }

public:
    // Clamp all icons inside the canvas bounds. Called both from
    // resizeEvent (below) and from the shell's dock-reflow path
    // when the canvas geometry didn't change but icon positions
    // were loaded from a different dock-layout session.
    void clampIcons() {
        if (width() <= 0 || height() <= 0) return;
        for (PoolIcon *ic : m_icons) {
            QPoint p = ic->pos();
            int maxX = width()  - ic->width();
            int maxY = height() - ic->height();
            p.setX(qBound(0, p.x(), qMax(0, maxX)));
            p.setY(qBound(0, p.y(), qMax(0, maxY)));
            ic->move(p);
        }
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        clampIcons();
    }

private:
    // ---- The Pool — collision + settle ----

    // Is this rect free of every widget and every other icon's
    // target? Padding gives icons a little breathing room.
    bool cellIsFree(const QRect &r, PoolIcon *ignore) {
        // Canvas bounds
        if (r.left() < 0 || r.top() < 0) return false;
        if (r.right() > width() || r.bottom() > height())
            return false;

        // Widget obstacles — only cards that are visible.
        QWidget *top = window();
        if (top) {
            for (QWidget *w : top->findChildren<QWidget *>()) {
                if (auto *card = dynamic_cast<WidgetCard *>(w)) {
                    if (!card->isVisible()) continue;
                    QRect g = card->globalRect();
                    QPoint tl = mapFromGlobal(g.topLeft());
                    QRect obs(tl, g.size());
                    if (obs.intersects(r.adjusted(-4, -4, 4, 4)))
                        return false;
                }
                // Dock obstacle (Variation B — soft 12px buffer).
                // Only counts when dock is visible; auto-hide makes
                // the obstacle vanish so icons flow into the space.
                if (auto *d = dynamic_cast<Dock *>(w)) {
                    if (!d->isVisible()) continue;
                    QPoint dg = d->mapToGlobal(QPoint(0, 0));
                    QPoint tl = mapFromGlobal(dg);
                    QRect obs(tl, d->size());
                    if (obs.intersects(r.adjusted(-12, -12, 12, 12)))
                        return false;
                }
            }
        }

        // Other icons' target cells
        for (PoolIcon *other : m_icons) {
            if (!other || other == ignore) continue;
            QRect o(other->targetPos(), other->size());
            if (o.intersects(r.adjusted(-4, -4, 4, 4)))
                return false;
        }

        return true;
    }

    // Find the nearest free grid cell for this icon. Cell size is
    // icon width + 20 px gap (matching the horizontal grid the
    // canvas already uses for its default layout).
    QPoint findFreeCellNear(PoolIcon *ic, const QRect &itsRect) {
        const int stepX = ic->width() + 20;
        const int stepY = ic->height() + 12;

        // Start from the icon's current cell, then spiral outward
        // in rings. Return the first free one.
        int cx = (itsRect.left() + stepX / 2) / stepX;
        int cy = (itsRect.top()  + stepY / 2) / stepY;

        for (int ring = 0; ring < 30; ++ring) {
            for (int dy = -ring; dy <= ring; ++dy) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (qAbs(dx) != ring && qAbs(dy) != ring)
                        continue;
                    QPoint p((cx + dx) * stepX,
                             (cy + dy) * stepY);
                    QRect r(p, ic->size());
                    if (cellIsFree(r, ic)) return p;
                }
            }
        }
        return itsRect.topLeft();   // fallback: don't move
    }

    // 33 Hz. Called from the tick.
    void stepPool() {
        // Master off → nothing animates. Icons sit still, widgets
        // don't displace anything. Just keep the tick alive so
        // toggling it back on resumes without a restart.
        if (!VisualConfigManager::instance().cfg().poolEnabled) return;

        // 1) Advance every animating icon toward its target.
        for (PoolIcon *ic : m_icons) {
            if (ic) ic->stepTowardTarget();
        }

        // 2) Displacement pass — for every icon whose target
        //    overlaps a widget, compute the nearest free cell.
        //    Foam mode also returns icons to their home cell when
        //    the path is clear.
        const QString behavior = VisualConfigManager::instance()
                                     .cfg().poolBehavior;
        bool anyDisplaced = false;
        for (PoolIcon *ic : m_icons) {
            if (!ic) continue;
            QRect tr(ic->targetPos(), ic->size());

            // Is this icon currently hitting a widget?
            bool hit = false;
            QWidget *top = window();
            if (top) {
                for (QWidget *w : top->findChildren<QWidget *>()) {
                    if (auto *card =
                            dynamic_cast<WidgetCard *>(w)) {
                        if (!card->isVisible()) continue;
                        QRect g = card->globalRect();
                        QPoint tl = mapFromGlobal(g.topLeft());
                        QRect obs(tl, g.size());
                        if (obs.intersects(
                                tr.adjusted(-4, -4, 4, 4))) {
                            hit = true; break;
                        }
                    }
                }
            }

            if (hit) {
                QPoint free = findFreeCellNear(ic, tr);
                if (free != ic->targetPos()) {
                    ic->setTargetPos(free);
                    // Stagger by icon index — later icons start
                    // their glide a beat after earlier ones, so
                    // the whole group reads as a wave.
                    int idx = m_icons.indexOf(ic);
                    ic->armStagger(idx * 12);
                    anyDisplaced = true;
                }
                continue;
            }

            // Not displaced. Foam mode → drift home when clear.
            if (behavior == "foam") {
                QPoint home = ic->homeCell();
                if (home != ic->targetPos()) {
                    QRect hr(home, ic->size());
                    if (cellIsFree(hr, ic)) {
                        ic->setTargetPos(home);
                        ic->armStagger(0);
                        anyDisplaced = true;
                    }
                }
            }
            // Water mode → stay wherever the icon landed.
        }

        if (anyDisplaced) {
            qInfo() << "[thePool] step — behavior =" << behavior
                    << "icons =" << m_icons.size();
        }
    }

    QList<PoolIcon*> m_icons;
    QTimer          *m_tick = nullptr;
};



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

        // Live-apply size when visual.json changes
        m_cfgWatch = new QTimer(this);
        m_cfgWatch->setInterval(400);
        QObject::connect(m_cfgWatch, &QTimer::timeout, this, [this]() {
            QString path = []() {
                QString dir = QStandardPaths::writableLocation(
                                  QStandardPaths::ConfigLocation);
                if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
                return dir + "/apokolips/visual.json";
            }();
            QFileInfo fi(path);
            qint64 m = fi.exists()
                ? fi.lastModified().toMSecsSinceEpoch() : 0;
            if (m != m_cfgStamp) {
                m_cfgStamp = m;
                VisualConfigManager::instance().load();
                if (isVisible()) {
                    QResizeEvent ev(size(), size());
                    QWidget::resizeEvent(&ev);
                    update();
                }
            }
        });
        m_cfgWatch->start();
    }

    void toggle() {
        auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.spotlightEnabled) return;
        isVisible() ? hideNow() : showNow();
    }

    void showNow() {
        if (parentWidget()) setGeometry(parentWidget()->rect());
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

        auto &spotCfg = VisualConfigManager::instance().cfg();

        // ---- Calculator: if query looks mathy ----
        if (spotCfg.spotCalcEnabled)
        if (!q.isEmpty() && q.at(0).isDigit()) {
            QString val = tryCalculate(q);
            if (!val.isEmpty()) {
                addResult({"Calc", val, "Copy to clipboard", val});
            }
        }

        // ---- Paths: if starts with / or ~ ----
        if (spotCfg.spotFilesEnabled
            && (q.startsWith("/") || q.startsWith("~"))) {
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

        // ---- Notes: search note content ----
        if (spotCfg.spotFilesEnabled && q.length() >= 2) {
            QDir nd(QDir::homePath()
                    + "/.local/share/apokolips/notes");
            if (nd.exists()) {
                const QStringList notes = nd.entryList(
                    { "*.txt" }, QDir::Files, QDir::Time);
                for (const QString &fname : notes) {
                    QFile f(nd.filePath(fname));
                    if (!f.open(QIODevice::ReadOnly)) continue;
                    const QString content =
                        QString::fromUtf8(f.readAll());
                    if (!content.contains(q, Qt::CaseInsensitive)
                        && !fname.contains(q, Qt::CaseInsensitive))
                        continue;
                    // Pull a snippet around the match.
                    QString snippet = content;
                    snippet.replace("\n", " ");
                    int at = snippet.indexOf(q, 0, Qt::CaseInsensitive);
                    if (at > 30) at -= 30;
                    if (at < 0) at = 0;
                    snippet = snippet.mid(at, 80).trimmed();
                    if (snippet.isEmpty())
                        snippet = fname;
                    addResult({"Note",
                               fname,
                               snippet,
                               f.fileName()});
                    f.close();
                }
            }
        }

        // ---- Apps: scan every common .desktop directory ----
        QStringList appDirs = {
            "/usr/share/applications",
            "/usr/local/share/applications",
            "/var/lib/snapd/desktop/applications",
            "/var/lib/flatpak/exports/share/applications",
            QDir::homePath() + "/.local/share/applications"
        };
        int shown = 0;
        for (const QString &dirPath : appDirs) {
            if (shown > 30) break;
            QDir appDir(dirPath);
            if (!appDir.exists()) continue;
            QStringList files;
            if (spotCfg.spotAppsEnabled)
                files = appDir.entryList({"*.desktop"}, QDir::Files);
            for (const QString &file : files) {
                if (shown > 30) break;
                QFile f(appDir.filePath(file));
                if (!f.open(QIODevice::ReadOnly)) continue;
                QString name, exec;
                bool noDisplay = false;
                while (!f.atEnd()) {
                    QString line = QString::fromUtf8(f.readLine()).trimmed();
                    if (line.startsWith("Name=") && name.isEmpty())
                        name = line.mid(5);
                    else if (line.startsWith("Exec=") && exec.isEmpty())
                        exec = line.mid(5);
                    else if (line.startsWith("NoDisplay=true"))
                        noDisplay = true;
                }
                f.close();
                if (noDisplay || name.isEmpty() || exec.isEmpty()) continue;
                // Strip %U %F etc. from Exec
                exec.remove(QRegularExpression(" ?%[a-zA-Z]"));
                exec = exec.trimmed();
                if (!q.isEmpty() &&
                    !name.toLower().contains(q.toLower())) continue;
                // Use gtk-launch on the desktop file basename — handles
                // snap wrappers, env prefixes, and %U placeholders.
                QString desktopId = file;
                if (desktopId.endsWith(".desktop"))
                    desktopId.chop(8);
                addResult({"App", name, exec, "gtk-launch:" + desktopId});
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
        auto &cfg = VisualConfigManager::instance().cfg();
        if (m_results->count() >= cfg.spotMaxResults) return;

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

        if (kind == "Note") {
            // payload = filename inside the notes folder.
            const QString full = QDir::homePath()
                + "/.local/share/apokolips/notes/" + payload;
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--notes", full });
            hideNow();
            return;
        }
        if (kind == "App") {
            if (payload.startsWith("gtk-launch:")) {
                QProcess::startDetached("gtk-launch",
                    { payload.mid(11) });
            } else {
                QProcess::startDetached(payload);
            }
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
        auto &cfg = VisualConfigManager::instance().cfg();
        int pw = qMin(cfg.spotlightBarWidth,  width() - 40);
        int ph = qMin(cfg.spotlightBarHeight, height() - 60);
        m_panel->setGeometry((width() - pw) / 2,
                             (height() - ph) / 2,
                             pw, ph);
    }

    QWidget     *m_panel   = nullptr;
    QLineEdit   *m_search  = nullptr;
    QListWidget *m_results = nullptr;
    QTimer      *m_scanTimer = nullptr;
    QTimer      *m_cfgWatch  = nullptr;
    qint64       m_cfgStamp  = 0;
    Theme        m_theme;
};

// ----------------------------------------------------------------
// CursorGlowOverlay — soft seal-coloured glow that follows the
// pointer. Trails 8 positions behind with falloff. Sits above
// every shell surface, ignores mouse events.
// ----------------------------------------------------------------
class CursorGlowOverlay : public QWidget {
public:
    explicit CursorGlowOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
        if (parent)
            setGeometry(0, 0, parent->width(), parent->height());

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, [this]() { tick(); });
        m_timer->start();
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_trail.isEmpty()) return;
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.cursorGlow) return;

        const int bright   = qBound(20, cfg.cursorGlowBrightness, 255);
        const int coreRad  = qBound(20, cfg.cursorGlowRadius, 100);
        const int softness = qBound(0,  cfg.cursorGlowSoftness, 100);
        const int fadePct  = qBound(0,  cfg.cursorGlowFade, 100);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QColor base = Sigil::accent();

        const int n = m_trail.size();

        // Smooth curve through the trail — quad-to midpoints.
        QPainterPath path;
        path.moveTo(m_trail[0]);
        if (n >= 3) {
            for (int i = 1; i < n - 1; ++i) {
                qreal mx = (m_trail[i].x() + m_trail[i + 1].x()) * 0.5;
                qreal my = (m_trail[i].y() + m_trail[i + 1].y()) * 0.5;
                path.quadTo(QPointF(m_trail[i]), QPointF(mx, my));
            }
            path.lineTo(m_trail[n - 1]);
        } else if (n == 2) {
            path.lineTo(m_trail[1]);
        }

        // Braces avoid the most-vexing-parse.
        QLinearGradient grad{QPointF(m_trail[0]),
                             QPointF(m_trail[n - 1])};

        QColor head = base.lighter(150);
        head.setAlpha(bright);

        QColor mid = base;
        int midAlpha = int(bright * (1.0 - fadePct / 140.0));
        if (midAlpha < 0) midAlpha = 0;
        mid.setAlpha(midAlpha);

        QColor tail = base;
        tail.setAlpha(0);

        grad.setColorAt(0.0, head);
        double midPos = 0.9 - fadePct * 0.008;
        midPos = qBound(0.05, midPos, 0.95);
        grad.setColorAt(midPos, mid);
        grad.setColorAt(1.0, tail);

        if (softness > 0 && n >= 2) {
            QColor softC = base;
            softC.setAlpha(int(bright * (0.15 + softness * 0.003)));
            QLinearGradient softGrad{QPointF(m_trail[0]),
                                     QPointF(m_trail[n - 1])};
            softGrad.setColorAt(0.0, softC);
            QColor softC2 = softC; softC2.setAlpha(0);
            softGrad.setColorAt(1.0, softC2);

            qreal glowW = coreRad * (1.2 + softness * 0.02);
            QPen glowPen(QBrush(softGrad), glowW, Qt::SolidLine,
                         Qt::RoundCap, Qt::RoundJoin);
            p.setPen(glowPen);
            p.drawPath(path);
        }

        if (cfg.cursorGlowStyle == "arrow") {
            const qreal lineW = coreRad * 0.35;
            QPen pen(QBrush(grad), lineW, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawPath(path);

            QColor headCore = base.lighter(190);
            headCore.setAlpha(qMin(255, bright + 60));
            p.setPen(Qt::NoPen);
            p.setBrush(headCore);
            const qreal hRad = lineW * 1.3;
            p.drawEllipse(QPointF(m_trail[0]), hRad, hRad);
        } else {
            const qreal lineW = coreRad * 0.85;
            QPen pen(QBrush(grad), lineW, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.drawPath(path);

            QColor headCore = base.lighter(140);
            headCore.setAlpha(bright);
            QRadialGradient core(QPointF(m_trail[0]), coreRad * 0.8);
            core.setColorAt(0.0, headCore);
            QColor core2 = headCore; core2.setAlpha(0);
            core.setColorAt(1.0, core2);
            p.setPen(Qt::NoPen);
            p.fillRect(rect(), core);
        }
    }

private:
    void tick() {
        if (!parentWidget()) return;
        // Read the current setting each tick so toggles apply live.
        const bool on =
            VisualConfigManager::instance().cfg().cursorGlow;
        if (!on) {
            if (isVisible()) { hide(); m_trail.clear(); }
            return;
        }
        if (!isVisible()) show();
        if (size() != parentWidget()->size())
            setGeometry(0, 0, parentWidget()->width(),
                        parentWidget()->height());

        // Trim the trail to the configured length.
        const int maxTrail = qMax(0,
            VisualConfigManager::instance().cfg().cursorGlowTrail);
        while (m_trail.size() > maxTrail + 1)
            m_trail.removeLast();

        const QPoint global = QCursor::pos();
        const QPoint local  = parentWidget()->mapFromGlobal(global);
        if (!m_trail.isEmpty() && m_trail.first() == local) return;
        m_trail.prepend(local);
        while (m_trail.size() > maxTrail + 1) m_trail.removeLast();
        update();
    }

    QTimer        *m_timer = nullptr;
    QList<QPoint>  m_trail;
    bool           m_enabled = true;
};

class PoolBackground : public QWidget {
public:
    explicit PoolBackground(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);

        // ---- Parallax: wallpaper shifts a few px opposite the
        // focused window's offset from screen center. Only polls
        // when the feature is enabled in config. 25 Hz poll,
        // 60 Hz smoothing, max 12 px shift.
        if (VisualConfigManager::instance().cfg().wallpaperParallax) {
            m_parallaxPollTimer = new QTimer(this);
            m_parallaxPollTimer->setInterval(250);
            QObject::connect(m_parallaxPollTimer, &QTimer::timeout,
                             this, [this]() { pollParallax(); });
            m_parallaxPollTimer->start();

            m_parallaxSmoothTimer = new QTimer(this);
            m_parallaxSmoothTimer->setInterval(16);
            QObject::connect(m_parallaxSmoothTimer, &QTimer::timeout,
                             this, [this]() { tickParallax(); });
            m_parallaxSmoothTimer->start();
        }

        // Install wallpaper-switch hook so PoolCanvas's menu can
        // trigger a wallpaper change without needing a class-order
        // forward reference.
        PoolCanvas::wallpaperHook() = [this](const QString &id) {
            // Single swap owner: same pattern as View menu. saveId
            // writes the config; the shell's wpPoll then applies the
            // wallpaper once. Direct setWallpaper here would conflict
            // with the poll and produce a black flash.
            Wallpaper *nw = WallpaperConfig::makeById(id);
            if (!nw) return;
            delete nw;
            WallpaperConfig::saveId(id);
            // Discovering a dev wallpaper via this menu unlocks it
            // permanently in Settings.
            auto &cfg = VisualConfigManager::instance().cfg();
            if (!cfg.discoveredWallpapers.contains(id)) {
                VisualConfigManager::instance().setAndSave(
                    [id](VisualConfig &c) {
                        if (!c.discoveredWallpapers.contains(id))
                            c.discoveredWallpapers.append(id);
                    });
                qInfo() << "[wallpaper] discovered:" << id;
            }
        };

        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(16);
        QObject::connect(m_tickTimer, &QTimer::timeout, this, [this]() {
            if (m_wallpaper && m_wallpaper->animated()) {
                m_wallpaper->tick();
                update();
                // Refresh the blurred dock backdrop every ~500 ms
                // so it tracks animated wallpapers (video, gif,
                // starfield, aurora) instead of freezing on the
                // first (often black) frame.
                if (++m_blurTickCounter >= 30) {   // 30 × 16 ms
                    m_blurTickCounter = 0;
                    m_blurTimer->start();
                }
            }
        });
        m_tickTimer->start();

        m_blurTimer = new QTimer(this);
        m_blurTimer->setSingleShot(true);
        m_blurTimer->setInterval(120);
        QObject::connect(m_blurTimer, &QTimer::timeout,
                         this, &PoolBackground::regenerateBlur);
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

    void setSpotlight(SpotlightOverlay *s) {
        m_spotlight = s;
        s->setParent(this);
        s->setGeometry(rect());
        s->raise();
    }

    void setCalcOverlay(QWidget *c) {
        m_calc = c;
        c->setParent(this);
        c->raise();
    }

    void setNotesOverlay(QWidget *n) {
        m_notes = n;
        n->setParent(this);
        n->raise();
    }

    void setMissionControl(QWidget *mc) {
        m_missionControl = mc;
        mc->setParent(this);
        mc->setGeometry(rect());
        mc->raise();
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
        QWidget::mouseMoveEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override {
        qDebug() << "[root] mousePress at" << e->position()
                 << "button:" << e->button();
        if (m_launcher && m_launcher->isVisible()) m_launcher->hideLauncher();
        if (m_notifications && m_notifications->isVisible())
            m_notifications->hideCenter();
        if (m_controlCenter && m_controlCenter->isVisible())
            m_controlCenter->hideCC();

        if (e->button() == Qt::RightButton) {
            const Theme &t = ThemeManager::instance().current();
            ApoMenu menu(this);
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
                    { "ruby",       "Magnific"             },
                    { "starfield",  "Starfield"            },
                    { "aurora",     "Aurora"               },
                    { "goldengate", "Golden Gate"          },
                    { "everest",    "Mount Everest"        },
                    { "apocalypse", "Apocalypse"           },
                    { "neonjinx",   "Neon Jinx"            },
                    { "mob",        "Mob (dev)"            },
                    { "vander",     "Vander Warwick (dev)" }
                };
                for (const auto &w : wps) {
                    QAction *wa = wallSub->addAction(w.name);
                    QString id = w.id;
                    QObject::connect(wa, &QAction::triggered,
                                     [this, id]() {
                        Wallpaper *nw = WallpaperConfig::makeById(id);
                        if (nw) {
                            setWallpaper(nw);
                            ThemeManager::instance().setTheme(nw->theme());
                            WallpaperConfig::saveId(id);
                        } else {
                            qWarning() << "[wallpaper] failed to load" << id;
                        }
                    });
                }
            }

            // ---- System ----
            QAction *aCalc     = menu.addAction("Calculator");
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
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--node" });
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

            QObject::connect(aCalc, &QAction::triggered, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--calc" });
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

    // Recursively find the focused con in a sway tree node.
    // Returns true and fills out when found.
    static bool findFocusedRect(const QJsonObject &node, QRect &out) {
        const QString type = node.value("type").toString();
        if (node.value("focused").toBool()
            && (type == "con" || type == "floating_con")) {
            QJsonObject r = node.value("rect").toObject();
            int w = r.value("width").toInt();
            int h = r.value("height").toInt();
            if (w > 0 && h > 0) {
                out = QRect(r.value("x").toInt(),
                            r.value("y").toInt(), w, h);
                return true;
            }
        }
        for (const QJsonValue &v : node.value("nodes").toArray()) {
            if (findFocusedRect(v.toObject(), out)) return true;
        }
        for (const QJsonValue &v :
             node.value("floating_nodes").toArray()) {
            if (findFocusedRect(v.toObject(), out)) return true;
        }
        return false;
    }

    void pollParallax() {
        static int s_tick = 0;
        ++s_tick;
        if (!VisualConfigManager::instance().cfg().wallpaperParallax)
            return;
        // Skip if the previous poll is still running.
        if (m_parallaxBusy) return;
        m_parallaxBusy = true;

        // Discover the sway IPC socket. Some environments don't
        // export SWAYSOCK into the shell's environment, and
        // swaymsg's auto-discovery does not always work.
        static QString s_swaySock;
        if (s_swaySock.isEmpty()) {
            s_swaySock = qEnvironmentVariable("SWAYSOCK");
            if (s_swaySock.isEmpty()) {
                QString xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
                if (xdg.isEmpty())
                    xdg = QString("/run/user/%1").arg(getuid());
                QDir d(xdg);
                // QDir::Files excludes Unix sockets, so include
                // QDir::System. sway-ipc sock is type 'srwx'.
                for (const QString &e :
                     d.entryList({ "sway-ipc.*.sock" },
                                 QDir::Files | QDir::System)) {
                    s_swaySock = d.filePath(e);
                    break;
                }
            }
            if (s_swaySock.isEmpty())
                qWarning() << "[parallax] no sway socket found";
        }

        auto *p = new QProcess(this);
        if (!s_swaySock.isEmpty()) {
            QProcessEnvironment env =
                QProcessEnvironment::systemEnvironment();
            env.insert("SWAYSOCK", s_swaySock);
            p->setProcessEnvironment(env);
        }
        QObject::connect(p,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, p](int code, QProcess::ExitStatus st) {
                QByteArray raw = p->readAllStandardOutput();
                QByteArray err = p->readAllStandardError();
                p->deleteLater();
                m_parallaxBusy = false;

                if (code != 0) {
                    static int s_err = 0;
                    if (++s_err % 20 == 1)
                        qWarning() << "[parallax] swaymsg err"
                                   << QString::fromUtf8(err.left(80));
                }
                QJsonDocument doc = QJsonDocument::fromJson(raw);
                if (!doc.isObject()) return;

                QRect wr;
                if (!findFocusedRect(doc.object(), wr)) {
                    m_parallaxTargetX = 0.0;
                    m_parallaxTargetY = 0.0;
                    return;
                }
                // Ignore tiny popups and anything covering the
                // whole screen (the shell root, maximized apps).
                if (wr.width()  < 200 || wr.height() < 200) return;
                if (wr.width() >= width() - 10) {
                    m_parallaxTargetX = 0.0;
                    m_parallaxTargetY = 0.0;
                    return;
                }

                const QPoint scrC(width() / 2, height() / 2);
                const QPoint winC = wr.center();
                const qreal dx = qreal(winC.x() - scrC.x()) / scrC.x();
                const qreal dy = qreal(winC.y() - scrC.y()) / scrC.y();
                const qreal MAX = 60.0;    // px ceiling — was 12, too subtle
                m_parallaxTargetX = -dx * MAX;
                m_parallaxTargetY = -dy * MAX;
            });
        p->start("swaymsg", { "-t", "get_tree", "-r" });
    }

    void tickParallax() {
        const qreal k = 0.15;
        qreal dx = m_parallaxTargetX - m_parallaxX;
        qreal dy = m_parallaxTargetY - m_parallaxY;
        if (qAbs(dx) < 0.05 && qAbs(dy) < 0.05) {
            if (m_parallaxX != m_parallaxTargetX
                || m_parallaxY != m_parallaxTargetY) {
                m_parallaxX = m_parallaxTargetX;
                m_parallaxY = m_parallaxTargetY;
                update();
            }
            return;
        }
        m_parallaxX += dx * k;
        m_parallaxY += dy * k;
        update();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        if (m_wallpaper) {
            // Oversize the paint rect by MAX so the parallax shift
            // never reveals an edge. Wallpaper subclasses scale
            // to whatever rect we hand them, so the cache remains
            // valid (the oversized size is constant).
            const int MAX = 80;
            QRect big = rect().adjusted(-MAX, -MAX, MAX, MAX);
            big.translate(int(m_parallaxX), int(m_parallaxY));
            m_wallpaper->paint(p, big);
        } else {
            p.fillRect(rect(), QColor(20, 4, 12));
        }

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
    QTimer             *m_parallaxPollTimer = nullptr;
    bool                m_parallaxBusy = false;
    QTimer             *m_parallaxSmoothTimer = nullptr;
    qreal               m_parallaxX = 0.0;
    qreal               m_parallaxY = 0.0;
    qreal               m_parallaxTargetX = 0.0;
    qreal               m_parallaxTargetY = 0.0;
    AppLauncher        *m_launcher = nullptr;
    NotificationCenter *m_notifications = nullptr;
    ControlCenter      *m_controlCenter = nullptr;
    SpotlightOverlay   *m_spotlight = nullptr;
    QWidget            *m_calc = nullptr;
    QWidget            *m_notes = nullptr;
    QWidget            *m_missionControl = nullptr;
    QTimer             *m_tickTimer = nullptr;
    QTimer             *m_blurTimer = nullptr;
    int                 m_blurTickCounter = 0;
};

class MissionControl : public QWidget {
public:
    explicit MissionControl(QWidget *parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });
        hide();
    }

    void toggle() { isVisible() ? hide() : show2(); }

    void show2() {
        // 1. Capture the screen BEFORE hiding windows
        refreshWindows();
        
        // 2. Force shell to fullscreen global to hide other windows
        runSway({"[app_id=\"apokolips-shell\"]", "fullscreen", "enable", "global"});
        
        // 3. Compute grid
        computeRects();
        if (parentWidget()) setGeometry(parentWidget()->rect());
        raise(); show(); setFocus();
        update();
    }

    void hideEvent(QHideEvent *e) override {
        runSway({"[app_id=\"apokolips-shell\"]", "fullscreen", "disable", "global"});
        QWidget::hideEvent(e);
    }

    void refreshWindows() {
        m_wins.clear();
        m_full = QImage();

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SWAYSOCK", g_swaySock);
        QDir rd(env.value("XDG_RUNTIME_DIR", "/run/user/1000"));
        for (const QString &f : rd.entryList({"wayland-*"}, QDir::Files | QDir::System)) {
            if (!f.endsWith(".lock")) { env.insert("WAYLAND_DISPLAY", f); break; }
        }

        QProcess grim;
        grim.setProcessEnvironment(env);
        grim.start("grim", {"-t", "ppm", "-"});
        if (grim.waitForFinished(1000)) {
            m_full = QImage::fromData(grim.readAllStandardOutput(), "PPM");
        }

        QProcess p;
        p.setProcessEnvironment(env);
        p.start("swaymsg", { "-t", "get_tree", "-r" });
        p.waitForFinished(500);
        QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
        if (doc.isObject()) {
            walk(doc.object());
        }
    }

    void walk(const QJsonObject &node) {
        QString app = node.value("app_id").toString();
        QJsonObject r = node.value("rect").toObject();
        int x = r.value("x").toInt();
        int y = r.value("y").toInt();
        int w = r.value("width").toInt();
        int h = r.value("height").toInt();
        QString name = node.value("name").toString();

        if (!app.isEmpty() && app != "apokolips-shell" && w > 100 && h > 100 && name != "Mission Control") {
            Win w1;
            w1.rect = QRect(x, y, w, h);
            w1.name = name;
            w1.appId = app;
            w1.conId = node.value("id").toInt();
            if (!m_full.isNull() && x >= 0 && y >= 0 &&
                x + w <= m_full.width() && y + h <= m_full.height()) {
                w1.thumb = QPixmap::fromImage(m_full.copy(x, y, w, h));
            }
            m_wins.append(w1);
        }

        for (const QJsonValue &v : node.value("nodes").toArray())
            walk(v.toObject());
        for (const QJsonValue &v : node.value("floating_nodes").toArray())
            walk(v.toObject());
    }

    void computeRects() {
        m_rects.clear();
        int n = m_wins.size();
        if (n == 0) return;

        int cols = int(std::ceil(std::sqrt(n)));
        int rows = (n + cols - 1) / cols;

        int marginX = 80;
        int marginY = 100;
        int usableW = width() - marginX * 2;
        int usableH = height() - marginY * 2 - 60;
        int cellW = usableW / cols;
        int cellH = usableH / rows;
        int pad = 20;

        for (int i = 0; i < n; ++i) {
            int r = i / cols;
            int c = i % cols;
            QRect cell(marginX + c * cellW, marginY + r * cellH, cellW, cellH);
            m_rects.append(cell.adjusted(pad, pad, -pad, -pad));
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        p.fillRect(rect(), QColor(0, 0, 0, 180));

        QFont tf = font();
        tf.setPixelSize(24);
        tf.setWeight(QFont::Light);
        p.setFont(tf);
        p.setPen(m_theme.textPrimary);
        p.drawText(QRect(0, 24, width(), 40), Qt::AlignCenter, "Mission Control");

        if (m_wins.isEmpty()) {
            QFont f = font(); f.setPixelSize(15);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(rect(), Qt::AlignCenter, "No windows open");
            return;
        }

        for (int i = 0; i < m_wins.size() && i < m_rects.size(); ++i) {
            const Win &w = m_wins[i];
            const QRect &cell = m_rects[i];
            bool hovered = (i == m_hovered);

            QPainterPath path;
            path.addRoundedRect(cell, 12, 12);
            p.fillPath(path, m_theme.panelBg);
            p.setPen(QPen(hovered ? m_theme.accent : m_theme.chromeBorder, hovered ? 2 : 1));
            p.drawPath(path);

            if (!w.thumb.isNull()) {
                QRect thumbRect = cell.adjusted(4, 4, -4, -28);
                QPixmap scaled = w.thumb.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                int dx = thumbRect.x() + (thumbRect.width() - scaled.width()) / 2;
                int dy = thumbRect.y() + (thumbRect.height() - scaled.height()) / 2;
                p.drawPixmap(dx, dy, scaled);
            }

            QFont f = font();
            f.setPixelSize(12);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect titleRect = cell.adjusted(10, cell.height() - 26, -10, -4);
            p.drawText(titleRect, Qt::AlignCenter, w.name.left(30));
        }
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { hide(); e->accept(); }
        else QWidget::keyPressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        int h = hitTest(e->position().toPoint());
        if (h != m_hovered) { m_hovered = h; update(); }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            int h = hitTest(e->position().toPoint());
            if (h >= 0 && h < m_wins.size()) {
                QString id = QString::number(m_wins[h].conId);
                // Focus the clicked window AFTER leaving fullscreen
                QProcess::startDetached("bash", {"-c",
                    "swaymsg '[app_id=\"apokolips-shell\"] fullscreen disable global'; "
                    "sleep 0.1; swaymsg '[con_id=" + id + "] focus'"});
                hide();
            } else {
                hide();
            }
        }
    }

private:
    struct Win {
        QRect   rect;
        QString name;
        QString appId;
        int     conId = 0;
        QPixmap thumb;
    };

    void runSway(const QStringList &args) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SWAYSOCK", g_swaySock);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start("swaymsg", args);
        p.waitForFinished(200);
    }

    int hitTest(const QPoint &pos) const {
        for (int i = m_rects.size() - 1; i >= 0; --i) {
            if (m_rects[i].contains(pos)) return i;
        }
        return -1;
    }

    QList<Win>   m_wins;
    QList<QRect> m_rects;
    QImage       m_full;
    int          m_hovered = -1;
    Theme        m_theme;
};



// --- Cleanup pass ---
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
        if (e->button() != Qt::LeftButton) return;
        const Theme &t = ThemeManager::instance().current();

        ApoMenu menu(this);
        menu.setStyleSheet(QString(
            "QMenu { background: rgb(24, 22, 30);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 12px; padding: 8px; color: #f0f0f5;"
            "  font-size: 13px; }"
            "QMenu::item { padding: 8px 24px 8px 14px;"
            "  border-radius: 7px; background: transparent; }"
            "QMenu::item:selected { background: rgba(255,255,255,45); }"
            "QMenu::separator { height: 1px;"
            "  background: rgba(255,255,255,30); margin: 6px 10px; }"));

        if (m_kind == Network) {
            QAction *hdr = menu.addAction(
                m_netKind == "wifi"     ? "Wi-Fi — Connected" :
                m_netKind == "ethernet" ? "Ethernet — Connected" :
                                          "Offline");
            hdr->setEnabled(false);
            menu.addSeparator();
            QAction *settings = menu.addAction("Open Network Settings");
            QObject::connect(settings, &QAction::triggered, []() {
                QProcess::startDetached("gnome-control-center", { "wifi" });
            });
        } else if (m_kind == Bluetooth) {
            QAction *hdr = menu.addAction(
                m_btOn ? "Bluetooth — On" : "Bluetooth — Off / None");
            hdr->setEnabled(false);
            menu.addSeparator();
            QAction *settings = menu.addAction("Open Bluetooth Settings");
            QObject::connect(settings, &QAction::triggered, []() {
                QProcess::startDetached("gnome-control-center",
                    { "bluetooth" });
            });
        } else {
            QString battStr = m_battPct >= 0
                ? QString("Battery — %1%").arg(m_battPct)
                : QString("Battery — AC Power");
            QAction *hdr = menu.addAction(battStr);
            hdr->setEnabled(false);
            menu.addSeparator();
            QAction *settings = menu.addAction("Open Power Settings");
            QObject::connect(settings, &QAction::triggered, []() {
                QProcess::startDetached("gnome-control-center",
                    { "power" });
            });
        }

        menu.exec(e->globalPosition().toPoint());
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

class BatteryPercentLabel : public QLabel {
public:
    BatteryPercentLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setMinimumWidth(38);
        ThemeManager::instance().subscribe([this](const Theme &t) {
            setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(t.textPrimary.name()));
        });
        m_timer = new QTimer(this);
        m_timer->setInterval(5000);
        QObject::connect(m_timer, &QTimer::timeout,
                         this, &BatteryPercentLabel::refresh);
        m_timer->start();
        refresh();
    }
    void refresh() {
        int pct = SysState::batteryPercent();
        if (pct < 0) { setText("AC"); }
        else         { setText(QString("%1%").arg(pct)); }
    }
private:
    QTimer *m_timer = nullptr;
};

#endif // APOKOLIPS_CHROME_H
