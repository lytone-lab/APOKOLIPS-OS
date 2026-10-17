#ifndef APOKOLIPS_MOSAIC_H
#define APOKOLIPS_MOSAIC_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include <QDirIterator>
#include <QImageReader>
#include <QScrollBar>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QListWidget>

// Mosaic — a library view of every image on the machine.
//
// Not a viewer — that's Aperture's job. Mosaic is the shelf:
// a justified-gallery wall of every image found in the standard
// folders, with a month timeline on the left and hover details
// on each tile. Double-click a tile to open in Aperture.

class MosaicApp : public ShellApp {
public:
    explicit MosaicApp(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("Mosaic", QSize(1100, 720), false);
        setShellWindowGeometry(this, QSize(1100, 720));
        setFocusPolicy(Qt::StrongFocus);

        buildUi();
        scanFolders();

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        relayout();
    }

private:
    // ---------- the item model ----------
    struct Item {
        QString  path;
        QString  name;
        QImage   thumb;   // cached, ~200 px
        QSize    origSize;
        qint64   mtime = 0;
        QString  monthKey;   // "2026-10"
        QString  monthLabel; // "October 2026"
    };

    QVector<Item> m_items;

    // ---------- widgets ----------
    QWidget      *m_header   = nullptr;
    QListWidget  *m_timeline = nullptr;
    QScrollArea  *m_scroll   = nullptr;
    QWidget      *m_canvas   = nullptr;
    QSlider      *m_sizeSl   = nullptr;
    QLabel       *m_status   = nullptr;

    int  m_targetRowH = 180;
    int  m_lastRowH   = 180;
    int  m_lastCanvasW = 0;
    QHash<int, QPoint> m_monthOffsets;  // month index -> y offset

    // ---------- ui ----------
    void buildUi() {
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // Header
        {
            m_header = new QWidget;
            m_header->setFixedHeight(42);
            m_header->setAttribute(Qt::WA_NoSystemBackground, true);
            QHBoxLayout *hh = new QHBoxLayout(m_header);
            hh->setContentsMargins(16, 0, 16, 0);
            hh->setSpacing(10);
            QLabel *titleLabel = nullptr;
            QHBoxLayout *stdHdr = buildStandardHeader(
                "Mosaic", QSize(1100, 720), &titleLabel, nullptr);
            while (QLayoutItem *item = stdHdr->takeAt(0)) {
                if (QWidget *w = item->widget()) hh->addWidget(w);
                else if (QLayout *l = item->layout()) hh->addLayout(l);
                else if (item->spacerItem())
                    hh->addSpacerItem(item->spacerItem());
                else delete item;
            }
            delete stdHdr;
            root->addWidget(m_header);
        }

        // Toolbar — size slider + count
        {
            QWidget *tb = new QWidget;
            tb->setFixedHeight(38);
            tb->setAttribute(Qt::WA_NoSystemBackground, true);
            QHBoxLayout *th = new QHBoxLayout(tb);
            th->setContentsMargins(18, 6, 18, 6);
            th->setSpacing(12);

            QLabel *l = new QLabel("Tile size");
            l->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            th->addWidget(l);

            m_sizeSl = new QSlider(Qt::Horizontal);
            m_sizeSl->setRange(100, 320);
            m_sizeSl->setValue(m_targetRowH);
            m_sizeSl->setFixedWidth(180);
            m_sizeSl->setStyleSheet(
                "QSlider::groove:horizontal { height: 4px;"
                "  background: rgba(255,255,255,25);"
                "  border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  background: #ffffff; width: 12px; height: 12px;"
                "  margin: -4px 0; border-radius: 6px; }"
                "QSlider::sub-page:horizontal {"
                "  background: rgb(168,50,50);"
                "  border-radius: 2px; }");
            QObject::connect(m_sizeSl, &QSlider::valueChanged,
                             [this](int v) {
                m_targetRowH = v;
                relayout();
            });
            th->addWidget(m_sizeSl);
            th->addStretch();

            m_status = new QLabel("");
            m_status->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent;")
                .arg(Sigil::textDim().name()));
            th->addWidget(m_status);

            root->addWidget(tb);
        }

