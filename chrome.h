#ifndef APOKOLIPS_CHROME_H
#define APOKOLIPS_CHROME_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "helpers.h"
#include "registry.h"
#include "weather.h"
#include "globals.h"

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
            // Clear history on theme change (simplest)
            for (QWidget *c : m_history) c->deleteLater();
            m_history.clear();
            if (m_historyHeader) m_historyHeader->setVisible(false);
            // Rebuild cards with new theme colors
            for (QWidget *c : m_cards) c->deleteLater();
            m_cards.clear();
            buildCards();
            updateThemeOnHeader();
        });
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
        QTimer::singleShot(ms, this, [this, guard, weOpened]() {
            if (!guard) return;
            m_cards.removeAll(guard);
            if (m_cardsLayout) m_cardsLayout->removeWidget(guard);
            guard->deleteLater();
            if (m_emptyLabel) m_emptyLabel->setVisible(m_cards.isEmpty());
            if (weOpened && m_cards.isEmpty()) hideCenter();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Soft left-side shadow — many thin rings
        p.setPen(Qt::NoPen);
        for (int i = 12; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.015 * m_slide);
            p.setBrush(sh);
            p.drawRoundedRect(rect().adjusted(-i - 2, -i, 0, i),
                              14 + i, 14 + i);
        }

        QRectF ncRect(rect());
        StyleRenderer::drawPanel(p, ncRect, 0, m_theme,
                                 int(230 * m_slide));
        QColor edge = m_theme.chromeBorder;
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
            "  border-left: 3px solid %2;"
            "  border-radius: 12px;"
            "}").arg(rgba(m_theme.chromeBg.lighter(110)),
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
            "color: %1; font-size: 11px; letter-spacing: 4px;"
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

    QPushButton *m_clearAllBtn = nullptr;
    QLabel      *m_historyHeader = nullptr;
    QVBoxLayout *m_historyLayout = nullptr;
    QWidget     *m_historyHolder = nullptr;
    QLabel      *m_emptyLabel = nullptr;
    QList<QWidget *> m_history;
};

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
    }

    void toggle() {
        auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.launcherEnabled) return;
        (isVisible() && m_opacity > 0.5) ? hideLauncher() : showLauncher();
    }
    void showLauncher() { raise(); show(); setFocus(); m_fadeTarget = 1.0; m_animTimer->start(); }
    void hideLauncher() { m_fadeTarget = 0.0; m_animTimer->start(); }

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

        // Build fresh columns
        const int COLS = 4;
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
            if (isInLauncher(QString::fromUtf8(e.id))) visible.append(e);

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

        QRect panelRect((width() - PANEL_W) / 2, 120, PANEL_W, PANEL_H);

        // ---- Soft edge: many thin rings = smooth fade ----
        // Skip while fading out fast — the ring pass is the most
        // expensive part of the frame.
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

        // ---- Heavy blur through the panel (glassy) ----
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        if (!bg.isNull()) {
            QPainterPath panelPath;
            panelPath.addRoundedRect(panelRect, 22, 22);
            p.save();
            p.setClipPath(panelPath);
            p.setOpacity(0.85 * m_opacity);
            p.drawPixmap(panelRect, bg, panelRect);
            p.restore();
        }

        // ---- Very light tint so tiles stay legible ----
        StyleRenderer::drawPanel(p, QRectF(panelRect), 22, m_theme,
                                 int(200 * m_opacity));
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
        m_tilesLayout->setContentsMargins(20, 20, 20, 20);

        // Pull from central registry — only entries flagged inLauncher.
        QVector<AppEntry> visible;
        for (const auto &e : appRegistry())
            if (isInLauncher(QString::fromUtf8(e.id))) visible.append(e);

        // 4 fixed-width columns, each a vertical stack.
        QList<QVBoxLayout *> cols;
        QHBoxLayout *gridRow = new QHBoxLayout;
        gridRow->setContentsMargins(0, 0, 0, 0);
        gridRow->setSpacing(14);
        for (int c = 0; c < 4; ++c) {
            QVBoxLayout *colLay = new QVBoxLayout;
            colLay->setContentsMargins(0, 0, 0, 0);
            colLay->setSpacing(14);
            colLay->setAlignment(Qt::AlignTop);
            cols.append(colLay);
            gridRow->addLayout(colLay);
        }

        const int appCount = visible.size();
        const int COLS = 4;
        for (int i = 0; i < appCount; ++i) {
            QWidget *tile = makeTile(QString::fromUtf8(visible[i].glyph),
                                     visible[i].label, i);
            cols[i % COLS]->addWidget(tile, 0,
                                      Qt::AlignTop | Qt::AlignHCenter);
        }
        m_gridColumns = cols;
        m_gridRow = gridRow;

        pl->addLayout(gridRow);
        pl->addStretch();

        outer->addWidget(panel, 0, Qt::AlignHCenter);
        outer->addStretch();
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
                if (m_name == "Glyph") {
                    drawGlyphMark(p, artRect);
                } else {
                    Icons::drawFor(m_name, p, artRect, m_color);
                }
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
            else
                args = { "--demo", appName, QString::number(offset) };
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
                onDesktop ? "Remove from Desktop" : "Add to Desktop");

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
    QList<QLabel *> m_iconLabels;
    QList<QLabel *> m_nameLabels;
    QList<QVBoxLayout *> m_gridColumns;
    QHBoxLayout *m_gridRow = nullptr;
};

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
        StyleManager::subscribe([this](VisualStyle) { update(); });
    }

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
        int y = (height() - visual) / 2 - 4 + m_bounceOffset;

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

        // App icon on top.
        Icons::drawFor(m_name, p, iconRect, m_theme.textPrimary);

        // Running indicator dot under the icon
        if (m_running) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_theme.accent);
            p.drawEllipse(QPoint(width() / 2, height() - 6), 3, 3);
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
        // Clear this app's notification badge when launched
        BadgeRegistry::clear(m_name);
        update();

        // Restore a matching Genie-minimized window instead of relaunching it.
        if (restoreGenieForDock(m_name))
            return;

        // If the app is already running, focus its window instead of
        // spawning a duplicate. Use swaymsg to raise the matching app_id.
        QString matchId;
        if (m_name == "Finder")         matchId = "apokolips-shell";
        else if (m_name == "Settings")  matchId = "apokolips-shell";
        else if (m_name == "BoomTube")  matchId = "apokolips-boombube";
        else if (m_name == "Aperture")  matchId = "apokolips-aperture";
        else if (m_name == "Node")      matchId = "apokolips-node";
        else if (m_name == "myMach")    matchId = "apokolips-mymach";
        else if (m_name == "Notes")     matchId = "apokolips-notes";
        else if (m_name == "Mail")      matchId = "thunderbird";
        else if (m_name == "Files")     matchId = "org.gnome.Nautilus";

        if (!matchId.isEmpty()) {
            // Bring a scratchpad-hidden window back first — "focus" alone
            // on a scratchpad window can report success without actually
            // surfacing it, which looked like "nothing happens" on click.
            QProcess::execute("swaymsg",
                { QString("[app_id=%1]").arg(matchId), "scratchpad", "show" });

            QProcess sway;
            sway.start("swaymsg",
                { QString("[app_id=%1]").arg(matchId), "focus" });
            sway.waitForFinished(300);
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
        } else if (m_name == "BoomTube") {
            program = QCoreApplication::applicationFilePath();
            args = { "--boomtube" };
        } else if (m_name == "Node") {
            program = QCoreApplication::applicationFilePath();
            args = { "--node" };
        } else if (m_name == "Notes") {
            program = QCoreApplication::applicationFilePath();
            args = { "--notes" };
        } else if (m_name == "Mail") {
            program = "thunderbird";
        } else {
            program = QCoreApplication::applicationFilePath();
            args = { "--demo", m_name, QString::number(m_index) };
        }

        qDebug() << "[dock] startDetached" << program << args;
        // (--from-dock removed: fresh launches no longer animate,
        //  minimised windows are restored via the reverse-genie path)

        qint64 launchedPid = 0;
        const bool ok = QProcess::startDetached(
            program, args, QString(), &launchedPid);
        setPid(ok ? launchedPid : 0);
        setRunning(ok && launchedPid > 0);
        qDebug() << "[dock] result:" << ok << "pid:" << m_pid;
    }

