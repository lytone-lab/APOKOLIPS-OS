#pragma once
#include <QSoundEffect>
#include <QUrl>
#include <QCoreApplication>
#include <QHash>
#include <QDateTime>
#include "visualconfig.h"

namespace SoundManager {
    inline void play(const QString& file, const QString& type) {
        VisualConfigManager::instance().load();
        const auto& cfg = VisualConfigManager::instance().cfg();
        
        qDebug() << "[sound] play requested:" << type
                 << "enabled=" << cfg.soundEnabled
                 << "typeEnabled=" << (type == "minimize" ? cfg.soundMinimize : true);
        if (!cfg.soundEnabled) return;
        if (type == "notify" && !cfg.soundNotify) return;
        if (type == "startup" && !cfg.soundStartup) return;

        static QHash<QString, QSoundEffect*> cache;
        if (!cache.contains(file)) {
            QSoundEffect* effect = new QSoundEffect;
            QString path = QCoreApplication::applicationDirPath() + "/../assets/sounds/" + file;
            effect->setSource(QUrl::fromLocalFile(path));
            cache[file] = effect;
        }
        QSoundEffect* effect = cache[file];
        effect->setVolume(cfg.soundVolume / 100.0);
        effect->play();
    }

    inline void playNotification() { play("notification.wav", "notify"); }

    inline void playStartup() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.soundEnabled || !cfg.soundStartup) return;

        QString file;
        switch (cfg.startupSoundVariant) {
            case 1:  file = "startup_oxblood.wav"; break;
            case 2:  file = "startup_ascending.wav"; break;
            default: file = "startup_deep.wav"; break;
        }

        static QHash<QString, QSoundEffect*> s_cache;
        if (!s_cache.contains(file)) {
            QSoundEffect *e = new QSoundEffect;
            QString path = QCoreApplication::applicationDirPath() + "/../assets/sounds/" + file;
            e->setSource(QUrl::fromLocalFile(path));
            s_cache[file] = e;
        }
        QSoundEffect *e = s_cache[file];
        e->setVolume(cfg.soundVolume / 100.0);
        e->play();
    }

    inline void playPool() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.soundEnabled || !cfg.poolSounds) return;
        static qint64 s_lastMs = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_lastMs < 80) return;   // rate limit ~12/sec
        s_lastMs = now;

        static QSoundEffect *s_effect = nullptr;
        if (!s_effect) {
            s_effect = new QSoundEffect;
            QString path = QCoreApplication::applicationDirPath() + "/../assets/sounds/pool.wav";
            s_effect->setSource(QUrl::fromLocalFile(path));
        }
        s_effect->setVolume(cfg.soundVolume / 100.0);
        s_effect->play();
    }
}
