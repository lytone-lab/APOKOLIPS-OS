#ifndef APOKOLIPS_NODE_H
#define APOKOLIPS_NODE_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "helpers.h"

class NodeWindow : public QWidget {
public:
    explicit NodeWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint);
        setWindowTitle("Node");
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setShellWindowGeometry(this, QSize(900, 600));

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

        auto makeLight = [](const QString &idle, const QString &hover) {
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
        QPushButton *closeBtn = makeLight("#7a2b25", "#ff5f57");
        QPushButton *minBtn   = makeLight("#7a5b18", "#febc2e");
        QPushButton *maxBtn   = makeLight("#155c1e", "#28c840");

        QObject::connect(closeBtn, &QPushButton::clicked,
                         [this]() { shellCloseViaGenie(this); });
        QObject::connect(minBtn, &QPushButton::clicked,
                         []() { shellMinimizeFocusedWindow(); });
        QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
            static bool zoomed = false;
            if (zoomed)
                setShellWindowGeometry(this, QSize(900, 600));
            else
                setGeometry(shellSafeArea());
            zoomed = !zoomed;
        });

        hh->addWidget(closeBtn);
        hh->addWidget(minBtn);
        hh->addWidget(maxBtn);
        hh->addSpacing(8);

        QLabel *title = new QLabel("Node");
        title->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 13px; font-weight: 600;"
            "background: transparent;");
        hh->addWidget(title);
        hh->addStretch();

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

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && e->pos().y() < 42) {
            if (QWindow *wh = window()->windowHandle()) {
                if (wh->startSystemMove()) { e->accept(); return; }
            }
        }
        QWidget::mousePressEvent(e);
    }

private:
    QTermWidget *m_term = nullptr;
};

#endif // APOKOLIPS_NODE_H
