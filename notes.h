#ifndef APOKOLIPS_NOTES_H
#define APOKOLIPS_NOTES_H

// NotesApp (Step 6)

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"

class NotesApp : public QWidget {
public:
    NotesApp(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("notesApp");
        if (parent) {
            setWindowFlags(Qt::Widget);
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAttribute(Qt::WA_NoSystemBackground, true);
        } else {
            setWindowTitle("Notes");
            setWindowFlags(Qt::FramelessWindowHint);
            setAttribute(Qt::WA_TranslucentBackground, true);
            setShellWindowGeometry(this, QSize(720, 520));
        }

        m_dir = QDir::homePath() + "/.local/share/apokolips/notes";
        QDir().mkpath(m_dir);

        buildUi();
        loadNotesList();

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        if (parent) hide();
        else
            setShellWindowGeometry(this, QSize(720, 520));
    }

    void toggle() { isVisible() ? hide() : show2(); }

    void show2() {
        if (parentWidget()) {
            int w = 780, h = 560;
            resize(w, h);
            move((parentWidget()->width()  - w) / 2,
                 (parentWidget()->height() - h) / 2);
        }
        raise();
        QWidget::show();
        setFocus();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(r, 14, 14);
        QColor bg(24, 22, 30, 245);
        p.fillPath(path, bg);
        QColor edge(255, 255, 255, 40);
        p.setPen(QPen(edge, 1));
        p.drawPath(path);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { hide(); e->accept(); }
        else QWidget::keyPressEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (!parentWidget()) {
            if (e->button() == Qt::LeftButton) {
                if (QWindow *wh = window()->windowHandle())
                    if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

private:
    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(16, 16, 16, 16);
        outer->setSpacing(12);

        // Header — traffic lights left (macOS order)
        QHBoxLayout *hdr = new QHBoxLayout;

        auto mkLight = [this](const QString &idle, const QString &hover) {
            QPushButton *b = new QPushButton;
            b->setFixedSize(13, 13);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { background: %1;"
                "  border: 1px solid rgba(0,0,0,60);"
                "  border-radius: 6px; }"
                "QPushButton:hover { background: %2; }")
                .arg(idle, hover));
            return b;
        };
        QPushButton *closeBtn = mkLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn   = mkLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn   = mkLight("#155c1e", "#28c840");
        hdr->addWidget(closeBtn);
        hdr->addWidget(minBtn);
        hdr->addWidget(maxBtn);
        hdr->addSpacing(10);

        m_title = new QLabel("Notes");
        hdr->addWidget(m_title);
        hdr->addStretch();

        QObject::connect(closeBtn, &QPushButton::clicked,
                         [this]() { shellCloseViaGenie(this); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         []() { shellMinimizeFocusedWindow(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            static bool zoomed = false;
            if (zoomed)
                setShellWindowGeometry(this, QSize(720, 520));
            else
                setGeometry(shellSafeArea());
            zoomed = !zoomed;
        });

        m_newBtn = new QPushButton("+ New");
        m_delBtn = new QPushButton("Delete");
        m_newBtn->setCursor(Qt::PointingHandCursor);
        m_delBtn->setCursor(Qt::PointingHandCursor);
        m_newBtn->setFixedHeight(28);
        m_delBtn->setFixedHeight(28);
        hdr->addWidget(m_newBtn);
        hdr->addWidget(m_delBtn);
        outer->addLayout(hdr);

        // Body: sidebar + editor
        QHBoxLayout *body = new QHBoxLayout;
        body->setSpacing(10);

        m_list = new QListWidget;
        m_list->setFixedWidth(190);
        m_list->setFrameShape(QFrame::NoFrame);
        body->addWidget(m_list);

        m_editor = new QTextEdit;
        m_editor->setFrameShape(QFrame::NoFrame);
        m_editor->setPlaceholderText("Start typing…");
        body->addWidget(m_editor, 1);

        outer->addLayout(body, 1);

        // Auto-save timer
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(800);
        QObject::connect(m_saveTimer, &QTimer::timeout,
                         this, &NotesApp::saveCurrent);

        QObject::connect(m_editor, &QTextEdit::textChanged, [this]() {
            m_saveTimer->start();
        });
        QObject::connect(m_list, &QListWidget::itemSelectionChanged, [this]() {
            QListWidgetItem *it = m_list->currentItem();
            if (!it) return;
            QString path = it->data(Qt::UserRole).toString();
            if (path == m_currentFile) return;
            saveCurrent();   // flush previous
            openNote(path);
        });
        QObject::connect(m_newBtn, &QPushButton::clicked,
                         this, &NotesApp::newNote);
        QObject::connect(m_delBtn, &QPushButton::clicked,
                         this, &NotesApp::deleteCurrent);
    }

    void loadNotesList() {
        m_list->clear();
        QDir d(m_dir);
        QStringList files = d.entryList({"*.txt"}, QDir::Files, QDir::Time);
        for (const QString &f : files) {
            QListWidgetItem *it = new QListWidgetItem(
                QFileInfo(f).completeBaseName());
            it->setData(Qt::UserRole, d.filePath(f));
            m_list->addItem(it);
        }
        if (m_list->count() > 0) {
            m_list->setCurrentRow(0);
        } else {
            newNote();
        }
    }

    void openNote(const QString &path) {
        m_currentFile = path;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            m_editor->blockSignals(true);
            m_editor->setPlainText(QString::fromUtf8(f.readAll()));
            m_editor->blockSignals(false);
            f.close();
        }
    }

    void newNote() {
        saveCurrent();
        QString base = m_dir + "/Untitled.txt";
        int n = 1;
        while (QFile::exists(base)) {
            base = m_dir + QString("/Untitled %1.txt").arg(++n);
        }
        QFile f(base);
        if (f.open(QIODevice::WriteOnly)) { f.close(); }
        loadNotesList();
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == base) {
                m_list->setCurrentRow(i);
                break;
            }
        }
        m_editor->setFocus();
    }

