#ifndef APOKOLIPS_STYLE_H
#define APOKOLIPS_STYLE_H

// VisualStyle + StyleManager + StyleRenderer + Sigil (Step 4)

#include "shell.h"
#include "theme.h"

enum class VisualStyle {
    Glass        = 0,
    Neumorphism  = 1,
    Skeuomorphism= 2,
    Claymorphism = 3
};

namespace StyleManager {

inline VisualStyle &current() {
    static VisualStyle s = VisualStyle::Glass;
    return s;
}

inline QList<std::function<void(VisualStyle)>> &callbacks() {
    static QList<std::function<void(VisualStyle)>> cbs;
    return cbs;
}

inline QString path() {
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::ConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
    return dir + "/apokolips/style.json";
}

inline void load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    int n = doc.object().value("style").toInt(0);
    if (n < 0 || n > 3) n = 0;
    current() = static_cast<VisualStyle>(n);
}

inline void save() {
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QJsonObject o;
    o["style"] = static_cast<int>(current());
    QFile f(path());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson());
}

inline void set(VisualStyle s) {
    current() = s;
    save();
    for (auto &cb : callbacks()) cb(s);
}

inline void subscribe(std::function<void(VisualStyle)> cb) {
    callbacks().append(cb);
    cb(current());
}

inline QTimer *startPolling(QObject *parent, std::function<void()> onChange) {
    QTimer *timer = new QTimer(parent);
    timer->setInterval(400);
    int *last = new int(static_cast<int>(current()));
    QObject::connect(timer, &QTimer::timeout, [last, onChange]() {
        QFile f(path());
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return;
        int n = doc.object().value("style").toInt(*last);
        if (n < 0 || n > 3) n = 0;
        if (n == *last) return;
        *last = n;
        current() = static_cast<VisualStyle>(n);
        onChange();
    });
    timer->start();
    return timer;
}

inline QString name(VisualStyle s) {
    switch (s) {
        case VisualStyle::Glass:         return "Glass";
        case VisualStyle::Neumorphism:   return "Neumorphism";
        case VisualStyle::Skeuomorphism: return "Skeuomorphism";
        case VisualStyle::Claymorphism:  return "Claymorphism";
    }
    return "Glass";
}

}

namespace StyleRenderer {

// Draw the background of a card / panel / window.
// 'alphaHint' is the widget opacity preference; each style interprets it.
inline void drawPanel(QPainter &p, const QRectF &r, qreal radius,
                      const Theme &t, int alphaHint)
{
    VisualStyle s = StyleManager::current();
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);

    switch (s) {
    case VisualStyle::Glass: {
        // Translucent + faint border
        QColor top = t.panelBg.lighter(118);
        QColor bot = t.panelBg.darker(135);
        top.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bot);
        p.fillPath(path, g);

        QColor edge = t.chromeBorder;
        p.setPen(QPen(edge, 1));
        p.drawPath(path);
        break;
    }

    case VisualStyle::Neumorphism: {
        // Same surface color as background, extruded via paired shadows.
        QColor base(50, 50, 60, alphaHint);
        p.fillPath(path, base);

        // Light from top-left, shadow bottom-right
        p.setPen(Qt::NoPen);
        for (int i = 4; i >= 1; --i) {
            QColor light(255, 255, 255, 10);
            p.setBrush(light);
            p.drawRoundedRect(r.adjusted(-i, -i, -i + 1, -i + 1),
                              radius + i, radius + i);
            QColor dark(0, 0, 0, 14);
            p.setBrush(dark);
            p.drawRoundedRect(r.adjusted(i - 1, i - 1, i, i),
                              radius + i, radius + i);
        }
        break;
    }

    case VisualStyle::Skeuomorphism: {
        // Vertical gradient that reads as polished metal/plastic.
        QColor top = t.panelBg.lighter(145);
        QColor mid = t.panelBg;
        QColor bot = t.panelBg.darker(160);
        top.setAlpha(alphaHint);
        mid.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, top);
        g.setColorAt(0.5, mid);
        g.setColorAt(1.0, bot);
        p.fillPath(path, g);

