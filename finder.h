#ifndef APOKOLIPS_FINDER_H
#define APOKOLIPS_FINDER_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include "helpers.h"
#include "theme_bridge.h"

class FileDragFilter final : public QObject {
public:
    FileDragFilter(QAbstractItemView *view, std::function<QString()> pathFn)
        : QObject(view), m_view(view), m_pathFn(std::move(pathFn)) {
        if (view && view->viewport())
            view->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                m_pressPos = me->pos();
                m_armed = true;
            }
        } else if (e->type() == QEvent::MouseMove && m_armed) {
            auto *me = static_cast<QMouseEvent*>(e);
            if ((me->pos() - m_pressPos).manhattanLength()
                >= QApplication::startDragDistance()) {
                m_armed = false;
                QString path = m_pathFn ? m_pathFn() : QString();
                if (!path.isEmpty() && QFileInfo::exists(path)) {
                    ApoDrag::startFileDrag(m_view, path);
                    return true;
                }
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_armed = false;
        }
        return QObject::eventFilter(obj, e);
    }
private:
    QAbstractItemView *m_view = nullptr;
    std::function<QString()> m_pathFn;
    QPoint m_pressPos;
    bool m_armed = false;
};

class FileExplorer : public ShellApp {
public:
    explicit FileExplorer(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("Files", QSize(900, 560), false);

        // Render the current wallpaper, blurred, as glass backdrop
        buildBlurredBackdrop();

        buildUi();

        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyleAll();
            update();
        });
        StyleManager::startPolling(this, [this]() { update(); });

        // Finder runs in its own process; the in-process subscribe
        // above only fires for writes that happen *inside this process*.
        // Settings writes visual.json from a different process, so we
        // watch the file itself and reload on change.
        {
            QString cfgPath = QStandardPaths::writableLocation(
                                  QStandardPaths::ConfigLocation);
            if (cfgPath.isEmpty())
                cfgPath = QDir::homePath() + "/.config";
            cfgPath += "/apokolips/visual.json";

            QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
            if (QFile::exists(cfgPath)) watcher->addPath(cfgPath);
            else {
                // Watch the directory until the file appears.
                QFileSystemWatcher *dirWatch =
                    new QFileSystemWatcher(this);
                dirWatch->addPath(QFileInfo(cfgPath).absolutePath());
                QObject::connect(dirWatch,
                    &QFileSystemWatcher::directoryChanged,
                    watcher, [watcher, cfgPath]() {
                        if (QFile::exists(cfgPath)
                            && !watcher->files().contains(cfgPath))
                            watcher->addPath(cfgPath);
                    });
            }
            QObject::connect(watcher, &QFileSystemWatcher::fileChanged,
                             this, [this, cfgPath, watcher]() {
                VisualConfigManager::instance().load();
                update();
                // Some editors replace the file; watcher drops it. Re-add.
                if (!watcher->files().contains(cfgPath)
                    && QFile::exists(cfgPath))
                    watcher->addPath(cfgPath);
            });
        }

        // Keep the in-process subscribe too — it fires when Settings
        // is opened from inside this same shell process.
        VisualConfigManager::instance().subscribe(
            [this](const VisualConfig &) {
                update();
            });

        // ---- Keyboard shortcuts ----
        auto sc = [this](const QString &key,
                         std::function<void()> fn) {
            QShortcut *s = new QShortcut(QKeySequence(key), this);
            QObject::connect(s, &QShortcut::activated, this, fn);
        };
        sc("F2",       [this]() { renameSelected();   });
        sc("Delete",   [this]() { deleteSelected();   });
        sc("Backspace",[this]() {
            QDir d(m_currentPath);
            if (d.cdUp()) navigateTo(d.absolutePath());
        });
        sc("Ctrl+C",   [this]() { copySelected(false); });
        sc("Ctrl+X",   [this]() { copySelected(true);  });
        sc("Ctrl+V",   [this]() { pasteInto();         });
        sc("Ctrl+H",   [this]() { toggleHidden();      });
        sc("Ctrl+T",   [this]() { addTab();            });
        // Ctrl+W is wired through ShellApp::onCloseWindow()
        // (overridden above) — do not install here, would clash.
        sc("Ctrl+Tab", [this]() {
            if (m_tabs.size() > 1) {
                switchTab((m_currentTab + 1) % m_tabs.size());
            }
        });
        sc("Space", [this]() {
            QString p = selectedPath();
            if (p.isEmpty()) return;
            if (!m_quickLook)
                m_quickLook = new QuickLook(this);
            m_quickLook->show(p);
        });
        sc("Alt+Left", [this]() { goBack();            });
        sc("Alt+Right",[this]() { goForward();         });
        sc("Ctrl+L",   [this]() {
            bool ok = false;
            QString p = ApokolipsInputDialog::getText(this, "Go to",
                "Path:", m_currentPath, &ok);
            if (ok && !p.isEmpty()) navigateTo(p);
        });

        // First tab
        addTab();

        // List view by default
        setViewMode(false);
        setShellWindowGeometry(this, QSize(900, 560));

        // ---- File drag-out (Apokolips shared protocol) ----
        if (m_treeView)
            new FileDragFilter(m_treeView,
                [this]() { return selectedPath(); });
        if (m_view)
            new FileDragFilter(m_view,
                [this]() { return selectedPath(); });

        show();
        update();
        QString startPath = QDir::homePath();
        {
            // "--files <path>" opens Finder inside that folder (a file
            // opens its containing folder). Anything else, such as
            // "--from-dock", is ignored and Finder opens at Home.
            const QStringList a = QCoreApplication::arguments();
            const int i = a.indexOf(QStringLiteral("--files"));
            if (i >= 0 && i + 1 < a.size()) {
                const QFileInfo fi(a.at(i + 1));
                if (fi.isDir())       startPath = fi.absoluteFilePath();
                else if (fi.exists()) startPath = fi.absolutePath();
            }
        }
        QTimer::singleShot(600, this, [this, startPath]() {
            navigateTo(startPath);
        });
    }