public:
    void handleClick(Qt::MouseButton btn, QPoint globalPos) {
        qDebug() << "[dockicon] handleClick" << m_name << "btn=" << btn;
        if (btn == Qt::LeftButton) {
            launchApp();
        } else if (btn == Qt::RightButton) {
            // Build right-click menu
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
        m_layout->setContentsMargins(18, 18, 18, 14);
        m_layout->setSpacing(4);

        m_timer = new QTimer(this);
        m_timer->setInterval(16);
        QObject::connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_timer->start();
        // (dock container swallows clicks — do NOT override mousePressEvent)

        // Separate timer to poll running state — cheaper than every frame
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(700);
        QObject::connect(m_pollTimer, &QTimer::timeout,
                         this, [this]() { pollRunning(); });
        m_pollTimer->start();
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

        static int dbg = 0;
        if (dbg++ % 10 == 0) {
            qInfo() << "[dots] tree bytes=" << raw.size()
                    << " matches=" << liveAppIds.size()
                    << " ids=" << liveAppIds.values()
                    << " icons=" << m_icons.size();
        }

        for (DockIcon *di : m_icons) {
            const QString appId = appIdForDockName(di->appName());
            bool alive = false;
            if (!appId.isEmpty() && liveAppIds.contains(appId))
                alive = true;
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
        return QString();
    }

    // Fallback title match for apps whose app_id may vary.
    static QString titleForDockName(const QString &name) {
        if (name == "Finder")     return "Files";
        if (name == "Settings")   return "Apokolips Settings";
        return QString();
    }

    void addIcon(const QString &icon, const QString &name, int index) {
        DockIcon *di = new DockIcon(icon, name, index, this);
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
    }
    void leaveEvent(QEvent *) override { m_hasCursor = false; }

private:
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

    QList<DockIcon *> m_icons;
    QBoxLayout  *m_layout = nullptr;
    bool         m_vertical = false;
    QTimer *m_timer = nullptr;
    QTimer *m_pollTimer = nullptr;
    qreal m_cursorX = 0;
    qreal m_cursorY = 0;
    bool  m_hasCursor = false;
    bool  m_debugOnce = false;
    Theme m_theme;
};

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

class ClipboardPanel : public QWidget {
public:
    ClipboardPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFixedSize(340, 420);
        hide();

        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(14, 14, 14, 14);
        outer->setSpacing(10);

        QLabel *title = new QLabel("Clipboard");
        title->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 15px; font-weight: 600;"
            "background: transparent;");
        outer->addWidget(title);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search clipboard…");
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
        QObject::connect(m_search, &QLineEdit::textChanged,
                         [this](const QString &s) {
            for (int i = 0; i < m_list->count(); ++i) {
                QListWidgetItem *it = m_list->item(i);
                bool hide = !s.isEmpty() &&
                    !it->text().contains(s, Qt::CaseInsensitive) &&
                    !it->data(Qt::UserRole).toString()
                        .contains(s, Qt::CaseInsensitive);
                it->setHidden(hide);
            }
        });
        outer->addWidget(m_search);

        m_list = new QListWidget;
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setStyleSheet(
            "QListWidget {"
            "  background: rgba(255,255,255,12);"
            "  border-radius: 10px;"
            "  color: rgb(240, 237, 232);"
            "  font-size: 12px;"
            "  padding: 4px;"
            "}"
            "QListWidget::item {"
            "  padding: 8px 10px;"
            "  border-radius: 6px;"
            "}"
            "QListWidget::item:hover { background: rgba(255,255,255,30); }"
            "QListWidget::item:selected {"
            "  background: rgba(168, 50, 50, 120);"
            "  color: #ffffff;"
            "}");
        outer->addWidget(m_list, 1);

        QObject::connect(m_list, &QListWidget::itemClicked,
                         [this](QListWidgetItem *it) {
            QString text = it->data(Qt::UserRole).toString();
            QProcess::startDetached("wl-copy", { text });
            hidePanel();
        });

        m_search->installEventFilter(this);

        // Poll the system clipboard for new entries
        m_poll = new QTimer(this);
        m_poll->setInterval(700);
        QObject::connect(m_poll, &QTimer::timeout,
                         this, &ClipboardPanel::pollClipboard);
        m_poll->start();
        pollClipboard();
    }

    void toggle() { isVisible() ? hidePanel() : showPanel(); }

    void showPanel() {
        QWidget *top = window();
        if (top) {
            QPoint c = top->mapToGlobal(QPoint(
                (top->width() - width()) / 2,
                (top->height() - height()) / 2));
            move(c);
        }
        raise();
        show();
        m_search->clear();
        m_search->setFocus();
    }

    void hidePanel() { hide(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().adjusted(0, 0, -1, -1);

        // Soft shadow
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
        if (ev->type() == QEvent::KeyPress) {
            QKeyEvent *k = static_cast<QKeyEvent *>(ev);
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                QListWidgetItem *it = m_list->currentItem();
                if (!it && m_list->count() > 0)
                    it = m_list->item(0);
                if (it) {
                    QProcess::startDetached("wl-copy",
                        { it->data(Qt::UserRole).toString() });
                    hidePanel();
                    return true;
                }
            }
            if (k->key() == Qt::Key_Escape) {
                hidePanel(); return true;
            }
        }
        if (o == m_search && ev->type() == QEvent::KeyPress) {
            QKeyEvent *k = static_cast<QKeyEvent *>(ev);
            if (k->key() == Qt::Key_Down) {
                m_list->setFocus();
                if (m_list->count() > 0) m_list->setCurrentRow(0);
                return true;
            }
        }
        return QWidget::eventFilter(o, ev);
    }