        // Glossy highlight — confined to top ~22%, softer
        QColor glossTop(255, 255, 255, 55);
        QColor glossBot(255, 255, 255, 0);
        qreal glossH = r.height() * 0.22;
        QLinearGradient glossGrad(r.topLeft() + QPointF(0, 2),
                                  r.topLeft() + QPointF(0, glossH));
        glossGrad.setColorAt(0.0, glossTop);
        glossGrad.setColorAt(1.0, glossBot);
        QPainterPath gl;
        gl.addRoundedRect(QRectF(r.left() + 2, r.top() + 2,
                                 r.width() - 4, glossH),
                          radius - 1, radius - 1);
        p.fillPath(gl, glossGrad);

        // Metallic bezel
        QColor bezel = t.textPrimary;
        bezel.setAlpha(90);
        p.setPen(QPen(bezel, 2));
        p.drawPath(path);

        // Inner thin light line for a beveled look
        QColor inner(255, 255, 255, 40);
        p.setPen(QPen(inner, 1));
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2),
                          radius - 1, radius - 1);
        break;
    }

    case VisualStyle::Claymorphism: {
        // Puffy pastel surface, big radius, soft everything.
        QColor top = t.panelBg.lighter(160);
        QColor bot = t.panelBg.lighter(115);
        top.setAlpha(alphaHint);
        bot.setAlpha(alphaHint);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bot);
        QPainterPath clay;
        clay.addRoundedRect(r, radius + 6, radius + 6);
        p.fillPath(clay, g);

        // Double outer shadow — light top-left, dark bottom-right, bigger
        p.setPen(Qt::NoPen);
        for (int i = 6; i >= 1; --i) {
            QColor dark(0, 0, 0, 8);
            p.setBrush(dark);
            p.drawRoundedRect(r.adjusted(i, i + 2, i + 2, i + 3),
                              radius + 6 + i, radius + 6 + i);
        }
        break;
    }
    }
}

}

namespace Sigil {

// Palette
// ---- Sigil: graphite and bone ----
inline QColor contentBg()   { return QColor(20, 20, 22); }
inline QColor cardBg()      { return QColor(30, 30, 32); }
inline QColor sidebarBg()   { return QColor(24, 24, 26); }
inline QColor headerBg()    { return QColor(28, 28, 30); }
inline QColor border()      { return QColor(255, 255, 255, 22); }
// Warm bone text — the ink of the system
inline QColor textPrimary() { return QColor(240, 237, 232); }
inline QColor textSecondary(){ return QColor(176, 173, 168); }
inline QColor textDim()     { return QColor(122, 120, 115); }
inline QColor rowHover()    { return QColor(255, 255, 255, 18); }
inline QColor rowSelected() { return QColor(58, 58, 62); }
// ---- Seal: the accent colour ----
// Sigil ships 7 curated seals. Every seal keeps the same
// three-tone structure (base / hover / press) and obeys the
// palette rule: warm, muted, earth-bound. No neon, no default blue.
inline QString &sealIdRef() {
    static QString id = "oxblood";
    return id;
}
inline void  setSealId(const QString &id) { sealIdRef() = id; }
inline QString sealId() { return sealIdRef(); }

inline QColor accent() {
    const QString id = sealIdRef();
    if (id == "violet")   return QColor(122, 106, 159);
    if (id == "cyan")     return QColor( 74, 143, 168);
    if (id == "gold")     return QColor(168, 138,  74);
    if (id == "forest")   return QColor( 74, 122,  90);
    if (id == "rose")     return QColor(168,  90, 122);
    if (id == "graphite") return QColor(106, 106, 112);
    return QColor(168, 50, 50);
}
inline QColor accentHover() {
    const QString id = sealIdRef();
    if (id == "violet")   return QColor(143, 127, 184);
    if (id == "cyan")     return QColor( 95, 176, 204);
    if (id == "gold")     return QColor(196, 168,  96);
    if (id == "forest")   return QColor( 92, 148, 112);
    if (id == "rose")     return QColor(196, 116, 148);
    if (id == "graphite") return QColor(130, 130, 138);
    return QColor(200, 74, 74);
}
inline QColor accentPress() {
    const QString id = sealIdRef();
    if (id == "violet")   return QColor( 92,  78, 125);
    if (id == "cyan")     return QColor( 53, 111, 133);
    if (id == "gold")     return QColor(133, 112,  47);
    if (id == "forest")   return QColor( 53,  96,  69);
    if (id == "rose")     return QColor(133,  68,  99);
    if (id == "graphite") return QColor( 79,  79,  84);
    return QColor(139, 26, 26);
}

// ---- Icon badge: colored rounded square with a glyph ----
class IconBadge : public QWidget {
public:
    IconBadge(const QString &glyph, const QColor &color,
              int size = 24, QWidget *parent = nullptr)
        : QWidget(parent), m_glyph(glyph), m_color(color), m_size(size) {
        setFixedSize(size, size);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect();
        QPainterPath path;
        path.addRoundedRect(r, 6, 6);
        p.fillPath(path, m_color);

        QFont f = font();
        f.setPixelSize(int(m_size * 0.55));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        p.drawText(r, Qt::AlignCenter, m_glyph);
    }
private:
    QString m_glyph;
    QColor  m_color;
    int     m_size;
};

// ---- Sidebar row: badge + label, selectable ----
// ---- Custom-paint badge (used for the Arc Reactor) ----
class CustomBadge : public QWidget {
public:
    CustomBadge(std::function<void(QPainter&, QRectF)> paint,
                int size = 22, QWidget *parent = nullptr)
        : QWidget(parent), m_paint(paint)
    {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!m_paint) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Clip to a smooth circle so badge edges never show corners
        QPainterPath clip;
        clip.addEllipse(QRectF(rect()));
        p.setClipPath(clip);

