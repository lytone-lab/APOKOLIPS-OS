#ifndef APOKOLIPS_APERTURE_H
#define APOKOLIPS_APERTURE_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include "helpers.h"

class ImageCanvas : public QWidget {
public:
    ImageCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
    }

    void setPixmap(const QPixmap &pix) {
        m_pix = pix;
        m_zoom = 1.0;
        m_pan = QPoint(0, 0);
        m_fitOnNextResize = true;
        fitToWindow();
    }

    void fitToWindow() {
        if (m_pix.isNull() || width() <= 0 || height() <= 0) return;
        QSize img = m_pix.size();
        QSize box = size() - QSize(40, 40);
        double zw = double(box.width())  / img.width();
        double zh = double(box.height()) / img.height();
        m_zoom = qMin(zw, zh);
        if (m_zoom > 1.0) m_zoom = qMin(m_zoom, 1.0);
        m_pan = QPoint(0, 0);
        m_fitOnNextResize = false;
        update();
    }

    void zoomIn()  { m_zoom = qMin(m_zoom * 1.25, 8.0); update(); }
    void zoomOut() { m_zoom = qMax(m_zoom / 1.25, 0.05); update(); }
    void zoomReset() { m_zoom = 1.0; m_pan = QPoint(0,0); update(); }

    double zoom() const { return m_zoom; }

    std::function<void()> onPrev;
    std::function<void()> onNext;
    std::function<void()> onClose;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor(20, 20, 22));

        if (m_pix.isNull()) {
            p.setPen(QColor(122, 120, 115));
            p.setFont(QFont(qApp->font().family(), 12));
            p.drawText(rect(), Qt::AlignCenter, "No image");
            return;
        }

        QSize target = m_pix.size() * m_zoom;
        QRectF dst(0, 0, target.width(), target.height());
        dst.moveCenter(QPointF(rect().center()) + QPointF(m_pan));
        p.drawPixmap(dst, m_pix, QRectF(m_pix.rect()));
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_fitOnNextResize) fitToWindow();
    }

    void wheelEvent(QWheelEvent *e) override {
        double step = (e->angleDelta().y() > 0) ? 1.15 : 1/1.15;
        m_zoom = qBound(0.05, m_zoom * step, 8.0);
        update();
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton
            || e->button() == Qt::MiddleButton) {
            m_drag = true;
            m_dragStart = e->pos() - m_pan;
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_drag) {
            m_pan = e->pos() - m_dragStart;
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        m_drag = false;
        setCursor(Qt::OpenHandCursor);
    }

    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
            case Qt::Key_Left:  if (onPrev) onPrev(); return;
            case Qt::Key_Right: if (onNext) onNext(); return;
            case Qt::Key_Escape: if (onClose) onClose(); return;
            case Qt::Key_F: fitToWindow(); return;
            case Qt::Key_0: zoomReset(); return;
            case Qt::Key_Plus: case Qt::Key_Equal: zoomIn(); return;
            case Qt::Key_Minus: zoomOut(); return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    QPixmap  m_pix;
    double   m_zoom = 1.0;
    QPoint   m_pan;
    bool     m_drag = false;
    QPoint   m_dragStart;
    bool     m_fitOnNextResize = true;
};

