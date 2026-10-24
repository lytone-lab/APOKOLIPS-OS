#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QApplication>
#pragma once
// LockScreen — Sigil-styled fullscreen lock overlay.
// Session 1: visual only, Esc dismisses. No PAM auth yet.
// Session 2 will add PAM auth + sway keybind lockdown.

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QDateTime>
#include <QPixmap>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include "visualconfig.h"
#include "theme.h"
#include "style.h"
#include <security/pam_appl.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <cstring>

static int pamConvFunc(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr) {
    if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG) return PAM_CONV_ERR;
    *resp = (struct pam_response *)calloc(num_msg, sizeof(struct pam_response));
    if (*resp == nullptr) return PAM_BUF_ERR;
    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            (*resp)[i].resp = strdup((const char*)appdata_ptr);
            break;
        default:
            break;
        }
    }
    return PAM_SUCCESS;
}

// Defined in main.cpp — finds the sway IPC socket.
static QString discoverSwaySock();

class LockScreen : public QWidget {
public:
    LockScreen(QWidget *parent = nullptr) : QWidget(parent) {
        // OVERLAY MODE: No window flags. This lives inside the shell.
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        // loadWallpaper() moved to showLock() so we have the full
        // screen geometry before generating the blur.

        m_clockTimer = new QTimer(this);
        m_clockTimer->setInterval(1000);
        QObject::connect(m_clockTimer, &QTimer::timeout, this,
                         [this]() { update(); });
        m_clockTimer->start();

        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(500);
        QObject::connect(m_cursorTimer, &QTimer::timeout, this, [this]() {
            m_cursorBlink = !m_cursorBlink;
            update();
        });
        m_cursorTimer->start();

        m_shakeTimer = new QTimer(this);
        m_shakeTimer->setInterval(16);
        m_shakePhase = 0;
        QObject::connect(m_shakeTimer, &QTimer::timeout, this, [this]() {
            m_shakePhase++;
            if (m_shakePhase > 20) { // ~320ms total
                m_shakeTimer->stop();
                m_shakeOffset = 0;
            } else {
                m_shakeOffset = int(std::sin(m_shakePhase * 1.2) * 12);
            }
            update();
        });

        setAttribute(Qt::WA_NoMousePropagation, true);
        setContextMenuPolicy(Qt::PreventContextMenu);
        hide();
        if (parentWidget()) parentWidget()->installEventFilter(this);
    }

