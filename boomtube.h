#ifndef APOKOLIPS_BOOMTUBE_H
#define APOKOLIPS_BOOMTUBE_H

#include "shell.h"
#include "theme.h"
#include "visualconfig.h"
#include "style.h"
#include "shared_fwd.h"
#include "shellapp.h"
#include "helpers.h"

class BoomTubeWindow : public ShellApp {
public:
    explicit BoomTubeWindow(const QString &initialPath = QString())
        : ShellApp(nullptr)
    {
        setupShellApp("BoomTube", QSize(1000, 700), false);
        setAutoFillBackground(false);
        setShellWindowGeometry(this, QSize(1000, 700), QPoint(0, -20));

        m_player    = new QMediaPlayer(this);
        m_audioOut  = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
        m_audioOut->setVolume(0.8);

        // ---- Root layout ----
        QVBoxLayout *root = new QVBoxLayout(this);
        // Bottom margin = rounded-corner radius. Keeps child
        // widgets from painting over the rounded bottom corners.
        root->setContentsMargins(0, 0, 0, 16);
        root->setSpacing(0);

        // ---- Header (drag + traffic lights) ----
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
                "BoomTube", QSize(1000, 700), &titleLabel, nullptr);
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

        root->addWidget(header);

        // ---- Video canvas ----
        m_video = new QVideoWidget;
        m_video->setStyleSheet("background: rgb(20, 20, 22);");
        m_player->setVideoOutput(m_video);

        // Splitter: video left, playlist right
        QSplitter *split = new QSplitter(Qt::Horizontal);
        split->setStyleSheet(
            "QSplitter { background: rgb(20, 20, 22); }"
            "QSplitter::handle { background: rgba(255,255,255,15);"
            "  width: 1px; }");
        split->addWidget(m_video);

        QWidget *playlistPane = new QWidget;
        playlistPane->setMinimumWidth(220);
        playlistPane->setMaximumWidth(400);
        QVBoxLayout *plv = new QVBoxLayout(playlistPane);
        plv->setContentsMargins(8, 8, 8, 8);
        plv->setSpacing(6);

        QHBoxLayout *plHeader = new QHBoxLayout;
        QLabel *plTitle = new QLabel("PLAYLIST");
        plTitle->setStyleSheet(
            "color: rgb(122, 120, 115);"
            "font-size: 10px; font-weight: 600;"
            "letter-spacing: 1.5px;"
            "background: transparent;");
        plHeader->addWidget(plTitle);
        plHeader->addStretch();
        m_folderBtn = new QPushButton("Folder…");
        m_folderBtn->setCursor(Qt::PointingHandCursor);
        m_folderBtn->setFixedHeight(22);
        m_folderBtn->setStyleSheet(
            "QPushButton { background: rgba(255,255,255,20);"
            "  color: rgb(240, 237, 232); border: none;"
            "  border-radius: 5px; font-size: 10px;"
            "  padding: 2px 10px; }"
            "QPushButton:hover { background: rgba(255,255,255,45); }");
        plHeader->addWidget(m_folderBtn);
        plv->addLayout(plHeader);

        m_playlistWidget = new QListWidget;
        m_playlistWidget->setFrameShape(QFrame::NoFrame);
        m_playlistWidget->setStyleSheet(
            "QListWidget { background: transparent; color: rgb(240,237,232);"
            "  font-size: 12px; border: none; }"
            "QListWidget::item { padding: 6px 8px; border-radius: 5px; }"
            "QListWidget::item:hover { background: rgba(255,255,255,20); }"
            "QListWidget::item:selected { background: rgba(168,50,50,140);"
            "  color: #ffffff; }");
        plv->addWidget(m_playlistWidget, 1);

