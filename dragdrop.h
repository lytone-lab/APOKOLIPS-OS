#ifndef APOKOLIPS_DRAGDROP_H
#define APOKOLIPS_DRAGDROP_H

// Shared drag-and-drop protocol — Apokolips flavour.
// Finder drags files out with a glowing thumbnail. Notes and
// Aperture accept the drop and glow their window borders.

#include <QDrag>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QWidget>
#include <QStringList>

namespace ApoDrag {

inline const char *kMimeFile = "application/x-apokolips-file";

inline QPixmap makeGlowThumb(const QString &path, int size = 96) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QFileInfo fi(path);
    static const QStringList imageExts = {
        "png","jpg","jpeg","gif","bmp","svg","ico","webp"
    };
    const bool isImage = imageExts.contains(fi.suffix().toLower());

    QPixmap src;
    if (isImage) src = QPixmap(path);

    const int pad = int(size * 0.22);
    QRect inner(pad, pad, size - pad*2, size - pad*2);

    if (!src.isNull()) {
        QPixmap scaled = src.scaled(inner.size(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect dst(QPoint(0,0), scaled.size());
        dst.moveCenter(QPoint(size/2, size/2));
        p.drawPixmap(dst, scaled);
    } else {
        p.setBrush(QColor(45, 43, 50));
        p.setPen(QPen(QColor(240,237,232,90), 1));
        p.drawRoundedRect(inner, 10, 10);
        p.setPen(QColor(200, 197, 192, 180));
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(11);
        p.setFont(f);
        QString ext = fi.suffix().toUpper();
        if (ext.isEmpty()) ext = QString::fromUtf8("\xE2\x80\xA2");
        p.drawText(inner, Qt::AlignCenter, ext);
    }

    QRadialGradient rg(QPointF(size/2.0, size/2.0), size/2.0);
    rg.setColorAt(0.0, QColor(168,50,80,0));
    rg.setColorAt(0.72, QColor(168,50,80,0));
    rg.setColorAt(0.88, QColor(168,50,80,180));
    rg.setColorAt(1.0, QColor(168,50,80,0));
    p.setBrush(rg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(size/2.0, size/2.0),
                  size/2.0 - 1, size/2.0 - 1);

    return pix;
}

inline void startFileDrag(QWidget *source, const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    QDrag *drag = new QDrag(source);
    QMimeData *mime = new QMimeData;
    mime->setUrls({ QUrl::fromLocalFile(path) });
    mime->setData(kMimeFile, path.toUtf8());
    drag->setMimeData(mime);
    QPixmap thumb = makeGlowThumb(path);
    drag->setPixmap(thumb);
    drag->setHotSpot(QPoint(thumb.width()/2, thumb.height()/2));
    drag->exec(Qt::CopyAction);
}

inline QString acceptedPath(const QMimeData *mime) {
    if (!mime) return {};
    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile()) return u.toLocalFile();
        }
    }
    if (mime->hasFormat(kMimeFile)) {
        return QString::fromUtf8(mime->data(kMimeFile));
    }
    return {};
}

} // namespace ApoDrag

class DropOverlay : public QWidget {
public:
    explicit DropOverlay(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        hide();
    }
    void setActive(bool active) {
        if (active == isVisible()) return;
        if (active) {
            if (parentWidget())
                setGeometry(parentWidget()->rect());
            show();
            raise();
        } else {
            hide();
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF body = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        QPainterPath path;
        path.addRoundedRect(body, 14, 14);
        for (int i = 6; i >= 1; --i) {
            p.setPen(QPen(QColor(168, 50, 80, 22), i));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setPen(QPen(QColor(168, 50, 80, 230), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    void resizeEvent(QResizeEvent *) override {
        if (parentWidget()) setGeometry(parentWidget()->rect());
    }
};

#endif