        m_paint(p, QRectF(rect()));
    }
private:
    std::function<void(QPainter&, QRectF)> m_paint;
};

// ---- Iron Man 2 style arc reactor mark ----
inline void drawArcReactor(QPainter &p, const QRectF &r)
{
    const QPointF c = r.center();
    const qreal   d = r.width();

    // Outer ring (cyan)
    QColor cyan(120, 220, 255);
    QColor cyanDim(80, 180, 220);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(16, 20, 28));
    p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));

    QPen outerPen(cyanDim, 1.1);
    p.setPen(outerPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r.adjusted(1.0, 1.0, -1.0, -1.0));

    // Mid ring
    qreal inset = d * 0.20;
    QRectF mid = r.adjusted(inset, inset, -inset, -inset);
    QPen midPen(QColor(cyanDim.red(), cyanDim.green(), cyanDim.blue(), 180), 0.8);
    p.setPen(midPen);
    p.drawEllipse(mid);

    // Triangle (pointing down — Iron Man 2 look)
    qreal s = d * 0.26;
    QPolygonF tri;
    tri << QPointF(c.x(),              c.y() - s)
        << QPointF(c.x() - s * 0.866,  c.y() + s * 0.5)
        << QPointF(c.x() + s * 0.866,  c.y() + s * 0.5);
    QPen triPen(cyan, 1.1);
    triPen.setJoinStyle(Qt::RoundJoin);
    p.setPen(triPen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(tri);

    // Inner glow at center
    QRadialGradient g(c, d * 0.24);
    g.setColorAt(0.0, QColor(230, 250, 255, 255));
    g.setColorAt(0.5, QColor(140, 230, 255, 220));
    g.setColorAt(1.0, QColor(80, 180, 220, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, d * 0.24, d * 0.24);
}

// ---- OmniStorage mark: clean infinity symbol ----
inline void drawOmniStorage(QPainter &p, const QRectF &r)
{
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c = r.center();
    const qreal   d = r.width();

    // Two circles whose edges touch, forming an infinity sign.
    // Left lobe = structured (hard), right lobe = smooth.
    const qreal lobeR = d * 0.235;
    const qreal leftCx  = c.x() - lobeR;
    const qreal rightCx = c.x() + lobeR;

    QPen pen(QColor(240, 237, 232), 1.7);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Left lobe: rounded square-ish — starts sharp, ends smooth
    // We render two perfect circles — the fusion IS the infinity.
    p.drawEllipse(QPointF(leftCx,  c.y()), lobeR, lobeR);
    p.drawEllipse(QPointF(rightCx, c.y()), lobeR, lobeR);
}

class SidebarRow : public QWidget {
public:
    SidebarRow(const QString &label, const QString &glyph,
               const QColor &color, QWidget *parent = nullptr,
               std::function<void(QPainter&, QRectF)> customPaint = nullptr)
        : QWidget(parent), m_label(label)
    {
        setFixedHeight(29);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);

        QHBoxLayout *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 0, 10, 0);
        h->setSpacing(9);

        if (customPaint) {
            CustomBadge *badge = new CustomBadge(customPaint, 22);
            h->addWidget(badge);
        } else {
            IconBadge *badge = new IconBadge(glyph, color, 22);
            badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            h->addWidget(badge);
        }

        m_text = new QLabel(label);
        m_text->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_text->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(Sigil::textPrimary().name()));
        h->addWidget(m_text);
        h->addStretch();
    }

    void setSelected(bool s) { m_selected = s; update(); }
    bool isSelected() const  { return m_selected; }
    QString label() const    { return m_label; }

    std::function<void()> onActivate;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 2, -2, -2), 7, 7);
        if (m_selected)      p.fillPath(path, rowSelected());
        else if (m_hover)    p.fillPath(path, rowHover());

        // Oxblood accent stripe on the left of the selected row
        if (m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(Sigil::accent());
            QRectF stripe(4, 8, 3, height() - 16);
            p.drawRoundedRect(stripe, 1.5, 1.5);
        }
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *)      override { m_hover = false; update(); }
    void mousePressEvent(QMouseEvent *) override {
        if (onActivate) onActivate();
    }

