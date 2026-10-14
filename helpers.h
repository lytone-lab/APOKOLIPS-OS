#ifndef APOKOLIPS_HELPERS_H
#define APOKOLIPS_HELPERS_H

// WallpaperConfig + Finder-related widgets (Step 7 prep)

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"

class WallpaperConfig {
public:
    static QString configPath() {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::ConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
        return dir + "/apokolips/wallpaper.json";
    }

    static QString loadId() {
        QFile f(configPath());
        if (!f.open(QIODevice::ReadOnly)) return "ruby";
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return "ruby";
        return doc.object().value("wallpaper").toString("ruby");
    }

    static QString loadGifPath() {
        QFile f(configPath());
        if (!f.open(QIODevice::ReadOnly)) return {};
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return {};
        return doc.object().value("gifPath").toString();
    }

    static void saveGifPath(const QString &path) {
        QString p = configPath();
        QDir().mkpath(QFileInfo(p).absolutePath());
        QJsonObject o;
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) o = doc.object();
            f.close();
        }
        o["gifPath"] = path;
        f.open(QIODevice::WriteOnly);
        if (f.isOpen()) { f.write(QJsonDocument(o).toJson()); f.close(); }
    }

    
    static void saveId(const QString &id) {
        QString path = configPath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonObject obj;
        // Preserve gifPath if present
        QFile r(path);
        if (r.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(r.readAll());
            if (doc.isObject()) obj = doc.object();
            r.close();
        }
        obj["wallpaper"] = id;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(obj).toJson());
    }

    static Theme themeForId(const QString &id) {
        Theme t;
        if (id == "starfield") {
            t.chromeBg = QColor(10,14,30,205);
            t.chromeBorder = QColor(100,150,230,160);
            t.accent = QColor(120,180,255);
            t.accentSoft = QColor(120,180,255,90);
            t.accentStrong = QColor(120,180,255,160);
            t.textPrimary = QColor(220,230,245);
            t.textSecondary = QColor(160,184,216);
            t.textDim = QColor(96,128,168);
            t.panelBg = QColor(8,12,26,240);
        } else if (id == "aurora") {
            t.chromeBg = QColor(8,20,24,205);
            t.chromeBorder = QColor(80,200,180,160);
            t.accent = QColor(100,230,200);
            t.accentSoft = QColor(100,230,200,90);
            t.accentStrong = QColor(100,230,200,160);
            t.textPrimary = QColor(216,240,232);
            t.textSecondary = QColor(150,200,188);
            t.textDim = QColor(90,136,120);
            t.panelBg = QColor(6,16,20,240);
        } else if (id == "goldengate") {
            t.chromeBg = QColor(28,28,34,90);
            t.chromeBorder = QColor(255,255,255,55);
            t.accent = QColor(235,240,250);
            t.accentSoft = QColor(235,240,250,50);
            t.accentStrong = QColor(235,240,250,100);
            t.textPrimary = QColor(248,250,252);
            t.textSecondary = QColor(200,208,218);
            t.textDim = QColor(140,148,160);
            t.panelBg = QColor(14,14,18,165);
        } else if (id == "everest") {
            t.chromeBg = QColor(252,250,246,135);
            t.chromeBorder = QColor(80,84,96,40);
            t.accent = QColor(60,68,82);
            t.accentSoft = QColor(60,68,82,55);
            t.accentStrong = QColor(60,68,82,120);
            t.textPrimary = QColor(30,34,44);
            t.textSecondary = QColor(72,80,96);
            t.textDim = QColor(130,138,152);
            t.panelBg = QColor(250,248,246);
        } else {
            t.chromeBg = QColor(18,6,14,205);
            t.chromeBorder = QColor(230,70,110,170);
            t.accent = QColor(255,90,130);
            t.accentSoft = QColor(255,90,130,90);
            t.accentStrong = QColor(255,60,110,160);
            t.textPrimary = QColor(255,225,232);
            t.textSecondary = QColor(232,180,195);
            t.textDim = QColor(176,112,128);
            t.panelBg = QColor(14,6,12,240);
        }
        return t;
    }

    static Wallpaper *makeById(const QString &id) {
        if (id == "starfield")  return new StarfieldWallpaper;
        if (id == "aurora")     return new AuroraWallpaper;
        if (id == "goldengate") return new GoldenGateWallpaper;
        if (id == "neonjinx" || id == "mob" || id == "vander") {
            QString file, name;
            if (id == "neonjinx")     { file = "neon-jinx-1080p.mp4";  name = "Neon Jinx";      }
            else if (id == "mob")     { file = "mob-1080p.mp4";        name = "Mob";            }
            else                      { file = "vander-1080p.mp4";     name = "Vander Warwick"; }
            return new VideoWallpaper({
                QDir::homePath() + "/apokolips-shell/assets/wallpapers/videos/" + file,
                "/home/lytone/apokolips-shell/assets/wallpapers/videos/" + file,
                "assets/wallpapers/videos/" + file,
                "/usr/share/apokolips/wallpapers/videos/" + file
            }, id, name);
        }
        if (id == "everest") {
            return new ImageWallpaper({
                QDir::homePath() +
                    "/apokolips-shell/assets/wallpapers/mount-everest.jpg",
                "/home/lytone/apokolips-shell/assets/wallpapers/mount-everest.jpg",
                "assets/wallpapers/mount-everest.jpg",
                "/usr/share/apokolips/wallpapers/mount-everest.jpg"
            }, "everest", "Mount Everest");
        }
        if (id == "apocalypse") {
            return new ImageWallpaper({
                QDir::homePath() +
                    "/apokolips-shell/assets/wallpapers/apocalypse.jpg",
                "/home/lytone/apokolips-shell/assets/wallpapers/apocalypse.jpg",
                "assets/wallpapers/apocalypse.jpg",
                "/usr/share/apokolips/wallpapers/apocalypse.jpg"
            }, "apocalypse", "Apocalypse");
        }
        if (id == "gif") {
            QString p = loadGifPath();
            if (!p.isEmpty() && QFile::exists(p))
                return new GifWallpaper(p);
        }
        // Default: ruby → Magnific image
        return new ImageWallpaper({
            QDir::homePath() +
                "/apokolips-shell/assets/wallpapers/magnific.jpg",
            "/home/lytone/apokolips-shell/assets/wallpapers/magnific.jpg",
            "assets/wallpapers/magnific.jpg",
            "/usr/share/apokolips/wallpapers/magnific.jpg"
        }, "ruby", "Magnific");
    }
};

