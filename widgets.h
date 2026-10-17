#ifndef APOKOLIPS_WIDGETS_H
#define APOKOLIPS_WIDGETS_H

// Widget framework — cards as direct children of the desktop root.
// No fullscreen layer. No top-level windows. Standard Qt.

#include "shell.h"
#include "theme.h"
#include "style.h"
#include "visualconfig.h"
#include "weather.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QRegularExpression>
#include <sys/statvfs.h>

class WidgetCard : public QWidget {
public:
    WidgetCard(const QString &id, const QSize &size,
               const QPoint &defaultPos, QWidget *parent = nullptr)
        : QWidget(parent), m_id(id), m_size(size),
          m_defaultPos(defaultPos)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setCursor(Qt::OpenHandCursor);
        setContextMenuPolicy(Qt::DefaultContextMenu);
        setFixedSize(size);

        m_theme = ThemeManager::instance().current();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            update();
        });
    }

    QString id() const { return m_id; }

    // Screen-space rect of this card. Phase 3 (The Pool) uses
    // this to hit-test icons against every widget obstacle.
    QRect globalRect() const {
        QWidget *par = parentWidget();
        if (!par) return QRect(pos(), size());
        QPoint tl = par->mapToGlobal(pos());
        return QRect(tl, size());
    }

    // Safe area = screen minus the dock's reserved strip.
    // Keeps widgets from overlapping the dock when it moves.
    static QRect safeAreaForDock(const QSize &screen) {
        constexpr int dockExtent    = 96;
        constexpr int bottomReserve = dockExtent + 14;
        constexpr int sideReserve   = dockExtent + 14 + 8;
        constexpr int topReserve    = 34;   // top bar height + padding
        QRect area(0, 0, screen.width(), screen.height());
        // Reserve the top bar strip for every dock position — the
        // top bar is always at the top of the screen.
        area.setTop(area.top() + topReserve);
        int pos = VisualConfigManager::instance().cfg().dockPosition;
        if (pos == 0)      area.setHeight(area.height() - bottomReserve);
        else if (pos == 1) area.setLeft(area.left() + sideReserve);
        else if (pos == 2) area.setWidth(area.width() - sideReserve);
        return area;
    }

    // Auto-arrange: place every visible card in a horizontal row
    // that fits the current screen, wrapping to a second row if
    // needed. Called on first install (no saved positions) and by
    // the Reset button. No hardcoded coordinates, works on any
    // resolution.
    static void autoArrange(const QList<WidgetCard *> &cards,
                            const QSize &screen) {
        if (cards.isEmpty() || screen.width() <= 0) return;
        const QRect safe = safeAreaForDock(screen);
        const int gap     = 20;
        const int rowPad  = 24;
        const int startX  = safe.left()  + rowPad;
        const int startY  = safe.top()   + rowPad;
        int x = startX;
        int y = startY;
        int rowH = 0;
        for (WidgetCard *c : cards) {
            if (!c || !c->isVisible()) continue;
            int w = c->width();
            int h = c->height();
            // Wrap if this card would poke past the safe area.
            if (x + w > safe.right() - rowPad && x > startX) {
                x = startX;
                y += rowH + gap;
                rowH = 0;
            }
            c->move(x, y);
            x += w + gap;
            rowH = qMax(rowH, h);

            // Persist
            QString id = c->id();
            QPoint np = c->pos();
            VisualConfigManager::instance().setAndSave(
                [id, np](VisualConfig &c) {
                    QVariantMap m;
                    m["x"] = np.x(); m["y"] = np.y();
                    c.widgetPositions[id] = m;
                });
        }
    }

    // Nudge this card back inside the safe area if the dock has
    // moved into its current position. Persists the adjusted
    // position so the fix survives a restart.
    void clampToSafeArea(const QSize &screen) {
        if (!isVisible() && parentWidget() == nullptr) return;
        const QRect safe = safeAreaForDock(screen);
        if (safe.isEmpty()) return;
        QPoint p = pos();
        int nx = qBound(safe.left(),
                        p.x(),
                        qMax(safe.left(), safe.right() - width() + 1));
        int ny = qBound(safe.top(),
                        p.y(),
                        qMax(safe.top(), safe.bottom() - height() + 1));
        if (nx == p.x() && ny == p.y()) return;
        move(nx, ny);
        // persist
        QString id = m_id;
        QPoint np(nx, ny);
        VisualConfigManager::instance().setAndSave(
            [id, np](VisualConfig &c) {
                QVariantMap m = c.widgetPositions.value(id).toMap();
                m["x"] = np.x(); m["y"] = np.y();
                c.widgetPositions[id] = m;
            });
        qInfo() << "[widgets] clamped" << m_id << "->" << np;
    }

    void applyVisibility() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        bool master = cfg.widgetsVisible;
        bool perW = cfg.widgetVisibility.value(m_id, true).toBool();
        bool target = master && perW;
        qInfo() << "[applyVis]" << m_id
                << "master=" << master
                << "perWidget=" << perW
                << "-> visible=" << target;
        setVisible(target);
    }

    void placeAtSavedOr(QPoint fallback = QPoint()) {
        if (fallback.isNull()) fallback = m_defaultPos;
        const auto &cfg = VisualConfigManager::instance().cfg();
        QVariant v = cfg.widgetPositions.value(m_id);
        if (v.canConvert<QVariantMap>()) {
            QVariantMap m = v.toMap();
            QPoint p(m.value("x").toInt(), m.value("y").toInt());
            move(p);
        } else {
            move(fallback);
        }
    }

    // ---- Variation system ----
    // Each widget instance has a variant string stored in config.
    // Subclasses declare their options via variants(). Default
    // variant is the first item in that list.
    QString variant() const {
        const auto &cfg = VisualConfigManager::instance().cfg();
        QString v = cfg.widgetVariants.value(m_id).toString();
        if (v.isEmpty()) {
            QStringList list = variants();
            v = list.isEmpty() ? QString() : list.first();
        }
        return v;
    }

    virtual QStringList variants() const { return {}; }
    virtual QString variantLabel(const QString &v) const { return v; }

    virtual void setVariant(const QString &v) {
        QString id = m_id;
        VisualConfigManager::instance().setAndSave(
            [id, v](VisualConfig &c) {
                c.widgetVariants[id] = v;
            });
        qInfo() << "[widgets]" << m_id << "variant ->" << v;
        update();
    }

