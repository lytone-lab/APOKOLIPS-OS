#ifndef APOKOLIPS_CALC_H
#define APOKOLIPS_CALC_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"

class CalculatorApp : public QWidget {
public:
    CalculatorApp(QWidget *parent = nullptr) : QWidget(parent) {
        if (parent) {
            // Child overlay — must NOT be a native window, or sway tiles it
            setWindowFlags(Qt::Widget);
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAttribute(Qt::WA_NoSystemBackground, true);
        } else {
            setWindowTitle("Calculator");
            setWindowFlags(Qt::FramelessWindowHint);
            setAttribute(Qt::WA_TranslucentBackground, true);
            setShellWindowGeometry(this, QSize(320, 480));
        }

        buildUi();
        if (parent) hide();

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
        if (!parent)
            setShellWindowGeometry(this, QSize(320, 480));
    }

    void toggle() { isVisible() ? hide() : show2(); }

    void show2() {
        if (parentWidget()) {
            int w = 340, h = 480;
            resize(w, h);
            move((parentWidget()->width()  - w) / 2,
                 (parentWidget()->height() - h) / 2);
        }
        raise();
        QWidget::show();
        setFocus();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *wh = window()->windowHandle())
                if (wh->startSystemMove()) return;
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().adjusted(0, 0, -1, -1);
        QPainterPath path;
        path.addRoundedRect(r, 16, 16);
        QColor bg(24, 22, 30, 240);
        p.fillPath(path, bg);
        QColor edge(255, 255, 255, 40);
        p.setPen(QPen(edge, 1));
        p.drawPath(path);
    }

private:
    void buildUi() {
        QVBoxLayout *v = new QVBoxLayout(this);
        v->setContentsMargins(16, 16, 16, 16);
        v->setSpacing(8);

        // Header — traffic lights on the left (standalone mode only)
        if (!parentWidget()) {
            QHBoxLayout *hdr = new QHBoxLayout;
            hdr->setContentsMargins(0, 0, 0, 0);

            auto mkLight = [](const QString &idle,
                              const QString &hover) {
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

            QLabel *title = new QLabel("Calculator");
            title->setStyleSheet(
                "color: rgb(240, 237, 232);"
                "font-size: 13px; font-weight: 600;"
                "background: transparent;");
            hdr->addSpacing(8);
            hdr->addWidget(title);
            hdr->addStretch();

            v->addLayout(hdr);

            QObject::connect(closeBtn, &QPushButton::clicked,
                             [this]() { shellCloseViaGenie(this); });
            QObject::connect(minBtn, &QPushButton::clicked,
                             []() { shellMinimizeFocusedWindow(); });
            QObject::connect(maxBtn, &QPushButton::clicked, [this]() {
                static bool zoomed = false;
                if (zoomed)
                    setShellWindowGeometry(this, QSize(320, 480));
                else
                    setGeometry(shellSafeArea());
                zoomed = !zoomed;
            });
        }

        m_display = new QLabel("0");
        m_display->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_display->setFixedHeight(80);
        v->addWidget(m_display);

        QGridLayout *g = new QGridLayout;
        g->setSpacing(8);
        const char *keys[5][4] = {
            {"C", "±", "%", "/"},
            {"7", "8", "9", "*"},
            {"4", "5", "6", "-"},
            {"1", "2", "3", "+"},
            {"0", ".", "", "="}
        };
        for (int r = 0; r < 5; ++r) {
            for (int c = 0; c < 4; ++c) {
                QString k = keys[r][c];
                if (k.isEmpty()) continue;
                QPushButton *b = new QPushButton(k);
                b->setFixedHeight(56);
                b->setCursor(Qt::PointingHandCursor);
                m_buttons.append(b);
                QObject::connect(b, &QPushButton::clicked,
                                 [this, k]() { press(k); });
                g->addWidget(b, r, c);
            }
        }
        v->addLayout(g, 1);

        restyle();
    }

    void press(const QString &k) {
        if (k == "C") { m_expr.clear(); m_display->setText("0"); return; }
        if (k == "=") {
            QProcess py;
            py.start("python3", { "-c",
                QString("print(eval('%1'))").arg(m_expr) });
            py.waitForFinished(400);
            QString out = QString::fromUtf8(py.readAllStandardOutput()).trimmed();
            if (out.isEmpty()) out = "error";
            m_display->setText(out);
            m_expr = out;
            return;
        }
        if (k == "±") { m_expr.prepend("-"); m_display->setText(m_expr); return; }
        if (k == "%") { m_expr += "/100"; m_display->setText(m_expr); return; }
        m_expr += k;
        m_display->setText(m_expr);
    }

    void restyle() {
        const Theme &t = m_theme;
        m_display->setStyleSheet(QString(
            "color: %1; font-size: 42px; font-weight: 200;"
            " padding: 8px 16px; background: transparent;")
            .arg(t.textPrimary.name()));
        for (QPushButton *b : m_buttons) {
            bool isOp = QString("+-*/=%").contains(b->text());
            QString bg = isOp ? "rgba(255,140,80,60)" : "rgba(255,255,255,25)";
            b->setStyleSheet(QString(
                "QPushButton { background: %1; color: %2; border: none;"
                "  border-radius: 12px; font-size: 20px; }"
                "QPushButton:hover { background: rgba(255,255,255,60); }"
                "QPushButton:pressed { background: rgba(255,255,255,90); }")
                .arg(bg, t.textPrimary.name()));
        }
    }

    QLabel *m_display = nullptr;
    QList<QPushButton *> m_buttons;
    QString m_expr;
    Theme m_theme;
};

#endif // APOKOLIPS_CALC_H