class ApokolipsInputDialog : public QDialog {
public:
    static QString getText(QWidget *parent,
                           const QString &title,
                           const QString &label,
                           const QString &initialText,
                           bool *ok)
    {
        ApokolipsInputDialog dlg(title, label, initialText, parent);
        int r = dlg.exec();
        if (ok) *ok = (r == QDialog::Accepted);
        return (r == QDialog::Accepted) ? dlg.text() : QString();
    }

    ApokolipsInputDialog(const QString &title, const QString &label,
                         const QString &initialText, QWidget *parent)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        resize(420, 180);

        if (parent) {
            QPoint c = parent->mapToGlobal(
                QPoint(parent->width()/2, parent->height()/2));
            move(c.x() - 210, c.y() - 90);
        } else if (QScreen *s = QApplication::primaryScreen()) {
            QRect g = s->availableGeometry();
            move(g.center().x() - 210, g.center().y() - 90);
        }

        // Build blurred backdrop from wallpaper
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        if (wp) {
            const int DOWN = 8;
            QImage small(qMax(1, width()/DOWN), qMax(1, height()/DOWN),
                         QImage::Format_ARGB32_Premultiplied);
            small.fill(Qt::transparent);
            QPainter p(&small);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(1.0/DOWN, 1.0/DOWN);
            wp->paint(p, QRect(0, 0, width(), height()));
            p.end();
            m_blurredBg = QPixmap::fromImage(
                small.scaled(size(), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation));
            delete wp;
        }

        buildUi(title, label, initialText);
        m_edit->setFocus();
        m_edit->selectAll();

        // Subscribe AFTER buildUi so restyle() has widgets to touch
        m_theme = ThemeManager::instance().current();
        restyle();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        StyleManager::startPolling(this, [this]() { update(); });
    }

    QString text() const { return m_edit->text(); }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            finish(QDialog::Rejected);
        } else if (e->key() == Qt::Key_Return ||
                   e->key() == Qt::Key_Enter) {
            finish(QDialog::Accepted);
        } else {
            QWidget::keyPressEvent(e);
        }
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect frameRect = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(frameRect, 14, 14);

        // Soft edge shadow
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = m_theme.textPrimary;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(frameRect.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // Blurred wallpaper backdrop
        if (!m_blurredBg.isNull()) {
            p.save();
            p.setClipPath(path);
            p.setOpacity(0.85);
            p.drawPixmap(rect(), m_blurredBg);
            p.restore();
        }

        // Style-aware body
        StyleRenderer::drawPanel(p, QRectF(frameRect), 14, m_theme, 200);

        // Header strip
        QPainterPath hdr;
        hdr.addRoundedRect(QRect(0, 0, width(), 36), 14, 14);
        QPainterPath sq;
        sq.addRect(QRect(0, 14, width(), 22));
        QPainterPath hdrFinal = hdr.united(sq);
        QColor hdrCol = m_theme.chromeBg;
        hdrCol.setAlpha(150);
        p.fillPath(hdrFinal, hdrCol);

        // Header separator
        p.setPen(QPen(m_theme.chromeBorder, 1));
        p.drawLine(0, 36, width(), 36);
    }