protected:
    virtual void paintContent(QPainter &p, const QRect &content) = 0;

    // Default: solid black. Override in a subclass to give the
    // card its own fill (Weather uses a sky-blue gradient).
    virtual void paintPanelBackground(QPainter &p, const QRectF &r,
                                      const QPainterPath &path,
                                      int alpha) {
        p.fillPath(path, QColor(0, 0, 0, alpha));
    }

    void paintEvent(QPaintEvent *) override {
        const auto &cfg = VisualConfigManager::instance().cfg();
        const int R = qBound(8,  cfg.widgetRadius,       40);
        const int rimPeak = qBound(0, cfg.widgetRimIntensity, 200);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        // Soft outer shadow
        p.setPen(Qt::NoPen);
        for (int i = 6; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.04 * (i / 6.0));
            p.setBrush(sh);
            p.drawRoundedRect(r.adjusted(-i, -i + 3, i, i + 3),
                              R + i, R + i);
        }

        int alpha = qBound(0, cfg.widgetOpacity, 255);

        QPainterPath panelPath;
        panelPath.addRoundedRect(r, R, R);
        paintPanelBackground(p, r, panelPath, alpha);

        // Diagonal gradient rim — brightness from config.
        if (alpha > 30 && rimPeak > 0) {
            int peak = int(rimPeak * alpha / 255);
            QLinearGradient rimGrad(r.topLeft(), r.bottomRight());
            rimGrad.setColorAt(0.00, QColor(255, 255, 255, peak));
            rimGrad.setColorAt(0.35,
                QColor(255, 255, 255, int(peak * 0.40)));
            rimGrad.setColorAt(0.75,
                QColor(255, 255, 255, int(peak * 0.10)));
            rimGrad.setColorAt(1.00, QColor(255, 255, 255, 0));
            p.setPen(QPen(QBrush(rimGrad), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(panelPath);

            QRectF innerR = r.adjusted(3, 3, -3, -3);
            int innerR2 = qMax(4, R - 3);
            QPainterPath innerPath;
            innerPath.addRoundedRect(innerR, innerR2, innerR2);
            QLinearGradient innerGrad(innerR.topLeft(),
                                      innerR.bottomRight());
            innerGrad.setColorAt(0.0,
                QColor(255, 255, 255, int(peak * 0.25)));
            innerGrad.setColorAt(0.6,
                QColor(255, 255, 255, 0));
            p.setPen(QPen(QBrush(innerGrad), 1.0));
            p.drawPath(innerPath);
        }

        const int pad = 18;
        QRect content = rect().adjusted(pad, pad, -pad, -pad);
        paintContent(p, content);
    }

    void mousePressEvent(QMouseEvent *e) override {
        qInfo() << "[widget] press on" << m_id << "at" << e->position()
                << "button=" << e->button();
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = e->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            raise();
            e->accept();
            return;
        }
        if (e->button() == Qt::RightButton) {
            // Absorb right-click so it never reaches the shell
            // root's own mousePressEvent (which would show the
            // desktop context menu at the same time).
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging && parentWidget()) {
            QPoint p = mapToParent(e->position().toPoint() - m_dragOffset);
            int maxX = qMax(0, parentWidget()->width()  - width());
            int maxY = qMax(0, parentWidget()->height() - height());
            p.setX(qBound(0, p.x(), maxX));
            p.setY(qBound(0, p.y(), maxY));

            const auto &cfg = VisualConfigManager::instance().cfg();
            if (cfg.widgetSnapToGrid) {
                int g = qMax(10, cfg.widgetGridSize);
                p.setX((p.x() + g / 2) / g * g);
                p.setY((p.y() + g / 2) / g * g);
            }
            move(p);
            e->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_dragging) {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
            QPoint p = pos();
            QString id = m_id;
            VisualConfigManager::instance().setAndSave(
                [id, p](VisualConfig &c) {
                    QVariantMap m = c.widgetPositions.value(id).toMap();
                    m["x"] = p.x();
                    m["y"] = p.y();
                    c.widgetPositions[id] = m;
                });
            qInfo() << "[widgets] saved position for" << id
                    << "->" << p;
            e->accept();
        }
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        QMenu m(this);
        m.setStyleSheet(
            "QMenu { background: rgb(24,22,30);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 10px; padding: 6px;"
            "  color: rgb(240,237,232); font-size: 13px; }"
            "QMenu::item { padding: 7px 20px 7px 16px;"
            "  border-radius: 6px; }"
            "QMenu::item:selected {"
            "  background: rgba(168,50,50,90); }");

        QAction *resetAct = m.addAction("Reset position");
        QAction *hideAct  = m.addAction("Hide widget");

        // Style submenu — populated from the subclass's variants().
        QHash<QAction *, QString> variantMap;
        QStringList opts = variants();
        if (!opts.isEmpty()) {
            m.addSeparator();
            QMenu *styleMenu = m.addMenu("Style");
            QString cur = variant();
            for (const QString &v : opts) {
                QAction *a = styleMenu->addAction(variantLabel(v));
                a->setCheckable(true);
                a->setChecked(v == cur);
                variantMap.insert(a, v);
            }
        }

        QAction *chosen = m.exec(e->globalPos());
        if (variantMap.contains(chosen)) {
            setVariant(variantMap.value(chosen));
            return;
        }
        if (chosen == resetAct) {
            move(m_defaultPos);
            QPoint p = pos();
            QString id = m_id;
            VisualConfigManager::instance().setAndSave(
                [id, p](VisualConfig &c) {
                    QVariantMap mm;
                    mm["x"] = p.x(); mm["y"] = p.y();
                    c.widgetPositions[id] = mm;
                });
        } else if (chosen == hideAct) {
            hide();
        }
    }

    QString m_id;
    QSize   m_size;
    QPoint  m_defaultPos;
    bool    m_dragging = false;
    QPoint  m_dragOffset;
    Theme   m_theme;
};

class ClockCard : public WidgetCard {
public:
    explicit ClockCard(QWidget *parent)
        : WidgetCard("clock", QSize(260, 290),
                     QPoint(40, 60), parent)
    {
        // Apply the stored variant's size before first paint.
        QString v0 = variant();
        if (v0 == "world")        setFixedSize(400, 165);
        else if (v0 == "digital") setFixedSize(260, 220);
        else                      setFixedSize(260, 290);
        m_tick = new QTimer(this);
        m_tick->setInterval(1000);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { update(); });
        m_tick->start();
    }

    QStringList variants() const override {
        return { "analog", "minimal", "digital", "world" };
    }
    QString variantLabel(const QString &v) const override {
        if (v == "analog")  return "Analog";
        if (v == "minimal") return "Minimal";
        if (v == "digital") return "Digital";
        if (v == "world")   return "World clock";
        return v;
    }

    void setVariant(const QString &v) override {
        // Set size FIRST so any paint triggered downstream sees
        // the new geometry.
        if (v == "world")        setFixedSize(400, 165);
        else if (v == "digital") setFixedSize(260, 220);
        else                     setFixedSize(260, 290);
        WidgetCard::setVariant(v);
        qInfo() << "[clockcard] variant" << v
                << "size ->" << size();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        const QString v = variant();
        const bool analogish = (v == "analog" || v == "minimal");

        const QString city  = m_city.isEmpty() ? "Local" : m_city;
        const QString when  = m_offsetH == 0 && m_offsetM == 0
                                ? QString()
                                : (m_offsetH >= 0 && m_offsetH < 12
                                    ? "Today" : "Tomorrow");

        if (v == "world") {
            // Four mini analog clocks in a row, each with a city
            // label and, if not local, an hour offset underneath.
            struct City { const char *name; int offsetH; };
            static const City cities[] = {
                { "Kampala",   0 },
                { "London",   -1 },
                { "New York", -7 },
                { "Tokyo",     6 },
            };
            const int n = 4;
            const int cellW = r.width() / n;
            const int faceD = qMin(cellW - 12, r.height() - 50);
            const int faceTop = r.top() + 4;

            for (int i = 0; i < n; ++i) {
                const int cx = r.left() + cellW * i + cellW / 2;
                const QPointF c(cx, faceTop + faceD / 2.0);
                const qreal R = faceD / 2.0 - 2;

                paintAnalogAt(p, c, R, true,
                              cities[i].offsetH);

                QFont f = font();
                f.setPixelSize(11); f.setWeight(QFont::DemiBold);
                p.setFont(f);
                p.setPen(m_theme.textPrimary);
                QRect nRect(cx - cellW / 2,
                            faceTop + faceD + 4,
                            cellW, 14);
                p.drawText(nRect, Qt::AlignCenter,
                           cities[i].name);

                if (cities[i].offsetH != 0) {
                    f.setPixelSize(9); f.setWeight(QFont::Normal);
                    p.setFont(f);
                    p.setPen(m_theme.textDim);
                    QRect oRect(cx - cellW / 2,
                                faceTop + faceD + 18,
                                cellW, 12);
                    QString off = cities[i].offsetH > 0
                        ? QString("+%1 HRS").arg(cities[i].offsetH)
                        : QString("%1 HRS").arg(cities[i].offsetH);
                    p.drawText(oRect, Qt::AlignCenter, off);
                }
            }
            return;
        }

        if (analogish) {
            const int faceD = qMin(r.width() - 16, r.height() - 88);
            const QPointF c(r.center().x(),
                            r.top() + faceD / 2 + 4);
            const qreal R = faceD / 2.0 - 2;
            paintAnalog(p, c, R, v == "analog");

            // City below the face
            QFont f = font(); f.setPixelSize(13); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect cityRect(r.left(),
                           int(c.y() + R) + 8,
                           r.width(), 18);
            p.drawText(cityRect, Qt::AlignCenter, city);

            if (!when.isEmpty()) {
                f.setPixelSize(10); f.setWeight(QFont::Normal);
                p.setFont(f);
                p.setPen(m_theme.textDim);
                QRect whenRect(r.left(),
                               int(c.y() + R) + 26,
                               r.width(), 14);
                p.drawText(whenRect, Qt::AlignCenter, when);
            }
        } else {  // digital
            QDateTime now = QDateTime::currentDateTime();
            QString hhmm = now.toString("H:mm");

            QFont f = font();
            f.setPixelSize(qMin(r.width() / 3, r.height() / 3));
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect tRect = r;
            tRect.setHeight(r.height() * 65 / 100);
            p.drawText(tRect, Qt::AlignCenter, hhmm);

            f.setPixelSize(12); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            QRect cRect(r.left(), tRect.bottom(),
                        r.width(), 16);
            p.drawText(cRect, Qt::AlignCenter, city);

            f.setPixelSize(10);
            p.setFont(f);
            QRect aRect(r.left(), cRect.bottom(),
                        r.width(), 14);
            p.drawText(aRect, Qt::AlignCenter,
                       now.toString("dddd, d MMMM"));
        }
    }