class SigilImageWindow : public ShellApp {
public:
    explicit SigilImageWindow(const QString &path)
        : ShellApp(nullptr)
    {
        setupShellApp("Aperture", QSize(1000, 700), false);
        setAutoFillBackground(false);
        // Aperture opens 20px up — matches its taller toolbar.
        setShellWindowGeometry(this, QSize(1000, 700), QPoint(0, -20));

        // ---- Outer layout ----
        QVBoxLayout *root = new QVBoxLayout(this);
        // Bottom margin = rounded-corner radius. Keeps child
        // widgets from painting over the rounded bottom corners.
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // ---- Header (drag + traffic lights) ----
        QWidget *header = new QWidget;
        header->setFixedHeight(42);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        header->setAutoFillBackground(false);
        QHBoxLayout *hh = new QHBoxLayout(header);
        hh->setContentsMargins(16, 0, 16, 0);
        hh->setSpacing(10);

        // Standard header — lights + title via ShellApp.
        {
            QLabel *titleLabel = nullptr;
            QHBoxLayout *stdHdr = buildStandardHeader(
                "Aperture", QSize(1000, 700), &titleLabel, nullptr);
            while (QLayoutItem *item = stdHdr->takeAt(0)) {
                if (QWidget *w = item->widget()) hh->addWidget(w);
                else if (QLayout *l = item->layout()) hh->addLayout(l);
                else if (item->spacerItem())
                    hh->addSpacerItem(item->spacerItem());
                else delete item;
            }
            delete stdHdr;
            Q_UNUSED(titleLabel);
        }

        root->addWidget(header);

        // ---- Toolbar (nav + zoom + rotate) ----
        QWidget *toolbar = new QWidget;
        toolbar->setFixedHeight(44);
        toolbar->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *th = new QHBoxLayout(toolbar);
        th->setContentsMargins(16, 6, 16, 6);
        th->setSpacing(8);

        auto mkIcon = [](const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(30, 30);
            b->setCursor(Qt::PointingHandCursor);
            b->setFlat(true);
            b->setStyleSheet(
                "QPushButton {"
                "  color: rgb(240, 237, 232);"
                "  background: transparent;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 15px;"
                "}"
                "QPushButton:hover { background: rgba(255,255,255,25); }"
                "QPushButton:disabled { color: rgb(100, 100, 105); }");
            return b;
        };

        // Nav pill (back / forward arrows)
        QWidget *navPill = new QWidget;
        navPill->setFixedHeight(30);
        navPill->setStyleSheet(
            "QWidget { background: rgba(255,255,255,15);"
            "  border-radius: 8px; }");
        QHBoxLayout *np = new QHBoxLayout(navPill);
        np->setContentsMargins(2, 2, 2, 2);
        np->setSpacing(0);
        QPushButton *prevBtn = mkIcon(QString::fromUtf8("\xE2\x80\xB9"));
        QPushButton *nextBtn = mkIcon(QString::fromUtf8("\xE2\x80\xBA"));
        prevBtn->setFixedSize(26, 26);
        nextBtn->setFixedSize(26, 26);
        np->addWidget(prevBtn);
        np->addWidget(nextBtn);
        th->addWidget(navPill);

        // Filename label (center)
        QLabel *filename = new QLabel("—");
        filename->setAlignment(Qt::AlignCenter);
        filename->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 13px; font-weight: 500;"
            "background: transparent;");
        th->addWidget(filename, 1);

        // Zoom + fit + rotate (right)
        QPushButton *zoomOut = mkIcon(QString::fromUtf8("\xE2\x88\x92"));
        QPushButton *zoomIn  = mkIcon("+");
        QPushButton *fit     = mkIcon("F");
        QPushButton *rotate  = mkIcon(QString::fromUtf8("\xE2\x86\xBB"));
        th->addWidget(zoomOut);
        th->addWidget(zoomIn);
        th->addWidget(fit);
        th->addWidget(rotate);

        root->addWidget(toolbar);

        // ---- Canvas ----
        m_canvas = new ImageCanvas;
        root->addWidget(m_canvas, 1);

        // Accept file drops (Apokolips shared protocol)
        setAcceptDrops(true);
        m_dropOverlay = new DropOverlay(this);

        // ---- Status bar ----
        QWidget *status = new QWidget;
        status->setFixedHeight(28);
        QHBoxLayout *sh = new QHBoxLayout(status);
        sh->setContentsMargins(16, 0, 16, 0);
        m_statusLabel = new QLabel("");
        m_statusLabel->setStyleSheet(
            "color: rgb(122, 120, 115);"
            "font-size: 11px; background: transparent;");
        sh->addWidget(m_statusLabel);
        sh->addStretch();
        root->addWidget(status);

        // ---- State ----
        m_currentPath = path;
        loadFolderFor(path);
        loadFile(path);

        // ---- Wiring ----
        QObject::connect(prevBtn, &QPushButton::clicked,
                         [this]() { goPrev(); });
        QObject::connect(nextBtn, &QPushButton::clicked,
                         [this]() { goNext(); });
        QObject::connect(zoomIn, &QPushButton::clicked,
                         [this]() { m_canvas->zoomIn(); updateStatus(); });
        QObject::connect(zoomOut, &QPushButton::clicked,
                         [this]() { m_canvas->zoomOut(); updateStatus(); });
        QObject::connect(fit, &QPushButton::clicked,
                         [this]() { m_canvas->fitToWindow();
                                    updateStatus(); });
        QObject::connect(rotate, &QPushButton::clicked, [this]() {
            m_rotation = (m_rotation + 90) % 360;
            applyRotation();
        });