private:
    QString m_label;
    QLabel *m_text = nullptr;
    bool    m_selected = false;
    bool    m_hover = false;
};

// ---- iOS-style toggle switch ----
class Toggle : public QWidget {
public:
    Toggle(bool initial = false, QWidget *parent = nullptr)
        : QWidget(parent), m_on(initial), m_slide(initial ? 1.0 : 0.0)
    {
        setFixedSize(40, 22);
        setCursor(Qt::PointingHandCursor);
        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(16);
        QObject::connect(m_animTimer, &QTimer::timeout, this, [this]() {
            double target = m_on ? 1.0 : 0.0;
            double diff = target - m_slide;
            if (qAbs(diff) < 0.01) {
                m_slide = target;
                m_animTimer->stop();
            } else {
                m_slide += diff * 0.30;
            }
            update();
        });
    }

    bool isOn() const { return m_on; }
    void setOn(bool on) {
        if (m_on == on) return;
        m_on = on;
        m_animTimer->start();
        if (onChanged) onChanged(m_on);
    }
    // Programmatic state change — no onChanged callback.
    // Used when an async subprocess resolves and we need to show
    // the real value without writing it back to config.
    void setOnSilent(bool on) {
        if (m_on == on) return;
        m_on = on;
        m_animTimer->start();
    }

    std::function<void(bool)> onChanged;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(r, r.height() / 2, r.height() / 2);

        QColor off(60, 60, 64);
        QColor on(90, 200, 120);
        QColor bg(
            int(off.red()   + (on.red()   - off.red())   * m_slide),
            int(off.green() + (on.green() - off.green()) * m_slide),
            int(off.blue()  + (on.blue()  - off.blue())  * m_slide));
        p.fillPath(path, bg);

        double knobD = r.height() - 2;
        double knobX = 1 + (r.width() - knobD - 2) * m_slide;
        p.setBrush(QColor(255, 255, 255));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(knobX, 1, knobD, knobD));
    }

    void mousePressEvent(QMouseEvent *) override { setOn(!m_on); }

private:
    bool   m_on = false;
    double m_slide = 0.0;
    QTimer *m_animTimer = nullptr;
};

// ---- Card: grouped content block ----
class Card : public QWidget {
public:
    explicit Card(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_StyledBackground, true);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
        p.fillPath(path, cardBg());
        p.setPen(QPen(border(), 1));
        p.drawPath(path);
    }
};

// ---- Search bar: pill with magnifier ----
class SearchBar : public QLineEdit {
public:
    explicit SearchBar(QWidget *parent = nullptr) : QLineEdit(parent) {
        setPlaceholderText("Search");
        setFixedHeight(30);
        setStyleSheet(QString(
            "QLineEdit {"
            "  background: rgba(255,255,255,18);"
            "  color: %1;"
            "  border: none;"
            "  border-radius: 8px;"
            "  padding: 4px 12px;"
            "  font-size: 13px;"
            "}"
            "QLineEdit:focus { background: rgba(255,255,255,32); }")
            .arg(textPrimary().name()));
    }
};

// ---- Section header label ----
inline QLabel *sectionTitle(const QString &text) {
    QLabel *l = new QLabel(text);
    l->setStyleSheet(QString(
        "color: %1; font-size: 20px; font-weight: 500;"
        " background: transparent; letter-spacing: 0.5px;")
        .arg(textPrimary().name()));
    return l;
}