private:
    void paintAnalog(QPainter &p, QPointF c, qreal R,
                     bool withNumbers) {
        paintAnalogAt(p, c, R, withNumbers, 0);
    }

    void paintAnalogAt(QPainter &p, QPointF c, qreal R,
                       bool withNumbers, int offsetH) {
        // Face plate — soft off-white
        QColor face(240, 240, 242, 235);
        p.setPen(Qt::NoPen);
        p.setBrush(face);
        p.drawEllipse(c, R, R);

        // Outline ring — dark
        p.setPen(QPen(QColor(30, 30, 34), 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, R, R);

        // Tick marks (always) + numbers (analog only)
        QColor tick(60, 60, 66);
        QColor num(20, 20, 24);
        for (int i = 0; i < 12; ++i) {
            const qreal ang = (i * 30.0) * 3.14159265 / 180.0;
            const qreal sinA = std::sin(ang);
            const qreal cosA = std::cos(ang);
            QPointF outer(c.x() + R * 0.86 * sinA,
                          c.y() - R * 0.86 * cosA);
            QPointF inner(c.x() + R * 0.76 * sinA,
                          c.y() - R * 0.76 * cosA);
            p.setPen(QPen(tick, 1.4));
            p.drawLine(inner, outer);

            if (withNumbers) {
                const QString numStr = QString::number(
                    i == 0 ? 12 : i);
                QFont nf = font();
                nf.setPixelSize(int(R * 0.25));
                nf.setWeight(QFont::DemiBold);
                p.setFont(nf);
                p.setPen(num);
                QPointF tp(c.x() + R * 0.64 * sinA,
                           c.y() - R * 0.64 * cosA);
                QRectF tr(tp.x() - R * 0.20,
                          tp.y() - R * 0.16,
                          R * 0.40, R * 0.32);
                p.drawText(tr, Qt::AlignCenter, numStr);
            }
        }

        // Time hands — offsetH shifts the displayed hour.
        QTime now = QTime::currentTime();
        const qreal minuteAng = (now.minute()
            + now.second() / 60.0) * 6.0;
        int dispHour = (now.hour() + offsetH) % 24;
        if (dispHour < 0) dispHour += 24;
        const qreal hourAng = (dispHour % 12
            + now.minute() / 60.0) * 30.0;

        auto handAngle = [&](qreal deg) {
            const qreal a = deg * 3.14159265 / 180.0;
            return QPointF(std::sin(a), -std::cos(a));
        };

        // Hour hand — dark, thick
        p.setPen(QPen(QColor(20, 20, 24), qMax(2.6, R * 0.065),
                      Qt::SolidLine, Qt::RoundCap));
        QPointF hv = handAngle(hourAng);
        p.drawLine(c, c + QPointF(hv.x() * R * 0.52,
                                  hv.y() * R * 0.52));

        // Minute hand — dark, thinner, longer
        p.setPen(QPen(QColor(20, 20, 24), qMax(2.0, R * 0.05),
                      Qt::SolidLine, Qt::RoundCap));
        QPointF mv = handAngle(minuteAng);
        p.drawLine(c, c + QPointF(mv.x() * R * 0.80,
                                  mv.y() * R * 0.80));

        // Second hand — orange
        const qreal secondAng = now.second() * 6.0;
        p.setPen(QPen(QColor(220, 130, 30), 1.6,
                      Qt::SolidLine, Qt::RoundCap));
        QPointF sv = handAngle(secondAng);
        p.drawLine(c, c + QPointF(sv.x() * R * 0.90,
                                  sv.y() * R * 0.90));

        // Center dot
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24));
        p.drawEllipse(c, qMax(1.8, R * 0.05),
                      qMax(1.8, R * 0.05));
    }

    QTimer *m_tick = nullptr;
    QString m_city;
    int     m_offsetH = 0;
    int     m_offsetM = 0;
};


// ----------------------------------------------------------------
// RingsCard — four circular gauges: CPU / RAM / NET / DISK.
// Matches macOS Tahoe widget style: dark card, rings with green
// arcs, icon inside each ring, value + label underneath.
// ----------------------------------------------------------------
class RingsCard : public WidgetCard {
public:
    explicit RingsCard(QWidget *parent)
        : WidgetCard("rings", QSize(360, 160),
                     QPoint(480, 60), parent)
    {
        m_tick = new QTimer(this);
        m_tick->setInterval(1500);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { refresh(); update(); });
        m_tick->start();
        refresh();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        const int n = 4;
        const int ringD = 66;               // outer diameter
        const qreal ringStroke = 4.5;
        const int gap = qMax(6, (r.width() - n * ringD) / (n - 1));
        const int cy = r.top() + ringD / 2 + 6;
        const QColor green(80, 210, 120);
        const QColor track(255, 255, 255, 35);

        double vals[4] = { m_cpu, m_ram, m_net, m_disk };
        QString labels[4] = { "CPU", "RAM", "NET", "DISK" };

        for (int i = 0; i < n; ++i) {
            const int cx = r.left() + ringD/2 + i * (ringD + gap);
            const QPointF c(cx, cy);
            const qreal R = ringD / 2.0 - ringStroke / 2.0;

            // Track
            p.setPen(QPen(track, ringStroke, Qt::SolidLine,
                          Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, R, R);

            // Fill arc (clockwise from 12 o'clock)
            if (vals[i] > 0.5) {
                p.setPen(QPen(green, ringStroke, Qt::SolidLine,
                              Qt::RoundCap));
                int span = int(-qBound(0.0, vals[i], 100.0)
                               / 100.0 * 360.0 * 16.0);
                QRectF arcRect(c.x() - R, c.y() - R, R * 2, R * 2);
                p.drawArc(arcRect, 90 * 16, span);
            }

            // Icon inside ring
            drawRingIcon(p, i, c, 20, m_theme.textSecondary);

            // Big value under the ring
            QFont f = font();
            f.setPixelSize(14); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect vRect(cx - ringD/2, cy + ringD/2 + 6,
                        ringD, 20);
            p.drawText(vRect, Qt::AlignCenter,
                       QString("%1%").arg(int(vals[i] + 0.5)));

            // Small label
            f.setPixelSize(10); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            QRect lRect(cx - ringD/2, cy + ringD/2 + 26,
                        ringD, 14);
            p.drawText(lRect, Qt::AlignCenter, labels[i]);
        }
    }

private:
    // Icon style: 0=CPU chip, 1=RAM stick, 2=network arrows,
    // 3=disk circle. Small, monochrome, matches the ring's tone.
    void drawRingIcon(QPainter &p, int type, QPointF c,
                      qreal sz, QColor col) {
        p.setPen(QPen(col, 1.4));
        p.setBrush(Qt::NoBrush);
        const qreal h = sz / 2.0;
        switch (type) {
        case 0: {   // CPU chip — square with pins
            QRectF r(c.x() - h + 2, c.y() - h + 2,
                     sz - 4, sz - 4);
            p.drawRect(r);
            for (int i = 0; i < 3; ++i) {
                qreal off = r.left() + (i + 1) * r.width() / 4;
                p.drawLine(QPointF(off, r.top() - 2),
                           QPointF(off, r.top()));
                p.drawLine(QPointF(off, r.bottom()),
                           QPointF(off, r.bottom() + 2));
            }
            break;
        }
        case 1: {   // RAM stick — rectangle with vertical lines
            QRectF r(c.x() - h, c.y() - h * 0.6,
                     sz, sz * 0.6);
            p.drawRect(r);
            for (int i = 1; i < 4; ++i) {
                qreal off = r.left() + i * r.width() / 4;
                p.drawLine(QPointF(off, r.top() + 2),
                           QPointF(off, r.bottom() - 2));
            }
            break;
        }
        case 2: {   // network — up arrow + down arrow
            qreal dx = h * 0.5;
            p.drawLine(QPointF(c.x() - dx, c.y() + h),
                       QPointF(c.x() - dx, c.y() - h));
            p.drawLine(QPointF(c.x() - dx, c.y() - h),
                       QPointF(c.x() - dx - 3, c.y() - h + 4));
            p.drawLine(QPointF(c.x() - dx, c.y() - h),
                       QPointF(c.x() - dx + 3, c.y() - h + 4));
            p.drawLine(QPointF(c.x() + dx, c.y() - h),
                       QPointF(c.x() + dx, c.y() + h));
            p.drawLine(QPointF(c.x() + dx, c.y() + h),
                       QPointF(c.x() + dx - 3, c.y() + h - 4));
            p.drawLine(QPointF(c.x() + dx, c.y() + h),
                       QPointF(c.x() + dx + 3, c.y() + h - 4));
            break;
        }
        case 3: {   // disk — circle with a dot
            p.drawEllipse(c, h - 1, h - 1);
            p.setBrush(col);
            p.drawEllipse(c, 1.8, 1.8);
            break;
        }
        }
    }

