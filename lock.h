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

class LockScreen : public QWidget {
public:
    LockScreen(QWidget *parent = nullptr) : QWidget(parent) {
        // OVERLAY MODE: No window flags. This lives inside the shell.
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        loadWallpaper();

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

        hide();
    }

    void showLock() {
        if (parentWidget())
            setGeometry(parentWidget()->rect());
        raise();
        show();
        setFocus();
        update();
    }

protected:
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
        const QRect field(fieldX, fieldY, fieldW, fieldH);

        QPainterPath fieldPath;
        fieldPath.addRoundedRect(field, fieldH / 2, fieldH / 2);
        p.fillPath(fieldPath, QColor(30, 30, 34, 220));
        p.setPen(QPen(Sigil::border(), 1));
        p.drawPath(fieldPath);

        if (m_password.isEmpty()) {
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
                   "Session 1: Esc to dismiss (auth in Session 2)");
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            hide();
            if (parentWidget()) parentWidget()->setFocus();
            return;
        }
        if (e->key() == Qt::Key_Backspace) {
            if (!m_password.isEmpty()) m_password.chop(1);
            update();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            qInfo() << "[lock] Enter pressed. Password length:"
                    << m_password.length()
                    << "(auth not wired — Session 2)";
            return;
        }
        const QString text = e->text();
        if (!text.isEmpty() && text.at(0).isPrint()) {
            m_password += text;
            update();
        }
    }

private:
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
};
