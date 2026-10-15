#ifndef APOKOLIPS_WIDGETS_H
#define APOKOLIPS_WIDGETS_H

// Widget framework — cards as direct children of the desktop root.
// No fullscreen layer. No top-level windows. Standard Qt.

#include "shell.h"
#include "theme.h"
#include "style.h"
#include "visualconfig.h"
#include <QContextMenuEvent>
#include <QMenu>

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

protected:
    virtual void paintContent(QPainter &p, const QRect &content) = 0;

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        p.setPen(Qt::NoPen);
        for (int i = 6; i >= 1; --i) {
            QColor sh(0, 0, 0);
            sh.setAlphaF(0.03 * (i / 6.0));
            p.setBrush(sh);
            p.drawRoundedRect(r.adjusted(-i, -i + 3, i, i + 3),
                              20 + i, 20 + i);
        }

        StyleRenderer::drawPanel(p, r, 20, m_theme, 200);

        const int pad = 16;
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
        if (m_dragging)
            qInfo() << "[widget] drag" << m_id << "->" << e->position();
        if (m_dragging && parentWidget()) {
            QPoint p = mapToParent(e->position().toPoint() - m_dragOffset);
            int maxX = qMax(0, parentWidget()->width()  - width());
            int maxY = qMax(0, parentWidget()->height() - height());
            p.setX(qBound(0, p.x(), maxX));
            p.setY(qBound(0, p.y(), maxY));
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

        QAction *chosen = m.exec(e->globalPos());
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
        : WidgetCard("clock", QSize(240, 110),
                     QPoint(1400, 100), parent)
    {
        m_tick = new QTimer(this);
        m_tick->setInterval(1000);
        QObject::connect(m_tick, &QTimer::timeout,
                         this, [this]() { update(); });
        m_tick->start();
    }

protected:
    void paintContent(QPainter &p, const QRect &r) override {
        QDateTime now = QDateTime::currentDateTime();

        QFont f = font();
        f.setPixelSize(34); f.setWeight(QFont::Light);
        p.setFont(f);
        p.setPen(m_theme.textPrimary);
        p.drawText(QRect(r.left(), r.top(), r.width(), 44),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   now.toString("HH:mm"));

        f.setPixelSize(12); f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(m_theme.textSecondary);
        p.drawText(QRect(r.left(), r.top() + 46, r.width(), 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   now.toString("dddd, d MMMM"));

        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(m_theme.textDim);
        p.drawText(QRect(r.left(), r.top() + 64, r.width(), 16),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   now.toString("yyyy"));
    }

private:
    QTimer *m_tick = nullptr;
};

#endif