    void refresh() {
        m_cpu  = readCpu();
        m_ram  = readRam();
        m_net  = readNet();
        m_disk = readDisk();
    }

    static double readCpu() {
        QFile f("/proc/stat");
        if (!f.open(QIODevice::ReadOnly)) return 0;
        QByteArray line = f.readLine();
        f.close();
        auto parts = line.trimmed().split(' ');
        quint64 total = 0, idle = 0;
        int field = 0;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok; quint64 v = parts[i].toULongLong(&ok);
            if (!ok) break;
            total += v;
            if (field == 3) idle = v;   // idle is 4th after "cpu"
            ++field;
        }
        static quint64 prevTotal = 0, prevIdle = 0;
        quint64 dT = total - prevTotal;
        quint64 dI = idle - prevIdle;
        prevTotal = total; prevIdle = idle;
        if (dT == 0) return 0;
        return 100.0 * (dT - dI) / double(dT);
    }

    static double readRam() {
        QFile f("/proc/meminfo");
        if (!f.open(QIODevice::ReadOnly)) return 0;
        quint64 total = 0, avail = 0;
        while (!f.atEnd()) {
            QByteArray line = f.readLine();
            if (line.startsWith("MemTotal:"))
                total = line.split(':')[1].trimmed()
                            .split(' ')[0].toULongLong();
            else if (line.startsWith("MemAvailable:"))
                avail = line.split(':')[1].trimmed()
                            .split(' ')[0].toULongLong();
        }
        if (total == 0) return 0;
        return 100.0 * (total - avail) / double(total);
    }

    static double readNet() {
        QFile f("/proc/net/dev");
        if (!f.open(QIODevice::ReadOnly)) return 0;
        quint64 rxTotal = 0, txTotal = 0;
        while (!f.atEnd()) {
            QByteArray line = f.readLine();
            int colon = line.indexOf(':');
            if (colon < 0) continue;
            QString iface = QString::fromUtf8(
                line.left(colon)).trimmed();
            if (iface == "lo") continue;
            QString rest = QString::fromUtf8(
                line.mid(colon + 1)).trimmed();
            auto parts = rest.split(
                QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 9) {
                rxTotal += parts[0].toULongLong();
                txTotal += parts[8].toULongLong();
            }
        }
        static quint64 prevRx = 0, prevTx = 0;
        static qint64  prevT  = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        quint64 dRx = prevRx ? (rxTotal - prevRx) : 0;
        quint64 dTx = prevTx ? (txTotal - prevTx) : 0;
        prevRx = rxTotal; prevTx = txTotal;
        if (prevT == 0) { prevT = now; return 0; }
        double dt = qMax(0.001, (now - prevT) / 1000.0);
        prevT = now;
        double bps = (dRx + dTx) / dt;
        // 10 MB/s == 100%
        return qBound(0.0, 100.0 * bps / (10.0 * 1024.0 * 1024.0),
                      100.0);
    }

    static double readDisk() {
        struct statvfs st;
        if (statvfs("/", &st) != 0) return 0;
        quint64 total = quint64(st.f_blocks) * st.f_frsize;
        quint64 avail = quint64(st.f_bavail) * st.f_frsize;
        if (total == 0) return 0;
        return 100.0 * (total - avail) / double(total);
    }

    QTimer *m_tick = nullptr;
    double m_cpu = 0, m_ram = 0, m_net = 0, m_disk = 0;
};


// ----------------------------------------------------------------
// StatsCard — macOS Tahoe hybrid card.
// Left: big disk ring + "Apokolips" + available + timestamp.
// Right: two stacked blocks — Memory, Battery. Divider between.
// ----------------------------------------------------------------
class StatsCard : public WidgetCard {
public:
    explicit StatsCard(QWidget *parent)
        : WidgetCard("stats", QSize(360, 215),
                     QPoint(1260, 60), parent)
    {
        m_tick = new QTimer(this);
        m_tick->setInterval(2000);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { refresh(); update(); });
        m_tick->start();
        refresh();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        const int leftW = 160;
        const QRect leftR(r.left(), r.top(),
                          leftW, r.height());
        const QRect rightR(r.left() + leftW + 18, r.top(),
                           r.width() - leftW - 18, r.height());

        // ---- Left column — big disk ring ----
        {
            const int ringD = 106;
            const QPointF c(leftR.center().x(),
                            leftR.top() + ringD / 2 + 6);
            const qreal R = ringD / 2.0 - 6;
            const qreal stroke = 6.0;

            // Track
            p.setPen(QPen(QColor(255, 255, 255, 40), stroke,
                          Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, R, R);

            // Fill arc — neutral white for disk
            if (m_diskPct > 0.5) {
                p.setPen(QPen(QColor(230, 232, 236), stroke,
                              Qt::SolidLine, Qt::RoundCap));
                int span = int(-qBound(0.0, m_diskPct, 100.0)
                               / 100.0 * 360.0 * 16.0);
                QRectF arcRect(c.x() - R, c.y() - R,
                               R * 2, R * 2);
                p.drawArc(arcRect, 90 * 16, span);
            }

            // Percent inside
            QFont f = font();
            f.setPixelSize(24); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(240, 237, 232));
            QRect pr(int(c.x() - R), int(c.y() - 14),
                     int(R * 2), 28);
            p.drawText(pr, Qt::AlignCenter,
                       QString("%1%").arg(int(m_diskPct + 0.5)));

            // Disk name
            f.setPixelSize(13); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(240, 237, 232));
            QRect nR(leftR.left(), int(c.y() + R) + 8,
                     leftR.width(), 18);
            p.drawText(nR, Qt::AlignCenter, "Apokolips");

            // Available GB
            f.setPixelSize(10); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(QColor(176, 173, 168));
            QRect aR(leftR.left(), nR.bottom(), leftR.width(), 14);
            p.drawText(aR, Qt::AlignCenter,
                       QString("%1 GB available").arg(m_diskFreeGb, 0, 'f', 2));

            // Timestamp
            f.setPixelSize(9);
            p.setFont(f);
            p.setPen(QColor(122, 120, 115));
            QRect tR(leftR.left(), aR.bottom() + 2,
                     leftR.width(), 12);
            p.drawText(tR, Qt::AlignCenter, QString::fromUtf8(
                "\xE2\x86\xBB now"));
        }

        // ---- Divider ----
        {
            int dx = leftR.right() + 9;
            p.setPen(QPen(QColor(255, 255, 255, 22), 1));
            p.drawLine(dx, r.top() + 12, dx, r.bottom() - 12);
        }

        // ---- Right column — two blocks ----
        const int blockH = rightR.height() / 2;
        QRect memBlock(rightR.left(), rightR.top(),
                       rightR.width(), blockH - 4);
        QRect batBlock(rightR.left(), rightR.top() + blockH + 4,
                       rightR.width(), blockH - 4);

        paintBlock(p, memBlock, 0,
                   QString("%1 GB").arg(m_memFreeGb, 0, 'f', 2),
                   "MEMORY",
                   m_memFreeGb > 0.05 ? "available" : "low");
        paintBlock(p, batBlock, 1,
                   QString("%1 %").arg(int(m_batPct + 0.5)),
                   "BATTERY",
                   m_batPresent ? (m_batCharging ? "AC Power"
                                                 : "On battery")
                                : "AC Power");
    }