private:
    void buildUi(const QString &title, const QString &label,
                 const QString &initialText)
    {
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Header: traffic lights on left, title centered
        QWidget *header = new QWidget;
        header->setFixedHeight(36);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hl = new QHBoxLayout(header);
        hl->setContentsMargins(12, 0, 12, 0);
        hl->setSpacing(8);

        auto makeLight = [](const QString &idle, const QString &hover,
                             const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(12, 12);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1;"
                "  border: 1px solid rgba(0,0,0,60); border-radius: 6px;"
                "  color: transparent;"
                "  font-size: 9px;"
                "  font-weight: bold;"
                "  padding: 0; }"
                "QPushButton:hover { background: %2;"
                "  color: rgba(0,0,0,180); }").arg(idle, hover));
            return b;
        };

        QPushButton *closeBtn = makeLight("#7a2b25", "#ff5f57",
                                          QString::fromUtf8("\xC3\x97"));
        QPushButton *minBtn   = makeLight("#7a5b18", "#febc2e",
                                          QString::fromUtf8("\xE2\x88\x92"));
        QPushButton *maxBtn   = makeLight("#155c1e", "#28c840",
                                          QString::fromUtf8("+"));

        hl->addWidget(closeBtn);
        hl->addWidget(minBtn);
        hl->addWidget(maxBtn);
        hl->addSpacing(10);

        m_title = new QLabel(title);
        m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        hl->addWidget(m_title);
        hl->addStretch();

        QObject::connect(closeBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Rejected); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         [this]() { showMinimized(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            static bool z = false;
            resize(z ? QSize(420, 180) : QSize(700, 320));
            z = !z;
        });

        root->addWidget(header);

        // Body: label + line edit + buttons
        QWidget *body = new QWidget;
        body->setAttribute(Qt::WA_NoSystemBackground, true);
        QVBoxLayout *bl = new QVBoxLayout(body);
        bl->setContentsMargins(20, 14, 20, 16);
        bl->setSpacing(10);

        m_label = new QLabel(label);
        bl->addWidget(m_label);

        m_edit = new QLineEdit(initialText);
        bl->addWidget(m_edit);

        QHBoxLayout *btnRow = new QHBoxLayout;
        btnRow->addStretch();

        m_cancelBtn = new QPushButton("Cancel");
        m_cancelBtn->setCursor(Qt::PointingHandCursor);
        m_cancelBtn->setFixedHeight(28);
        btnRow->addWidget(m_cancelBtn);

        m_okBtn = new QPushButton("OK");
        m_okBtn->setCursor(Qt::PointingHandCursor);
        m_okBtn->setFixedHeight(28);
        m_okBtn->setDefault(true);
        btnRow->addWidget(m_okBtn);

        QObject::connect(m_okBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Accepted); });
        QObject::connect(m_cancelBtn, &QPushButton::clicked,
                         [this]() { finish(QDialog::Rejected); });

        bl->addLayout(btnRow);
        root->addWidget(body);
    }

    void restyle() {
        const Theme &t = m_theme;

        m_title->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));

        m_label->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(t.textSecondary.name()));

        m_edit->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2;"
            "  border: 1px solid %3; border-radius: 8px;"
            "  padding: 8px 12px; font-size: 13px;"
            "  selection-background-color: %4; }"
            "QLineEdit:focus { border: 1px solid %5; }")
            .arg(rgba(t.chromeBg.lighter(130)),
                 t.textPrimary.name(),
                 rgba(t.chromeBorder),
                 rgba(t.accentSoft),
                 t.accent.name()));

        QString btnCss = QString(
            "QPushButton { background: %1; color: %2;"
            "  border: 1px solid %3; border-radius: 8px;"
            "  padding: 4px 18px; font-size: 12px; }"
            "QPushButton:hover { background: %4; }"
            "QPushButton:pressed { background: %5; }")
            .arg(rgba(t.chromeBg.lighter(120)),
                 t.textPrimary.name(),
                 rgba(t.chromeBorder),
                 rgba(t.accentSoft),
                 rgba(t.accentStrong));

        m_okBtn->setStyleSheet(btnCss);
        m_cancelBtn->setStyleSheet(btnCss);
    }

    void finish(int result) {
        if (result == QDialog::Accepted) accept();
        else                             reject();
    }

    QLabel *m_title = nullptr;
    QLabel *m_label = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPixmap m_blurredBg;
    Theme m_theme;
};