    // Follow the parent: it can change size after the lock is shown
    // (fullscreen toggles, VM resize); a one-time copy goes stale.
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize && isVisible())
            setGeometry(parentWidget()->rect());
        return QWidget::eventFilter(o, e);
    }

    void showLock() {
        if (QWidget *pw = QApplication::activePopupWidget()) pw->close();
        if (parentWidget())
            setGeometry(parentWidget()->rect());
        loadWallpaper();
        runSwayMode("locked");
        raise();
        show();
        setFocus();
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override { e->accept(); }
    void mouseReleaseEvent(QMouseEvent *e) override { e->accept(); }
    void mouseDoubleClickEvent(QMouseEvent *e) override { e->accept(); }
    void mouseMoveEvent(QMouseEvent *e) override { e->accept(); }
    void wheelEvent(QWheelEvent *e) override { e->accept(); }
    void contextMenuEvent(QContextMenuEvent *e) override { e->accept(); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // 1. Blurred wallpaper
        if (!m_blurredWall.isNull())
            p.drawPixmap(rect(), m_blurredWall);
        else
            p.fillRect(rect(), QColor(20, 20, 24));

        // 2. Dark overlay for text contrast
        p.fillRect(rect(), QColor(0, 0, 0, 140));

        const auto &cfg = VisualConfigManager::instance().cfg();
        const QDateTime now = QDateTime::currentDateTime();

        // 3. Clock — big, weighty
        QFont clockFont = font();
        clockFont.setPixelSize(qMin(width(), height()) / 6);
        clockFont.setWeight(QFont::Light);
        p.setFont(clockFont);
        p.setPen(Sigil::textPrimary());
        const QString timeStr = cfg.clock24Hour
            ? now.toString("HH:mm")
            : now.toString("h:mm AP");
        p.drawText(QRect(0, height() / 6, width(), height() / 5),
                   Qt::AlignCenter, timeStr);

        // 4. Date below clock
        QFont dateFont = font();
        dateFont.setPixelSize(qMin(width(), height()) / 36);
        p.setFont(dateFont);
        p.setPen(Sigil::textSecondary());
        const QString dateStr = now.toString("dddd, d MMMM yyyy");
        p.drawText(QRect(0, height() / 6 + height() / 5 - 10,
                         width(), 40),
                   Qt::AlignCenter, dateStr);

        // 5. Avatar circle
        const int avatarSize = qMin(width(), height()) / 12;
        const int avatarX = (width() - avatarSize) / 2;
        const int avatarY = height() / 2 + height() / 25;
        p.setBrush(Sigil::cardBg());
        p.setPen(QPen(Sigil::border(), 2));
        p.drawEllipse(avatarX, avatarY, avatarSize, avatarSize);

        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty()) user = "lytone";
        QFont avatarFont = font();
        avatarFont.setPixelSize(avatarSize / 2);
        avatarFont.setWeight(QFont::Bold);
        p.setFont(avatarFont);
        p.setPen(Sigil::textPrimary());
        p.drawText(QRect(avatarX, avatarY, avatarSize, avatarSize),
                   Qt::AlignCenter, user.left(1).toUpper());

        // 6. Username below avatar
        const int nameY = avatarY + avatarSize + 12;
        QFont nameFont = font();
        nameFont.setPixelSize(qMin(width(), height()) / 45);
        nameFont.setWeight(QFont::DemiBold);
        p.setFont(nameFont);
        p.setPen(Sigil::textPrimary());
        p.drawText(QRect(0, nameY, width(), 30),
                   Qt::AlignCenter, user);

        // 7. Password field
        const int fieldW = qMin(400, width() / 3);
        const int fieldH = 44;
        const int fieldX = (width() - fieldW) / 2;
        const int fieldY = nameY + 50;
        const QRect field(fieldX + m_shakeOffset, fieldY, fieldW, fieldH);

        QPainterPath fieldPath;
        fieldPath.addRoundedRect(field, fieldH / 2, fieldH / 2);
        p.fillPath(fieldPath, QColor(30, 30, 34, 220));
        p.setPen(QPen(Sigil::border(), 1));
        p.drawPath(fieldPath);

        if (m_authenticating) {
            p.setPen(Sigil::textDim());
            QFont hintFont = font();
            hintFont.setPixelSize(13);
            p.setFont(hintFont);
            p.drawText(field, Qt::AlignCenter, "Verifying...");
        } else if (m_password.isEmpty()) {
            p.setPen(Sigil::textDim());
            QFont hintFont = font();
            hintFont.setPixelSize(13);
            p.setFont(hintFont);
            p.drawText(field, Qt::AlignCenter, "Password");
        } else {
            const int dotSize = 8;
            const int dotSpacing = 14;
            const int totalW =
                m_password.length() * dotSpacing - (dotSpacing - dotSize);
            const int startX = field.center().x() - totalW / 2;
            const int dotY = field.center().y() - dotSize / 2;
            p.setPen(Qt::NoPen);
            p.setBrush(Sigil::textPrimary());
            for (int i = 0; i < m_password.length(); ++i)
                p.drawEllipse(startX + i * dotSpacing, dotY,
                              dotSize, dotSize);
            if (m_cursorBlink) {
                const int cursorX =
                    startX + m_password.length() * dotSpacing;
                p.fillRect(cursorX, dotY - 2, 2, dotSize + 4,
                           Sigil::textPrimary());
            }
        }

        // 8. Hint
        QFont hintFont = font();
        hintFont.setPixelSize(11);
        p.setFont(hintFont);
        p.setPen(Sigil::textDim());
        p.drawText(QRect(0, height() - 40, width(), 20),
                   Qt::AlignCenter,
                   "");
    }

    void tryAuthenticate() {
        if (m_password.isEmpty() || m_authenticating) return;
        
        m_authenticating = true;
        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty()) user = "lytone";
        QByteArray passBytes = m_password.toUtf8();
        m_password.clear();
        update();

        QFutureWatcher<int> *watcher = new QFutureWatcher<int>(this);
        QObject::connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher]() {
            int ret = watcher->result();
            watcher->deleteLater();
            m_authenticating = false;
            
            if (ret == PAM_SUCCESS) {
                qInfo() << "[lock] auth success";
                runSwayMode("default");
                hide();
                if (parentWidget()) parentWidget()->setFocus();
            } else {
                qInfo() << "[lock] auth failed";
                m_shakePhase = 0;
                m_shakeTimer->start();
            }
            update();
        });

        QFuture<int> future = QtConcurrent::run([user, passBytes]() {
            struct pam_conv conv = { pamConvFunc, (void*)passBytes.constData() };
            pam_handle_t* pamh = nullptr;
            int ret = pam_start("login", user.toUtf8().constData(), &conv, &pamh);
            if (ret == PAM_SUCCESS) {
                ret = pam_authenticate(pamh, 0);
                pam_end(pamh, ret);
            }
            return ret;
        });
        watcher->setFuture(future);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            m_password.clear(); // Esc no longer unlocks
            update();
            return;
        }
        if (e->key() == Qt::Key_Backspace) {
            if (!m_password.isEmpty()) m_password.chop(1);
            update();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            tryAuthenticate();
            return;
        }
        const QString text = e->text();
        if (!text.isEmpty() && text.at(0).isPrint()) {
            m_password += text;
            update();
        }
    }

