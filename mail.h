#ifndef APOKOLIPS_MAIL_H
#define APOKOLIPS_MAIL_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"

// Mail — placeholder for now. Thunderbird is not installed on the
// VM, and building a real IMAP client is a multi-session project.
// This window is honest about that: it says so, and offers a
// Compose button that hands off to the system mail handler
// (xdg-email) so at least "write a message" works today.
//
// PROJECT_STATE.txt rule: NO HALF-BAKED APPS. Every control
// either does something or is clearly marked "coming soon".
// This app obeys that — nothing pretends to work that doesn't.

class MailApp : public ShellApp {
public:
    explicit MailApp(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("Mail", QSize(720, 520), false);
        setShellWindowGeometry(this, QSize(720, 520));
        buildUi();

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
    }

private:
    void buildUi() {
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // Header — traffic lights + title.
        QWidget *header = new QWidget;
        header->setFixedHeight(42);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hh = new QHBoxLayout(header);
        hh->setContentsMargins(16, 0, 16, 0);
        hh->setSpacing(10);
        QLabel *titleLabel = nullptr;
        QHBoxLayout *stdHdr = buildStandardHeader(
            "Mail", QSize(720, 520), &titleLabel, nullptr);
        while (QLayoutItem *item = stdHdr->takeAt(0)) {
            if (QWidget *w = item->widget()) hh->addWidget(w);
            else if (QLayout *l = item->layout()) hh->addLayout(l);
            else if (item->spacerItem())
                hh->addSpacerItem(item->spacerItem());
            else delete item;
        }
        delete stdHdr;
        root->addWidget(header);

        // Body — centre column with an envelope glyph, a title,
        // a paragraph of copy, and the Compose button.
        QWidget *body = new QWidget;
        QVBoxLayout *bv = new QVBoxLayout(body);
        bv->setContentsMargins(60, 40, 60, 40);
        bv->setSpacing(16);
        bv->setAlignment(Qt::AlignCenter);

        // Envelope mark.
        QLabel *mark = new QLabel(QString::fromUtf8("\xE2\x9C\x89"));
        mark->setAlignment(Qt::AlignCenter);
        mark->setStyleSheet(QString(
            "color: %1; font-size: 72px; background: transparent;")
            .arg(Sigil::textDim().name()));
        bv->addWidget(mark);

        QLabel *heading = new QLabel("Mail is coming soon");
        heading->setAlignment(Qt::AlignCenter);
        heading->setStyleSheet(QString(
            "color: %1; font-size: 20px; font-weight: 600;"
            " background: transparent;")
            .arg(Sigil::textPrimary().name()));
        bv->addWidget(heading);

        QLabel *sub = new QLabel(
            "A native Mail app is in the works. For now, "
            "\u201cCompose\u201d opens your system mail handler.");
        sub->setWordWrap(true);
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet(QString(
            "color: %1; font-size: 12px; background: transparent;")
            .arg(Sigil::textDim().name()));
        sub->setMaximumWidth(420);
        bv->addWidget(sub, 0, Qt::AlignHCenter);

        QPushButton *compose = new QPushButton("Compose");
        compose->setCursor(Qt::PointingHandCursor);
        compose->setFixedHeight(36);
        compose->setMinimumWidth(140);
        compose->setStyleSheet(
            "QPushButton {"
            "  background: rgba(168, 50, 80, 200);"
            "  border: 1px solid rgba(168, 50, 80, 240);"
            "  border-radius: 9px;"
            "  color: rgb(240, 237, 232);"
            "  font-size: 13px; font-weight: 600;"
            "}"
            "QPushButton:hover {"
            "  background: rgba(200, 70, 100, 230);"
            "}");
        QObject::connect(compose, &QPushButton::clicked,
                         []() {
            // Hand off to the system mail handler. Will open
            // whatever xdg-email resolves to — even if nothing
            // is installed, it fails cleanly.
            QProcess::startDetached("xdg-email", {});
        });
        bv->addWidget(compose, 0, Qt::AlignHCenter);

        root->addWidget(body, 1);
    }

    void restyle() {
        update();
    }

    Theme m_theme;
};

#endif // APOKOLIPS_MAIL_H