private:
    // type: 0 = memory, 1 = battery.
    void paintBlock(QPainter &p, const QRect &b, int type,
                    const QString &value, const QString &tag,
                    const QString &sub) {
        const int iconSize = 26;
        QRect iconR(b.left(), b.top() + 4, iconSize, iconSize);
        paintBlockIcon(p, type, iconR);

        // Tag badge (small uppercase label under the icon)
        QFont f = font();
        f.setPixelSize(8); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(220, 220, 220));
        QRect tagR(iconR.left() - 4, iconR.bottom() + 3,
                   iconSize + 8, 12);
        p.drawText(tagR, Qt::AlignCenter, tag);

        // Value + sub to the right of the icon
        int textX = iconR.right() + 12;
        int textW = b.right() - textX;

        f.setPixelSize(18); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 237, 232));
        QRect vR(textX, b.top() + 4, textW, 22);
        p.drawText(vR, Qt::AlignLeft | Qt::AlignVCenter, value);

        f.setPixelSize(10); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(150, 150, 155));
        QRect sR(textX, vR.bottom() + 2, textW, 14);
        p.drawText(sR, Qt::AlignLeft | Qt::AlignVCenter, sub);
    }

    void paintBlockIcon(QPainter &p, int type, const QRect &r) {
        QColor col(230, 230, 235);
        if (type == 0) {
            // Memory chip — rectangle with pins
            QRectF body(r.left() + 4, r.top() + 6,
                        r.width() - 8, r.height() - 12);
            p.setPen(QPen(col, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRect(body);
            for (int i = 1; i < 4; ++i) {
                qreal off = body.left() + i * body.width() / 4;
                p.drawLine(QPointF(off, body.top() - 3),
                           QPointF(off, body.top()));
                p.drawLine(QPointF(off, body.bottom()),
                           QPointF(off, body.bottom() + 3));
            }
        } else {
            // Battery — rounded rect + tip + fill
            QRectF body(r.left() + 4, r.top() + 5,
                        r.width() - 10, r.height() - 10);
            QRectF tip(body.right() + 1,
                       body.center().y() - 3, 3, 6);
            p.setPen(QPen(col, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(body, 3, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawRoundedRect(tip, 1, 1);
            QRectF fill = body.adjusted(2, 2, -2, -2);
            p.drawRoundedRect(fill, 2, 2);
        }
    }

    void refresh() {
        // Disk
        struct statvfs st;
        if (statvfs("/", &st) == 0) {
            quint64 total = quint64(st.f_blocks) * st.f_frsize;
            quint64 avail = quint64(st.f_bavail) * st.f_frsize;
            if (total > 0) {
                m_diskPct   = 100.0 * (total - avail) / double(total);
                m_diskFreeGb = avail / (1024.0 * 1024.0 * 1024.0);
            }
        }
        // Memory
        QFile mf("/proc/meminfo");
        if (mf.open(QIODevice::ReadOnly)) {
            quint64 avail = 0;
            while (!mf.atEnd()) {
                QByteArray line = mf.readLine();
                if (line.startsWith("MemAvailable:")) {
                    avail = line.split(':')[1].trimmed()
                                .split(' ')[0].toULongLong();
                    break;
                }
            }
            m_memFreeGb = avail / (1024.0 * 1024.0);
        }
        // Battery via /sys (present on VMs as BAT0 sometimes)
        QFile bf("/sys/class/power_supply/BAT0/capacity");
        QFile bs("/sys/class/power_supply/BAT0/status");
        if (bf.exists() && bf.open(QIODevice::ReadOnly)) {
            m_batPct = bf.readAll().trimmed().toInt();
            m_batPresent = true;
        } else {
            m_batPct = 100;
            m_batPresent = false;
        }
        if (bs.open(QIODevice::ReadOnly)) {
            QString st = QString::fromUtf8(bs.readAll()).trimmed();
            m_batCharging = (st == "Charging" || st == "Full");
        }
    }

    QTimer *m_tick = nullptr;
    double  m_diskPct    = 0;
    double  m_diskFreeGb = 0;
    double  m_memFreeGb  = 0;
    int     m_batPct     = 100;
    bool    m_batPresent = false;
    bool    m_batCharging = false;
};

class NowPlayingCard : public WidgetCard {
public:
    explicit NowPlayingCard(QWidget *parent)
        : WidgetCard("nowplaying", QSize(360, 170),
                     QPoint(60, 400), parent)
    {
        m_tick = new QTimer(this);
        m_tick->setInterval(500);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { refresh(); update(); });
        m_tick->start();

        const QString btnCss =
            "QPushButton {"
            "  background: rgba(255,255,255,22);"
            "  border: 1px solid rgba(255,255,255,35);"
            "  border-radius: 8px;"
            "  color: rgb(240,237,232);"
            "  font-size: 13px;"
            "  padding: 0;"
            "}"
            "QPushButton:hover { background: rgba(255,255,255,50); }"
            "QPushButton:pressed { background: rgba(168,50,80,160); }";

        m_prevBtn = new QPushButton(
            QString::fromUtf8("\xE2\x8F\xAE"), this);
        m_playBtn = new QPushButton(
            QString::fromUtf8("\xE2\x96\xB6"), this);
        m_nextBtn = new QPushButton(
            QString::fromUtf8("\xE2\x8F\xAD"), this);

        for (QPushButton *b : { m_prevBtn, m_playBtn, m_nextBtn }) {
            b->setStyleSheet(btnCss);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedSize(36, 30);
        }

        // Absolute-position the three buttons, right-aligned.
        // Card is 360x170. Content pad is 18.
        m_prevBtn->move(216, 96);
        m_playBtn->move(258, 96);
        m_nextBtn->move(300, 96);

        QObject::connect(m_prevBtn, &QPushButton::clicked,
                         [this]() { sendCmd("prev"); });
        QObject::connect(m_playBtn, &QPushButton::clicked,
                         [this]() { sendCmd("toggle"); });
        QObject::connect(m_nextBtn, &QPushButton::clicked,
                         [this]() { sendCmd("next"); });

        refresh();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        // ---- Album art tile (left) ----
        QRectF art(r.left(), r.top() + 4, 96, 96);
        QPainterPath artPath;
        artPath.addRoundedRect(art, 12, 12);
        QLinearGradient g(art.topLeft(), art.bottomRight());
        g.setColorAt(0.0, QColor(168, 50, 80));
        g.setColorAt(1.0, QColor(80, 26, 46));
        p.fillPath(artPath, g);

        QFont gf = font();
        gf.setPixelSize(40);
        p.setFont(gf);
        p.setPen(QColor(240, 237, 232, 230));
        p.drawText(art, Qt::AlignCenter,
                   QString::fromUtf8("\xE2\x99\xAB"));

        // ---- Title + state + time (right) ----
        const int tx = int(art.right()) + 16;
        const int tw = r.right() - tx;

        QFont f = font();
        f.setPixelSize(15); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 237, 232));
        const QString title = m_title.isEmpty()
            ? QString("Nothing playing") : m_title;
        QRect titleR(tx, r.top() + 6, tw, 44);
        p.drawText(titleR,
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   title);

        f.setPixelSize(11); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(176, 173, 168));
        QString st = m_state == "playing" ? "Playing"
                   : m_state == "paused"  ? "Paused"
                                          : "Stopped";
        QString t  = fmt(m_posMs) + " / " + fmt(m_durMs);
        QRect infoR(tx, r.top() + 56, tw, 16);
        p.drawText(infoR, Qt::AlignLeft | Qt::AlignVCenter,
                   st + "  \xC2\xB7  " + t);

        // ---- Progress bar (bottom) ----
        QRectF bar(r.left() + 2, r.bottom() - 10,
                   r.width() - 4, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawRoundedRect(bar, 1.5, 1.5);
        if (m_durMs > 0) {
            qreal pct = qBound(0.0,
                double(m_posMs) / double(m_durMs), 1.0);
            QRectF fill(bar.left(), bar.top(),
                        bar.width() * pct, bar.height());
            p.setBrush(QColor(168, 50, 80));
            p.drawRoundedRect(fill, 1.5, 1.5);
        }
    }

private:
    QTimer      *m_tick    = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QString      m_title;
    QString      m_path;
    QString      m_state;
    qint64       m_posMs = 0;
    qint64       m_durMs = 0;

    static QString fmt(qint64 ms) {
        if (ms < 0) ms = 0;
        qint64 s = ms / 1000;
        return QString("%1:%2").arg(s / 60)
                               .arg(s % 60, 2, 10, QChar('0'));
    }

    void refresh() {
        const QString path = QDir::tempPath()
                             + "/apokolips-boombube-state.json";
        QFileInfo fi(path);
        if (!fi.exists()
            || fi.lastModified().msecsTo(
                   QDateTime::currentDateTime()) > 3000) {
            // BoomTube isn't running or hasn't published recently.
            m_title.clear();
            m_path.clear();
            m_state = "stopped";
            m_posMs = m_durMs = 0;
            m_playBtn->setText(QString::fromUtf8("\xE2\x96\xB6"));
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!d.isObject()) return;
        QJsonObject o = d.object();
        m_title = o.value("title").toString();
        m_path  = o.value("path").toString();
        m_state = o.value("state").toString();
        m_posMs = (qint64)o.value("posMs").toDouble();
        m_durMs = (qint64)o.value("durMs").toDouble();

        m_playBtn->setText(m_state == "playing"
            ? QString::fromUtf8("\xE2\x8F\xB8")
            : QString::fromUtf8("\xE2\x96\xB6"));
    }

    void sendCmd(const QString &cmd) {
        const QString dst = QDir::tempPath()
                            + "/apokolips-boombube-cmd.json";
        const QString tmp = dst + ".tmp";
        QJsonObject o;
        o["cmd"] = cmd;
        o["ts"]  = double(QDateTime::currentMSecsSinceEpoch());
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(o).toJson());
        f.close();
        QFile::remove(dst);
        QFile::rename(tmp, dst);
    }
};