protected:
    // Finder: Ctrl+W closes the current tab, not the window.
    void onCloseWindow() override { closeCurrentTab(); }

    void buildBlurredBackdrop() {
        Wallpaper *wp = WallpaperConfig::makeById(WallpaperConfig::loadId());
        if (!wp) return;
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

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect frameRect = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(frameRect, 14, 14);

        // ---- Soft outer shadow ----
        p.setPen(Qt::NoPen);
        for (int i = 5; i >= 1; --i) {
            QColor sh = m_theme.textPrimary;
            sh.setAlphaF(0.02 * (i / 5.0));
            p.setBrush(sh);
            p.drawRoundedRect(frameRect.adjusted(-i, -i + 2, i, i + 2),
                              14 + i, 14 + i);
        }

        // Blurred wallpaper backdrop was drawn here. It made Finder
        // show a static copy of the wallpaper instead of revealing
        // whatever is actually behind the window. Other apps use a
        // plain translucent panel via StyleRenderer::drawPanel and
        // show real transparency; Finder now does the same.

        // ---- Style-aware body ----
        const auto &vc = VisualConfigManager::instance().cfg();
        if (hasOverride()) {
            ShellApp::paintChrome(p, rect(), m_embedded, m_override);
        } else {
            StyleRenderer::drawPanel(p, QRectF(frameRect), 14, m_theme,
                                     vc.finderAlpha);
        }

        // ---- Top strip (header + tab bar + toolbar bg, 30+34+46=110 tall) ----
        const int TOP_H = 30;
        const int TAB_H = 34;
        const int TOOLBAR_H = 46;
        const int TOP_TOTAL = TOP_H + TAB_H + TOOLBAR_H;

        QPainterPath topPath;
        topPath.addRoundedRect(QRect(0, 0, width(), TOP_TOTAL), 14, 14);
        QPainterPath squareBottom;
        squareBottom.addRect(QRect(0, 14, width(), TOP_TOTAL - 14));
        QPainterPath topFinal = topPath.united(squareBottom);

        QColor topTint = m_theme.chromeBg;
        topTint.setAlpha(190);
        p.fillPath(topFinal, topTint);

        // ---- Sidebar background fill (below the top strip) ----
        if (m_sidebar) {
            QColor sb(24, 24, 26);
            sb.setAlpha(255);
            p.save();
            p.setClipPath(path, Qt::IntersectClip);
            p.fillRect(QRect(0, TOP_TOTAL, m_sidebar->width(),
                             height() - TOP_TOTAL), sb);
            p.restore();
        }

        // ---- Only the sidebar / content divider stays ----
        if (m_sidebar) {
            p.setPen(QPen(QColor(255, 255, 255, 22), 1));
            p.drawLine(m_sidebar->width(), 0,
                       m_sidebar->width(), height() - 1);
        }
    }