        // Body: timeline + scroll area
        QHBoxLayout *body = new QHBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        // Timeline sidebar
        m_timeline = new QListWidget;
        m_timeline->setFixedWidth(180);
        m_timeline->setFrameShape(QFrame::NoFrame);
        m_timeline->setStyleSheet(
            "QListWidget {"
            "  background: rgba(0,0,0,80);"
            "  border: none;"
            "  color: rgb(200, 197, 192);"
            "  font-size: 12px;"
            "  padding: 10px 6px;"
            "}"
            "QListWidget::item {"
            "  padding: 8px 12px;"
            "  border-radius: 6px;"
            "}"
            "QListWidget::item:hover {"
            "  background: rgba(255,255,255,20);"
            "}"
            "QListWidget::item:selected {"
            "  background: rgba(168,50,80,120);"
            "  color: #ffffff;"
            "}");
        QObject::connect(m_timeline, &QListWidget::itemClicked,
                         [this](QListWidgetItem *it) {
            int idx = it->data(Qt::UserRole).toInt();
            if (m_monthOffsets.contains(idx)) {
                int y = m_monthOffsets.value(idx).y();
                m_scroll->verticalScrollBar()->setValue(y);
            }
        });
        body->addWidget(m_timeline);

        // Scroll area
        m_scroll = new QScrollArea;
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical {"
            "  background: transparent; width: 10px; margin: 0;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: rgba(255,255,255,60);"
            "  border-radius: 5px; min-height: 30px;"
            "}"
            "QScrollBar::add-line:vertical,"
            "QScrollBar::sub-line:vertical { height: 0; }");

        m_canvas = new QWidget;
        m_canvas->setAttribute(Qt::WA_TranslucentBackground, true);
        m_canvas->installEventFilter(this);
        m_scroll->setWidget(m_canvas);
        body->addWidget(m_scroll, 1);