private:
    void pollClipboard() {
        QProcess p;
        p.start("wl-paste", { "-n" });
        p.waitForFinished(400);
        QString text = QString::fromUtf8(p.readAllStandardOutput());
        if (text.isEmpty() || text == m_last) return;
        m_last = text;

        // Skip if already in list
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == text)
                return;
        }

        QString preview = text;
        preview.replace("\n", " ⏎ ");
        if (preview.length() > 100) preview = preview.left(97) + "…";

        QListWidgetItem *it = new QListWidgetItem(preview);
        it->setData(Qt::UserRole, text);
        it->setToolTip(text.left(500));
        m_list->insertItem(0, it);

        while (m_list->count() > 20)
            delete m_list->takeItem(m_list->count() - 1);
    }

    QListWidget *m_list = nullptr;
    QLineEdit   *m_search = nullptr;
    QTimer      *m_poll = nullptr;
    QString      m_last;
};

class DesktopIcon : public QWidget {
public:
    DesktopIcon(const QString &id, const QString &label,
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

            const QString id = m_id;
            const bool inDock = isInDock(id);

            QAction *rmDesk = m.addAction("Remove from Desktop");
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
        if (!m_dragging && m_onActivate) m_onActivate();
        m_dragging = false;
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
};

class DesktopCanvas : public QWidget {
public:
    DesktopCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("desktopCanvas");
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
        DesktopIcon *icon = new DesktopIcon(id, label, paint,
                                             onActivate, this);
        icon->move(pos);
        icon->show();
        icon->raise();
        m_icons.append(icon);