class QuickLook : public QWidget {
public:
    QuickLook(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::StrongFocus);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setContentsMargins(20, 20, 20, 20);

        m_container = new QWidget(this);
        m_container->setObjectName("qlContainer");
        QVBoxLayout *cv = new QVBoxLayout(m_container);
        cv->setContentsMargins(16, 16, 16, 16);
        cv->setSpacing(12);

        m_title = new QLabel;
        m_title->setAlignment(Qt::AlignCenter);
        cv->addWidget(m_title);

        m_stack = new QStackedWidget;
        m_image = new QLabel;
        m_image->setAlignment(Qt::AlignCenter);
        m_image->setMinimumSize(200, 200);
        m_text = new QTextEdit;
        m_text->setReadOnly(true);
        m_stack->addWidget(m_image);
        m_stack->addWidget(m_text);
        cv->addWidget(m_stack, 1);

        m_meta = new QLabel;
        m_meta->setAlignment(Qt::AlignCenter);
        cv->addWidget(m_meta);

        v->addWidget(m_container);

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
        });
    }

    void show(const QString &path) {
        m_path = path;
        QFileInfo fi(path);
        m_title->setText(fi.fileName());
        m_meta->setText(QString("%1  ·  %2")
            .arg(fi.isDir() ? "Folder" : "File")
            .arg(humanSize(fi.size())));

        QString ext = fi.suffix().toLower();
        bool isImage = QStringList({"png","jpg","jpeg","gif","bmp",
            "webp","svg","tiff","ico"}).contains(ext);

        if (isImage) {
            QPixmap px(path);
            if (!px.isNull()) {
                m_stack->setCurrentWidget(m_image);
                if (px.width() > 900 || px.height() > 600)
                    px = px.scaled(900, 600, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
                m_image->setPixmap(px);
            } else {
                m_stack->setCurrentWidget(m_text);
                m_text->setPlainText("(Unable to load image)");
            }
        } else if (fi.size() < 512 * 1024) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                m_text->setPlainText(QString::fromUtf8(f.readAll()));
                f.close();
            }
            m_stack->setCurrentWidget(m_text);
        } else {
            m_stack->setCurrentWidget(m_text);
            m_text->setPlainText("(File too large to preview)");
        }

        if (parentWidget()) {
            resize(parentWidget()->size().width() * 3 / 4,
                   parentWidget()->size().height() * 3 / 4);
            move(parentWidget()->mapToGlobal(
                QPoint(parentWidget()->width()/8,
                       parentWidget()->height()/8)));
        }

        QWidget::show(); raise(); setFocus();
    }

    static QString humanSize(qint64 bytes) {
        if (bytes < 1024) return QString("%1 B").arg(bytes);
        if (bytes < 1024*1024) return QString("%1 KB").arg(bytes/1024.0, 0, 'f', 1);
        if (bytes < 1024*1024*1024) return QString("%1 MB").arg(bytes/(1024.0*1024.0), 0, 'f', 1);
        return QString("%1 GB").arg(bytes/(1024.0*1024.0*1024.0), 0, 'f', 2);
    }

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Space) {
            hide(); e->accept();
        } else {
            QWidget::keyPressEvent(e);
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        // click outside → close
        if (!m_container->geometry().contains(e->pos()))
            hide();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 120));
    }