        root->addLayout(body, 1);
    }

    // ---------- scanning ----------
    static bool isImageFile(const QString &p) {
        static const QStringList exts = {
            "png","jpg","jpeg","gif","bmp","svg","ico","webp"
        };
        return exts.contains(QFileInfo(p).suffix().toLower());
    }

    void scanFolders() {
        const QString home = QDir::homePath();
        QStringList roots = {
            home + "/Pictures",
            home + "/Desktop",
        };
        QSet<QString> seen;
        for (const QString &root : roots) {
            QDirIterator it(root,
                QDir::Files | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString p = it.next();
                if (!isImageFile(p) || seen.contains(p)) continue;
                seen.insert(p);
                Item item;
                item.path = p;
                item.name = QFileInfo(p).fileName();
                QFileInfo fi(p);
                item.mtime = fi.lastModified().toSecsSinceEpoch();
                QDateTime dt = fi.lastModified();
                item.monthKey   = dt.toString("yyyy-MM");
                item.monthLabel = dt.toString("MMMM yyyy");
                m_items.append(item);
            }
        }
        // Sort newest first
        std::sort(m_items.begin(), m_items.end(),
                  [](const Item &a, const Item &b) {
                      return a.mtime > b.mtime;
                  });

        // Load thumbs (synchronous — the machine will have a
        // few hundred at most, and we downscale hard).
        for (Item &it : m_items) {
            QImageReader r(it.path);
            r.setAutoTransform(true);
            it.origSize = r.size();
            r.setScaledSize(QSize(240, 240));
            QImage img = r.read();
            if (!img.isNull()) it.thumb = img;
        }

        // Build timeline (distinct months in order)
        QSet<QString> seenMonths;
        for (int i = 0; i < m_items.size(); ++i) {
            const Item &it = m_items[i];
            if (seenMonths.contains(it.monthKey)) continue;
            seenMonths.insert(it.monthKey);
            QListWidgetItem *row = new QListWidgetItem(it.monthLabel);
            row->setData(Qt::UserRole, i);
            m_timeline->addItem(row);
        }

        m_status->setText(QString("%1 images").arg(m_items.size()));
        relayout();
    }

    // ---------- layout (justified gallery) ----------
    void relayout() {
        if (!m_canvas) return;
        const int cw = m_scroll->viewport()->width() - 24;
        if (cw < 200) return;
        // Redraw if EITHER the canvas width OR the target row
        // height changed. Slider drags change only the latter.
        if (cw == m_lastCanvasW && m_targetRowH == m_lastRowH)
            return;
        m_lastCanvasW = cw;
        m_lastRowH    = m_targetRowH;

        const int gap = 6;
        const int rowH = m_targetRowH;

        // Group items into rows by target height, then scale each
        // row to fit exactly.
        QVector<QVector<int>> rows;    // indices into m_items
        QVector<int>        curRow;
        qreal curAspectSum = 0.0;
        for (int i = 0; i < m_items.size(); ++i) {
            const Item &it = m_items[i];
            if (it.thumb.isNull()) continue;
            qreal aspect = qreal(it.thumb.width())
                         / qMax(1, it.thumb.height());
            curRow.append(i);
            curAspectSum += aspect;
            // Widths at target height + gaps reach canvas width?
            qreal neededW = curAspectSum * rowH
                          + gap * (curRow.size() - 1);
            if (neededW >= cw) {
                rows.append(curRow);
                curRow.clear();
                curAspectSum = 0.0;
            }
        }
        if (!curRow.isEmpty()) rows.append(curRow);

        // Compute canvas height
        int y = 12;
        m_monthOffsets.clear();
        QSet<QString> monthSeen;
        for (int r = 0; r < rows.size(); ++r) {
            int rowItems = rows[r].size();
            qreal aspectSum = 0.0;
            for (int idx : rows[r]) {
                const Item &it = m_items[idx];
                aspectSum += qreal(it.thumb.width())
                           / qMax(1, it.thumb.height());
            }
            int availW = cw - gap * (rowItems - 1);
            int scaledH = int(availW / qMax(0.0001, aspectSum));
            scaledH = qBound(80, scaledH, 400);
            y += scaledH + gap;
        }
        y += 12;

        m_canvas->setMinimumHeight(y);
        m_canvas->setMinimumWidth(cw);
        m_canvas->update();
    }

    // ---------- painting ----------
    void paintEvent(QPaintEvent *e) override {
        // ShellApp draws the rounded body, glass header, and border.
        ShellApp::paintEvent(e);
    }

    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj == m_canvas && ev->type() == QEvent::Paint) {
            paintCanvas();
            return true;
        }
        if (obj == m_canvas && ev->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(ev);
            int idx = itemAt(me->pos());
            if (idx >= 0) openInAperture(m_items[idx].path);
            return true;
        }
        if (obj == m_canvas && ev->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(ev);
            int idx = itemAt(me->pos());
            if (idx != m_hoverIdx) {
                m_hoverIdx = idx;
                m_canvas->update();
            }
            return false;
        }
        if (obj == m_canvas && ev->type() == QEvent::ContextMenu) {
            auto *me = static_cast<QContextMenuEvent *>(ev);
            int idx = itemAt(me->pos());
            if (idx >= 0) showContextMenu(idx,
                m_canvas->mapToGlobal(me->pos()));
            return true;
        }
        if (obj == m_canvas && ev->type() == QEvent::Leave) {
            m_hoverIdx = -1;
            m_canvas->update();
        }
        return ShellApp::eventFilter(obj, ev);
    }

    int m_hoverIdx = -1;
    QHash<int, QRect> m_tileRects;
    QHash<int, int>   m_tileHeights;

    void paintCanvas() {
        QPainter p(m_canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(m_canvas->rect(), QColor(16, 16, 20));

        const int cw = m_scroll->viewport()->width() - 24;
        const int gap = 6;
        const int rowH = m_targetRowH;

        int y = 12;
        m_tileRects.clear();
        m_tileHeights.clear();

        // Re-run grouping (same math as relayout).
        QVector<QVector<int>> rows;
        QVector<int> curRow;
        qreal curAspectSum = 0.0;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].thumb.isNull()) continue;
            qreal aspect = qreal(m_items[i].thumb.width())
                         / qMax(1, m_items[i].thumb.height());
            curRow.append(i);
            curAspectSum += aspect;
            qreal neededW = curAspectSum * rowH
                          + gap * (curRow.size() - 1);
            if (neededW >= cw) {
                rows.append(curRow);
                curRow.clear();
                curAspectSum = 0.0;
            }
        }
        if (!curRow.isEmpty()) rows.append(curRow);

        const QColor textPrimary = Sigil::textPrimary();
        const QColor textDim     = Sigil::textDim();
        const QColor accent      = Sigil::accent();

        for (int r = 0; r < rows.size(); ++r) {
            const QVector<int> &row = rows[r];
            qreal aspectSum = 0.0;
            for (int idx : row) {
                aspectSum += qreal(m_items[idx].thumb.width())
                           / qMax(1, m_items[idx].thumb.height());
            }
            int availW = cw - gap * (row.size() - 1);
            int scaledH = qBound(80, int(availW / qMax(0.0001, aspectSum)), 400);

            int x = 12;
            for (int idx : row) {
                const Item &it = m_items[idx];
                qreal aspect = qreal(it.thumb.width())
                             / qMax(1, it.thumb.height());
                int tw = int(scaledH * aspect);
                QRect tile(x, y, tw, scaledH);
                m_tileRects.insert(idx, tile);
                m_tileHeights.insert(idx, scaledH);

                // Image
                QRectF dst(tile);
                QImage scaled = it.thumb.scaled(
                    tile.size(), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
                QRect src(0, 0,
                    qMin(scaled.width(),  tile.width()),
                    qMin(scaled.height(), tile.height()));
                src.moveCenter(QPoint(scaled.width() / 2,
                                      scaled.height() / 2));
                p.drawImage(dst, scaled, src);

                // Hover overlay
                if (idx == m_hoverIdx) {
                    QRectF ov = dst;
                    QLinearGradient g(ov.bottomLeft(), ov.topLeft());
                    g.setColorAt(0.0, QColor(0, 0, 0, 200));
                    g.setColorAt(1.0, QColor(0, 0, 0, 0));
                    p.fillRect(ov, g);

                    QFont f = font();
                    f.setPixelSize(11); f.setWeight(QFont::DemiBold);
                    p.setFont(f);
                    p.setPen(QColor(255, 255, 255));
                    QRect nameR(int(dst.left()) + 6,
                                int(dst.bottom()) - 34,
                                int(dst.width()) - 12, 14);
                    p.drawText(nameR, Qt::AlignLeft | Qt::AlignVCenter,
                               it.name);

                    f.setPixelSize(9); f.setWeight(QFont::Normal);
                    p.setFont(f);
                    p.setPen(QColor(200, 200, 200));
                    QRect subR(int(dst.left()) + 6,
                               int(dst.bottom()) - 18,
                               int(dst.width()) - 12, 12);
                    QString sub = QString("%1 \xC2\xB7 %2\xC3\x97%3")
                        .arg(QDateTime::fromSecsSinceEpoch(it.mtime)
                             .toString("dd MMM yyyy"))
                        .arg(it.origSize.width())
                        .arg(it.origSize.height());
                    p.drawText(subR, Qt::AlignLeft | Qt::AlignVCenter,
                               sub);

                    // Accent stroke
                    p.setPen(QPen(accent, 2));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(dst.adjusted(1, 1, -1, -1));
                }

                x += tw + gap;
            }
            y += scaledH + gap;
        }
    }

    int itemAt(const QPoint &pt) {
        for (auto it = m_tileRects.constBegin();
             it != m_tileRects.constEnd(); ++it) {
            if (it.value().contains(pt)) return it.key();
        }
        return -1;
    }

    void openInAperture(const QString &path) {
        QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            { "--image", path });
    }

    void showContextMenu(int idx, const QPoint &global) {
        if (idx < 0 || idx >= m_items.size()) return;
        const Item &it = m_items[idx];
        ApoMenu m;
        QAction *openAct  = m.addAction("Open in Aperture");
        QAction *reveal   = m.addAction("Reveal in Finder");
        QAction *copyAct  = m.addAction("Copy path");
        m.addSeparator();
        QAction *trash    = m.addAction("Move to Trash");
        QAction *picked = m.exec(global);
        if (picked == openAct)   openInAperture(it.path);
        else if (picked == reveal) {
            QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                { "--files", QFileInfo(it.path).absolutePath() });
        } else if (picked == copyAct) {
            QProcess::startDetached("wl-copy", { it.path });
        } else if (picked == trash) {
            QProcess::startDetached("gio", { "trash", it.path });
        }
    }

    void restyle() {
        // Panels pull their look from stylesheets set at build time;
        // theme changes only affect painted text — update triggers
        // a repaint.
        if (m_canvas) m_canvas->update();
    }

    Theme m_theme;
};

#endif // APOKOLIPS_MOSAIC_H
