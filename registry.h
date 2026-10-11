#ifndef APOKOLIPS_REGISTRY_H
#define APOKOLIPS_REGISTRY_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"

namespace ShellRegistry {
    inline std::function<void()> &launcherToggle() {
        static std::function<void()> f;
        return f;
    }
    inline std::function<void()> &settingsOpen() {
        static std::function<void()> f;
        return f;
    }
}

namespace BadgeRegistry {

inline QHash<QString, int> &counts() {
    static QHash<QString, int> c;
    return c;
}

inline int get(const QString &app) {
    return counts().value(app, 0);
}

inline void set(const QString &app, int n) {
    if (n <= 0) counts().remove(app);
    else        counts()[app] = n;
}

inline void decrement(const QString &app) {
    int n = get(app);
    if (n > 0) set(app, n - 1);
}

inline void clear(const QString &app) {
    counts().remove(app);
}

inline void seedDefaults() {
    // Matches the hardcoded notification cards in NotificationCenter
    set("Mail", 3);
    set("Music", 1);
    set("Settings", 1);
}

}

namespace Icons {

// ---- Shared: painted rounded-square app tile ----
static void appTile(QPainter &p, const QRectF &r,
                    const QColor &top, const QColor &bottom)
{
    p.setRenderHint(QPainter::Antialiasing);
    qreal radius = r.width() * 0.22;
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bottom);
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, g);

    // Inner top highlight — subtle macOS-style depth
    QColor hi(255, 255, 255, 55);
    p.setPen(QPen(hi, qMax(1.0, r.width() * 0.012)));
    p.setBrush(Qt::NoBrush);
    QPainterPath inner;
    inner.addRoundedRect(r.adjusted(r.width()*0.05, r.height()*0.05,
                                    -r.width()*0.05, -r.height()*0.05),
                         radius * 0.82, radius * 0.82);
    p.drawPath(inner);
}

// ---- Motif helper: white pen of consistent weight ----
static QPen whitePen(const QRectF &r, qreal factor = 0.08) {
    QPen pen(QColor(255, 255, 255, 235),
             qMax(1.5, r.width() * factor),
             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

static void drawFinder(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor( 90, 130, 200), QColor( 40,  70, 130));
    p.setPen(whitePen(r));
    p.setBrush(Qt::NoBrush);
    // Folder
    qreal cx = r.center().x(), cy = r.center().y() + r.height() * 0.02;
    qreal fw = r.width() * 0.52, fh = r.height() * 0.36;
    QRectF folder(cx - fw/2, cy - fh/2 + r.height()*0.03, fw, fh);
    QPainterPath fp;
    fp.moveTo(folder.left(), folder.top() + fh * 0.20);
    fp.lineTo(folder.left(), folder.bottom());
    fp.lineTo(folder.right(), folder.bottom());
    fp.lineTo(folder.right(), folder.top() + fh * 0.20);
    fp.lineTo(folder.left() + fw * 0.40, folder.top() + fh * 0.20);
    fp.lineTo(folder.left() + fw * 0.30, folder.top());
    fp.lineTo(folder.left(), folder.top());
    fp.closeSubpath();
    p.drawPath(fp);
    // Magnifier bubble top-right
    qreal mr = r.width() * 0.11;
    QPointF mc(folder.right() - r.width() * 0.02,
               folder.top() + r.height() * 0.02);
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawEllipse(mc, mr, mr);
    p.setPen(QPen(QColor( 90, 130, 200), qMax(1.5, r.width() * 0.05)));
    p.drawLine(mc + QPointF(mr*0.7, mr*0.7),
               mc + QPointF(mr*1.6, mr*1.6));
}

static void drawLaunchpad(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor(180,  60,  60), QColor(110,  26,  26));
    QFont f = p.font();
    f.setPixelSize(int(r.width() * 0.68));
    f.setWeight(QFont::Light);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 240));
    p.drawText(r, Qt::AlignCenter, QString::fromUtf8("\xCE\xA9"));
}