// ----------------------------------------------------------------
// WeatherCard — current conditions + high/low + wind + humidity.
// Data comes from Weather::Fetcher (already used by the old
// DesktopWidgets column).
// ----------------------------------------------------------------
class WeatherCard : public WidgetCard {
public:
    explicit WeatherCard(QWidget *parent)
        : WidgetCard("weather", QSize(280, 170),
                     QPoint(880, 60), parent)
    {
        if (variant() == "full")         setFixedSize(320, 240);
        else if (variant() == "compact") setFixedSize(340, 150);
        else if (variant() == "week")    setFixedSize(340, 180);
        else                             setFixedSize(280, 170);

        m_weather = new Weather::Fetcher(this);
        auto fetch = [this]() {
            m_weather->fetch([this](const Weather::Snapshot &s) {
                m_wx = s;
                update();
            });
        };
        fetch();
        m_tick = new QTimer(this);
        m_tick->setInterval(10 * 60 * 1000);
        QObject::connect(m_tick, &QTimer::timeout, this, fetch);
        m_tick->start();
    }

    QStringList variants() const override {
        return { "full", "compact", "sunset", "conditions", "week" };
    }
    QString variantLabel(const QString &v) const override {
        if (v == "full")       return "Full";
        if (v == "compact")    return "Compact";
        if (v == "sunset")     return "Sunset";
        if (v == "conditions") return "Conditions";
        if (v == "week")       return "5-Day";
        return v;
    }

    void setVariant(const QString &v) override {
        if (v == "full")         setFixedSize(320, 240);
        else if (v == "compact") setFixedSize(340, 150);
        else if (v == "week")    setFixedSize(340, 180);
        else                     setFixedSize(280, 170);
        WidgetCard::setVariant(v);
    }

protected:
    // Blue sky gradient — sky blue at top, deeper blue at bottom.
    void paintPanelBackground(QPainter &p, const QRectF &r,
                              const QPainterPath &path,
                              int alpha) override {
        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        QColor top( 92, 160, 226, alpha);
        QColor bot( 30,  84, 156, alpha);
        grad.setColorAt(0.0, top);
        grad.setColorAt(1.0, bot);
        p.fillPath(path, grad);
    }