        split->addWidget(playlistPane);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);

        root->addWidget(split, 1);

        // ---- Transport bar ----
        QWidget *transport = new QWidget;
        transport->setFixedHeight(110);
        transport->setAttribute(Qt::WA_NoSystemBackground, true);
        QVBoxLayout *tv = new QVBoxLayout(transport);
        tv->setContentsMargins(20, 8, 20, 12);
        tv->setSpacing(6);

        // Seek slider
        m_seek = new QSlider(Qt::Horizontal);
        m_seek->setRange(0, 0);
        m_seek->setStyleSheet(
            "QSlider::groove:horizontal { height: 4px;"
            "  background: rgba(255,255,255,25);"
            "  border-radius: 2px; }"
            "QSlider::handle:horizontal {"
            "  background: #ffffff; width: 14px; height: 14px;"
            "  margin: -5px 0; border-radius: 7px; }"
            "QSlider::sub-page:horizontal {"
            "  background: rgb(168,50,50);"
            "  border-radius: 2px; }");
        tv->addWidget(m_seek);

        // Time labels
        QHBoxLayout *times = new QHBoxLayout;
        times->setContentsMargins(0, 0, 0, 0);
        m_timeNow = new QLabel("0:00");
        m_timeEnd = new QLabel("0:00");
        QString timeCss =
            "color: rgb(122, 120, 115);"
            "font-size: 11px; background: transparent;";
        m_timeNow->setStyleSheet(timeCss);
        m_timeEnd->setStyleSheet(timeCss);
        times->addWidget(m_timeNow);
        times->addStretch();
        times->addWidget(m_timeEnd);
        tv->addLayout(times);

        // Controls row
        QHBoxLayout *controls = new QHBoxLayout;
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(10);

        auto mkBtn = [](const QString &glyph, int sz) {
            QPushButton *b = new QPushButton(glyph);
            b->setFixedSize(sz, sz);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(
                "QPushButton {"
                "  color: rgb(240, 237, 232);"
                "  background: transparent;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 15px;"
                "}"
                "QPushButton:hover { background: rgba(255,255,255,25); }");
            return b;
        };

        QPushButton *prevBtn  = mkBtn(QString::fromUtf8("\xE2\x8F\xAE"), 30);
        m_playBtn             = mkBtn(QString::fromUtf8("\xE2\x96\xB6"), 38);
        QPushButton *nextBtn  = mkBtn(QString::fromUtf8("\xE2\x8F\xAD"), 30);

        controls->addWidget(prevBtn);
        controls->addWidget(m_playBtn);
        controls->addWidget(nextBtn);
        controls->addSpacing(20);

        // Volume
        QLabel *volIcon = new QLabel(QString::fromUtf8("\xF0\x9F\x94\x8A"));
        volIcon->setStyleSheet(
            "font-size: 14px; background: transparent;");
        controls->addWidget(volIcon);
        m_volume = new QSlider(Qt::Horizontal);
        m_volume->setRange(0, 100);
        m_volume->setValue(80);
        m_volume->setFixedWidth(120);
        m_volume->setStyleSheet(
            "QSlider::groove:horizontal { height: 4px;"
            "  background: rgba(255,255,255,25);"
            "  border-radius: 2px; }"
            "QSlider::handle:horizontal {"
            "  background: #ffffff; width: 12px; height: 12px;"
            "  margin: -4px 0; border-radius: 6px; }"
            "QSlider::sub-page:horizontal {"
            "  background: rgb(168,50,50);"
            "  border-radius: 2px; }");
        controls->addWidget(m_volume);
        controls->addSpacing(14);

        // Shuffle
        m_shuffleBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x80"));
        m_shuffleBtn->setFixedSize(28, 28);
        m_shuffleBtn->setCursor(Qt::PointingHandCursor);
        m_shuffleBtn->setCheckable(true);
        m_shuffleBtn->setStyleSheet(
            "QPushButton { background: transparent;"
            "  color: rgb(176, 173, 168); border: none;"
            "  border-radius: 6px; font-size: 13px; }"
            "QPushButton:hover { background: rgba(255,255,255,20); }"
            "QPushButton:checked { background: rgba(168,50,50,180);"
            "  color: #ffffff; }");
        controls->addWidget(m_shuffleBtn);

        // Loop
        m_loopBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x81"));
        m_loopBtn->setFixedSize(28, 28);
        m_loopBtn->setCursor(Qt::PointingHandCursor);
        m_loopBtn->setStyleSheet(
            "QPushButton { background: transparent;"
            "  color: rgb(176, 173, 168); border: none;"
            "  border-radius: 6px; font-size: 13px; }"
            "QPushButton:hover { background: rgba(255,255,255,20); }");
        controls->addWidget(m_loopBtn);

        controls->addStretch();

        m_filename = new QLabel("No file loaded");
        m_filename->setStyleSheet(
            "color: rgb(176, 173, 168);"
            "font-size: 12px; background: transparent;");
        controls->addWidget(m_filename);

        tv->addLayout(controls);
        root->addWidget(transport);

        // ---- Wiring ----
        QObject::connect(m_playBtn, &QPushButton::clicked,
                         [this]() { togglePlay(); });
        QObject::connect(prevBtn, &QPushButton::clicked,
                         [this]() { goPrev(); });
        QObject::connect(nextBtn, &QPushButton::clicked,
                         [this]() { goNext(); });
        QObject::connect(m_volume, &QSlider::valueChanged,
                         [this](int v) {
            m_audioOut->setVolume(v / 100.0);
        });

        // Publish state to a file so external consumers (the Now
        // Playing desktop widget) can see what's playing. 500 ms
        // is fast enough for a progress bar and cheap enough not
        // to matter.
        m_publishTimer = new QTimer(this);
        m_publishTimer->setInterval(500);
        QObject::connect(m_publishTimer, &QTimer::timeout,
                         this, [this]() { publishState(); });
        m_publishTimer->start();

        // Poll for commands from external controls (the Now
        // Playing widget's buttons). 300 ms keeps button feedback
        // feeling immediate without busy-looping.
        m_cmdTimer = new QTimer(this);
        m_cmdTimer->setInterval(300);
        QObject::connect(m_cmdTimer, &QTimer::timeout,
                         this, [this]() { pollCommand(); });
        m_cmdTimer->start();
        QObject::connect(m_seek, &QSlider::sliderMoved, [this](int pos) {
            m_player->setPosition(qint64(pos) * 1000);
        });
        QObject::connect(m_player, &QMediaPlayer::positionChanged,
                         this, [this](qint64 ms) {
            if (!m_seek->isSliderDown())
                m_seek->setValue(int(ms / 1000));
            m_timeNow->setText(fmtTime(ms));
        });
        QObject::connect(m_player, &QMediaPlayer::durationChanged,
                         this, [this](qint64 ms) {
            m_seek->setRange(0, int(ms / 1000));
            m_timeEnd->setText(fmtTime(ms));
        });
        QObject::connect(m_player, &QMediaPlayer::playbackStateChanged,
                         this, [this](QMediaPlayer::PlaybackState st) {
            if (st == QMediaPlayer::PlayingState)
                m_playBtn->setText(QString::fromUtf8("\xE2\x8F\xB8"));
            else
                m_playBtn->setText(QString::fromUtf8("\xE2\x96\xB6"));
        });
        QObject::connect(m_player, &QMediaPlayer::mediaStatusChanged,
                         this, [this](QMediaPlayer::MediaStatus st) {
            if (st != QMediaPlayer::EndOfMedia) return;
            auto &c = VisualConfigManager::instance().cfg();
            if (c.boomtubeLoop == "one") {
                m_player->setPosition(0);
                m_player->play();
                return;
            }
            if (c.boomtubeLoop == "off"
                && m_index == m_playlist.size() - 1) return;
            goNext();
        });

        QObject::connect(m_shuffleBtn, &QPushButton::toggled,
                         [this](bool on) {
            VisualConfigManager::instance().setAndSave(
                [on](VisualConfig &c) { c.boomtubeShuffle = on; });
        });
        QObject::connect(m_loopBtn, &QPushButton::clicked, [this]() {
            auto &c = VisualConfigManager::instance().cfg();
            QString next = (c.boomtubeLoop == "off")   ? "all"
                         : (c.boomtubeLoop == "all")   ? "one"
                                                        : "off";
            VisualConfigManager::instance().setAndSave(
                [next](VisualConfig &v) { v.boomtubeLoop = next; });
            refreshLoopButton();
        });
        QObject::connect(m_folderBtn, &QPushButton::clicked, [this]() {
            bool ok = false;
            QString def = VisualConfigManager::instance().cfg()
                            .boomtubeFolder;
            if (def.isEmpty()) def = QDir::homePath() + "/Music";
            QString path = ApokolipsInputDialog::getText(
                this, "Music folder",
                "Folder containing audio/video files:",
                def, &ok);
            if (!ok || path.isEmpty()) return;
            if (!QDir(path).exists()) return;
            VisualConfigManager::instance().setAndSave(
                [path](VisualConfig &c) { c.boomtubeFolder = path; });
            openFolder(path);
        });

        QObject::connect(m_playlistWidget, &QListWidget::itemClicked,
                         [this](QListWidgetItem *it) {
            int idx = it->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_playlist.size())
                playIndex(idx);
        });

        // Initial state
        m_shuffleBtn->setChecked(
            VisualConfigManager::instance().cfg().boomtubeShuffle);
        refreshLoopButton();

        if (!initialPath.isEmpty()) open(initialPath);
        else {
            QString folder = VisualConfigManager::instance()
                                .cfg().boomtubeFolder;
            if (!folder.isEmpty() && QDir(folder).exists())
                openFolder(folder);
        }
        setShellWindowGeometry(this, QSize(1000, 700), QPoint(0, -20));
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
        p.fillRect(QRect(0, 0, width(), 42), QColor(28, 28, 30));
        p.restore();

        p.setPen(QPen(QColor(255, 255, 255, 22), 1));
        p.drawLine(0, 42, width(), 42);

        p.setPen(QPen(QColor(255, 255, 255, 22), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(full);
    }

    // BoomTube-specific: red-light close instead of hide-on-escape.
    void onEscape() override { close(); }

    // Media keys. Escape is handled via onEscape() shortcut.
    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
            case Qt::Key_Space: togglePlay(); return;
            case Qt::Key_Left:  m_player->setPosition(
                                    m_player->position() - 5000); return;
            case Qt::Key_Right: m_player->setPosition(
                                    m_player->position() + 5000); return;
        }
        ShellApp::keyPressEvent(e);
    }