static void drawPhotos(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor( 90, 180, 180), QColor( 30, 100, 100));
    p.setPen(whitePen(r, 0.065));
    p.setBrush(Qt::NoBrush);
    QPointF ctr = r.center();
    qreal inner = r.width() * 0.06;
    qreal outer = r.width() * 0.32;
    for (int i = 0; i < 6; ++i) {
        double a = i * 60.0 * 3.14159265 / 180.0;
        double b = a + 0.90;
        QPointF p1(ctr.x() + std::cos(a) * inner, ctr.y() + std::sin(a) * inner);
        QPointF p2(ctr.x() + std::cos(b) * outer, ctr.y() + std::sin(b) * outer);
        p.drawLine(p1, p2);
    }
}

// Aperture — hexagonal iris with six angled blades
static void drawAperture(QPainter &p, const QRectF &r, const QColor &)
{
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c = r.center();
    const qreal   d = r.width();

    QColor bone(240, 237, 232, 230);
    QColor ox(168, 50, 50);
    QColor graphite(30, 30, 32);

    // Outer hex ring
    QPolygonF hex;
    const qreal outerR = d * 0.42;
    for (int i = 0; i < 6; ++i) {
        qreal a = -M_PI / 2.0 + i * M_PI / 3.0;
        hex << QPointF(c.x() + std::cos(a) * outerR,
                       c.y() + std::sin(a) * outerR);
    }

    QPainterPath outer;
    outer.addPolygon(hex);
    outer.closeSubpath();

    p.fillPath(outer, graphite);
    QPen bonePen(bone, 1.4);
    bonePen.setJoinStyle(Qt::MiterJoin);
    p.setPen(bonePen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(hex);

    // Six inner iris blades — each a thin oxblood triangle pointing
    // at the centre, offset around a hexagon to suggest a lens iris.
    const qreal innerR = outerR * 0.62;
    const qreal bladeTipR = outerR * 0.20;
    const qreal bladeBaseR = outerR * 0.55;

    QPen bladePen(ox, 1.6);
    bladePen.setCapStyle(Qt::RoundCap);
    p.setPen(bladePen);
    p.setBrush(Qt::NoBrush);

    for (int i = 0; i < 6; ++i) {
        qreal a0 = -M_PI / 2.0 + i * M_PI / 3.0;
        qreal a1 = a0 + M_PI / 3.0;

        QPointF base1(c.x() + std::cos(a0) * bladeBaseR,
                      c.y() + std::sin(a0) * bladeBaseR);
        QPointF base2(c.x() + std::cos(a1) * bladeBaseR,
                      c.y() + std::sin(a1) * bladeBaseR);
        QPointF tip(c.x() + std::cos(a0 + M_PI / 6.0) * bladeTipR,
                    c.y() + std::sin(a0 + M_PI / 6.0) * bladeTipR);

        p.drawLine(base1, tip);
        p.drawLine(base2, tip);
    }

    // Centre dot — the lens core
    p.setPen(Qt::NoPen);
    p.setBrush(ox);
    p.drawEllipse(c, d * 0.045, d * 0.045);
}

// myMach — monitor outline with gear inside
static void drawMyMach(QPainter &p, const QRectF &r, const QColor &)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor bone(240, 237, 232, 230);
    QColor ox(168, 50, 50);

    QRectF mon(r.left() + r.width() * 0.10,
               r.top()  + r.height() * 0.16,
               r.width() * 0.80,
               r.height() * 0.58);
    QPen bp(bone, 1.5);
    bp.setJoinStyle(Qt::RoundJoin);
    p.setPen(bp);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(mon, 3, 3);

    p.drawLine(QPointF(mon.center().x(), mon.bottom()),
               QPointF(mon.center().x(),
                       mon.bottom() + r.height() * 0.08));
    p.drawLine(QPointF(mon.center().x() - r.width() * 0.10,
                       mon.bottom() + r.height() * 0.08),
               QPointF(mon.center().x() + r.width() * 0.10,
                       mon.bottom() + r.height() * 0.08));

    QPointF gc = mon.center();
    qreal outer = r.width() * 0.16;
    qreal inner = r.width() * 0.08;
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

    p.setBrush(bone);
    p.drawEllipse(gc, inner, inner);
}