private:
    void restyle() {
        const Theme &t = m_theme;
        QString panel = QString(
            "#qlContainer {"
            "  background: rgba(24, 22, 30, 245);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 14px;"
            "}");
        m_container->setStyleSheet(panel);
        m_title->setStyleSheet(QString(
            "color: %1; font-size: 15px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));
        m_meta->setStyleSheet(QString(
            "color: %1; font-size: 11px; background: transparent;")
            .arg(t.textDim.name()));
        m_text->setStyleSheet(QString(
            "QTextEdit { background: rgba(0,0,0,120); color: %1;"
            "  border: none; border-radius: 8px; padding: 8px;"
            "  font-family: 'Consolas','Menlo',monospace; font-size: 12px; }")
            .arg(t.textPrimary.name()));
    }

    QString        m_path;
    QWidget       *m_container = nullptr;
    QLabel        *m_title = nullptr;
    QLabel        *m_image = nullptr;
    QTextEdit     *m_text = nullptr;
    QStackedWidget*m_stack = nullptr;
    QLabel        *m_meta = nullptr;
    Theme          m_theme;
};

class FinderPlaceRow : public QWidget {
public:
    FinderPlaceRow(const QString &glyph, const QColor &color,
                   const QString &label, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(32);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);

        QHBoxLayout *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 0, 10, 0);
        h->setSpacing(10);

        QLabel *badge = new QLabel(glyph);
        badge->setFixedSize(20, 20);
        badge->setAlignment(Qt::AlignCenter);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        badge->setStyleSheet(QString(
            "background: rgb(%1,%2,%3);"
            "color: #ffffff;"
            "border-radius: 10px;"
            "font-size: 11px;")
            .arg(color.red()).arg(color.green()).arg(color.blue()));
        h->addWidget(badge);

        QLabel *txt = new QLabel(label);
        txt->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        txt->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 13px; background: transparent;");
        h->addWidget(txt);
        h->addStretch();

        m_selected = false;
        m_hover = false;
    }

    void setSelected(bool s) { m_selected = s; update(); }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 1, -2, -1), 7, 7);
        if (m_selected)
            p.fillPath(path, QColor(58, 58, 62));
        else if (m_hover)
            p.fillPath(path, QColor(255, 255, 255, 18));

        // Oxblood accent stripe on selected row
        if (m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(168, 50, 50));
            p.drawRoundedRect(QRectF(4, 6, 3, height() - 12), 1.5, 1.5);
        }
    }
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mousePressEvent(QMouseEvent *) override {
        if (onClick) onClick();
    }
private:
    bool m_selected;
    bool m_hover;
};

// SysControl — system control helpers (added for Settings step)
namespace SysControl {

inline int audioGet() {
    QProcess p;
    p.start("wpctl", { "get-volume", "@DEFAULT_AUDIO_SINK@" });
    p.waitForFinished(500);
    QString out = QString::fromUtf8(p.readAllStandardOutput());
    // Output format: "Volume: 0.40" or "Volume: 0.40 [MUTED]"
    int idx = out.indexOf("Volume:");
    if (idx == -1) return 50;
    QString num = out.mid(idx + 7).trimmed().split(' ').first();
    bool ok = false;
    double v = num.toDouble(&ok);
    if (!ok) return 50;
    return int(v * 100.0 + 0.5);
}

inline void audioSet(int pct) {
    pct = qBound(0, pct, 150);
    QProcess::startDetached("wpctl",
        { "set-volume", "@DEFAULT_AUDIO_SINK@",
          QString::number(pct) + "%" });
}

inline int brightnessGet() {
    QDir d("/sys/class/backlight");
    if (!d.exists()) return 50;
    QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return 50;
    QFile maxF("/sys/class/backlight/" + entries.first() + "/max_brightness");
    QFile curF("/sys/class/backlight/" + entries.first() + "/brightness");
    if (!maxF.open(QIODevice::ReadOnly)) return 50;
    if (!curF.open(QIODevice::ReadOnly)) return 50;
    int mx = QString(maxF.readAll()).trimmed().toInt();
    int cu = QString(curF.readAll()).trimmed().toInt();
    if (mx <= 0) return 50;
    return int(100.0 * cu / mx + 0.5);
}

inline void brightnessSet(int pct) {
    QDir d("/sys/class/backlight");
    if (!d.exists()) return;
    QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return;
    QFile maxF("/sys/class/backlight/" + entries.first() + "/max_brightness");
    if (!maxF.open(QIODevice::ReadOnly)) return;
    int mx = QString(maxF.readAll()).trimmed().toInt();
    if (mx <= 0) return;
    int target = qBound(0, pct, 100) * mx / 100;
    QFile curF("/sys/class/backlight/" + entries.first() + "/brightness");
    if (!curF.open(QIODevice::WriteOnly)) return;
    curF.write(QString::number(target).toUtf8());
}

}



// --- Cleanup pass ---
class MouseSpy : public QObject {
public:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            qDebug() << "[spy] target="
                     << (obj ? obj->metaObject()->className() : "null")
                     << "btn=" << me->button()
                     << "pos=" << me->position();
        }
        return QObject::eventFilter(obj, event);
    }
};

#endif // APOKOLIPS_HELPERS_H
