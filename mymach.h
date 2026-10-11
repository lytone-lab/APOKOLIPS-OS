#ifndef APOKOLIPS_MYMACH_H
#define APOKOLIPS_MYMACH_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include "helpers.h"

class UsageBar : public QWidget {
public:
    UsageBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(8);
        setMinimumWidth(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setValue(double pct) {
        m_pct = qBound(0.0, pct, 1.0);
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = rect();
        QPainterPath bg;
        bg.addRoundedRect(r, 4, 4);
        p.fillPath(bg, QColor(255, 255, 255, 30));

        QRectF fill = r;
        fill.setWidth(r.width() * m_pct);
        QPainterPath fp;
        fp.addRoundedRect(fill, 4, 4);
        // Red only when genuinely alarming. Under 90% it stays
        // neutral so the UI doesn't read as perpetually on fire.
        QColor fillCol = (m_pct >= 0.90)
            ? QColor(200, 74, 74)
            : Sigil::textPrimary();
        fillCol.setAlpha(m_pct >= 0.90 ? 220 : 200);
        p.fillPath(fp, fillCol);
    }
private:
    double m_pct = 0.0;
};

class Sparkline : public QWidget {
public:
    enum class Style { Line, Bar, Area, Radial };

    explicit Sparkline(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(48);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        for (int i = 0; i < kMax; ++i) m_samples.append(0.0);
    }
    void setStyle(Style s) { m_style = s; update(); }
    Style style() const    { return m_style; }

