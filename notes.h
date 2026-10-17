#ifndef APOKOLIPS_NOTES_H
#define APOKOLIPS_NOTES_H

// NotesApp (Step 6)

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"

class NotesApp : public ShellApp {
public:
    NotesApp(QWidget *parent = nullptr,
             const QString &initialPath = QString())
        : ShellApp(parent) {
        setObjectName("notesApp");
        setupShellApp("Notes", QSize(720, 520), parent != nullptr);

        m_dir = QDir::homePath() + "/.local/share/apokolips/notes";
        QDir().mkpath(m_dir);

        // Accept file drops (Apokolips shared protocol)
        setAcceptDrops(true);
        m_dropOverlay = new DropOverlay(this);

        buildUi();
        loadNotesList();

        // If Spotlight opened us with a specific note, focus it.
        if (!initialPath.isEmpty() && QFile::exists(initialPath)) {
            openNote(initialPath);
        }

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        if (parent) hide();
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
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (!e->mimeData()) { e->ignore(); return; }
        QString p = ApoDrag::acceptedPath(e->mimeData());
        if (p.isEmpty()) { e->ignore(); return; }
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
        if (p.isEmpty()) { e->ignore(); return; }
        e->acceptProposedAction();
        addFileAsNote(p);
    }

private:
    void addFileAsNote(const QString &path) {
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) return;
        QString name = fi.completeBaseName();
        if (name.isEmpty()) name = "Dropped";
        QString notePath = m_dir + "/" + name + ".txt";
        int n = 1;
        while (QFile::exists(notePath)) {
            notePath = m_dir + "/" + name + " "
                       + QString::number(++n) + ".txt";
        }
        QMimeDatabase db;
        QString mime = db.mimeTypeForFile(path).name();
        QString content;
        if (mime.startsWith("text/")
            || mime == "application/json"
            || mime == "application/xml") {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                content = QString::fromUtf8(f.readAll());
        }
        if (content.isEmpty()) {
            content = QString::fromUtf8("\xF0\x9F\x93\x8E ")
                      + path + "\n";
        }
        QFile out(notePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        out.write(content.toUtf8());
        out.close();
        loadNotesList();
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            if (it && it->data(Qt::UserRole).toString() == notePath) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
    void buildUi() {
        QVBoxLayout *outer = new QVBoxLayout(this);
        outer->setContentsMargins(16, 16, 16, 16);
        outer->setSpacing(12);

        // Standard header — traffic lights + title via ShellApp.
        QLabel *titleLabel = nullptr;
        QHBoxLayout *hdr = buildStandardHeader("Notes",
                                                QSize(720, 520),
                                                &titleLabel,
                                                nullptr);
        m_title = titleLabel;

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
    DropOverlay   *m_dropOverlay = nullptr;
    QPushButton   *m_newBtn = nullptr;
    QPushButton   *m_delBtn = nullptr;
    QTimer        *m_saveTimer = nullptr;
    QString        m_currentFile;
    Theme          m_theme;
};

#endif // APOKOLIPS_NOTES_H