        icon->onMoved = [this](const QString &iconId, const QPoint &p) {
            if (m_onMoved) m_onMoved(iconId, p);
        };
    }

    std::function<void(const QString&, const QPoint&)> m_onMoved;

    QList<DesktopIcon*> icons() const { return m_icons; }

    // Wallpaper switching hook — set by DesktopBackground at construction.
    // Lets the canvas menu call into the background's setWallpaper without
    // needing a class-order forward reference.
    static std::function<void(const QString &)> &wallpaperHook() {
        static std::function<void(const QString &)> fn;
        return fn;
    }

    void showCanvasMenu(const QPoint &pos) {
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
                auto &hook = DesktopCanvas::wallpaperHook();
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
        }
        else if (chosen == aNewFile) {
            QString base = QDir::homePath() + "/Desktop";
            QDir().mkpath(base);
            QString path = base + "/New Text File.txt";
            int n = 1;
            while (QFile::exists(path))
                path = base + QString("/New Text File %1.txt").arg(++n);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.close();
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
            [](VisualConfig &c) { c.desktopIconPos.clear(); });
        rebuildIcons();
    }

    void rebuildIcons() {
        for (DesktopIcon *ic : m_icons) ic->deleteLater();
        m_icons.clear();

        // Which apps are on the desktop
        QStringList ids;
        auto &cfg = VisualConfigManager::instance().cfg();
        if (cfg.desktopApps.isEmpty()) ids = { "mymach" };
        else                          ids = cfg.desktopApps;

        // Default grid — right column fills top-down, then wraps to
        // the next column leftward when the column runs out of space.
        // Uses SCREEN geometry, not our widget (which may be 0 during
        // the first layout pass).
        int screenW = 1200, screenH = 800;
        if (QScreen *s = QApplication::primaryScreen()) {
            screenW = s->availableGeometry().width();
            screenH = s->availableGeometry().height();
        }
        const int iconH    = 116;   // slot step
        const int iconW    = 116;
        const int startX   = screenW - 130;
        const int startY   = 40;
        const int usableH  = screenH - startY - 110;   // leave room for dock
        const int rowsMax  = qMax(1, usableH / iconH);

        int slot = 0;
        for (const QString &id : ids) {
            if (id.isEmpty()) continue;

            int col = slot / rowsMax;
            int row = slot % rowsMax;
            int defX = startX - col * iconW;
            int defY = startY + row * iconH;

            QPoint pos;
            if (cfg.desktopIconPos.contains(id)) {
                QVariantMap mp = cfg.desktopIconPos[id].toMap();
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
                    QProcess::startDetached(
                        QCoreApplication::applicationFilePath(),
                        desktopLaunchArgs(capId));
                }, pos);
            if (m_icons.size() > before) {
                DesktopIcon *added = m_icons.last();
                added->m_onTidyUp = [this]() { tidyUp(); };
            }
            slot++;
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
        for (DesktopIcon *ic : m_icons) {
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
    QList<DesktopIcon*> m_icons;
};

class DesktopWidgets : public QWidget {
public:
    explicit DesktopWidgets(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("desktopWidgets");
        setAttribute(Qt::WA_NoSystemBackground, true);
        // NOTE: do NOT set WA_TransparentForMouseEvents — we need mouse
        // events for hover magnify. The dock conflict is handled in the
        // dock's own widget instead.
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

        m_weather = new Weather::Fetcher(this);
        auto doFetch = [this]() {
            m_weather->fetch([this](const Weather::Snapshot &s) {
                m_wx = s;
                update();
            });
        };
        doFetch();
        QTimer *wxTimer = new QTimer(this);
        wxTimer->setInterval(10 * 60 * 1000);
        QObject::connect(wxTimer, &QTimer::timeout, this, doFetch);
        wxTimer->start();

        // Hover magnify: track mouse, animate at 60 Hz
        setMouseTracking(true);
        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setInterval(16);
        QObject::connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            bool needsRepaint = false;
            for (int i = 0; i < 4; ++i) {
                double target = (i == m_hoverCard) ? 1.04 : 1.0;
                double diff   = target - m_scales[i];
                if (qAbs(diff) > 0.001) {
                    m_scales[i] += diff * 0.22;
                    needsRepaint = true;
                }
            }
            if (needsRepaint) update();
        });
        m_hoverTimer->start();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = e->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            e->accept();
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging) {
            QPoint parentPos = mapToParent(
                e->position().toPoint() - m_dragOffset);
            move(parentPos);
            m_userMoved = true;
            e->accept();
            return;
        }
        handleHoverAt(e->position().toPoint());
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_dragging) {
            m_dragging = false;
            setCursor(Qt::ArrowCursor);
            e->accept();
        }
        QWidget::mouseReleaseEvent(e);
    }

    // Public: allow parent to forward hover coords