    void deleteCurrent() {
        if (m_currentFile.isEmpty()) return;
        QFile::remove(m_currentFile);
        m_currentFile.clear();
        m_editor->clear();
        loadNotesList();
    }

    void saveCurrent() {
        if (m_currentFile.isEmpty()) return;
        QFile f(m_currentFile);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(m_editor->toPlainText().toUtf8());
            f.close();
        }
    }

    void restyle() {
        const Theme &t = m_theme;
        m_title->setStyleSheet(QString(
            "color: %1; font-size: 15px; font-weight: 600;"
            " background: transparent;").arg(t.textPrimary.name()));
        QString btn = QString(
            "QPushButton { background: rgba(255,255,255,25);"
            "  color: %1; border: none; border-radius: 8px;"
            "  padding: 4px 14px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,55); }")
            .arg(t.textPrimary.name());
        m_newBtn->setStyleSheet(btn);
        m_delBtn->setStyleSheet(btn);
        m_list->setStyleSheet(QString(
            "QListWidget { background: rgba(0,0,0,90); border: none;"
            "  border-radius: 10px; padding: 6px; color: %1;"
            "  font-size: 13px; }"
            "QListWidget::item { padding: 8px 10px; border-radius: 6px; }"
            "QListWidget::item:selected { background: rgba(255,255,255,50); }")
            .arg(t.textPrimary.name()));
        m_editor->setStyleSheet(QString(
            "QTextEdit { background: rgba(0,0,0,90); color: %1;"
            "  border: none; border-radius: 10px; padding: 12px;"
            "  font-size: 14px;"
            "  selection-background-color: rgba(255,255,255,60); }")
            .arg(t.textPrimary.name()));
    }

    QString        m_dir;
    QLabel        *m_title = nullptr;
    QListWidget   *m_list = nullptr;
    QTextEdit     *m_editor = nullptr;
    QPushButton   *m_newBtn = nullptr;
    QPushButton   *m_delBtn = nullptr;
    QTimer        *m_saveTimer = nullptr;
    QString        m_currentFile;
    Theme          m_theme;
};

#endif // APOKOLIPS_NOTES_H