static void drawMusic(QPainter &p, const QRectF &r, const QColor &)
{
    // BoomTube — horizontal tube with three oxblood energy rings
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c = r.center();
    const qreal   d = r.width();

    QColor bone(240, 237, 232, 230);
    QColor ox(168, 50, 50);
    QColor graphite(30, 30, 32);

    QRectF tube(c.x() - d * 0.40, c.y() - d * 0.14,
                d * 0.56,        d * 0.28);

    QPainterPath tubePath;
    tubePath.addRoundedRect(tube, tube.height() / 2, tube.height() / 2);
    p.fillPath(tubePath, graphite);

    QPen bonePen(bone, 1.5);
    p.setPen(bonePen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(tubePath);

    QRectF mouth(tube.right() - tube.height() * 0.5, tube.top(),
                 tube.height(), tube.height());
    QPen boneThin(bone, 1.2);
    p.setPen(boneThin);
    p.drawEllipse(mouth);

    QPen ringPen(ox, 1.8);
    ringPen.setCapStyle(Qt::RoundCap);
    p.setPen(ringPen);

    const QPointF ringCenter(tube.right() + tube.height() * 0.05, c.y());

    for (int i = 0; i < 3; ++i) {
        qreal radius = tube.height() * (0.62 + i * 0.30);
        QRectF arc(ringCenter.x() - radius * 0.55,
                   ringCenter.y() - radius,
                   radius * 1.1,
                   radius * 2.0);
        p.setOpacity(1.0 - i * 0.30);
        p.drawArc(arc, -80 * 16, 160 * 16);
    }
    p.setOpacity(1.0);
}

static void drawNotes(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor(232, 220, 198), QColor(190, 175, 145));
    // Page rectangle
    qreal w = r.width() * 0.52;
    qreal h = r.height() * 0.66;
    QRectF page(r.center().x() - w/2, r.center().y() - h/2, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawRoundedRect(page, r.width() * 0.06, r.width() * 0.06);

    // Red corner fold
    QPolygonF corner;
    corner << QPointF(page.right() - w * 0.28, page.top())
           << QPointF(page.right(), page.top())
           << QPointF(page.right(), page.top() + h * 0.20);
    p.setBrush(QColor(200, 60, 60));
    p.drawPolygon(corner);

    // Ruled lines
    p.setPen(QPen(QColor(160, 150, 130, 200),
                  qMax(1.0, r.width() * 0.035), Qt::SolidLine, Qt::RoundCap));
    qreal xL = page.left() + w * 0.18;
    qreal xR = page.right() - w * 0.18;
    qreal y1 = page.top() + h * 0.42;
    qreal y2 = page.top() + h * 0.62;
    qreal y3 = page.top() + h * 0.80;
    p.drawLine(QPointF(xL, y1), QPointF(xR, y1));
    p.drawLine(QPointF(xL, y2), QPointF(xR, y2));
    p.drawLine(QPointF(xL, y3), QPointF(xL + (xR - xL) * 0.55, y3));
}

static void drawMail(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor(170, 125, 90), QColor(105,  70,  45));
    // Envelope body
    qreal w = r.width() * 0.62;
    qreal h = r.height() * 0.42;
    QRectF body(r.center().x() - w/2, r.center().y() - h/2, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawRoundedRect(body, r.width() * 0.05, r.width() * 0.05);

    // Envelope flap (V shape)
    p.setPen(QPen(QColor(170, 125, 90), qMax(1.5, r.width() * 0.055),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPointF mid(body.center().x(), body.top() + h * 0.62);
    p.drawLine(QPointF(body.left() + w * 0.04, body.top() + h * 0.12), mid);
    p.drawLine(QPointF(body.right() - w * 0.04, body.top() + h * 0.12), mid);
}

static void drawSettings(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor(120, 120, 128), QColor( 55,  55,  62));
    // Gear: outer circle + teeth + inner ring
    QPointF ctr = r.center();
    qreal outer = r.width() * 0.26;
    qreal inner = r.width() * 0.10;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 240));

    // 8 teeth
    for (int i = 0; i < 8; ++i) {
        double a = i * 3.14159265 / 4.0;
        QPointF dir(std::cos(a), std::sin(a));
        QPointF tip = ctr + dir * (outer + r.width() * 0.08);
        QPointF base = ctr + dir * outer * 0.85;
        QPolygonF tooth;
        QPointF perp(-dir.y(), dir.x());
        tooth << base + perp * r.width() * 0.05
              << tip + perp * r.width() * 0.035
              << tip - perp * r.width() * 0.035
              << base - perp * r.width() * 0.05;
        p.drawPolygon(tooth);
    }
    p.drawEllipse(ctr, outer, outer);
    // Cut inner hole
    p.setBrush(QColor(120, 120, 128));
    p.drawEllipse(ctr, inner, inner);
}