        m_canvas->onPrev = [this]() { goPrev(); };
        m_canvas->onNext = [this]() { goNext(); };
        m_canvas->onClose = [this]() { close(); };
        setShellWindowGeometry(this, QSize(1000, 700), QPoint(0, -20));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath full;
        full.addRoundedRect(body, 14, 14);

        p.fillPath(full, QColor(20, 20, 22));

        p.save();
        p.setClipPath(full);
        p.fillRect(QRect(0, 0, width(), 42), QColor(28, 28, 30));
        p.restore();

        p.setPen(QPen(QColor(255, 255, 255, 22), 1));
        p.drawLine(0, 42, width(), 42);
        p.drawLine(0, 86, width(), 86);

        p.setPen(QPen(QColor(255, 255, 255, 22), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(full);
    }

private:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (!e->mimeData()) { e->ignore(); return; }
        QString p = ApoDrag::acceptedPath(e->mimeData());
        if (p.isEmpty() || !isImageFile(p)) { e->ignore(); return; }
        e->acceptProposedAction();
        if (m_dropOverlay) m_dropOverlay->setActive(true);
    }
    void dragLeaveEvent(QDragLeaveEvent *) override {
        if (m_dropOverlay) m_dropOverlay->setActive(false);
    }
    void dropEvent(QDropEvent *e) override {
        if (m_dropOverlay) m_dropOverlay->setActive(false);
        if (!e->mimeData()) { e->ignore(); return; }
        QString p = ApoDrag::acceptedPath(e->mimeData());
        if (p.isEmpty() || !isImageFile(p)) { e->ignore(); return; }
        e->acceptProposedAction();
        m_siblings.clear();
        loadFolderFor(p);
        loadFile(p);
    }
    bool isImageFile(const QString &p) const {
        QFileInfo fi(p);
        static const QStringList exts = {
            "png","jpg","jpeg","gif","bmp","svg","ico","webp"
        };
        return exts.contains(fi.suffix().toLower());
    }
    void loadFolderFor(const QString &path) {
        QFileInfo fi(path);
        QDir d = fi.absoluteDir();
        QStringList filters = { "*.png", "*.jpg", "*.jpeg", "*.gif",
                                "*.bmp", "*.svg", "*.ico", "*.webp" };
        for (const QString &f :
             d.entryList(filters, QDir::Files, QDir::Name))
            m_siblings << d.filePath(f);
        m_index = m_siblings.indexOf(fi.absoluteFilePath());
    }

    void loadFile(const QString &path) {
        QPixmap p(path);
        if (p.isNull()) {
            m_statusLabel->setText("Could not load " + path);
            return;
        }
        m_origPix = p;
        m_rotation = 0;
        applyRotation();
        m_currentPath = path;
        QFileInfo fi(path);
        m_canvas->setPixmap(p);
        updateStatus();
    }

    void applyRotation() {
        if (m_origPix.isNull()) return;
        if (m_rotation == 0) {
            m_canvas->setPixmap(m_origPix);
        } else {
            QTransform t;
            t.rotate(m_rotation);
            QPixmap rotated = m_origPix.transformed(t,
                Qt::SmoothTransformation);
            m_canvas->setPixmap(rotated);
        }
    }

    void updateStatus() {
        QFileInfo fi(m_currentPath);
        double kb = fi.size() / 1024.0;
        QString sizeText = kb > 1024
            ? QString("%1 MB").arg(kb / 1024.0, 0, 'f', 1)
            : QString("%1 KB").arg(kb, 0, 'f', 0);
        QString dim = m_origPix.isNull() ? "?"
            : QString("%1 × %2")
                .arg(m_origPix.width()).arg(m_origPix.height());
        QString zoom = QString("%1%")
            .arg(int(m_canvas->zoom() * 100));
        m_statusLabel->setText(QString("%1    ·    %2    ·    %3    ·    %4")
            .arg(fi.fileName(), dim, sizeText, zoom));
    }

    void goPrev() {
        if (m_siblings.isEmpty()) return;
        m_index = (m_index - 1 + m_siblings.size()) % m_siblings.size();
        loadFile(m_siblings[m_index]);
    }
    void goNext() {
        if (m_siblings.isEmpty()) return;
        m_index = (m_index + 1) % m_siblings.size();
        loadFile(m_siblings[m_index]);
    }

    ImageCanvas *m_canvas = nullptr;
    DropOverlay *m_dropOverlay = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QStringList  m_siblings;
    int          m_index = -1;
    QString      m_currentPath;
    QPixmap      m_origPix;
    int          m_rotation = 0;
};

#endif // APOKOLIPS_APERTURE_H