private:
    static void flog(const QString &msg) {
        QFile f("/tmp/finder.log");
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            f.write((QDateTime::currentDateTime()
                     .toString("HH:mm:ss.zzz ")).toUtf8());
            f.write(msg.toUtf8());
            f.write("\n");
        }
    }

    void buildUi() {
        // Cage sessions have no XDG settings daemon, so QIcon theme lookup
        // fails. Point Qt at standard icon directories and pick Adwaita.
        QIcon::setThemeSearchPaths({
            "/usr/share/icons",
            "/usr/local/share/icons",
            QDir::homePath() + "/.icons",
            QDir::homePath() + "/.local/share/icons"
        });
        QIcon::setThemeName("Adwaita");

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // ---- Header strip: traffic lights + title ----
        m_header = new QWidget;
        m_header->setFixedHeight(30);
        m_header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hh = new QHBoxLayout(m_header);
        hh->setContentsMargins(14, 0, 14, 0);
        hh->setSpacing(8);

        {
            QLabel *titleLabel = nullptr;
            QHBoxLayout *stdHdr = buildStandardHeader(
                "Files", QSize(900, 560), &titleLabel, nullptr);
            while (QLayoutItem *item = stdHdr->takeAt(0)) {
                if (QWidget *w = item->widget()) hh->addWidget(w);
                else if (QLayout *l = item->layout()) hh->addLayout(l);
                else if (item->spacerItem())
                    hh->addSpacerItem(item->spacerItem());
                else delete item;
            }
            delete stdHdr;
            m_title = titleLabel;
        }
        root->addWidget(m_header);

        // ---- Tab bar (below header, above toolbar) ----
        m_tabBar = new QWidget;
        m_tabBar->setFixedHeight(34);
        m_tabBar->setAttribute(Qt::WA_NoSystemBackground, true);
        m_tabBarLayout = new QHBoxLayout(m_tabBar);
        m_tabBarLayout->setContentsMargins(10, 4, 10, 4);
        m_tabBarLayout->setSpacing(4);

        m_newTabBtn = new QPushButton("+");
        m_newTabBtn->setFixedSize(26, 26);
        m_newTabBtn->setCursor(Qt::PointingHandCursor);
        QObject::connect(m_newTabBtn, &QPushButton::clicked,
                         this, &FileExplorer::addTab);
        m_tabBarLayout->addWidget(m_newTabBtn);
        m_tabBarLayout->addStretch();

        root->addWidget(m_tabBar);

        // ---- Toolbar (Ventura-style: left nav · center title · right cluster) ----
        m_toolbar = new QWidget;
        m_toolbar->setFixedHeight(52);
        m_toolbar->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *th = new QHBoxLayout(m_toolbar);
        th->setContentsMargins(14, 8, 14, 8);
        th->setSpacing(6);

        // -- LEFT: back / forward --
        auto mkIconBtn = [this](const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(28, 28);
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
                "QPushButton:disabled { color: rgba(240,237,232,80); }");
            m_navButtons.append(b);
            return b;
        };

        // Segmented pill: [ ‹ | › ]
        QWidget *navPill = new QWidget;
        navPill->setFixedSize(60, 28);
        navPill->setStyleSheet(
            "QWidget {"
            "  background: rgba(255,255,255,18);"
            "  border-radius: 7px;"
            "}");
        QHBoxLayout *pill = new QHBoxLayout(navPill);
        pill->setContentsMargins(0, 0, 0, 0);
        pill->setSpacing(0);

        auto mkArrow = [this](const QString &glyph) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(29, 28);
            b->setCursor(Qt::PointingHandCursor);
            b->setFlat(true);
            b->setStyleSheet(
                "QPushButton {"
                "  color: rgb(240, 237, 232);"
                "  background: transparent;"
                "  border: none;"
                "  font-size: 15px;"
                "}"
                "QPushButton:hover { background: rgba(255,255,255,22); }"
                "QPushButton:disabled { color: rgba(240,237,232,70); }");
            m_navButtons.append(b);
            return b;
        };
        m_backBtn = mkArrow(QString::fromUtf8("\xE2\x80\xB9"));
        m_fwdBtn  = mkArrow(QString::fromUtf8("\xE2\x80\xBA"));
        pill->addWidget(m_backBtn);

        // Thin divider between the two arrows
        QFrame *divider = new QFrame;
        divider->setFixedSize(1, 14);
        divider->setStyleSheet(
            "QFrame { background: rgba(255,255,255,30); border: none; }");
        pill->addWidget(divider);

        pill->addWidget(m_fwdBtn);

        th->addWidget(navPill);

        QObject::connect(m_backBtn, &QPushButton::clicked, [this]() { goBack(); });
        QObject::connect(m_fwdBtn,  &QPushButton::clicked, [this]() { goForward(); });

        th->addSpacing(10);

        // -- CENTER: title (bold path name) --
        m_breadcrumb = new QLabel("Files");
        m_breadcrumb->setAlignment(Qt::AlignCenter);
        m_breadcrumb->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 15px;"
            "font-weight: 600;"
            "background: transparent;");
        th->addWidget(m_breadcrumb, 1);

        // -- RIGHT: view toggles, then search icon --
        auto mkIcon = [this](const QString &glyph) {
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
                "QPushButton:checked { background: rgba(255,255,255,45); }");
            m_navButtons.append(b);
            return b;
        };

        m_gridViewBtn = mkIcon(QString::fromUtf8("\xE2\x96\xA6"));
        m_gridViewBtn->setCheckable(true);
        m_gridViewBtn->setChecked(false);
        m_listViewBtn = mkIcon(QString::fromUtf8("\xE2\x98\xB0"));
        m_listViewBtn->setCheckable(true);
        m_listViewBtn->setChecked(true);

        QObject::connect(m_gridViewBtn, &QPushButton::clicked, [this]() {
            setViewMode(true);
            m_gridViewBtn->setChecked(true);
            m_listViewBtn->setChecked(false);
        });
        QObject::connect(m_listViewBtn, &QPushButton::clicked, [this]() {
            setViewMode(false);
            m_gridViewBtn->setChecked(false);
            m_listViewBtn->setChecked(true);
        });

        m_sortBtn = mkIcon(QString::fromUtf8("\xE2\x87\x85\xE2\x87\x86"));
        QObject::connect(m_sortBtn, &QPushButton::clicked,
                         this, &FileExplorer::showSortMenu);

        th->addWidget(m_gridViewBtn);
        th->addWidget(m_listViewBtn);
        th->addWidget(m_sortBtn);
        th->addSpacing(6);

        // -- search: hidden behind a magnifier icon --
        m_searchBtn = mkIcon(QString::fromUtf8("\xF0\x9F\x94\x8D"));
        QObject::connect(m_searchBtn, &QPushButton::clicked, [this]() {
            m_search->setVisible(!m_search->isVisible());
            if (m_search->isVisible()) m_search->setFocus();
        });
        th->addWidget(m_searchBtn);

        m_search = new QLineEdit;
        m_search->setPlaceholderText("Search");
        m_search->setFixedWidth(180);
        m_search->setAttribute(Qt::WA_MacShowFocusRect, false);
        m_search->setVisible(false);
        QObject::connect(m_search, &QLineEdit::textChanged,
                         [this](const QString &s) {
            if (s.isEmpty()) m_proxy->setFilterFixedString("");
            else             m_proxy->setFilterFixedString(s);
        });
        th->addWidget(m_search);

        root->addWidget(m_toolbar);

        // ---- Body: sidebar + file view ----
        QHBoxLayout *body = new QHBoxLayout;
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        m_sidebar = new QWidget;
        m_sidebar->setFixedWidth(200);
        m_sidebar->setAutoFillBackground(false);
        m_sidebar->setStyleSheet(
            "QWidget { background: transparent; }");
        QVBoxLayout *sb = new QVBoxLayout(m_sidebar);
        sb->setContentsMargins(10, 8, 10, 14);
        sb->setSpacing(1);

        auto addGroupHeader = [&](const QString &text) {
            QLabel *h = new QLabel(text);
            h->setStyleSheet(
                "color: rgb(122, 120, 115);"
                "font-size: 10px;"
                "font-weight: 600;"
                "letter-spacing: 1.2px;"
                "background: transparent;"
                "padding: 10px 10px 6px 10px;");
            sb->addWidget(h);
        };

        auto addRow = [&](const char *glyph, const QColor &color,
                          const QString &name, const QString &relPath)
        {
            QString path = QString(relPath).replace("%HOME%", QDir::homePath());
            FinderPlaceRow *row = new FinderPlaceRow(
                QString::fromUtf8(glyph), color, name);
            m_placeRows.append(row);
            sb->addWidget(row);
            row->onClick = [this, row, path]() {
                for (FinderPlaceRow *r : m_placeRows) r->setSelected(false);
                row->setSelected(true);
                navigateTo(path);
            };
        };

        addGroupHeader("FAVORITES");
        addRow("\xF0\x9F\x8F\xA0", QColor(168, 120,  90), "Home",      "%HOME%");
        addRow("\xF0\x9F\x96\xA5", QColor( 90, 122, 158), "Desktop",   "%HOME%/Desktop");
        addRow("\xF0\x9F\x93\x84", QColor(122, 106, 159), "Documents", "%HOME%/Documents");
        addRow("\xE2\xAC\x87",     QColor( 74, 122,  90), "Downloads", "%HOME%/Downloads");
        addRow("\xF0\x9F\x8E\xB5", QColor(160,  90, 106), "Music",     "%HOME%/Music");
        addRow("\xF0\x9F\x96\xBC", QColor( 74, 122, 122), "Pictures",  "%HOME%/Pictures");
        addRow("\xF0\x9F\x8E\xAC", QColor(166, 122,  90), "Videos",    "%HOME%/Videos");

        addGroupHeader("LOCATIONS");
        addRow("\xE2\x9A\x99", QColor(90, 122, 158), "myMach", "/");
        addRow("\xF0\x9F\x97\x91", QColor(106, 106, 112), "Trash",
               "%HOME%/.local/share/Trash/files");

        if (!m_placeRows.isEmpty()) m_placeRows[0]->setSelected(true);
        sb->addStretch();

        // ---- File view ----
        m_fs = new QFileSystemModel(this);
        m_fs->setRootPath(QDir::homePath());
        m_fs->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
        m_fs->setReadOnly(true);

        m_proxy = new QSortFilterProxyModel(this);
        m_proxy->setSourceModel(m_fs);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

        m_treeView = new QTreeView;
        m_treeView->setModel(m_proxy);
        m_treeView->setRootIndex(m_proxy->mapFromSource(
            m_fs->index(QDir::homePath())));
        m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_treeView->setFrameShape(QFrame::NoFrame);
        m_treeView->setRootIsDecorated(false);
        m_treeView->setItemsExpandable(false);
        m_treeView->setIndentation(0);
        m_treeView->setUniformRowHeights(true);
        m_treeView->setAlternatingRowColors(false);
        m_treeView->setSortingEnabled(true);
        m_treeView->hide();   // grid view shown by default
        m_treeView->header()->setStretchLastSection(false);
        m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_treeView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_treeView->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

        QObject::connect(m_treeView, &QTreeView::doubleClicked,
                         [this](const QModelIndex &idx) {
            QModelIndex srcIdx = m_proxy->mapToSource(idx);
            QString path = m_fs->filePath(srcIdx);
            QFileInfo fi(path);
            if (fi.isDir()) navigateTo(path);
            else            Recents::open(path, "apokolips-files");
        });
        QObject::connect(m_treeView, &QTreeView::customContextMenuRequested,
                         [this](const QPoint &p) {
            showContextMenu(p);
        });
        m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

        m_view = new QListView;
        m_view->setModel(m_proxy);
        m_view->setViewMode(QListView::IconMode);
        m_view->setIconSize(QSize(56, 56));
        m_view->setGridSize(QSize(140, 120));
        m_view->setTextElideMode(Qt::ElideMiddle);
        m_view->setWordWrap(true);
        m_view->setResizeMode(QListView::Adjust);
        m_view->setMovement(QListView::Static);
        m_view->setWordWrap(true);
        m_view->setUniformItemSizes(true);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_view->setFrameShape(QFrame::NoFrame);

        m_view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(m_view, &QListView::customContextMenuRequested,
                         this, &FileExplorer::showContextMenu);

        QObject::connect(m_view, &QListView::doubleClicked,
                         [this](const QModelIndex &idx) {
            flog(QString("doubleClicked valid=%1").arg(idx.isValid()));
            QModelIndex srcIdx = m_proxy->mapToSource(idx);
            QString path = m_fs->filePath(srcIdx);
            QFileInfo fi(path);
            flog(QString("  -> path=%1 isDir=%2").arg(path).arg(fi.isDir()));
            if (fi.isDir()) navigateTo(path);
        });

        // Also log single-click for diagnostics
        QObject::connect(m_view, &QListView::clicked,
                         [this](const QModelIndex &) {
            updateStatusBar();
        });
        QObject::connect(m_view->selectionModel(),
                         &QItemSelectionModel::selectionChanged,
                         [this](const QItemSelection &, const QItemSelection &) {
            updateStatusBar();
        });

        body->addWidget(m_sidebar);
        body->addWidget(m_view, 1);
        body->addWidget(m_treeView, 1);
        root->addLayout(body, 1);

        // ---- Bottom bar: breadcrumb left · centered status ----
        m_statusBar = new QWidget;
        m_statusBar->setFixedHeight(26);
        m_statusBar->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *sb2 = new QHBoxLayout(m_statusBar);
        sb2->setContentsMargins(14, 0, 14, 0);
        sb2->setSpacing(8);

        // Breadcrumb path (right-truncated if long)
        m_pathCrumb = new QLabel;
        m_pathCrumb->setStyleSheet(
            "color: rgb(122, 120, 115);"
            "font-size: 11px;"
            "background: transparent;");
        sb2->addWidget(m_pathCrumb);

        sb2->addStretch();

        // Centered status
        m_statusLeft = new QLabel;
        m_statusLeft->setAlignment(Qt::AlignCenter);
        m_statusLeft->setStyleSheet(
            "color: rgb(122, 120, 115);"
            "font-size: 11px;"
            "background: transparent;");
        sb2->addWidget(m_statusLeft);

        sb2->addStretch();

        // Right slot (kept empty for now; could show free space)
        m_statusRight = new QLabel;
        m_statusRight->setStyleSheet(
            "color: rgb(122, 120, 115);"
            "font-size: 11px;"
            "background: transparent;");
        sb2->addWidget(m_statusRight);

        root->addWidget(m_statusBar);

        // ---- Floating status toast (bottom-right of window) ----
        m_statusLabel = new QLabel(this);
        m_statusLabel->setVisible(false);
        m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_statusLabel->setStyleSheet(
            "QLabel { background: rgba(0,0,0,180); color: white;"
            "  padding: 6px 12px; border-radius: 8px;"
            "  font-size: 12px; }");
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_statusLabel) {
            m_statusLabel->adjustSize();
            m_statusLabel->move(width() - m_statusLabel->width() - 20,
                                height() - m_statusLabel->height() - 20);
        }
    }

    void navigateTo(const QString &path) {
        QFileInfo fi(path);
        flog(QString("navigateTo path=%1 exists=%2 isDir=%3")
             .arg(path)
             .arg(fi.exists())
             .arg(fi.isDir()));
        if (!fi.exists() || !fi.isDir()) return;

        if (!m_currentPath.isEmpty() && m_currentPath != path) {
            m_history = m_history.mid(0, m_historyIndex + 1);
            m_history.append(path);
            m_historyIndex = m_history.size() - 1;
        } else if (m_history.isEmpty()) {
            m_history.append(path);
            m_historyIndex = 0;
        }

        m_currentPath = path;
        if (m_currentTab >= 0 && m_currentTab < m_tabs.size()) {
            m_tabs[m_currentTab].path = path;
            m_tabs[m_currentTab].history = m_history;
            m_tabs[m_currentTab].historyIndex = m_historyIndex;
            refreshTabBar();
        }
        // Non-flicker swap: keep the current view visible until the
        // target directory is loaded by QFileSystemModel, THEN swap.
        // Otherwise setRootIndex would land on an empty model and the
        // view would flash empty for 100-300ms while the directory
        // loads in the background.
        swapToDirectory(path);
        m_breadcrumb->setText(shortenPath(path));
        updateNavButtons();
        updateStatusBar();
    }

    // Load `path` in the background if needed, then swap the view's
    // root index once the directory is cached. If the directory is
    // already cached, swap immediately.
    void swapToDirectory(const QString &path) {
        const QModelIndex fsIdx = m_fs->index(path);
        if (m_loadedDirs.contains(path) || !m_fs->canFetchMore(fsIdx)) {
            applyRootIndex(fsIdx);
            return;
        }
        // Directory not yet loaded. Kick off load and swap on ready.
        QObject::connect(
            m_fs, &QFileSystemModel::directoryLoaded,
            this,
            [this, path, fsIdx](const QString &loadedPath) {
                if (loadedPath != path) return;
                m_loadedDirs.insert(path);
                applyRootIndex(fsIdx);
            },
            Qt::SingleShotConnection);
        m_fs->fetchMore(fsIdx);
    }

    void applyRootIndex(const QModelIndex &fsIdx) {
        m_view->setRootIndex(m_proxy->mapFromSource(fsIdx));
        if (m_treeView)
            m_treeView->setRootIndex(m_proxy->mapFromSource(fsIdx));
    }

    void goBack() {
        if (m_historyIndex <= 0) return;
        --m_historyIndex;
        m_currentPath = m_history[m_historyIndex];
        swapToDirectory(m_currentPath);
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
    }

    void goForward() {
        if (m_historyIndex >= m_history.size() - 1) return;
        ++m_historyIndex;
        m_currentPath = m_history[m_historyIndex];
        swapToDirectory(m_currentPath);
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
    }

    void updateNavButtons() {
        m_backBtn->setEnabled(m_historyIndex > 0);
        m_fwdBtn->setEnabled(m_historyIndex < m_history.size() - 1);
    }

    QString selectedPath() const {
        QModelIndex idx = m_view->currentIndex();
        if (!idx.isValid()) return {};
        return m_fs->filePath(m_proxy->mapToSource(idx));
    }

    void openSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        if (fi.isDir()) navigateTo(p);
        else themedXdgOpen(p);
    }

    void renameSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        bool ok = false;
        QString newName = ApokolipsInputDialog::getText(this, "Rename",
            "New name:", fi.fileName(), &ok);
        if (!ok || newName.isEmpty() || newName == fi.fileName()) return;
        QDir d(fi.absolutePath());
        if (d.rename(fi.fileName(), newName))
            showStatus("Renamed to " + newName);
        else
            showStatus("Rename failed");
    }

    void deleteSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        // Prefer trash, fall back to permanent
        QProcess tr;
        tr.start("gio", { "trash", p });
        tr.waitForFinished(1500);
        bool ok = (tr.exitStatus() == QProcess::NormalExit && tr.exitCode() == 0);
        if (!ok) {
            if (fi.isDir()) QDir(p).removeRecursively();
            else            QFile::remove(p);
        }
        showStatus("Moved to trash: " + fi.fileName());
    }

    void copySelected(bool cut) {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        m_clipboardPaths = { p };
        m_clipboardCut = cut;
        showStatus((cut ? "Cut: " : "Copied: ") + QFileInfo(p).fileName());

        // Also write to the system clipboard so other apps and the
        // ClipboardPanel can see it. text/uri-list is the standard
        // MIME for file drops/copies on Wayland.
        QProcess wlc;
        wlc.start("wl-copy", { "-t", "text/uri-list" });
        if (wlc.waitForStarted(300)) {
            QString payload = "file://" + p + "\r\n";
            wlc.write(payload.toUtf8());
            wlc.closeWriteChannel();
            wlc.waitForFinished(300);
        }
    }

    void pasteInto() {
        if (m_clipboardPaths.isEmpty()) return;
        QDir dest(m_currentPath);
        for (const QString &srcPath : m_clipboardPaths) {
            QFileInfo fi(srcPath);
            QString target = dest.filePath(fi.fileName());
            // Avoid overwriting
            if (QFile::exists(target)) {
                QString base = fi.completeBaseName();
                QString ext = fi.suffix().isEmpty()
                              ? "" : "." + fi.suffix();
                int n = 1;
                while (QFile::exists(target)) {
                    target = dest.filePath(
                        QString("%1 copy %2%3").arg(base).arg(n).arg(ext));
                    ++n;
                }
            }
            if (m_clipboardCut) {
                if (fi.isDir()) {
                    // Qt needs the dir to not exist for rename
                    QProcess::startDetached("mv", { srcPath, target });
                } else {
                    QProcess::startDetached("mv", { srcPath, target });
                }
            } else {
                if (fi.isDir())
                    QProcess::startDetached("cp",
                        { "-r", srcPath, target });
                else
                    QProcess::startDetached("cp", { srcPath, target });
            }
        }
        showStatus(m_clipboardCut ? "Moved" : "Pasted");
        if (m_clipboardCut) { m_clipboardPaths.clear(); m_clipboardCut = false; }
    }

    void newFolder() {
        QDir dest(m_currentPath);
        QString target = dest.filePath("New Folder");
        int n = 1;
        while (QFile::exists(target))
            target = dest.filePath(QString("New Folder %1").arg(++n));
        if (dest.mkdir(QFileInfo(target).fileName()))
            showStatus("New folder created");
    }

    void newFile() {
        QDir dest(m_currentPath);
        QString target = dest.filePath("Untitled.txt");
        int n = 1;
        while (QFile::exists(target))
            target = dest.filePath(QString("Untitled %1.txt").arg(++n));
        QFile f(target);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
            showStatus("New file created");
        }
    }

    void toggleHidden() {
        m_showHidden = !m_showHidden;
        m_fs->setFilter(m_showHidden
            ? (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
            : (QDir::AllEntries | QDir::NoDotAndDotDot));
        showStatus(m_showHidden ? "Hidden files shown" : "Hidden files hidden");
    }

    void showStatus(const QString &msg) {
        if (!m_statusLabel) return;
        m_statusLabel->setText(msg);
        m_statusLabel->setVisible(true);
        QTimer::singleShot(2200, m_statusLabel, [this]() {
            if (m_statusLabel) m_statusLabel->setVisible(false);
        });
    }

    void showContextMenu(const QPoint &pos) {
        QModelIndex idx = m_view->indexAt(pos);
        if (idx.isValid()) m_view->setCurrentIndex(idx);

        const Theme &t = ThemeManager::instance().current();
        QMenu menu(this);
        menu.setStyleSheet(QString(
            "QMenu { background: %1; border: 1px solid %2;"
            "  border-radius: 8px; padding: 5px; color: %3;"
            "  font-size: 12px; }"
            "QMenu::item { padding: 6px 20px 6px 14px; border-radius: 5px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %2;"
            "  margin: 4px 8px; }")
            .arg(rgba(t.panelBg), rgba(t.chromeBorder),
                 t.textPrimary.name(), rgba(t.accentSoft)));

        QAction *aOpen   = menu.addAction("Open");
        QAction *aRename = menu.addAction("Rename");
        menu.addSeparator();
        QAction *aCut   = menu.addAction("Cut");
        QAction *aCopy  = menu.addAction("Copy");
        QAction *aPaste = menu.addAction("Paste");
        menu.addSeparator();
        QAction *aOpenWith = menu.addAction("Open With…");
        menu.addSeparator();
        QAction *aNewFolder = menu.addAction("New Folder");
        QAction *aNewFile   = menu.addAction("New File");
        menu.addSeparator();
        QAction *aCompress = menu.addAction("Compress to .tar.gz");
        QAction *aExtract  = menu.addAction("Extract Here");
        menu.addSeparator();
        QAction *aQuickLook = menu.addAction("Quick Look (Space)");
        menu.addSeparator();
        QAction *aDelete = menu.addAction("Move to Trash");

        bool hasSel = idx.isValid();
        aOpen->setEnabled(hasSel);
        aRename->setEnabled(hasSel);
        aCut->setEnabled(hasSel);
        aCopy->setEnabled(hasSel);
        aDelete->setEnabled(hasSel);
        aOpenWith->setEnabled(hasSel);
        aCompress->setEnabled(hasSel);
        aQuickLook->setEnabled(hasSel);
        aExtract->setEnabled(hasSel);
        aPaste->setEnabled(!m_clipboardPaths.isEmpty());

        QObject::connect(aOpenWith, &QAction::triggered, this,
                         &FileExplorer::openWith);
        QObject::connect(aCompress, &QAction::triggered, this,
                         &FileExplorer::compressSelected);
        QObject::connect(aExtract, &QAction::triggered, this,
                         &FileExplorer::extractSelected);
        QObject::connect(aQuickLook, &QAction::triggered, [this]() {
            QString p = selectedPath();
            if (p.isEmpty()) return;
            if (!m_quickLook) m_quickLook = new QuickLook(this);
            m_quickLook->show(p);
        });

        QObject::connect(aOpen,      &QAction::triggered, this, &FileExplorer::openSelected);
        QObject::connect(aRename,    &QAction::triggered, this, &FileExplorer::renameSelected);
        QObject::connect(aCut,       &QAction::triggered, [this]() { copySelected(true); });
        QObject::connect(aCopy,      &QAction::triggered, [this]() { copySelected(false); });
        QObject::connect(aPaste,     &QAction::triggered, this, &FileExplorer::pasteInto);
        QObject::connect(aNewFolder, &QAction::triggered, this, &FileExplorer::newFolder);
        QObject::connect(aNewFile,   &QAction::triggered, this, &FileExplorer::newFile);
        QObject::connect(aDelete,    &QAction::triggered, this, &FileExplorer::deleteSelected);

        menu.exec(m_view->mapToGlobal(pos));
    }

    struct Tab {
        QString path;
        QStringList history;
        int historyIndex = -1;
    };

    void addTab() {
        Tab t;
        t.path = QDir::homePath();
        m_tabs.append(t);
        m_currentTab = m_tabs.size() - 1;
        refreshTabBar();
        // Don't navigate yet if we're still in the ctor; navigateTo
        // will be called once the model is ready.
        if (m_fs && !m_currentPath.isEmpty()) navigateTo(t.path);
    }

    void switchTab(int idx) {
        if (idx < 0 || idx >= m_tabs.size()) return;
        if (idx == m_currentTab) return;

        // Save current state into current tab
        if (m_currentTab >= 0 && m_currentTab < m_tabs.size()) {
            m_tabs[m_currentTab].path = m_currentPath;
            m_tabs[m_currentTab].history = m_history;
            m_tabs[m_currentTab].historyIndex = m_historyIndex;
        }

        m_currentTab = idx;
        m_currentPath = m_tabs[idx].path;
        m_history = m_tabs[idx].history;
        m_historyIndex = m_tabs[idx].historyIndex;
        m_view->setRootIndex(m_proxy->mapFromSource(
            m_fs->index(m_currentPath)));
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
        refreshTabBar();
    }

    void closeCurrentTab() {
        if (m_tabs.size() <= 1) { close(); deleteLater(); return; }
        m_tabs.removeAt(m_currentTab);
        if (m_currentTab >= m_tabs.size()) m_currentTab = m_tabs.size() - 1;
        m_currentPath = m_tabs[m_currentTab].path;
        m_history = m_tabs[m_currentTab].history;
        m_historyIndex = m_tabs[m_currentTab].historyIndex;
        m_view->setRootIndex(m_proxy->mapFromSource(
            m_fs->index(m_currentPath)));
        m_breadcrumb->setText(shortenPath(m_currentPath));
        updateNavButtons();
        refreshTabBar();
    }

    void refreshTabBar() {
        if (!m_tabBarLayout) return;
        // Remove old tab buttons (everything except the "+" button and stretch)
        QLayoutItem *item;
        while ((item = m_tabBarLayout->takeAt(0))) {
            if (item->widget() && item->widget() != m_newTabBtn) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        // Hide the entire tab bar when only one tab is open.
        // It reappears as soon as the user opens a second tab (Ctrl+T).
        if (m_tabs.size() <= 1) {
            if (m_tabBar) m_tabBar->setVisible(false);
            return;
        }
        if (m_tabBar) m_tabBar->setVisible(true);

        // Re-add "+" button
        m_tabBarLayout->addWidget(m_newTabBtn);

        for (int i = 0; i < m_tabs.size(); ++i) {
            QString name = QFileInfo(m_tabs[i].path).fileName();
            if (name.isEmpty()) name = "/";
            QPushButton *tb = new QPushButton(name);
            tb->setCursor(Qt::PointingHandCursor);
            tb->setFixedHeight(26);
            tb->setCheckable(true);
            tb->setChecked(i == m_currentTab);
            int idx = i;
            QObject::connect(tb, &QPushButton::clicked,
                             [this, idx]() { switchTab(idx); });
            m_tabBarLayout->addWidget(tb);
        }
        m_tabBarLayout->addStretch();
        restyleTabs();
    }

    void restyleTabs() {
        if (!m_newTabBtn) return;
        const Theme &t = m_theme;
        QString common = QString(
            "QPushButton {"
            "  background: rgba(255,255,255,18);"
            "  color: %1;"
            "  border: 1px solid rgba(255,255,255,25);"
            "  border-radius: 7px;"
            "  padding: 2px 12px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover { background: rgba(255,255,255,35); }"
            "QPushButton:checked {"
            "  background: rgba(255,255,255,50);"
            "  border-color: rgba(255,255,255,70);"
            "  font-weight: 600;"
            "}").arg(t.textPrimary.name());
        m_newTabBtn->setStyleSheet(common);
        for (int i = 0; i < m_tabBarLayout->count(); ++i) {
            QWidget *w = m_tabBarLayout->itemAt(i)->widget();
            if (w && w != m_newTabBtn && qobject_cast<QPushButton*>(w))
                w->setStyleSheet(common);
        }
    }

    void setViewMode(bool grid) {
        if (grid) {
            m_treeView->hide();
            m_view->show();
        } else {
            m_view->hide();
            m_treeView->show();
            m_treeView->setRootIndex(m_proxy->mapFromSource(
                m_fs->index(m_currentPath)));
        }
        updateStatusBar();
    }

    void openWith() {
        const QString p = selectedPath();
        if (p.isEmpty()) return;
        OpenWith::show(this, p, "apokolips-files");
    }

    void compressSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        QString out = fi.absolutePath() + "/" + fi.fileName() + ".tar.gz";
        QProcess::startDetached("tar",
            { "-czf", out, "-C", fi.absolutePath(), fi.fileName() });
        showStatus("Compressing " + fi.fileName() + "…");
    }

    void extractSelected() {
        QString p = selectedPath();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        QString dir = fi.absolutePath();
        if (fi.suffix() == "gz" || fi.fileName().endsWith(".tar.gz")) {
            QProcess::startDetached("tar", { "-xzf", p, "-C", dir });
        } else if (fi.suffix() == "zip") {
            QProcess::startDetached("unzip", { "-o", p, "-d", dir });
        } else {
            showStatus("Unsupported archive");
            return;
        }
        showStatus("Extracting " + fi.fileName() + "…");
    }

    void showSortMenu() {
        const Theme &t = ThemeManager::instance().current();
        QMenu m(this);
        m.setStyleSheet(QString(
            "QMenu { background: rgb(24, 22, 30);"
            "  border: 1px solid rgba(255,255,255,30);"
            "  border-radius: 12px; padding: 6px; color: #f0f0f5;"
            "  font-size: 13px; }"
            "QMenu::item { padding: 7px 22px 7px 16px;"
            "  border-radius: 7px; background: transparent; }"
            "QMenu::item:selected { background: rgba(255,255,255,45); }"));

        QAction *aName = m.addAction("Sort by Name");
        QAction *aSize = m.addAction("Sort by Size");
        QAction *aDate = m.addAction("Sort by Date");
        QAction *aType = m.addAction("Sort by Type");
        m.addSeparator();
        QAction *aAsc  = m.addAction("Ascending");
        QAction *aDesc = m.addAction("Descending");

        QObject::connect(aName, &QAction::triggered, [this]() {
            m_proxy->sort(0, m_sortOrder);
        });
        QObject::connect(aSize, &QAction::triggered, [this]() {
            m_proxy->sort(1, m_sortOrder);
        });
        QObject::connect(aDate, &QAction::triggered, [this]() {
            m_proxy->sort(3, m_sortOrder);
        });
        QObject::connect(aType, &QAction::triggered, [this]() {
            m_proxy->sort(2, m_sortOrder);
        });
        QObject::connect(aAsc, &QAction::triggered, [this]() {
            m_sortOrder = Qt::AscendingOrder;
            m_proxy->sort(m_proxy->sortColumn(), m_sortOrder);
        });
        QObject::connect(aDesc, &QAction::triggered, [this]() {
            m_sortOrder = Qt::DescendingOrder;
            m_proxy->sort(m_proxy->sortColumn(), m_sortOrder);
        });

        m.exec(m_sortBtn->mapToGlobal(QPoint(0, m_sortBtn->height() + 4)));
    }

    void updateStatusBar() {
        if (!m_statusLeft || !m_statusRight) return;

        // Count items in current root (works for both view modes)
        int count = 0;
        if (m_fs) {
            QModelIndex root = m_fs->index(m_currentPath.isEmpty()
                                           ? QDir::homePath()
                                           : m_currentPath);
            if (root.isValid()) count = m_fs->rowCount(root);
        }
        int sel = 0;
        if (m_view && m_view->selectionModel())
            sel = m_view->selectionModel()->selectedIndexes().size();

        QString st = QString("%1 item%2")
            .arg(count).arg(count == 1 ? "" : "s");
        if (sel > 0) st += QString(", %1 selected").arg(sel);

        // Append free space
        QStorageInfo storage(m_currentPath.isEmpty()
                             ? QDir::homePath() : m_currentPath);
        if (storage.isValid()) {
            double free = storage.bytesAvailable() / (1024.0*1024.0*1024.0);
            st += QString(", %1 GB available").arg(free, 0, 'f', 2);
        }
        m_statusLeft->setText(st);

        // Breadcrumb at left
        if (m_pathCrumb) {
            QString home = QDir::homePath();
            QString p = m_currentPath;
            if (p.startsWith(home)) p = "~" + p.mid(home.size());
            m_pathCrumb->setText(p);
        }

        // Right slot empty for now
        m_statusRight->setText("");
    }

    QString shortenPath(const QString &p) const {
        // Center title shows just the current folder's name.
        QFileInfo fi(p);
        QString name = fi.fileName();
        if (name.isEmpty()) name = p;             // root
        return name;
    }

    void restyleAll() {
        const Theme &t = m_theme;

        m_title->setStyleSheet(QString(
            "color: %1; font-size: 13px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));

        m_breadcrumb->setStyleSheet(QString(
            "color: %1; font-size: 13px; background: transparent;")
            .arg(t.textPrimary.name()));

        m_search->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: none;"
            "  border-radius: 8px; padding: 5px 10px; font-size: 12px; }"
            "QLineEdit:focus { background: %3; }")
            .arg(rgba(t.chromeBg.lighter(130)),
                 t.textPrimary.name(),
                 rgba(t.chromeBg.lighter(150))));

        for (QPushButton *b : m_navButtons) {
            b->setStyleSheet(QString(
                "QPushButton { color: %1; background: transparent;"
                "  border: none; font-size: 15px; border-radius: 6px; }"
                "QPushButton:hover { background: %2; }"
                "QPushButton:disabled { color: %3; }")
                .arg(t.textPrimary.name(),
                     rgba(t.accentSoft),
                     t.textDim.name()));
        }

        // (sidebar rows handle their own styling now)

        m_view->setStyleSheet(QString(
            "QListView { background: transparent; border: none;"
            "  padding: 12px; color: %1; font-size: 12px; }"
            "QListView::item { padding: 8px; border-radius: 8px; }"
            "QListView::item:hover { background: %2; }"
            "QListView::item:selected { background: %3; color: %4; }")
            .arg(t.textPrimary.name(),
                 rgba(t.accentSoft),
                 rgba(t.accent),
                 t.textPrimary.name()));

        if (m_treeView) {
            m_treeView->setStyleSheet(QString(
                "QTreeView { background: transparent; border: none;"
                "  color: %1; font-size: 12px; padding: 4px; }"
                "QTreeView::item { padding: 4px; }"
                "QTreeView::item:hover { background: %2; }"
                "QTreeView::item:selected { background: %3; color: %4; }"
                "QHeaderView::section {"
                "  background: transparent; color: %5; border: none;"
                "  padding: 6px 8px; font-size: 11px;"
                "  border-bottom: 1px solid %6; }")
                .arg(t.textPrimary.name(),
                     rgba(t.accentSoft),
                     rgba(t.accent),
                     t.textPrimary.name(),
                     t.textDim.name(),
                     rgba(t.chromeBorder)));
        }
        if (m_statusLeft && m_statusRight) {
            QString st = QString(
                "color: rgb(122, 120, 115); font-size: 11px;"
                " background: transparent;");
            m_statusLeft->setStyleSheet(st);
            m_statusRight->setStyleSheet(st);
        }
        if (m_sortBtn) {
            m_sortBtn->setStyleSheet(QString(
                "QPushButton { color: %1; background: transparent;"
                "  border: none; font-size: 12px; padding: 4px 10px;"
                "  border-radius: 6px; }"
                "QPushButton:hover { background: %2; }")
                .arg(t.textPrimary.name(), rgba(t.accentSoft)));
        }
        restyleTabs();
    }

    Theme m_theme;
    QPixmap m_blurredBg;
    QWidget *m_header = nullptr;
    QWidget *m_toolbar = nullptr;
    QWidget *m_sidebar = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_fwdBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QListView *m_view = nullptr;
    QFileSystemModel *m_fs = nullptr;
    QSet<QString> m_loadedDirs;   // dirs confirmed loaded by QFileSystemModel
    QSortFilterProxyModel *m_proxy = nullptr;
    QList<QPushButton *> m_navButtons;
    QList<FinderPlaceRow *> m_placeRows;
    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex = -1;

    // ---- Clipboard (internal) + selection state ----
    QStringList m_clipboardPaths;
    bool        m_clipboardCut = false;
    bool        m_showHidden   = false;
    QLabel     *m_statusLabel  = nullptr;

    // ---- Tabs ----
    QWidget     *m_tabBar       = nullptr;
    QHBoxLayout *m_tabBarLayout = nullptr;
    QPushButton *m_newTabBtn    = nullptr;
    QList<Tab>   m_tabs;
    int          m_currentTab   = -1;

    // ---- Views + status bar ----
    QPushButton   *m_gridViewBtn = nullptr;
    QPushButton   *m_listViewBtn = nullptr;
    QPushButton   *m_sortBtn     = nullptr;
    QPushButton   *m_searchBtn   = nullptr;
    QLabel        *m_pathCrumb   = nullptr;
    QWidget       *m_statusBar   = nullptr;
    QLabel        *m_statusLeft  = nullptr;
    QLabel        *m_statusRight = nullptr;
    Qt::SortOrder  m_sortOrder   = Qt::AscendingOrder;

    QTreeView *m_treeView = nullptr;
    QuickLook *m_quickLook = nullptr;
};

#endif // APOKOLIPS_FINDER_H