static void drawTerminal(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor( 55,  55,  62), QColor( 18,  18,  22));
    // Box outline
    p.setPen(whitePen(r, 0.065));
    p.setBrush(Qt::NoBrush);
    QRectF box(r.center().x() - r.width() * 0.34,
               r.center().y() - r.height() * 0.26,
               r.width() * 0.68, r.height() * 0.52);
    p.drawRoundedRect(box, r.width() * 0.06, r.width() * 0.06);

    // > chevron
    qreal x1 = box.left() + r.width() * 0.12;
    qreal y1 = box.top() + r.height() * 0.18;
    qreal x2 = box.left() + r.width() * 0.28;
    qreal y2 = box.center().y();
    qreal x3 = box.left() + r.width() * 0.12;
    qreal y3 = box.bottom() - r.height() * 0.18;
    p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    p.drawLine(QPointF(x2, y2), QPointF(x3, y3));

    // _ cursor
    p.drawLine(QPointF(box.left() + r.width() * 0.40,
                       box.bottom() - r.height() * 0.18),
               QPointF(box.right() - r.width() * 0.12,
                       box.bottom() - r.height() * 0.18));
}

static void drawPower(QPainter &p, const QRectF &r, const QColor &) {
    appTile(p, r, QColor( 90,  90,  98), QColor( 35,  35,  40));
    p.setPen(whitePen(r, 0.10));
    p.setBrush(Qt::NoBrush);
    qreal s = r.width() * 0.30;
    QPointF ctr = r.center();
    QRectF arcR(ctr.x() - s, ctr.y() - s + r.width()*0.04, 2*s, 2*s);
    p.drawArc(arcR, 45 * 16, 270 * 16);
    p.drawLine(QPointF(ctr.x(), ctr.y() - s - r.width()*0.04),
               QPointF(ctr.x(), ctr.y() - r.width()*0.02));
}



static void drawGlyph(QPainter &p, const QRectF &r, const QColor &) {
    p.setRenderHint(QPainter::Antialiasing, true);

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

    p.drawLine(frame.topLeft(), frame.topLeft() + QPointF(L, 0));
    p.drawLine(frame.topLeft(), frame.topLeft() + QPointF(0, L));
    p.drawLine(frame.topRight(), frame.topRight() + QPointF(-L, 0));
    p.drawLine(frame.topRight(), frame.topRight() + QPointF(0, L));
    p.drawLine(frame.bottomLeft(), frame.bottomLeft() + QPointF(L, 0));
    p.drawLine(frame.bottomLeft(), frame.bottomLeft() + QPointF(0, -L));

    QPointF br = frame.bottomRight();
    QPolygonF caret;
    caret << br + QPointF(-L * 0.85, 0)
          << br
          << br + QPointF(0, -L * 0.85);
    p.setPen(Qt::NoPen);
    p.setBrush(ox);
    p.drawPolygon(caret);
}

static void drawFor(const QString &name, QPainter &p,
                    const QRectF &r, const QColor &c) {
    if (name == "Finder")         drawFinder(p, r, c);
    else if (name == "Launchpad") drawLaunchpad(p, r, c);
    else if (name == "Aperture")  drawAperture(p, r, c);
    else if (name == "myMach")    drawMyMach(p, r, c);
    else if (name == "Photos")    drawPhotos(p, r, c);
    else if (name == "BoomTube")  drawMusic(p, r, c);
    else if (name == "Music")     drawMusic(p, r, c);
    else if (name == "Notes")     drawNotes(p, r, c);
    else if (name == "Mail")      drawMail(p, r, c);
    else if (name == "Settings")  drawSettings(p, r, c);
    else if (name == "Power")     drawPower(p, r, c);
    else if (name == "Node")      drawTerminal(p, r, c);
    else if (name == "Terminal")  drawTerminal(p, r, c);
    else if (name == "Glyph")     drawGlyph(p, r, c);
}

