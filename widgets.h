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


// ----------------------------------------------------------------
// RingsCard — four circular gauges: CPU / RAM / NET / DISK.
// Matches macOS Tahoe widget style: dark card, rings with green
// arcs, icon inside each ring, value + label underneath.
// ----------------------------------------------------------------
class RingsCard : public WidgetCard {
public:
    explicit RingsCard(QWidget *parent)
        : WidgetCard("rings", QSize(340, 140),
                     QPoint(400, 340), parent)
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
        const int ringD = 58;               // outer diameter
        const qreal ringStroke = 5.0;
        const int gap = qMax(4, (r.width() - n * ringD) / (n - 1));
        const int cy = r.top() + ringD / 2 + 4;
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
            drawRingIcon(p, i, c, 18, m_theme.textSecondary);

            // Big value under the ring
            QFont f = font();
            f.setPixelSize(13); f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_theme.textPrimary);
            QRect vRect(cx - ringD/2, cy + ringD/2 + 4,
                        ringD, 18);
            p.drawText(vRect, Qt::AlignCenter,
                       QString("%1%").arg(int(vals[i] + 0.5)));

            // Small label
            f.setPixelSize(9); f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(m_theme.textDim);
            QRect lRect(cx - ringD/2, cy + ringD/2 + 22,
                        ringD, 12);
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

#endif