    void paintContent(QPainter &p, const QRect &r) override {
        const QString v = variant();
        if      (v == "sunset")   paintSunset(p, r);
        else if (v == "week")     paintWeek(p, r);
        else if (v == "full")     paintFull(p, r);
        else if (v == "compact")  paintCompact(p, r);
        else                      paintConditions(p, r);
    }

private:
    void paintConditions(QPainter &p, const QRect &r) {
        QFont f = font();

        // City header
        f.setPixelSize(13); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 245, 255));
        p.drawText(QRect(r.left(), r.top(), r.width(), 18),
                   Qt::AlignLeft, m_wx.city);

        // Big temp
        f.setPixelSize(52); f.setWeight(QFont::Light);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        QRect tR(r.left(), r.top() + 22, 108, 64);
        p.drawText(tR, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(int(m_wx.tempC + 0.5))
                   + QString::fromUtf8("\xC2\xB0"));

        // Right block
        int ix = r.left() + 98;
        int iw = r.width() - 98;

        f.setPixelSize(12); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(230, 235, 245));
        p.drawText(QRect(ix, r.top() + 22, iw, 18), Qt::AlignLeft,
                   m_wx.condition.isEmpty() ? "—" : m_wx.condition);

        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor(200, 215, 235));

        p.drawText(QRect(ix, r.top() + 38, iw, 14), Qt::AlignLeft,
                   QString("H: %1\xC2\xB0   L: %2\xC2\xB0")
                       .arg(int(m_wx.tempMax + 0.5))
                       .arg(int(m_wx.tempMin + 0.5)));

        p.drawText(QRect(ix, r.top() + 54, iw, 14), Qt::AlignLeft,
                   QString("Wind %1 km/h")
                       .arg(int(m_wx.windKmh + 0.5)));

        p.drawText(QRect(ix, r.top() + 70, iw, 14), Qt::AlignLeft,
                   QString("Humidity %1%").arg(m_wx.humidityPct));
    }

    void paintCompact(QPainter &p, const QRect &r) {
        QFont f = font();

        // Header — city + condition on one line
        f.setPixelSize(13); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 245, 255));
        p.drawText(QRect(r.left(), r.top(), r.width() / 2, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, m_wx.city);

        f.setPixelSize(11); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(220, 228, 240));
        p.drawText(QRect(r.left() + r.width() / 2, r.top(),
                         r.width() / 2, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   m_wx.condition.isEmpty() ? "—" : m_wx.condition);

        // Big temp on the left
        f.setPixelSize(44); f.setWeight(QFont::Light);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        QRect tR(r.left(), r.top() + 20, 110, 62);
        p.drawText(tR, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(int(m_wx.tempC + 0.5))
                   + QString::fromUtf8("\xC2\xB0"));

        // H/L on the right of temp
        f.setPixelSize(10); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(200, 215, 235));
        QRect hlR(r.left() + 110, r.top() + 32,
                  r.width() - 110, 14);
        p.drawText(hlR, Qt::AlignLeft,
                   QString("H:%1\xC2\xB0  L:%2\xC2\xB0")
                       .arg(int(m_wx.tempMax + 0.5))
                       .arg(int(m_wx.tempMin + 0.5)));

        // Hourly row across the bottom — 6 slots
        int rowTop = r.top() + 88;
        int cellW = r.width() / 6;
        for (int i = 0; i < 6 && i < m_wx.hourlyTemps.size(); ++i) {
            int cx = r.left() + cellW * i + cellW / 2;
            QString t = QString("%1\xC2\xB0")
                .arg(int(m_wx.hourlyTemps[i] + 0.5));
            f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(240, 245, 255));
            p.drawText(QRect(cx - cellW / 2, rowTop, cellW, 16),
                       Qt::AlignCenter, t);
        }
    }

    void paintFull(QPainter &p, const QRect &r) {
        QFont f = font();
        f.setPixelSize(13); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 245, 255));
        p.drawText(QRect(r.left(), r.top(), r.width(), 18),
                   Qt::AlignLeft, m_wx.city);

        f.setPixelSize(50); f.setWeight(QFont::Light);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        QRect tR(r.left(), r.top() + 20, 130, 66);
        p.drawText(tR, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(int(m_wx.tempC + 0.5))
                   + QString::fromUtf8("\xC2\xB0"));

        // Right side H/L + condition
        int ix = r.left() + 128;
        int iw = r.width() - 128;
        f.setPixelSize(11); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(230, 235, 245));
        p.drawText(QRect(ix, r.top() + 26, iw, 16), Qt::AlignLeft,
                   m_wx.condition.isEmpty() ? "—" : m_wx.condition);
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor(210, 220, 235));
        p.drawText(QRect(ix, r.top() + 44, iw, 14), Qt::AlignLeft,
                   QString("H:%1\xC2\xB0  L:%2\xC2\xB0")
                       .arg(int(m_wx.tempMax + 0.5))
                       .arg(int(m_wx.tempMin + 0.5)));

        // Hourly row — first 6 entries
        int rowTop = r.top() + 92;
        int cellW = r.width() / 6;
        f.setPixelSize(9);
        p.setFont(f);
        for (int i = 0; i < 6 && i < m_wx.hourlyTemps.size(); ++i) {
            int cx = r.left() + cellW * i + cellW / 2;
            QString t = QString("%1\xC2\xB0")
                .arg(int(m_wx.hourlyTemps[i] + 0.5));
            p.setPen(QColor(230, 235, 245));
            p.drawText(QRect(cx - cellW / 2, rowTop, cellW, 14),
                       Qt::AlignCenter, t);
        }

        // Bottom summary
        int bt = r.top() + 118;
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor(215, 225, 240));
        p.drawText(QRect(r.left(), bt, r.width(), 14), Qt::AlignLeft,
                   QString("Wind %1 km/h   ·   Humidity %2%")
                       .arg(int(m_wx.windKmh + 0.5))
                       .arg(m_wx.humidityPct));
    }

    void paintSunset(QPainter &p, const QRect &r) {
        QFont f = font();
        f.setPixelSize(11); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 245, 255));
        p.drawText(QRect(r.left(), r.top(), r.width(), 14),
                   Qt::AlignLeft, QString::fromUtf8("\xE2\x98\x80 SUNSET"));

        // Sun arc — big time in the middle
        f.setPixelSize(26); f.setWeight(QFont::Light);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        QRect mainR(r.left(), r.top() + 20, r.width(), 34);
        p.drawText(mainR, Qt::AlignCenter, m_wx.sunset);

        // Arc graphic
        int arcTop = r.top() + 62;
        int arcW = r.width() - 40;
        int arcH = 20;
        QRectF arcRect(r.left() + 20, arcTop, arcW, arcH * 2);
        p.setPen(QPen(QColor(255, 255, 255, 90), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawArc(arcRect, 0, 180 * 16);

        // Sun dot on the arc (roughly where sunset time is)
        // Position: middle of the arc.
        QPointF dot(arcRect.center().x(), arcTop + 4);
        p.setBrush(QColor(255, 255, 255));
        p.setPen(Qt::NoPen);
        p.drawEllipse(dot, 3, 3);

        // Sunrise underneath
        f.setPixelSize(10); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(220, 230, 245));
        p.drawText(QRect(r.left(), arcTop + 30, r.width(), 14),
                   Qt::AlignCenter,
                   QString::fromUtf8("Sunrise: ")
                   + m_wx.sunrise);
    }

    void paintWeek(QPainter &p, const QRect &r) {
        QFont f = font();

        // Header — city (left) + "5-DAY" (right)
        f.setPixelSize(12); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(240, 245, 255));
        p.drawText(QRect(r.left(), r.top(), r.width(), 16),
                   Qt::AlignLeft | Qt::AlignVCenter, m_wx.city);
        f.setPixelSize(9); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(200, 215, 235));
        p.drawText(QRect(r.left(), r.top(), r.width(), 16),
                   Qt::AlignRight | Qt::AlignVCenter, "5-DAY");

        if (m_wx.daily.isEmpty()) return;

        const int cols   = qMin(5, m_wx.daily.size());
        const int stripTop = r.top() + 22;
        const int cellW = r.width() / cols;

        for (int i = 0; i < cols; ++i) {
            const Weather::Snapshot::Day &d = m_wx.daily[i];
            const int cx = r.left() + cellW * i + cellW / 2;

            // Day name
            f.setPixelSize(10); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(230, 240, 255));
            p.drawText(QRect(cx - cellW/2, stripTop, cellW, 14),
                       Qt::AlignCenter, d.name);

            // Icon
            QRectF iconR(cx - 13, stripTop + 18, 26, 26);
            drawDayIcon(p, iconR, d.code);

            // High temp
            f.setPixelSize(11); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(240, 245, 255));
            p.drawText(QRect(cx - cellW/2, stripTop + 50, cellW, 14),
                       Qt::AlignCenter,
                       QString("%1\xC2\xB0").arg(int(d.maxC + 0.5)));

            // Low temp
            f.setPixelSize(10); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(QColor(190, 205, 225));
            p.drawText(QRect(cx - cellW/2, stripTop + 66, cellW, 14),
                       Qt::AlignCenter,
                       QString("%1\xC2\xB0").arg(int(d.minC + 0.5)));
        }
    }

    // Simple inline icon per weather code — a sun, cloud, rain,
    // snow, fog, or storm drawn with plain QPainter primitives so
    // there is nothing to load and nothing to cache.
    void drawDayIcon(QPainter &p, const QRectF &r, int code) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);

        const bool isClear  = (code == 0 || code == 1);
        const bool isCloudy = (code == 2 || code == 3);
        const bool isFog    = (code == 45 || code == 48);
        const bool isRain   = (code >= 51 && code <= 67)
                           || (code >= 80 && code <= 82);
        const bool isSnow   = (code >= 71 && code <= 77)
                           || (code >= 85 && code <= 86);
        const bool isStorm  = (code >= 95);

        const QPointF c = r.center();
        const qreal rad = r.width() * 0.28;

        if (isClear) {
            QColor sun(255, 220, 120);
            p.setPen(Qt::NoPen);
            p.setBrush(sun);
            p.drawEllipse(c, rad, rad);
            p.setPen(QPen(sun, 1.6, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 8; ++i) {
                qreal a = i * M_PI / 4.0;
                p.drawLine(
                    QPointF(c.x() + std::cos(a) * rad * 1.4,
                            c.y() + std::sin(a) * rad * 1.4),
                    QPointF(c.x() + std::cos(a) * rad * 1.9,
                            c.y() + std::sin(a) * rad * 1.9));
            }
        } else if (isCloudy || isRain || isSnow || isStorm) {
            QColor cloud(225, 232, 242);
            p.setPen(Qt::NoPen);
            p.setBrush(cloud);
            const qreal cw = r.width() * 0.55;
            const qreal ch = r.height() * 0.32;
            p.drawEllipse(QPointF(c.x() - cw * 0.35,
                                  c.y() + ch * 0.15),
                          cw * 0.50, ch * 0.80);
            p.drawEllipse(QPointF(c.x() + cw * 0.15,
                                  c.y() + ch * 0.15),
                          cw * 0.55, ch * 0.90);
            p.drawEllipse(QPointF(c.x(), c.y() - ch * 0.20),
                          cw * 0.65, ch * 1.00);

            if (isRain || isStorm) {
                p.setPen(QPen(QColor(140, 200, 240), 1.4,
                              Qt::SolidLine, Qt::RoundCap));
                for (int i = 0; i < 3; ++i) {
                    qreal x = c.x() - cw * 0.30 + i * cw * 0.30;
                    p.drawLine(QPointF(x, c.y() + ch * 0.90),
                               QPointF(x - 2, c.y() + ch * 1.70));
                }
            } else if (isSnow) {
                p.setPen(QPen(QColor(220, 230, 240), 1.2,
                              Qt::SolidLine, Qt::RoundCap));
                for (int i = 0; i < 3; ++i) {
                    qreal x = c.x() - cw * 0.30 + i * cw * 0.30;
                    qreal y = c.y() + ch * 1.20;
                    p.drawLine(QPointF(x - 2, y), QPointF(x + 2, y));
                    p.drawLine(QPointF(x, y - 2), QPointF(x, y + 2));
                }
            }
            if (isStorm) {
                QPolygonF bolt;
                bolt << QPointF(c.x() + 3, c.y() + 8)
                     << QPointF(c.x() - 2, c.y() + 13)
                     << QPointF(c.x() + 1, c.y() + 13)
                     << QPointF(c.x() - 3, c.y() + 21)
                     << QPointF(c.x() + 4, c.y() + 14)
                     << QPointF(c.x() + 1, c.y() + 14);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 220, 90));
                p.drawPolygon(bolt);
            }
        } else if (isFog) {
            p.setPen(QPen(QColor(215, 225, 240), 1.6,
                          Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 3; ++i) {
                qreal y = c.y() - 4 + i * 4;
                p.drawLine(QPointF(r.left() + r.width() * 0.2, y),
                           QPointF(r.right() - r.width() * 0.2, y));
            }
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 210, 225));
            p.drawEllipse(c, 3, 3);
        }
        p.restore();
    }

    Weather::Fetcher  *m_weather = nullptr;
    Weather::Snapshot  m_wx;
    QTimer            *m_tick = nullptr;
};