// ---- Tray icon helpers (small, single-color) ----
static void drawWifi(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QColor col = on ? c : QColor(c.red(), c.green(), c.blue(), 90);
    QPen pen(col, qMax(1.0, r.width() * 0.09), Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPointF ctr(r.center().x(), r.center().y() + r.height() * 0.10);
    for (int i = 0; i < 3; ++i) {
        qreal rad = r.width() * (0.14 + 0.13 * i);
        QRectF arc(ctr.x() - rad, ctr.y() - rad, 2*rad, 2*rad);
        p.drawArc(arc, 45 * 16, 90 * 16);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawEllipse(ctr, r.width()*0.055, r.width()*0.055);
}

static void drawEthernet(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QColor col = on ? c : QColor(c.red(), c.green(), c.blue(), 90);
    QPen pen(col, qMax(1.0, r.width() * 0.085), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    qreal w = r.width() * 0.42;
    qreal h = r.height() * 0.30;
    QRectF box(r.center().x() - w/2, r.center().y() + r.height()*0.02, w, h);
    p.drawRoundedRect(box, r.width()*0.05, r.width()*0.05);
    qreal step = w / 4.0;
    for (int i = 1; i <= 3; ++i) {
        qreal x = box.left() + step * i;
        p.drawLine(QPointF(x, box.top()),
                   QPointF(x, box.top() - r.height()*0.12));
    }
}

static void drawBluetooth(QPainter &p, const QRectF &r, const QColor &c, bool on) {
    QColor col = on ? c : QColor(c.red(), c.green(), c.blue(), 90);
    QPen pen(col, qMax(1.0, r.width() * 0.09), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPointF ctr = r.center();
    qreal s = r.width() * 0.28;
    QPolygonF path;
    path << QPointF(ctr.x(), ctr.y() - s)
         << QPointF(ctr.x() + s*0.7, ctr.y() - s*0.5)
         << QPointF(ctr.x() - s*0.7, ctr.y() + s*0.5)
         << QPointF(ctr.x(), ctr.y() + s)
         << QPointF(ctr.x(), ctr.y() - s)
         << QPointF(ctr.x() - s*0.7, ctr.y() - s*0.5)
         << QPointF(ctr.x() + s*0.7, ctr.y() + s*0.5)
         << QPointF(ctr.x(), ctr.y() + s);
    p.drawPolyline(path);
}

static void drawBattery(QPainter &p, const QRectF &r, const QColor &c,
                        int pct, bool charging)
{
    qreal w = r.width() * 0.72;
    qreal h = r.height() * 0.40;
    QRectF body(r.center().x() - w/2, r.center().y() - h/2, w, h);
    QPen pen(c, qMax(1.0, r.width() * 0.075), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, r.width()*0.04, r.width()*0.04);
    QRectF nub(body.right() + r.width()*0.03,
               body.center().y() - h*0.18,
               r.width()*0.06, h*0.36);
    p.setBrush(c); p.setPen(Qt::NoPen);
    p.drawRoundedRect(nub, 2, 2);
    if (pct >= 0) {
        qreal inner = w * (pct / 100.0) * 0.88;
        QRectF fill(body.left() + w*0.06, body.top() + h*0.20,
                    inner, h*0.60);
        QColor fc = charging ? QColor(80, 200, 120) : c;
        p.setBrush(fc);
        p.drawRoundedRect(fill, 2, 2);
    }
}

static QPixmap render(const QString &name, int size, const QColor &color) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    drawFor(name, p, QRectF(0, 0, size, size), color);
    p.end();
    return px;
}

}

struct AppEntry {
    const char *id;
    const char *label;
    const char *glyph;
    bool inDock;
    bool inLauncher;
};

static const QVector<AppEntry> &appRegistry() {
    static const QVector<AppEntry> list = {
        { "files",       "Finder",    "\xE2\x8C\x98",       true,  true  },
        { "launchpad",   "Launchpad", "\xF0\x9F\x9A\x80",  true,  true  },
        { "aperture",    "Aperture",  "\xE2\x9C\xA6",       true,  true  },
        { "mymach",      "myMach",    "\xE2\x9A\x99",       true,  true  },
        { "boombube",    "BoomTube",  "\xE2\x96\xB6",       true,  true  },
        { "notes",       "Notes",     "\xF0\x9F\x93\x9D",  true,  true  },
        { "mail",        "Mail",      "\xE2\x9C\x89",       true,  true  },
        { "settings",    "Settings",  "\xE2\x9A\x99",       true,  true  },
        { "node",        "Node",      "\xF0\x9F\x92\xBB",  true,  true  },
        { "power",       "Power",     "\xE2\x9A\xA1",       true,  true  },
        { "glyph",       "Glyph",     "\xE2\x97\x89",       false, true  },
        { "calculator",  "Calculator","\xF0\x9F\xA7\xAE",  false, false },
        { "screensaver", "Screensaver","\xF0\x9F\x8C\x8C", false, false },
        { "snap",        "Snap Layouts","\xE2\x96\xA6",      false, false },
    };
    return list;
}

static QString idForLabel(const QString &label) {
    for (const auto &e : appRegistry())
        if (label == QLatin1String(e.label)) return e.id;
    return QString();
}

static QString labelForId(const QString &id) {
    for (const auto &e : appRegistry())
        if (id == QLatin1String(e.id)) return e.label;
    return id;
}

static bool isInDock(const QString &id) {
    auto &cfg = VisualConfigManager::instance().cfg();
    if (!cfg.dockApps.isEmpty())
        return cfg.dockApps.contains(id);
    for (const auto &e : appRegistry())
        if (id == QLatin1String(e.id)) return e.inDock;
    return false;
}

static bool isInLauncher(const QString &id) {
    auto &cfg = VisualConfigManager::instance().cfg();
    if (!cfg.launcherApps.isEmpty())
        return cfg.launcherApps.contains(id);
    for (const auto &e : appRegistry())
        if (id == QLatin1String(e.id)) return e.inLauncher;
    return false;
}

static bool isOnDesktop(const QString &id) {
    auto &cfg = VisualConfigManager::instance().cfg();
    if (!cfg.desktopApps.isEmpty())
        return cfg.desktopApps.contains(id);
    return id == QLatin1String("mymach");
}

static void setInDock(const QString &id, bool on) {
    VisualConfigManager::instance().setAndSave(
        [id, on](VisualConfig &c) {
            if (c.dockApps.isEmpty()) {
                // Seed from registry defaults the first time
                for (const auto &e : appRegistry())
                    if (e.inDock) c.dockApps << e.id;
            }
            if (on) {
                if (!c.dockApps.contains(id)) c.dockApps << id;
            } else {
                c.dockApps.removeAll(id);
            }
        });
}

static void setOnDesktop(const QString &id, bool on) {
    VisualConfigManager::instance().setAndSave(
        [id, on](VisualConfig &c) {
            if (c.desktopApps.isEmpty()) c.desktopApps << "mymach";
            if (on) {
                if (!c.desktopApps.contains(id)) c.desktopApps << id;
            } else {
                c.desktopApps.removeAll(id);
            }
        });
}

static QStringList desktopLaunchArgs(const QString &id) {
    if (id == "mymach")     return { "--mymach" };
    if (id == "files")      return { "--files" };
    if (id == "settings")   return { "--settings" };
    if (id == "aperture")   return { "--image" };
    if (id == "boombube")   return { "--boomtube" };
    if (id == "glyph")      return { "--glyph" };
    if (id == "calculator") return { "--calc" };
    if (id == "notes")      return { "--notes" };
    if (id == "node")       return { "--node" };
    if (id == "terminal")   return { "--node" };
    return { "--demo", id, "0" };
}

static void desktopIconsLaunchArg(const QString &) {}

#endif // APOKOLIPS_REGISTRY_H