private:
    static QString fmtTime(qint64 ms) {
        qint64 s = ms / 1000;
        return QString("%1:%2").arg(s / 60)
                               .arg(s % 60, 2, 10, QChar('0'));
    }

    void open(const QString &path) {
        QFileInfo fi(path);
        QDir d = fi.absoluteDir();
        scanFolder(d.absolutePath());
        int idx = m_playlist.indexOf(fi.absoluteFilePath());
        if (idx < 0) idx = 0;
        playIndex(idx);
    }

    void openFolder(const QString &path) {
        scanFolder(path);
        if (!m_playlist.isEmpty()) playIndex(0);
    }

    void scanFolder(const QString &folder) {
        QDir d(folder);
        QStringList filters = { "*.mp3", "*.flac", "*.ogg", "*.wav",
                                "*.m4a", "*.aac",
                                "*.mp4", "*.mkv", "*.webm",
                                "*.avi", "*.mov" };
        m_playlist.clear();
        for (const QString &f :
             d.entryList(filters, QDir::Files, QDir::Name))
            m_playlist << d.filePath(f);
        refreshPlaylist();
    }

    void refreshPlaylist() {
        if (!m_playlistWidget) return;
        m_playlistWidget->clear();
        for (int i = 0; i < m_playlist.size(); ++i) {
            QListWidgetItem *it = new QListWidgetItem(
                QFileInfo(m_playlist[i]).fileName());
            it->setData(Qt::UserRole, i);
            m_playlistWidget->addItem(it);
        }
    }

    void refreshLoopButton() {
        auto &c = VisualConfigManager::instance().cfg();
        QString suffix;
        QColor color;
        if (c.boomtubeLoop == "all") {
            suffix = " · all";  color = QColor(168, 50, 50);
        } else if (c.boomtubeLoop == "one") {
            suffix = " · 1";    color = QColor(168, 50, 50);
        } else {
            suffix = "";        color = QColor(176, 173, 168);
        }
        m_loopBtn->setText(
            QString::fromUtf8("\xF0\x9F\x94\x81") + suffix);
        m_loopBtn->setStyleSheet(QString(
            "QPushButton { background: transparent;"
            "  color: %1; border: none;"
            "  border-radius: 6px; font-size: 13px; padding: 0 6px; }"
            "QPushButton:hover { background: rgba(255,255,255,20); }")
            .arg(color.name()));
    }

    void playIndex(int idx) {
        if (idx < 0 || idx >= m_playlist.size()) return;
        m_index = idx;
        QString path = m_playlist[idx];
        m_player->setSource(QUrl::fromLocalFile(path));
        m_filename->setText(QFileInfo(path).fileName());
        if (m_playlistWidget && idx < m_playlistWidget->count())
            m_playlistWidget->setCurrentRow(idx);
        m_player->play();
    }

    void goPrev() {
        if (m_playlist.isEmpty()) return;
        auto &c = VisualConfigManager::instance().cfg();
        if (c.boomtubeShuffle) {
            int r = QRandomGenerator::global()
                        ->bounded(m_playlist.size());
            playIndex(r);
            return;
        }
        playIndex((m_index - 1 + m_playlist.size())
                  % m_playlist.size());
    }
    void goNext() {
        if (m_playlist.isEmpty()) return;
        auto &c = VisualConfigManager::instance().cfg();
        if (c.boomtubeShuffle) {
            int r = QRandomGenerator::global()
                        ->bounded(m_playlist.size());
            playIndex(r);
            return;
        }
        int next = m_index + 1;
        if (next >= m_playlist.size()) {
            if (c.boomtubeLoop == "all") next = 0;
            else return;
        }
        playIndex(next);
    }
    void togglePlay() {
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    }

    void publishState() {
        const QString dst = QDir::tempPath()
                            + "/apokolips-boombube-state.json";
        const QString tmp = dst + ".tmp";
        QJsonObject o;
        const QString curPath =
            (m_index >= 0 && m_index < m_playlist.size())
                ? m_playlist[m_index] : QString();
        o["title"] = curPath.isEmpty()
            ? QString()
            : QFileInfo(curPath).completeBaseName();
        o["path"]  = curPath;
        QMediaPlayer::PlaybackState st = m_player->playbackState();
        o["state"] = st == QMediaPlayer::PlayingState ? "playing"
                   : st == QMediaPlayer::PausedState  ? "paused"
                                                     : "stopped";
        o["posMs"] = double(m_player->position());
        o["durMs"] = double(m_player->duration());
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(o).toJson());
        f.close();
        QFile::remove(dst);
        QFile::rename(tmp, dst);
    }

    void pollCommand() {
        const QString path = QDir::tempPath()
                             + "/apokolips-boombube-cmd.json";
        QFile f(path);
        if (!f.exists()) return;
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument d = QJsonDocument::fromJson(f.readAll());
        f.close();
        QFile::remove(path);
        if (!d.isObject()) return;
        const QString cmd = d.object().value("cmd").toString();
        if (cmd == "toggle")      togglePlay();
        else if (cmd == "next")   goNext();
        else if (cmd == "prev")   goPrev();
        else if (cmd == "play")   m_player->play();
        else if (cmd == "pause")  m_player->pause();
        publishState();
    }

    QTimer         *m_publishTimer = nullptr;
    QTimer         *m_cmdTimer     = nullptr;
    QMediaPlayer   *m_player   = nullptr;
    QAudioOutput   *m_audioOut = nullptr;
    QVideoWidget   *m_video    = nullptr;
    QSlider        *m_seek     = nullptr;
    QSlider        *m_volume   = nullptr;
    QPushButton    *m_playBtn  = nullptr;
    QPushButton    *m_shuffleBtn = nullptr;
    QPushButton    *m_loopBtn    = nullptr;
    QPushButton    *m_folderBtn  = nullptr;
    QLabel         *m_timeNow  = nullptr;
    QLabel         *m_timeEnd  = nullptr;
    QLabel         *m_filename = nullptr;
    QListWidget    *m_playlistWidget = nullptr;
    QStringList     m_playlist;
    int             m_index = 0;
};

#endif // APOKOLIPS_BOOMTUBE_H
