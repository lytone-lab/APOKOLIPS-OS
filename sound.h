#pragma once
#include <QSoundEffect>
#include <QUrl>
#include <QCoreApplication>
#include <QHash>
#include <QDateTime>
#include <QDebug>
#include "visualconfig.h"
#include "audio_safe.h"

// All sounds go through audio_safe.h: one external player at a time and a
// hard 2-second limit, so rapid triggers can never pile up or glitch.
namespace SoundManager {
    inline QString soundPath(const QString &file) {
        return QCoreApplication::applicationDirPath() + "/../assets/sounds/" + file;
    }

    inline void play(const QString& file, const QString& type) {
        VisualConfigManager::instance().load();
        const auto& cfg = VisualConfigManager::instance().cfg();

        qDebug() << "[sound] play requested:" << type
                 << "enabled=" << cfg.soundEnabled
                 << "typeEnabled=" << (type == "minimize" ? cfg.soundMinimize : true);
        if (!cfg.soundEnabled) return;
        if (type == "notify" && !cfg.soundNotify) return;
        if (type == "startup" && !cfg.soundStartup) return;

        playSoundSafe(soundPath(file), cfg.soundVolume / 100.0);
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
        playSoundSafe(soundPath(file), cfg.soundVolume / 100.0);
    }

    inline void playPool() {
        const auto &cfg = VisualConfigManager::instance().cfg();
        if (!cfg.soundEnabled || !cfg.poolSounds) return;
        playSoundSafe(soundPath("pool.wav"), cfg.soundVolume / 100.0, 150);
    }
}