public:
    void handleHoverAt(const QPoint &localPos) {
        int idx = cardAt(localPos);
        if (idx != m_hoverCard) {
            m_hoverCard = idx;
            update();
        }
    }
protected:

    void leaveEvent(QEvent *e) override {
        if (m_hoverCard != -1) {
            m_hoverCard = -1;
            update();
        }
        QWidget::leaveEvent(e);
    }

public:

    void showEvent(QShowEvent *e) override {
        qDebug() << "[widgets] SHOW event";
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override {
        qDebug() << "[widgets] HIDE event";
        QWidget::hideEvent(e);
    }

    // dockPos: 0 = bottom, 1 = left, 2 = right.
    // Passed explicitly so this never races the config file write.
    void repositionTo(const QSize &parentSize, int dockPos = -1) {
        const int CARD_W = 240;
        setFixedWidth(CARD_W);
        setFixedHeight(qMin(parentSize.height() - 130, 560));

        if (dockPos < 0)
            dockPos = VisualConfigManager::instance()
                          .cfg().dockPosition;   // fallback

        int x = parentSize.width() - CARD_W - 380;
        if (dockPos == 1) {
            // Dock left — widgets on the right.
            x = parentSize.width() - CARD_W - 60;
        } else if (dockPos == 2) {
            // Dock right — widgets on the left.
            x = 60;
        }
        // dockPos == 0 (bottom): keep the default right-side offset.

        if (m_userMoved) return;   // honor manual drag position
        move(x, 50);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int y = 0;

        // --- Clock card ---
        drawCardScaled(p, 0, QRect(0, y, width(), 96));
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
        drawCardScaled(p, 1, QRect(0, y, width(), 132));
        {
            int r = 34;
            int gap = 32;
            int totalW = 4*r + gap;            // 4 radiuses + gap
            int leftPad = (width() - totalW) / 2;
            int cy = y + 56;
            int cx1 = leftPad + r;
            int cx2 = leftPad + 3*r + gap;

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
        drawCardScaled(p, 2, QRect(0, y, width(), 92));
        {
            QFont f = font(); f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(14, y + 10, width()-28, 14),
                       Qt::AlignLeft, "WEATHER  ·  KAMPALA");

            f.setPixelSize(30); f.setWeight(QFont::Light);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            p.drawText(QRect(14, y + 28, 80, 48),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(int(m_wx.tempC + 0.5)) + "°");

            f.setPixelSize(11); f.setWeight(QFont::Normal);
            p.setFont(f);
            int infoX = 92;
            int infoW = width() - infoX - 14;
            p.setPen(m_theme.textSecondary);
            p.drawText(QRect(infoX, y + 30, infoW, 16),
                       Qt::AlignLeft, m_wx.condition);
            p.setPen(m_theme.textDim);
            p.drawText(QRect(infoX, y + 48, infoW, 16),
                       Qt::AlignLeft,
                       QString("H: %1°  L: %2°")
                           .arg(int(m_wx.tempMax + 0.5))
                           .arg(int(m_wx.tempMin + 0.5)));
            p.drawText(QRect(infoX, y + 64, infoW, 16),
                       Qt::AlignLeft,
                       QString("Wind %1 km/h")
                           .arg(int(m_wx.windKmh + 0.5)));
            p.drawText(QRect(infoX, y + 64, infoW, 16),
                       Qt::AlignRight,
                       QString("HUM %1%").arg(m_wx.humidityPct));
        }
        y += 92 + 12;

        // --- Mini calendar ---
        drawCardScaled(p, 3, QRect(0, y, width(), 130));
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
    void drawCardScaled(QPainter &p, int idx, const QRect &r) {
        double s = m_scales[idx];
        if (qAbs(s - 1.0) < 0.002) {
            drawCard(p, r);
            return;
        }

        // Pivot at card center
        QPointF ctr = r.center();
        p.save();
        p.translate(ctr);
        p.scale(s, s);
        p.translate(-ctr);

        // Subtle glow behind hovered card
        if (s > 1.005) {
            QColor glow = m_theme.textPrimary;
            glow.setAlpha(int(20 * (s - 1.0) / 0.04));
            p.setPen(Qt::NoPen);
            for (int i = 8; i >= 1; --i) {
                p.setBrush(glow);
                p.drawRoundedRect(r.adjusted(-i, -i, i, i),
                                  14 + i, 14 + i);
            }
        }

        drawCard(p, r);
        p.restore();
    }

    void drawCard(QPainter &p, const QRect &r) {
        // Soft outer shadow
        QColor shadowBase = m_theme.textPrimary;
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = shadowBase;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(r.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // Blur backdrop
        const auto &vc = VisualConfigManager::instance().cfg();
        const QPixmap &bg = ThemeManager::instance().blurredBg();
        QPainterPath path;
        path.addRoundedRect(r, 14, 14);
        if (!bg.isNull() && window() && vc.blurStrength > 0.001) {
            p.save();
            p.setClipPath(path);
            QPoint pos = mapTo(window(), QPoint(0, 0));
            p.setOpacity(vc.blurStrength);
            p.drawPixmap(r, bg, QRect(pos + r.topLeft(), r.size()));
            p.restore();
        }

        // Style-aware panel
        StyleRenderer::drawPanel(p, QRectF(r), 14, m_theme,
                                 vc.widgetCardAlpha);
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
    Weather::Fetcher  *m_weather = nullptr;
    Weather::Snapshot  m_wx;

    // Drag state
    bool   m_dragging   = false;
    bool   m_userMoved  = false;
    QPoint m_dragOffset;

    // ---- Hover magnify ----
    QTimer *m_hoverTimer = nullptr;
    int    m_hoverCard = -1;          // -1 = none
    double m_scales[4] = {1.0, 1.0, 1.0, 1.0};

    // Card Y bands (top-left positions of each card)
    QRect cardRect(int idx) const {
        switch (idx) {
            case 0: return QRect(0,   0, width(),  96);
            case 1: return QRect(0, 108, width(), 128);
            case 2: return QRect(0, 248, width(),  92);
            case 3: return QRect(0, 352, width(), 130);
        }
        return {};
    }

    int cardAt(const QPoint &pos) const {
        for (int i = 0; i < 4; ++i) {
            if (cardRect(i).contains(pos)) return i;
        }
        return -1;
    }
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

class DesktopBackground : public QWidget {
public:
    explicit DesktopBackground(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);

        // Install wallpaper-switch hook so DesktopCanvas's menu can
        // trigger a wallpaper change without needing a class-order
        // forward reference.
        DesktopCanvas::wallpaperHook() = [this](const QString &id) {
            Wallpaper *nw = WallpaperConfig::makeById(id);
            if (!nw) return;
            setWallpaper(nw);
            ThemeManager::instance().setTheme(nw->theme());
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
        if (m_spotlight)     m_spotlight->raise();
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
        // Forward hover to widgets so they can magnify
        if (m_widgets) {
            QPoint local = m_widgets->mapFrom(this,
                                              e->position().toPoint());
            m_widgets->handleHoverAt(local);
        }
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
                        setWallpaper(nw);
                        ThemeManager::instance().setTheme(nw->theme());
                        WallpaperConfig::saveId(id);
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

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        if (m_wallpaper) m_wallpaper->paint(p, rect());
        else             p.fillRect(rect(), QColor(20, 4, 12));

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
    AppLauncher        *m_launcher = nullptr;
    NotificationCenter *m_notifications = nullptr;
    ControlCenter      *m_controlCenter = nullptr;
    DesktopWidgets     *m_widgets = nullptr;
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
        refreshWindows();
        if (parentWidget()) setGeometry(parentWidget()->rect());
        raise(); show(); setFocus();
        update();
    }

    void refreshWindows() {
        m_wins.clear();
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SWAYSOCK", g_swaySock);
        p.setProcessEnvironment(env);
        p.start("swaymsg", { "-t", "get_tree", "-r" });
        p.waitForFinished(500);
        QByteArray out = p.readAllStandardOutput();
        QJsonDocument doc = QJsonDocument::fromJson(out);
        if (!doc.isObject()) return;
        QJsonObject root = doc.object();
        walk(root, 0, 0);
    }

    void walk(const QJsonObject &node, int ox, int oy) {
        QString app = node.value("app_id").toString();
        QJsonObject r = node.value("rect").toObject();
        int x = r.value("x").toInt() + ox;
        int y = r.value("y").toInt() + oy;
        int w = r.value("width").toInt();
        int h = r.value("height").toInt();
        QString name = node.value("name").toString();

        // Only include windows that look like apps (exclude the shell, __i3)
        if (!app.isEmpty() && app != "apokolips-shell" && w > 100 && h > 100) {
            Win w1;
            w1.rect = QRect(x, y, w, h);
            w1.name = name;
            w1.appId = app;
            w1.conId = node.value("id").toInt();
            m_wins.append(w1);
        }

        for (const QJsonValue &v : node.value("nodes").toArray())
            walk(v.toObject(), x, y);
        for (const QJsonValue &v : node.value("floating_nodes").toArray())
            walk(v.toObject(), x, y);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Dim backdrop
        p.fillRect(rect(), QColor(0, 0, 0, 180));

        // Title
        QFont tf = font();
        tf.setPixelSize(24);
        tf.setWeight(QFont::Light);
        p.setFont(tf);
        p.setPen(m_theme.textPrimary);
        p.drawText(QRect(0, 24, width(), 40), Qt::AlignCenter,
                   "Mission Control");

        // Empty hint
        if (m_wins.isEmpty()) {
            QFont f = font(); f.setPixelSize(15);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            p.drawText(rect(), Qt::AlignCenter,
                       "No windows open");
            return;
        }

        // Draw each window as a card
        for (int i = 0; i < m_wins.size(); ++i) {
            const Win &w = m_wins[i];
            QRect r = w.rect;
            // Scale down a bit so they fit nicer
            r = QRect(r.x() + 40, r.y() + 80,
                      qMin(r.width(),  width()  - 120),
                      qMin(r.height(), height() - 160));

            bool hovered = (i == m_hovered);
            QColor card = m_theme.panelBg;
            card.setAlpha(hovered ? 235 : 200);
            QColor border = hovered ? m_theme.textPrimary
                                    : m_theme.chromeBorder;

            QPainterPath path;
            path.addRoundedRect(r, 12, 12);
            p.fillPath(path, card);
            p.setPen(QPen(border, hovered ? 2 : 1));
            p.drawPath(path);

            // Title text inside the card
            QFont f = font();
            f.setPixelSize(14);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect titleRect = r.adjusted(12, 8, -12, -r.height() + 32);
            p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                       w.name.left(40));

            QFont sf = font();
            sf.setPixelSize(10);
            p.setFont(sf);
            p.setPen(m_theme.textDim);
            QRect subRect = r.adjusted(12, 30, -12, -r.height() + 52);
            p.drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                       w.appId);
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
            if (h >= 0) {
                QString id = QString::number(m_wins[h].conId);
                QProcess::startDetached("swaymsg",
                    { QString("[con_id=%1]").arg(id), "focus" });
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
    };

    int hitTest(const QPoint &pos) const {
        for (int i = m_wins.size() - 1; i >= 0; --i) {
            QRect r = m_wins[i].rect;
            r = QRect(r.x() + 40, r.y() + 80,
                      qMin(r.width(),  width()  - 120),
                      qMin(r.height(), height() - 160));
            if (r.contains(pos)) return i;
        }
        return -1;
    }

    QList<Win> m_wins;
    int        m_hovered = -1;
    Theme      m_theme;
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

        QMenu menu(this);
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
