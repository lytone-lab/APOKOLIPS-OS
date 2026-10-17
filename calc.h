#ifndef APOKOLIPS_CALC_H
#define APOKOLIPS_CALC_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"

class CalculatorApp : public ShellApp {
public:
    CalculatorApp(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("Calculator", QSize(320, 480), parent != nullptr);

        buildUi();
        if (parent) hide();

        m_theme = ThemeManager::instance().current();
        restyle();
        ThemeManager::instance().subscribe([this](const Theme &t) {
            m_theme = t;
            restyle();
            update();
        });
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
private:
    void buildUi() {
        // Root layout — bottom margin = rounded-corner radius,
        // matching Aperture / BoomTube / Notes.
        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // Header — dedicated 42px widget flush against the top.
        // ShellApp::paintEvent draws the glass strip at y=0..40
        // *behind* this widget; the traffic lights sit inside it.
        if (!m_embedded) {
            QWidget *header = new QWidget;
            header->setFixedHeight(42);
            header->setAttribute(Qt::WA_NoSystemBackground, true);
            header->setAutoFillBackground(false);
            QHBoxLayout *hh = new QHBoxLayout(header);
            hh->setContentsMargins(16, 0, 16, 0);
            hh->setSpacing(10);
            QLabel *titleLabel = nullptr;
            QHBoxLayout *stdHdr = buildStandardHeader(
                "Calculator", QSize(320, 480), &titleLabel, nullptr);
            while (QLayoutItem *item = stdHdr->takeAt(0)) {
                if (QWidget *w = item->widget()) hh->addWidget(w);
                else if (QLayout *l = item->layout()) hh->addLayout(l);
                else if (item->spacerItem())
                    hh->addSpacerItem(item->spacerItem());
                else delete item;
            }
            delete stdHdr;
            root->addWidget(header);
        }

        // Body — everything below the header gets its own padding.
        QWidget *body = new QWidget;
        QVBoxLayout *v = new QVBoxLayout(body);
        v->setContentsMargins(16, 16, 16, 16);
        v->setSpacing(8);

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

        root->addWidget(body, 1);

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