    void push(double v01) {
        v01 = qBound(0.0, v01, 1.0);
        m_samples.append(v01);
        while (m_samples.size() > kMax) m_samples.removeFirst();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int N = m_samples.size();
        if (N < 2) return;
        const qreal w = width(), h = height();
        const qreal step = w / qreal(N - 1);
        const QColor a = Sigil::textPrimary();

        // Common faint midline
        QColor grid = Sigil::textDim(); grid.setAlpha(40);
        p.setPen(QPen(grid, 1, Qt::DotLine));
        p.drawLine(QPointF(0, h * 0.5), QPointF(w, h * 0.5));

        auto yAt = [&](qreal v) {
            return h - v * (h - 6) - 3;
        };

        switch (m_style) {
        case Style::Line: {
            QPainterPath line;
            for (int i = 0; i < N; ++i) {
                qreal x = i * step;
                qreal y = yAt(m_samples[i]);
                if (i == 0) line.moveTo(x, y);
                else        line.lineTo(x, y);
            }
            p.setPen(QPen(a, 1.8, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(line);
            break;
        }
        case Style::Area: {
            QPainterPath area;
            area.moveTo(0, h);
            for (int i = 0; i < N; ++i)
                area.lineTo(i * step, yAt(m_samples[i]));
            area.lineTo(w, h);
            area.closeSubpath();
            QLinearGradient g(0, 0, 0, h);
            QColor top = a; top.setAlpha(140);
            QColor bot = a; bot.setAlpha(0);
            g.setColorAt(0, top);
            g.setColorAt(1, bot);
            p.fillPath(area, g);
            QPainterPath line;
            for (int i = 0; i < N; ++i) {
                qreal x = i * step;
                qreal y = yAt(m_samples[i]);
                if (i == 0) line.moveTo(x, y);
                else        line.lineTo(x, y);
            }
            p.setPen(QPen(a, 1.6, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(line);
            break;
        }
        case Style::Bar: {
            const qreal bw = qMax<qreal>(1.0, step * 0.7);
            QColor bar = a; bar.setAlpha(200);
            p.setPen(Qt::NoPen);
            p.setBrush(bar);
            for (int i = 0; i < N; ++i) {
                qreal x = i * step - bw * 0.5 + step * 0.5;
                qreal y = yAt(m_samples[i]);
                p.drawRoundedRect(QRectF(x, y, bw, h - y),
                                  1.5, 1.5);
            }
            break;
        }
        case Style::Radial: {
            const qreal cur = m_samples.last();
            const qreal d   = qMin(w, h) * 0.9;
            const QRectF ring(
                (w - d) * 0.5, (h - d) * 0.5, d, d);
            QColor track = Sigil::textDim(); track.setAlpha(60);
            p.setPen(QPen(track, 6.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(ring, 90 * 16, -360 * 16);
            p.setPen(QPen(a, 6.0, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(ring, 90 * 16,
                      int(-cur * 360.0 * 16));
            break;
        }
        }
    }

    Style m_style = Style::Line;
private:
    static constexpr int kMax = 60;
    QList<qreal> m_samples;
};

class MyMachWindow : public ShellApp {
public:
    explicit MyMachWindow(QWidget *parent = nullptr) : ShellApp(parent) {
        setupShellApp("myMach", QSize(900, 640), false);
        setAutoFillBackground(false);

        QVBoxLayout *root = new QVBoxLayout(this);
        // Bottom margin = rounded-corner radius. Keeps child
        // widgets from painting over the rounded bottom corners.
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        QWidget *header = new QWidget;
        header->setFixedHeight(42);
        header->setAttribute(Qt::WA_NoSystemBackground, true);
        QHBoxLayout *hh = new QHBoxLayout(header);
        hh->setContentsMargins(16, 0, 16, 0);
        hh->setSpacing(10);

        // Standard header — lights + title via ShellApp.
        {
            QLabel *titleLabel = nullptr;
            QHBoxLayout *stdHdr = buildStandardHeader(
                "myMach", QSize(900, 640), &titleLabel, nullptr);
            while (QLayoutItem *item = stdHdr->takeAt(0)) {
                if (QWidget *w = item->widget()) hh->addWidget(w);
                else if (QLayout *l = item->layout()) hh->addLayout(l);
                else if (item->spacerItem())
                    hh->addSpacerItem(item->spacerItem());
                else delete item;
            }
            delete stdHdr;
            Q_UNUSED(titleLabel);
        }

        QLabel *title = new QLabel("myMach");
        title->setStyleSheet(
            "color: rgb(240, 237, 232);"
            "font-size: 13px; font-weight: 600;"
            "background: transparent;");
        hh->addWidget(title);
        hh->addStretch();

        root->addWidget(header);

        QScrollArea *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(
            "QScrollArea { background: transparent; border: none; }"
            "QScrollBar:vertical { background: transparent;"
            "  width: 10px; margin: 2px; }"
            "QScrollBar::handle:vertical {"
            "  background: rgba(255,255,255,60);"
            "  border-radius: 4px; min-height: 24px; }"
            "QScrollBar::add-line:vertical,"
            "QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical,"
            "QScrollBar::sub-page:vertical { background: transparent; }");
        scroll->viewport()->setStyleSheet("background: transparent;");
        scroll->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);

        QWidget *content = new QWidget;
        content->setAutoFillBackground(false);
        content->setStyleSheet("QWidget { background: transparent; }");
        QVBoxLayout *v = new QVBoxLayout(content);
        v->setContentsMargins(28, 24, 28, 28);
        v->setSpacing(18);

        auto addRowKV = [](QVBoxLayout *cv, const QString &k,
                            const QString &val) {
            QHBoxLayout *h = new QHBoxLayout;
            QLabel *l = new QLabel(k);
            l->setStyleSheet(QString(
                "color: %1; font-size: 12px;"
                " background: transparent;")
                .arg(Sigil::textDim().name()));
            QLabel *r = new QLabel(val);
            r->setStyleSheet(QString(
                "color: %1; font-size: 12px;"
                " background: transparent;")
                .arg(Sigil::textSecondary().name()));
            r->setTextInteractionFlags(Qt::TextSelectableByMouse);
            h->addWidget(l);
            h->addStretch();
            h->addWidget(r);
            cv->addLayout(h);
        };

        auto fmtGB = [](quint64 b) {
            return QString("%1 GB").arg(b / 1073741824.0, 0, 'f', 1);
        };

        // LIVE — CPU and RAM with rolling sparklines
        {
            // Read chart style from config once — live changes are
            // picked up via the config file watcher below.
            const QString gstyle =
                VisualConfigManager::instance().cfg().mymachGraphStyle;
            auto styleFromString = [](const QString &s) {
                if (s == "bar")    return Sparkline::Style::Bar;
                if (s == "area")   return Sparkline::Style::Area;
                if (s == "radial") return Sparkline::Style::Radial;
                return Sparkline::Style::Line;
            };
            const Sparkline::Style chosen = styleFromString(gstyle);
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(14);
            cv->addWidget(Sigil::groupLabel("LIVE"));

            QHBoxLayout *cols = new QHBoxLayout;
            cols->setSpacing(28);

            auto mkCol = [](const QString &label, QLabel *&valOut,
                            Sparkline *&spOut) {
                QVBoxLayout *col = new QVBoxLayout;
                col->setSpacing(6);
                QLabel *k = new QLabel(label);
                k->setStyleSheet(QString(
                    "color: %1; font-size: 11px;"
                    " letter-spacing: 2px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                QLabel *v = new QLabel("—");
                v->setStyleSheet(QString(
                    "color: %1; font-size: 30px;"
                    " font-weight: 300;"
                    " background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                Sparkline *sp = new Sparkline;
                col->addWidget(k);
                col->addWidget(v);
                col->addWidget(sp);
                valOut = v;
                spOut = sp;
                return col;
            };

            cols->addLayout(mkCol("CPU", m_cpuValue, m_cpuSpark), 1);
            cols->addLayout(mkCol("RAM", m_ramValue, m_ramSpark), 1);

            if (m_cpuSpark) m_cpuSpark->setStyle(chosen);
            if (m_ramSpark) m_ramSpark->setStyle(chosen);

            cv->addLayout(cols);
            v->addWidget(c);
        }

        // MACHINE
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(10);
            cv->addWidget(Sigil::groupLabel("MACHINE"));

            QString host = QHostInfo::localHostName();
            QLabel *big = new QLabel(host.isEmpty() ? "Apokolips" : host);
            big->setStyleSheet(QString(
                "color: %1; font-size: 22px; font-weight: 300;"
                " background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cv->addWidget(big);

            QString os = "Unknown";
            QFile osf("/etc/os-release");
            if (osf.open(QIODevice::ReadOnly)) {
                for (const QString &line :
                     QString::fromUtf8(osf.readAll()).split('\n')) {
                    if (line.startsWith("PRETTY_NAME=")) {
                        os = line.mid(12).trimmed();
                        os.remove('"');
                        break;
                    }
                }
            }
            addRowKV(cv, "Operating System", os);

            QProcess kp;
            kp.start("uname", { "-r" });
            kp.waitForFinished(500);
            QString kern = QString::fromUtf8(
                kp.readAllStandardOutput()).trimmed();
            addRowKV(cv, "Kernel", kern.isEmpty() ? "unknown" : kern);

            QFile up("/proc/uptime");
            QString upStr = "unknown";
            if (up.open(QIODevice::ReadOnly)) {
                double sec = QString::fromUtf8(up.readAll())
                                .section(' ', 0, 0).toDouble();
                int d = int(sec / 86400);
                int h = int(sec / 3600) % 24;
                int m = int(sec / 60) % 60;
                if (d > 0)
                    upStr = QString("%1d %2h %3m").arg(d).arg(h).arg(m);
                else if (h > 0)
                    upStr = QString("%1h %2m").arg(h).arg(m);
                else
                    upStr = QString("%1m").arg(m);
            }
            addRowKV(cv, "Uptime", upStr);

            v->addWidget(c);
        }

        // PROCESSOR & MEMORY
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("PROCESSOR & MEMORY"));

            QString cpuModel = "Unknown";
            QFile cf("/proc/cpuinfo");
            if (cf.open(QIODevice::ReadOnly)) {
                for (const QString &line :
                     QString::fromUtf8(cf.readAll()).split('\n')) {
                    if (line.startsWith("model name")) {
                        cpuModel = line.section(':', 1).trimmed();
                        break;
                    }
                }
            }
            QLabel *cpuL = new QLabel(cpuModel);
            cpuL->setStyleSheet(QString(
                "color: %1; font-size: 13px; background: transparent;")
                .arg(Sigil::textPrimary().name()));
            cpuL->setWordWrap(true);
            cv->addWidget(cpuL);

            int cores = 0;
            QFile cf2("/proc/cpuinfo");
            if (cf2.open(QIODevice::ReadOnly)) {
                for (const QString &line :
                     QString::fromUtf8(cf2.readAll()).split('\n')) {
                    if (line.startsWith("processor"))
                        ++cores;
                }
            }
            if (cores == 0) cores = QThread::idealThreadCount();
            addRowKV(cv, "Logical cores", QString::number(cores));

            quint64 ramTotal = 0, ramAvail = 0;
            QFile mf("/proc/meminfo");
            if (mf.open(QIODevice::ReadOnly)) {
                for (const QString &line :
                     QString::fromUtf8(mf.readAll()).split('\n')) {
                    if (line.startsWith("MemTotal:"))
                        ramTotal = line.section(':', 1).trimmed()
                                    .section(' ', 0, 0)
                                    .toULongLong() * 1024ULL;
                    else if (line.startsWith("MemAvailable:"))
                        ramAvail = line.section(':', 1).trimmed()
                                    .section(' ', 0, 0)
                                    .toULongLong() * 1024ULL;
                }
            }
            quint64 ramUsed = (ramTotal > ramAvail)
                ? ramTotal - ramAvail : 0;

            addRowKV(cv, "RAM used",
                QString("%1 / %2").arg(fmtGB(ramUsed), fmtGB(ramTotal)));

            if (ramTotal > 0) {
                UsageBar *bar = new UsageBar;
                bar->setValue(double(ramUsed) / double(ramTotal));
                cv->addWidget(bar);
            }

            v->addWidget(c);
        }

        // STORAGE
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("STORAGE"));

            const auto volumes = QStorageInfo::mountedVolumes();
            bool anyShown = false;
            for (const QStorageInfo &vol : volumes) {
                if (!vol.isValid() || !vol.isReady()) continue;
                const QString dev = vol.device();
                if (dev.startsWith("/dev/loop")) continue;
                const QString fs = vol.fileSystemType();
                if (fs == "tmpfs" || fs == "devtmpfs"
                    || fs == "squashfs" || fs == "overlay") continue;

                quint64 total = quint64(vol.bytesTotal());
                quint64 free  = quint64(vol.bytesAvailable());
                quint64 used  = total > free ? total - free : 0;

                QWidget *row = new QWidget;
                QVBoxLayout *rv = new QVBoxLayout(row);
                rv->setContentsMargins(0, 6, 0, 6);
                rv->setSpacing(4);

                QHBoxLayout *top = new QHBoxLayout;
                QLabel *nameL = new QLabel(vol.displayName()
                                            + "  (" + vol.rootPath() + ")");
                nameL->setStyleSheet(QString(
                    "color: %1; font-size: 12px;"
                    " background: transparent;")
                    .arg(Sigil::textPrimary().name()));
                QLabel *valL = new QLabel(QString("%1 / %2")
                    .arg(fmtGB(used), fmtGB(total)));
                valL->setStyleSheet(QString(
                    "color: %1; font-size: 11px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                top->addWidget(nameL);
                top->addStretch();
                top->addWidget(valL);
                rv->addLayout(top);

                UsageBar *bar = new UsageBar;
                bar->setValue(total > 0
                              ? double(used) / double(total) : 0.0);
                rv->addWidget(bar);

                cv->addWidget(row);
                anyShown = true;
            }
            if (!anyShown) {
                QLabel *none = new QLabel("No mounted volumes");
                none->setStyleSheet(QString(
                    "color: %1; font-size: 12px;"
                    " background: transparent;")
                    .arg(Sigil::textDim().name()));
                cv->addWidget(none);
            }

            v->addWidget(c);
        }

        // ACTIONS
        {
            Sigil::Card *c = new Sigil::Card;
            QVBoxLayout *cv = new QVBoxLayout(c);
            cv->setContentsMargins(20, 18, 20, 18);
            cv->setSpacing(12);
            cv->addWidget(Sigil::groupLabel("ACTIONS"));

            QHBoxLayout *row = new QHBoxLayout;
            row->setSpacing(10);

            auto mkBtn = [](const QString &label, bool danger) {
                QPushButton *b = new QPushButton(label);
                b->setCursor(Qt::PointingHandCursor);
                b->setFixedHeight(32);
                b->setMinimumWidth(140);
                if (danger) {
                    b->setStyleSheet(
                        "QPushButton {"
                        "  background: rgb(168, 50, 50);"
                        "  color: #ffffff;"
                        "  border: none;"
                        "  border-radius: 8px;"
                        "  padding: 4px 18px;"
                        "  font-size: 12px;"
                        "  font-weight: 600; }"
                        "QPushButton:hover { background: rgb(200, 74, 74); }");
                } else {
                    b->setStyleSheet(
                        "QPushButton {"
                        "  background: rgba(255,255,255,20);"
                        "  color: rgb(240, 237, 232);"
                        "  border: none;"
                        "  border-radius: 8px;"
                        "  padding: 4px 18px;"
                        "  font-size: 12px; }"
                        "QPushButton:hover { background: rgba(255,255,255,45); }");
                }
                return b;
            };

            QPushButton *openFiles = mkBtn("Open Files at /", false);
            QPushButton *openTerm  = mkBtn("Open Terminal", false);
            QPushButton *sysReport = mkBtn("System Report", true);

            QObject::connect(openFiles, &QPushButton::clicked, []() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    { "--files" });
            });
            QObject::connect(openTerm, &QPushButton::clicked, []() {
                QProcess::startDetached("foot", {});
            });
            QObject::connect(sysReport, &QPushButton::clicked, []() {
                QString cmd =
                    "echo '=== lscpu ==='; lscpu; echo; "
                    "echo '=== lsblk ==='; lsblk; echo; "
                    "echo '=== df -h ==='; df -h; echo; "
                    "echo '=== lspci ==='; lspci 2>/dev/null | head -20; "
                    "echo; read -p 'Press Enter to close...'";
                QProcess::startDetached("foot",
                    { "bash", "-c", cmd });
            });

            row->addWidget(openFiles);
            row->addWidget(openTerm);
            row->addWidget(sysReport);
            row->addStretch();
            cv->addLayout(row);

            v->addWidget(c);
        }

        v->addStretch();
        scroll->setWidget(content);
        root->addWidget(scroll, 1);

        // Live refresh every second — pushes CPU/RAM samples into the
        // sparklines and updates the big percentage labels.
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setInterval(1000);
        QObject::connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
            const double cpu = SysStats::cpuPercent();
            const double ram = SysStats::ramPercent();
            if (m_cpuSpark) m_cpuSpark->push(cpu / 100.0);
            if (m_ramSpark) m_ramSpark->push(ram / 100.0);
            if (m_cpuValue)
                m_cpuValue->setText(QString("%1%")
                    .arg(cpu, 0, 'f', 0));
            if (m_ramValue)
                m_ramValue->setText(QString("%1%")
                    .arg(ram, 0, 'f', 0));
        });
        m_refreshTimer->start();
    }

private:
    QTimer    *m_refreshTimer = nullptr;
    QLabel    *m_cpuValue = nullptr;
    QLabel    *m_ramValue = nullptr;
    Sparkline *m_cpuSpark = nullptr;
    Sparkline *m_ramSpark = nullptr;

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

        p.fillPath(full, Sigil::contentBg());

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
};

#endif // APOKOLIPS_MYMACH_H