// ---- Small group header inside content ----
inline QLabel *groupLabel(const QString &text) {
    QLabel *l = new QLabel(text);
    l->setStyleSheet(QString(
        "color: %1; font-size: 11px; font-weight: 600;"
        " letter-spacing: 1px; background: transparent;"
        " text-transform: uppercase;")
        .arg(textDim().name()));
    return l;
}

// ---- Dropdown: label + value + chevron, opens themed menu ----
class Dropdown : public QWidget {
public:
    Dropdown(const QStringList &items, int current,
             std::function<void(int)> onChange,
             QWidget *parent = nullptr)
        : QWidget(parent), m_items(items), m_current(current),
          m_onChange(onChange)
    {
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);

        QHBoxLayout *h = new QHBoxLayout(this);
        h->setContentsMargins(12, 0, 12, 0);
        h->setSpacing(8);

        m_value = new QLabel(items.value(current));
        m_value->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_value->setStyleSheet(QString(
            "color: %1; font-size: 13px; background: transparent;")
            .arg(textPrimary().name()));
        h->addWidget(m_value);
        h->addStretch();

        m_chevron = new QLabel(QString::fromUtf8("\xE2\x8C\x84"));
        m_chevron->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_chevron->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(textDim().name()));
        h->addWidget(m_chevron);
    }

    int current() const { return m_current; }

    // Replace options programmatically — no onChange callback.
    // Used when an async subprocess resolves with the real item
    // list (e.g. font families loaded after page construction).
    void setItemsSilent(const QStringList &items, int current = 0) {
        m_items = items;
        if (current < 0 || current >= items.size()) current = 0;
        m_current = current;
        if (m_value) m_value->setText(items.value(current));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 7, 7);
        QColor bg(255, 255, 255, m_hover ? 35 : 20);
        p.fillPath(path, bg);
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }

    void mousePressEvent(QMouseEvent *) override {
        QMenu m(this);
        m.setStyleSheet(QString(
            "QMenu {"
            "  background: rgb(24, 22, 30);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 10px;"
            "  padding: 6px;"
            "  color: %1;"
            "  font-size: 13px;"
            "}"
            "QMenu::item {"
            "  padding: 7px 24px 7px 16px;"
            "  border-radius: 6px;"
            "  background: transparent;"
            "}"
            "QMenu::item:selected { background: rgba(255,255,255,45); }")
            .arg(textPrimary().name()));
        for (int i = 0; i < m_items.size(); ++i) {
            QAction *a = m.addAction(m_items[i]);
            a->setCheckable(true);
            a->setChecked(i == m_current);
            QObject::connect(a, &QAction::triggered, [this, i]() {
                m_current = i;
                m_value->setText(m_items[i]);
                if (m_onChange) m_onChange(i);
            });
        }
        m.exec(mapToGlobal(QPoint(0, height() + 4)));
    }

private:
    QStringList               m_items;
    int                       m_current;
    std::function<void(int)>  m_onChange;
    QLabel                   *m_value = nullptr;
    QLabel                   *m_chevron = nullptr;
    bool                      m_hover = false;
};

// ---- Find the Toggle inside a row from makeToggleRow ---- //
// Used by async cards to update a toggle after a subprocess
// resolves, without writing back to config.
inline Toggle *toggleIn(QWidget *row) {
    if (!row) return nullptr;
    QWidget *w = row->findChild<QWidget *>("sigilToggle");
    return dynamic_cast<Toggle *>(w);
}

// ---- Toggle row: label + subtitle left, iOS toggle right ----
inline QWidget *makeToggleRow(const QString &title, const QString &sub,
                              bool initial,
                              std::function<void(bool)> onChange)
{
    QWidget *row = new QWidget;
    row->setAutoFillBackground(false);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 6, 0, 6);
    h->setSpacing(14);

    QVBoxLayout *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(2);

    QLabel *t = new QLabel(title);
    t->setStyleSheet(QString(
        "color: %1; font-size: 13px; background: transparent;")
        .arg(textPrimary().name()));
    left->addWidget(t);

    if (!sub.isEmpty()) {
        QLabel *s = new QLabel(sub);
        s->setStyleSheet(QString(
            "color: %1; font-size: 11px; background: transparent;")
            .arg(textDim().name()));
        left->addWidget(s);
    }
    h->addLayout(left, 1);

    Toggle *tg = new Toggle(initial);
    tg->setObjectName("sigilToggle");
    tg->onChanged = onChange;
    h->addWidget(tg, 0, Qt::AlignVCenter);

    return row;
}

}

#endif // APOKOLIPS_STYLE_H
