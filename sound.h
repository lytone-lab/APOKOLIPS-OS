#pragma once
#include <QSoundEffect>
#include <QUrl>
#include <QCoreApplication>
#include <QHash>
#include "visualconfig.h"

namespace SoundManager {
    inline void play(const QString& file, const QString& type) {
        const auto& cfg = VisualConfigManager::instance().cfg();
        if (!cfg.soundEnabled) return;
        if (type == "minimize" && !cfg.soundMinimize) return;
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

    inline void playMinimize() { play("pool_pop.wav", "minimize"); }
    inline void playNotification() { play("pool_bubble.wav", "notify"); }
    inline void playStartup() { play("pool_bubble.wav", "startup"); }
}