private:
    void runSwayMode(const QString &m) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString sock = discoverSwaySock();
        if (!sock.isEmpty()) env.insert("SWAYSOCK", sock);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start("swaymsg", { "mode", m });
        if (!p.waitForFinished(800)) {
            qWarning() << "[lock] swaymsg mode" << m << "TIMEOUT";
            return;
        }
        if (p.exitCode() != 0) {
            qWarning() << "[lock] swaymsg mode" << m << "FAILED:"
                       << QString::fromUtf8(p.readAllStandardError()).trimmed();
            return;
        }
        qInfo() << "[lock] sway mode →" << m;

        // When entering "locked" mode, also raise the shell above any
        // external app (LibreOffice, VLC, Epiphany) that might be open.
        // Without this, sway keeps the external window above the lock
        // screen since they're peers in the same layer.
        if (m == "locked") {
            // Force the shell into fullscreen so it covers ANY open apps
            // (LibreOffice, VLC, etc.) sitting next to it on the tiled layout.
            QProcess f1;
            f1.setProcessEnvironment(env);
            f1.start("swaymsg", { "[app_id=\"apokolips-shell\"]", "fullscreen", "enable" });
            f1.waitForFinished(500);

            QProcess f2;
            f2.setProcessEnvironment(env);
            f2.start("swaymsg", { "[app_id=\"apokolips-shell\"]", "focus" });
            f2.waitForFinished(500);
        } else if (m == "default") {
            // Drop out of fullscreen when unlocking
            QProcess f3;
            f3.setProcessEnvironment(env);
            f3.start("swaymsg", { "[app_id=\"apokolips-shell\"]", "fullscreen", "disable" });
            f3.waitForFinished(500);
        }
    }

    void loadWallpaper() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        QString path = cfg.lockWallpaper;
        if (path.isEmpty() || !QFile::exists(path)) {
            path = QCoreApplication::applicationDirPath()
                + "/../assets/wallpapers/magnific.jpg";
        }
        QPixmap raw;
        if (!raw.load(path)) {
            qWarning() << "[lock] failed to load wallpaper:" << path;
            return;
        }

        QSize target = size();
        if (target.isEmpty()) target = QSize(1920, 1080);

        // Downscale by 6× with smooth interpolation. Base 320×180 at
        // 1920×1080 — enough detail that the blur doesn't lose structure,
        // small enough that box blur is nearly free.
        const int DOWN = 6;
        QImage small(qMax(1, target.width() / DOWN),
                     qMax(1, target.height() / DOWN),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(Qt::transparent);
        {
            QPainter sp(&small);
            sp.setRenderHint(QPainter::SmoothPixmapTransform);
            sp.scale(1.0 / DOWN, 1.0 / DOWN);
            sp.drawPixmap(0, 0, target.width(), target.height(), raw);
        }

        // 3-pass separable box blur ≈ Gaussian.
        // Without this, the 6× downscale/upscale cycle creates
        // visible pixel blocks. Box blur flattens the small image
        // BEFORE upscale, so the result is smooth gradient, not blocks.
        const int R = 4;
        const int PASSES = 3;
        for (int pass = 0; pass < PASSES; ++pass) {
            QImage tmp = small;
            // Horizontal pass
            for (int y = 0; y < small.height(); ++y) {
                const QRgb *sLine = reinterpret_cast<const QRgb*>(
                    small.constScanLine(y));
                QRgb *dLine = reinterpret_cast<QRgb*>(tmp.scanLine(y));
                for (int x = 0; x < small.width(); ++x) {
                    int r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int k = -R; k <= R; ++k) {
                        int xx = qBound(0, x + k, small.width() - 1);
                        QRgb c = sLine[xx];
                        r += qRed(c); g += qGreen(c);
                        b += qBlue(c); a += qAlpha(c);
                        ++n;
                    }
                    dLine[x] = qRgba(r / n, g / n, b / n, a / n);
                }
            }
            // Vertical pass
            QImage tmp2 = tmp;
            for (int y = 0; y < tmp.height(); ++y) {
                QRgb *dLine = reinterpret_cast<QRgb*>(tmp2.scanLine(y));
                for (int x = 0; x < tmp.width(); ++x) {
                    int r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int k = -R; k <= R; ++k) {
                        int yy = qBound(0, y + k, tmp.height() - 1);
                        QRgb c = reinterpret_cast<const QRgb*>(
                            tmp.constScanLine(yy))[x];
                        r += qRed(c); g += qGreen(c);
                        b += qBlue(c); a += qAlpha(c);
                        ++n;
                    }
                    dLine[x] = qRgba(r / n, g / n, b / n, a / n);
                }
            }
            small = tmp2;
        }

        // Upscale the now-smooth image to full resolution.
        m_blurredWall = QPixmap::fromImage(
            small.scaled(target, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation));
    }

    QPixmap m_blurredWall;
    QTimer *m_clockTimer = nullptr;
    QTimer *m_cursorTimer = nullptr;
    bool m_cursorBlink = true;
    QString m_password;
    bool m_authenticating = false;
    QTimer *m_shakeTimer = nullptr;
    int m_shakePhase = 0;
    int m_shakeOffset = 0;
};
