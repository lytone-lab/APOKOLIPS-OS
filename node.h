#ifndef APOKOLIPS_NODE_H
#define APOKOLIPS_NODE_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include "helpers.h"

class NodeWindow : public ShellApp {
public:
    explicit NodeWindow(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("Node", QSize(900, 600), false);
        setAutoFillBackground(false);

        QVBoxLayout *root = new QVBoxLayout(this);
        // Bottom margin = rounded-corner radius. Keeps child
        // widgets from painting over the rounded bottom corners.
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // ---- Header ----
        QWidget *header = new QWidget;
        header->setFixedHeight(42);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hh = new QHBoxLayout(header);
        hh->setContentsMargins(16, 0, 16, 0);
        hh->setSpacing(10);

        // Standard header — lights + title via ShellApp.
        QLabel *titleLabel = nullptr;
        QHBoxLayout *stdHdr = buildStandardHeader(
            "Node", QSize(900, 600), &titleLabel, nullptr);
        // Adopt the standard header's items into this local widget.
        while (QLayoutItem *item = stdHdr->takeAt(0)) {
            if (QWidget *w = item->widget())
                hh->addWidget(w);
            else if (QLayout *l = item->layout())
                hh->addLayout(l);
            else if (item->spacerItem())
                hh->addSpacerItem(item->spacerItem());
            else
                delete item;
        }
        delete stdHdr;
        Q_UNUSED(titleLabel);

        root->addWidget(header);

        // ---- Terminal ----
        m_term = new QTermWidget(0, this);
        m_term->setColorScheme("WhiteOnBlack");
        m_term->setScrollBarPosition(QTermWidget::ScrollBarRight);
        m_term->setShellProgram("/bin/bash");
        m_term->setWorkingDirectory(QDir::homePath());
        m_term->startShellProgram();
        root->addWidget(m_term, 1);
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
        p.fillRect(QRect(0, 0, width(), 42), Sigil::headerBg());
        p.restore();

        p.setPen(QPen(Sigil::border(), 1));
        p.drawLine(0, 42, width(), 42);

        p.setPen(QPen(Sigil::border(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(full);
    }

private:
    QTermWidget *m_term = nullptr;
};

#endif // APOKOLIPS_NODE_H