// ----------------------------------------------------------------
// CalendarCard — month grid, today highlighted with the accent
// colour. Migrated from the old DesktopWidgets stacked column.
// ----------------------------------------------------------------
class CalendarCard : public WidgetCard {
public:
    explicit CalendarCard(QWidget *parent)
        : WidgetCard("calendar", QSize(260, 215),
                     QPoint(40, 380), parent)
    {
        m_tick = new QTimer(this);
        m_tick->setInterval(60 * 60 * 1000);   // re-render hourly
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { update(); });
        m_tick->start();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        QDate today = QDate::currentDate();
        const QString monthName =
            today.toString("MMMM yyyy").toUpper();

        // Month header
        QFont f = font();
        f.setPixelSize(11); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_theme.textDim);
        QRect hdr(r.left(), r.top(), r.width(), 16);
        p.drawText(hdr, Qt::AlignLeft, monthName);

        // Days-of-week header row
        int gridTop = r.top() + 26;
        int cellW = r.width() / 7;
        int cellH = 22;

        const char *names[] = { "M","T","W","T","F","S","S" };
        f.setPixelSize(10); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(m_theme.textDim);
        for (int i = 0; i < 7; ++i) {
            p.drawText(QRect(r.left() + i * cellW, gridTop,
                             cellW, 14),
                       Qt::AlignCenter, names[i]);
        }

        // Day cells
        QDate first(today.year(), today.month(), 1);
        int startCol = (first.dayOfWeek() + 6) % 7;   // Mon = 0
        int daysInMonth = today.daysInMonth();

        f.setPixelSize(12); f.setWeight(QFont::Normal);
        p.setFont(f);

        for (int d = 1; d <= daysInMonth; ++d) {
            int idx = startCol + d - 1;
            int row = idx / 7;
            int col = idx % 7;
            QRect cell(r.left() + col * cellW,
                       gridTop + 16 + row * cellH,
                       cellW, cellH);

            bool isToday = (d == today.day());
            if (isToday) {
                p.setBrush(m_theme.accent);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(cell.adjusted(2, 2, -2, -2),
                                  6, 6);
                p.setPen(QColor(255, 255, 255));
            } else {
                p.setPen(m_theme.textSecondary);
            }
            p.drawText(cell, Qt::AlignCenter,
                       QString::number(d));
        }
    }

private:
    QTimer *m_tick = nullptr;
};


// ============================================================
// StickyCard — Model A philosophy, 5 independent floating notes.
// Each note carries its own text, its own position, its own
// colour pin. Persistence is in stickies.json (NOT visual.json,
// which has a known setAndSave() save-race that can lose typed
// content). Key "0".."4" -> {text: QString}.
// ============================================================
#include <QPlainTextEdit>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QFileInfo>

class StickyCard : public WidgetCard {
public:
    explicit StickyCard(int index, QWidget *parent = nullptr)
        : WidgetCard(QString("sticky%1").arg(index),
                     QSize(240, 200),
                     QPoint(40 + index * 20, 60 + index * 220),
                     parent),
          m_index(index)
    {
        loadNoteText();

        m_body = new QPlainTextEdit(this);
        m_body->setPlainText(m_cachedText);
        m_body->setStyleSheet(QString(
            "QPlainTextEdit {"
            "  background: rgba(30, 30, 32, 200);"
            "  color: %1;"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 8px;"
            "  padding: 8px;"
            "  font-size: 12px;"
            "}")
            .arg(m_theme.textPrimary.name()));

        m_close = new QPushButton(QString::fromUtf8("\xC3\x97"),
                                  this);
        m_close->setFixedSize(22, 22);
        m_close->setCursor(Qt::PointingHandCursor);
        m_close->setToolTip("Delete this note");
        m_close->setStyleSheet(QString(
            "QPushButton {"
            "  background: transparent;"
            "  color: %1;"
            "  border: none;"
            "  font-size: 16px;"
            "  padding: 0;"
            "}"
            "QPushButton:hover {"
            "  color: %2;"
            "}").arg(m_theme.textSecondary.name(),
                     m_theme.accent.name()));

        QObject::connect(m_close, &QPushButton::clicked,
                         this, [this]() {
            removeNoteFromDisk();
            hide();
        });

        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(500);
        QObject::connect(m_saveTimer, &QTimer::timeout,
                         this, [this]() {
            m_cachedText = m_body->toPlainText();
            saveNoteToDisk(m_cachedText);
        });
        QObject::connect(m_body, &QPlainTextEdit::textChanged,
                         this, [this]() { m_saveTimer->start(); });
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        WidgetCard::resizeEvent(e);
        int bodyTop = 48;
        int pad = 12;
        m_body->setGeometry(pad, bodyTop,
                            width() - 2 * pad,
                            height() - bodyTop - pad);
        m_close->move(width() - 28, 8);
    }

    void paintContent(QPainter &p, const QRect &r) override {
        QColor pinCol = cycleColor(m_index);
        QRect pinRect(r.left() + 12, r.top() + 8, 26, 26);
        p.setBrush(pinCol);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pinRect);
        p.setPen(QColor(255, 255, 255));
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(pinRect, Qt::AlignCenter,
                   QString::number(m_index + 1));

        f.setPixelSize(11);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(m_theme.textSecondary);
        QRect labelRect(r.left() + 48, r.top() + 8,
                        r.width() - 84, 26);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                   "Sticky");
    }

private:
    int m_index;
    QPlainTextEdit *m_body = nullptr;
    QPushButton *m_close = nullptr;
    QTimer *m_saveTimer = nullptr;
    QString m_cachedText;

    static QColor cycleColor(int i) {
        static const QColor cols[] = {
            QColor(122, 106, 159),
            QColor(90, 122, 158),
            QColor(166, 122, 90),
            QColor(160, 90, 106),
            QColor(74, 122, 122),
        };
        return cols[i % 5];
    }

    static QString stickiesPath() {
        return QDir::homePath()
            + "/.config/apokolips/stickies.json";
    }

    static QJsonObject readAll() {
        QFile f(stickiesPath());
        if (!f.open(QIODevice::ReadOnly)) return {};
        QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        return d.object();
    }

    void loadNoteText() {
        QJsonObject all = readAll();
        QString key = QString::number(m_index);
        if (all.contains(key)) {
            QJsonObject entry = all.value(key).toObject();
            m_cachedText = entry.value("text").toString();
        }
    }

    void saveNoteToDisk(const QString &text) {
        QJsonObject all = readAll();
        QJsonObject entry;
        entry["text"] = text;
        all[QString::number(m_index)] = entry;
        QFile f(stickiesPath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(all).toJson());
    }

    void removeNoteFromDisk() {
        QJsonObject all = readAll();
        all.remove(QString::number(m_index));
        QFile f(stickiesPath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(all).toJson());
    }
};


#endif
