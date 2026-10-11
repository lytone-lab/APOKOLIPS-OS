#ifndef APOKOLIPS_VISUALCONFIG_H
#define APOKOLIPS_VISUALCONFIG_H

// VisualConfig + VisualConfigManager (Step 2)

#include "shell.h"

struct VisualConfig {
    double blurStrength       = 0.25;   // 0=none, 1=heavy
    int    topBarAlpha        = 110;
    int    dockAlpha          = 130;
    int    launcherAlpha      = 165;
    int    controlCenterAlpha = 165;
    int    widgetCardAlpha    = 140;
    int    finderAlpha        = 190;
    bool   widgetsVisible     = true;
    bool   dndEnabled         = false;
    int    notificationBannerMs = 6000;
    int    dockPosition       = 0;   // 0=Bottom, 1=Left, 2=Right
    bool   clock24Hour       = true;
    bool   clockShowSeconds  = false;
    int    dockIconSize       = 52;   // base visual size (before magnify)
    bool   genieEnabled       = true;
    QString minimizeStyle     = "genie";   // genie | scale | suck
    int    genieDurationMs    = 480;
    int    genieStrips        = 140;
    double genieStagger       = 0.35;
    double genieCurve         = 0.4;
    double genieFadeStart     = 0.95;
    int    screenOffTimeoutMin = 15;   // 0 = never
    int    sleepTimeoutMin     = 30;   // 0 = never
    bool   lowPowerMode        = false;
    bool   ccShowWiFi          = true;
    bool   ccShowBluetooth     = true;
    bool   ccShowMedia         = true;
    bool   ccShowToggles       = true;
    bool   ccShowSound         = true;
    bool   ccShowDisplay       = true;
    int    lockTimeoutMin      = 10;   // 0 = never
    bool   sshEnabled          = true;
    bool   firewallEnabled     = false;
    bool   lockOnSleep         = true;
    bool   lockOnScreenOff     = true;
    int    lockGraceSeconds    = 0;    // 0 = immediately
    bool   lockShowClock       = true;
    bool   lockShowDate        = true;
    QString keyboardLayout     = "us";
    QString keyboardVariant    = "";
    int    keyboardRepeatDelay = 600;
    int    keyboardRepeatRate  = 25;
    QString keyboardCapsLock   = "default";
    int    mouseAccel          = 0;      // -1.0 .. 1.0 as int*10
    bool   touchpadNatural     = true;
    bool   touchpadTap         = true;
    bool   touchpadTwoFinger   = true;
    bool   touchpadDisableWhileTyping = true;
    QString touchpadScrollMethod = "two_finger";
    QString systemLanguage     = "en_US.UTF-8";
    QString backupDestination  = "";
    QString backupSchedule     = "manual"; // manual | hourly | daily | weekly
    QStringList backupIncludeDirs;
    bool   shellReduceMotion  = false;
    bool   shellHighContrast  = false;
    double shellTextScale     = 1.0;
    int    cursorSize         = 24;
    QString visualAlerts      = "off";
    int    notifTimeoutSec    = 6;
    bool   locationEnabled    = false;
    bool   telemetryEnabled   = false;
    bool   spotCalcEnabled    = true;
    bool   spotAppsEnabled    = true;
    bool   spotFilesEnabled   = true;
    bool   spotWebEnabled     = false;   // reserved for V2
    int    spotMaxResults     = 8;
    QString focusMode         = "off";   // off | work | sleep | personal
    QString focusPolicy       = "priority"; // all | priority | mute
    bool   focusScheduleOn    = false;
    int    focusStartHour     = 9;
    int    focusEndHour       = 17;
    QStringList focusAllowedApps;
    bool   screensaverEnabled  = true;
    int    screensaverTimeoutMin = 10;
    QString screensaverStyle   = "clock";  // dim | clock | slideshow | video
    QString screensaverImageDir= "";
    QString screensaverVideoPath = "";
    QString shellFontFamily    = "";      // empty = Qt default
    QString shellMonoFontFamily= "";
    int     shellBaseFontPt    = 10;
    QString boomtubeFolder     = "";
    bool    boomtubeShuffle    = false;
    QString boomtubeLoop       = "off";   // off | all | one
    QVariantMap desktopIconPos;   // id -> {x, y}
    bool    launcherEnabled    = true;
    bool    spotlightEnabled   = true;
    bool    launcherHasSearch  = false;   // merge search into launcher
    int     spotlightBarWidth  = 680;
    int     spotlightBarHeight = 260;
    int     launcherIconSize   = 52;      // 40 small / 52 medium / 64 large
    QStringList dockApps;      // empty = registry default
    QStringList launcherApps;  // empty = registry default
    QStringList desktopApps;   // empty = ["mymach"]
    QString lockWallpaper      = "";   // empty = solid color
    int    blurRefreshMs      = 240;
    double dockMagnifyMax     = 0.42;
    double dockSigma          = 55.0;
};

class VisualConfigManager {
public:
    static VisualConfigManager &instance() {
        static VisualConfigManager v;
        v.load();
        return v;
    }
    using Callback = std::function<void(const VisualConfig &)>;

    VisualConfig &cfg() { return m_cfg; }
    const VisualConfig &cfg() const { return m_cfg; }

    void subscribe(Callback cb) {
        m_callbacks.append(cb);
        cb(m_cfg);
    }

    template<typename F>
    void setAndSave(F mutator) {
        mutator(m_cfg);
        save();
        for (auto &cb : m_callbacks) cb(m_cfg);
    }

    void load() {
        QFile f(path());
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) return;
        QJsonObject o = doc.object();
        auto getD = [&](const char *k, double d) { return o.contains(k) ? o[k].toDouble() : d; };
        auto getI = [&](const char *k, int d)    { return o.contains(k) ? o[k].toInt()    : d; };
        m_cfg.blurStrength       = getD("blurStrength",       m_cfg.blurStrength);
        m_cfg.topBarAlpha        = getI("topBarAlpha",        m_cfg.topBarAlpha);
        m_cfg.dockAlpha          = getI("dockAlpha",          m_cfg.dockAlpha);
        m_cfg.launcherAlpha      = getI("launcherAlpha",      m_cfg.launcherAlpha);
        m_cfg.controlCenterAlpha = getI("controlCenterAlpha", m_cfg.controlCenterAlpha);
        m_cfg.widgetCardAlpha    = getI("widgetCardAlpha",    m_cfg.widgetCardAlpha);
        m_cfg.finderAlpha        = getI("finderAlpha",        m_cfg.finderAlpha);
        m_cfg.widgetsVisible     = o.contains("widgetsVisible")
                                   ? o["widgetsVisible"].toBool(true)
                                   : m_cfg.widgetsVisible;
        m_cfg.dockIconSize       = getI("dockIconSize", m_cfg.dockIconSize);
        m_cfg.genieEnabled       = o.contains("genieEnabled")
                                   ? o["genieEnabled"].toBool(true)
                                   : m_cfg.genieEnabled;
        m_cfg.minimizeStyle      = o.contains("minimizeStyle")
                                   ? o["minimizeStyle"].toString()
                                   : m_cfg.minimizeStyle;
        m_cfg.genieDurationMs    = getI("genieDurationMs",
                                        m_cfg.genieDurationMs);
        m_cfg.genieStrips        = getI("genieStrips", m_cfg.genieStrips);
        m_cfg.genieStagger       = o.contains("genieStagger")
                                   ? o["genieStagger"].toDouble(0.35)
                                   : m_cfg.genieStagger;
        m_cfg.genieCurve         = o.contains("genieCurve")
                                   ? o["genieCurve"].toDouble(0.65)
                                   : m_cfg.genieCurve;
        m_cfg.genieFadeStart     = o.contains("genieFadeStart")
                                   ? o["genieFadeStart"].toDouble(0.95)
                                   : m_cfg.genieFadeStart;
        m_cfg.screenOffTimeoutMin = getI("screenOffTimeoutMin",
                                         m_cfg.screenOffTimeoutMin);
        m_cfg.sleepTimeoutMin     = getI("sleepTimeoutMin",
                                         m_cfg.sleepTimeoutMin);
        m_cfg.lowPowerMode        = o.contains("lowPowerMode")
                                   ? o["lowPowerMode"].toBool(false)
                                   : m_cfg.lowPowerMode;
        m_cfg.ccShowWiFi          = o.contains("ccShowWiFi")
                                   ? o["ccShowWiFi"].toBool(true)
                                   : m_cfg.ccShowWiFi;
        m_cfg.ccShowBluetooth     = o.contains("ccShowBluetooth")
                                   ? o["ccShowBluetooth"].toBool(true)
                                   : m_cfg.ccShowBluetooth;
        m_cfg.ccShowMedia         = o.contains("ccShowMedia")
                                   ? o["ccShowMedia"].toBool(true)
                                   : m_cfg.ccShowMedia;
        m_cfg.ccShowToggles       = o.contains("ccShowToggles")
                                   ? o["ccShowToggles"].toBool(true)
                                   : m_cfg.ccShowToggles;
        m_cfg.ccShowSound         = o.contains("ccShowSound")
                                   ? o["ccShowSound"].toBool(true)
                                   : m_cfg.ccShowSound;
        m_cfg.ccShowDisplay       = o.contains("ccShowDisplay")
                                   ? o["ccShowDisplay"].toBool(true)
                                   : m_cfg.ccShowDisplay;
        m_cfg.lockTimeoutMin      = getI("lockTimeoutMin",
                                         m_cfg.lockTimeoutMin);
        m_cfg.sshEnabled          = o.contains("sshEnabled")
                                   ? o["sshEnabled"].toBool(true)
                                   : m_cfg.sshEnabled;
        m_cfg.firewallEnabled     = o.contains("firewallEnabled")
                                   ? o["firewallEnabled"].toBool(false)
                                   : m_cfg.firewallEnabled;
        m_cfg.lockOnSleep         = o.contains("lockOnSleep")
                                   ? o["lockOnSleep"].toBool(true)
                                   : m_cfg.lockOnSleep;
        m_cfg.lockOnScreenOff     = o.contains("lockOnScreenOff")
                                   ? o["lockOnScreenOff"].toBool(true)
                                   : m_cfg.lockOnScreenOff;
        m_cfg.lockGraceSeconds    = getI("lockGraceSeconds",
                                         m_cfg.lockGraceSeconds);
        m_cfg.lockShowClock       = o.contains("lockShowClock")
                                   ? o["lockShowClock"].toBool(true)
                                   : m_cfg.lockShowClock;
        m_cfg.lockShowDate        = o.contains("lockShowDate")
                                   ? o["lockShowDate"].toBool(true)
                                   : m_cfg.lockShowDate;
        m_cfg.keyboardLayout      = o.contains("keyboardLayout")
                                   ? o["keyboardLayout"].toString()
                                   : m_cfg.keyboardLayout;
        m_cfg.keyboardVariant     = o.contains("keyboardVariant")
                                   ? o["keyboardVariant"].toString()
                                   : m_cfg.keyboardVariant;
        m_cfg.keyboardRepeatDelay = getI("keyboardRepeatDelay",
                                         m_cfg.keyboardRepeatDelay);
        m_cfg.keyboardRepeatRate  = getI("keyboardRepeatRate",
                                         m_cfg.keyboardRepeatRate);
        m_cfg.keyboardCapsLock    = o.contains("keyboardCapsLock")
                                   ? o["keyboardCapsLock"].toString()
                                   : m_cfg.keyboardCapsLock;
        m_cfg.mouseAccel          = getI("mouseAccel", m_cfg.mouseAccel);
        m_cfg.touchpadNatural     = o.contains("touchpadNatural")
                                   ? o["touchpadNatural"].toBool(true)
                                   : m_cfg.touchpadNatural;
        m_cfg.touchpadTap         = o.contains("touchpadTap")
                                   ? o["touchpadTap"].toBool(true)
                                   : m_cfg.touchpadTap;
        m_cfg.touchpadTwoFinger   = o.contains("touchpadTwoFinger")
                                   ? o["touchpadTwoFinger"].toBool(true)
                                   : m_cfg.touchpadTwoFinger;
        m_cfg.touchpadDisableWhileTyping =
            o.contains("touchpadDisableWhileTyping")
                ? o["touchpadDisableWhileTyping"].toBool(true)
                : m_cfg.touchpadDisableWhileTyping;
        m_cfg.touchpadScrollMethod =
            o.contains("touchpadScrollMethod")
                ? o["touchpadScrollMethod"].toString()
                : m_cfg.touchpadScrollMethod;
        m_cfg.systemLanguage      = o.contains("systemLanguage")
                                   ? o["systemLanguage"].toString()
                                   : m_cfg.systemLanguage;
        m_cfg.backupDestination   = o.contains("backupDestination")
                                   ? o["backupDestination"].toString()
                                   : m_cfg.backupDestination;
        m_cfg.backupSchedule      = o.contains("backupSchedule")
                                   ? o["backupSchedule"].toString()
                                   : m_cfg.backupSchedule;
        m_cfg.shellReduceMotion   = o.contains("shellReduceMotion")
                                   ? o["shellReduceMotion"].toBool(false)
                                   : m_cfg.shellReduceMotion;
        m_cfg.shellHighContrast   = o.contains("shellHighContrast")
                                   ? o["shellHighContrast"].toBool(false)
                                   : m_cfg.shellHighContrast;
        m_cfg.shellTextScale      = o.contains("shellTextScale")
                                   ? o["shellTextScale"].toDouble(1.0)
                                   : m_cfg.shellTextScale;
        m_cfg.cursorSize          = getI("cursorSize", m_cfg.cursorSize);
        m_cfg.visualAlerts        = o.contains("visualAlerts")
                                   ? o["visualAlerts"].toString()
                                   : m_cfg.visualAlerts;
        m_cfg.notifTimeoutSec     = getI("notifTimeoutSec",
                                         m_cfg.notifTimeoutSec);
        m_cfg.locationEnabled     = o.contains("locationEnabled")
                                   ? o["locationEnabled"].toBool(false)
                                   : m_cfg.locationEnabled;
        m_cfg.telemetryEnabled    = o.contains("telemetryEnabled")
                                   ? o["telemetryEnabled"].toBool(false)
                                   : m_cfg.telemetryEnabled;
        m_cfg.spotCalcEnabled     = o.contains("spotCalcEnabled")
                                   ? o["spotCalcEnabled"].toBool(true)
                                   : m_cfg.spotCalcEnabled;
        m_cfg.spotAppsEnabled     = o.contains("spotAppsEnabled")
                                   ? o["spotAppsEnabled"].toBool(true)
                                   : m_cfg.spotAppsEnabled;
        m_cfg.spotFilesEnabled    = o.contains("spotFilesEnabled")
                                   ? o["spotFilesEnabled"].toBool(true)
                                   : m_cfg.spotFilesEnabled;
        m_cfg.spotWebEnabled      = o.contains("spotWebEnabled")
                                   ? o["spotWebEnabled"].toBool(false)
                                   : m_cfg.spotWebEnabled;
        m_cfg.spotMaxResults      = getI("spotMaxResults",
                                         m_cfg.spotMaxResults);
        m_cfg.focusMode           = o.contains("focusMode")
                                   ? o["focusMode"].toString()
                                   : m_cfg.focusMode;
        m_cfg.focusPolicy         = o.contains("focusPolicy")
                                   ? o["focusPolicy"].toString()
                                   : m_cfg.focusPolicy;
        m_cfg.focusScheduleOn     = o.contains("focusScheduleOn")
                                   ? o["focusScheduleOn"].toBool(false)
                                   : m_cfg.focusScheduleOn;
        m_cfg.focusStartHour      = getI("focusStartHour",
                                         m_cfg.focusStartHour);
        m_cfg.focusEndHour        = getI("focusEndHour",
                                         m_cfg.focusEndHour);
        m_cfg.screensaverEnabled  = o.contains("screensaverEnabled")
                                   ? o["screensaverEnabled"].toBool(true)
                                   : m_cfg.screensaverEnabled;
        m_cfg.screensaverTimeoutMin = getI("screensaverTimeoutMin",
                                           m_cfg.screensaverTimeoutMin);
        m_cfg.screensaverStyle    = o.contains("screensaverStyle")
                                   ? o["screensaverStyle"].toString()
                                   : m_cfg.screensaverStyle;
        m_cfg.screensaverImageDir = o.contains("screensaverImageDir")
                                   ? o["screensaverImageDir"].toString()
                                   : m_cfg.screensaverImageDir;
        m_cfg.screensaverVideoPath =
            o.contains("screensaverVideoPath")
                ? o["screensaverVideoPath"].toString()
                : m_cfg.screensaverVideoPath;
        m_cfg.shellFontFamily     = o.contains("shellFontFamily")
                                   ? o["shellFontFamily"].toString()
                                   : m_cfg.shellFontFamily;
        m_cfg.shellMonoFontFamily = o.contains("shellMonoFontFamily")
                                   ? o["shellMonoFontFamily"].toString()
                                   : m_cfg.shellMonoFontFamily;
        m_cfg.shellBaseFontPt     = getI("shellBaseFontPt",
                                         m_cfg.shellBaseFontPt);
        m_cfg.boomtubeFolder      = o.contains("boomtubeFolder")
                                   ? o["boomtubeFolder"].toString()
                                   : m_cfg.boomtubeFolder;
        m_cfg.boomtubeShuffle     = o.contains("boomtubeShuffle")
                                   ? o["boomtubeShuffle"].toBool(false)
                                   : m_cfg.boomtubeShuffle;
        m_cfg.boomtubeLoop        = o.contains("boomtubeLoop")
                                   ? o["boomtubeLoop"].toString()
                                   : m_cfg.boomtubeLoop;
        if (o.contains("desktopIconPos")
            && o["desktopIconPos"].isObject()) {
            m_cfg.desktopIconPos = o["desktopIconPos"].toObject()
                                       .toVariantMap();
        }
        m_cfg.launcherEnabled   = o.contains("launcherEnabled")
                                ? o["launcherEnabled"].toBool(true)
                                : m_cfg.launcherEnabled;
        m_cfg.spotlightEnabled  = o.contains("spotlightEnabled")
                                ? o["spotlightEnabled"].toBool(true)
                                : m_cfg.spotlightEnabled;
        m_cfg.launcherHasSearch = o.contains("launcherHasSearch")
                                ? o["launcherHasSearch"].toBool(false)
                                : m_cfg.launcherHasSearch;
        m_cfg.spotlightBarWidth = getI("spotlightBarWidth",
                                       m_cfg.spotlightBarWidth);
        m_cfg.spotlightBarHeight = getI("spotlightBarHeight",
                                        m_cfg.spotlightBarHeight);
        m_cfg.launcherIconSize  = getI("launcherIconSize",
                                       m_cfg.launcherIconSize);
        if (o.contains("dockApps") && o["dockApps"].isArray()) {
            m_cfg.dockApps.clear();
            for (const QJsonValue &v : o["dockApps"].toArray())
                m_cfg.dockApps << v.toString();
        }
        if (o.contains("launcherApps") && o["launcherApps"].isArray()) {
            m_cfg.launcherApps.clear();
            for (const QJsonValue &v : o["launcherApps"].toArray())
                m_cfg.launcherApps << v.toString();
        }
        if (o.contains("desktopApps") && o["desktopApps"].isArray()) {
            m_cfg.desktopApps.clear();
            for (const QJsonValue &v : o["desktopApps"].toArray())
                m_cfg.desktopApps << v.toString();
        }
        m_cfg.shellFontFamily     = o.contains("shellFontFamily")
                                   ? o["shellFontFamily"].toString()
                                   : m_cfg.shellFontFamily;
        m_cfg.shellMonoFontFamily = o.contains("shellMonoFontFamily")
                                   ? o["shellMonoFontFamily"].toString()
                                   : m_cfg.shellMonoFontFamily;
        m_cfg.shellBaseFontPt     = getI("shellBaseFontPt",
                                         m_cfg.shellBaseFontPt);
        m_cfg.lockWallpaper       = o.contains("lockWallpaper")
                                   ? o["lockWallpaper"].toString()
                                   : m_cfg.lockWallpaper;
        m_cfg.dndEnabled         = o.contains("dndEnabled")
                                   ? o["dndEnabled"].toBool(false)
                                   : m_cfg.dndEnabled;
        m_cfg.notificationBannerMs = getI("notificationBannerMs",
                                          m_cfg.notificationBannerMs);
        m_cfg.dockPosition       = getI("dockPosition",
                                        m_cfg.dockPosition);
        m_cfg.clock24Hour        = o.contains("clock24Hour")
                                   ? o["clock24Hour"].toBool(true)
                                   : m_cfg.clock24Hour;
        m_cfg.clockShowSeconds   = o.contains("clockShowSeconds")
                                   ? o["clockShowSeconds"].toBool(false)
                                   : m_cfg.clockShowSeconds;
        m_cfg.blurRefreshMs      = getI("blurRefreshMs",      m_cfg.blurRefreshMs);
        m_cfg.dockMagnifyMax     = getD("dockMagnifyMax",     m_cfg.dockMagnifyMax);
        m_cfg.dockSigma          = getD("dockSigma",          m_cfg.dockSigma);
        clampToSafeRanges();
    }

    void clampToSafeRanges() {
        if (m_cfg.topBarAlpha        < 30)  m_cfg.topBarAlpha        = 30;
        if (m_cfg.dockAlpha          < 30)  m_cfg.dockAlpha          = 30;
        if (m_cfg.launcherAlpha      < 60)  m_cfg.launcherAlpha      = 60;
        if (m_cfg.controlCenterAlpha < 60)  m_cfg.controlCenterAlpha = 60;
        if (m_cfg.widgetCardAlpha    < 40)  m_cfg.widgetCardAlpha    = 40;
        if (m_cfg.finderAlpha        < 30)  m_cfg.finderAlpha        = 30;
        if (m_cfg.dockIconSize       < 32)  m_cfg.dockIconSize       = 32;
        if (m_cfg.dockIconSize       > 90)  m_cfg.dockIconSize       = 90;
        if (m_cfg.minimizeStyle != "genie"
            && m_cfg.minimizeStyle != "scale"
            && m_cfg.minimizeStyle != "suck")
            m_cfg.minimizeStyle = "genie";
        if (m_cfg.genieDurationMs < 120) m_cfg.genieDurationMs = 120;
        if (m_cfg.genieDurationMs > 1500) m_cfg.genieDurationMs = 1500;
        if (m_cfg.genieStrips < 40)   m_cfg.genieStrips = 40;
        if (m_cfg.genieStrips > 320)  m_cfg.genieStrips = 320;
        if (m_cfg.genieStagger < 0.0) m_cfg.genieStagger = 0.0;
        if (m_cfg.genieStagger > 0.8) m_cfg.genieStagger = 0.8;
        if (m_cfg.genieCurve < 0.3)   m_cfg.genieCurve = 0.3;
        if (m_cfg.genieCurve > 2.0)   m_cfg.genieCurve = 2.0;
        if (m_cfg.genieFadeStart < 0.5) m_cfg.genieFadeStart = 0.5;
        if (m_cfg.genieFadeStart > 1.0) m_cfg.genieFadeStart = 1.0;
        if (m_cfg.screenOffTimeoutMin < 0)   m_cfg.screenOffTimeoutMin = 0;
        if (m_cfg.screenOffTimeoutMin > 240) m_cfg.screenOffTimeoutMin = 240;
        if (m_cfg.sleepTimeoutMin < 0)   m_cfg.sleepTimeoutMin = 0;
        if (m_cfg.sleepTimeoutMin > 240) m_cfg.sleepTimeoutMin = 240;
        if (m_cfg.lockTimeoutMin < 0)   m_cfg.lockTimeoutMin = 0;
        if (m_cfg.lockTimeoutMin > 240) m_cfg.lockTimeoutMin = 240;
        if (m_cfg.lockGraceSeconds < 0) m_cfg.lockGraceSeconds = 0;
        if (m_cfg.lockGraceSeconds > 60) m_cfg.lockGraceSeconds = 60;
        if (m_cfg.keyboardRepeatDelay < 100) m_cfg.keyboardRepeatDelay = 100;
        if (m_cfg.keyboardRepeatDelay > 2000) m_cfg.keyboardRepeatDelay = 2000;
        if (m_cfg.keyboardRepeatRate < 1)  m_cfg.keyboardRepeatRate = 1;
        if (m_cfg.keyboardRepeatRate > 100) m_cfg.keyboardRepeatRate = 100;
        if (m_cfg.mouseAccel < -10) m_cfg.mouseAccel = -10;
        if (m_cfg.mouseAccel > 10)  m_cfg.mouseAccel = 10;
        if (m_cfg.shellTextScale < 0.8) m_cfg.shellTextScale = 0.8;
        if (m_cfg.shellTextScale > 1.6) m_cfg.shellTextScale = 1.6;
        if (m_cfg.cursorSize < 16) m_cfg.cursorSize = 16;
        if (m_cfg.cursorSize > 96) m_cfg.cursorSize = 96;
        if (m_cfg.notifTimeoutSec < 1)  m_cfg.notifTimeoutSec = 1;
        if (m_cfg.notifTimeoutSec > 60) m_cfg.notifTimeoutSec = 60;
        if (m_cfg.spotMaxResults < 3)  m_cfg.spotMaxResults = 3;
        if (m_cfg.spotMaxResults > 20) m_cfg.spotMaxResults = 20;
        if (m_cfg.focusStartHour < 0)  m_cfg.focusStartHour = 0;
        if (m_cfg.focusStartHour > 23) m_cfg.focusStartHour = 23;
        if (m_cfg.focusEndHour < 0)    m_cfg.focusEndHour = 0;
        if (m_cfg.focusEndHour > 23)   m_cfg.focusEndHour = 23;
        if (m_cfg.screensaverTimeoutMin < 1)
            m_cfg.screensaverTimeoutMin = 1;
        if (m_cfg.screensaverTimeoutMin > 120)
            m_cfg.screensaverTimeoutMin = 120;
        if (m_cfg.shellBaseFontPt < 8)  m_cfg.shellBaseFontPt = 8;
        if (m_cfg.shellBaseFontPt > 18) m_cfg.shellBaseFontPt = 18;
        if (m_cfg.spotlightBarWidth < 400)
            m_cfg.spotlightBarWidth = 400;
        if (m_cfg.spotlightBarWidth > 1400)
            m_cfg.spotlightBarWidth = 1400;
        if (m_cfg.spotlightBarHeight < 60)
            m_cfg.spotlightBarHeight = 60;
        if (m_cfg.spotlightBarHeight > 700)
            m_cfg.spotlightBarHeight = 700;
        if (m_cfg.launcherIconSize < 36)
            m_cfg.launcherIconSize = 36;
        if (m_cfg.launcherIconSize > 80)
            m_cfg.launcherIconSize = 80;
        if (m_cfg.shellBaseFontPt < 8)  m_cfg.shellBaseFontPt = 8;
        if (m_cfg.shellBaseFontPt > 18) m_cfg.shellBaseFontPt = 18;
        if (m_cfg.notificationBannerMs < 1000)  m_cfg.notificationBannerMs = 1000;
        if (m_cfg.dockPosition < 0)  m_cfg.dockPosition = 0;
        if (m_cfg.dockPosition > 2)  m_cfg.dockPosition = 2;
        if (m_cfg.notificationBannerMs > 30000) m_cfg.notificationBannerMs = 30000;
        // Note: user's saved 110 will be respected; delete visual.json
        // to pick up the new default.
        if (m_cfg.dockMagnifyMax     < 0.05) m_cfg.dockMagnifyMax    = 0.05;
        if (m_cfg.dockSigma          < 15)  m_cfg.dockSigma          = 15;
    }

    void resetToDefaults() {
        m_cfg = VisualConfig();
        save();
    }

    void save() {
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QJsonObject o;
        o["blurStrength"]       = m_cfg.blurStrength;
        o["topBarAlpha"]        = m_cfg.topBarAlpha;
        o["dockAlpha"]          = m_cfg.dockAlpha;
        o["launcherAlpha"]      = m_cfg.launcherAlpha;
        o["controlCenterAlpha"] = m_cfg.controlCenterAlpha;
        o["widgetCardAlpha"]    = m_cfg.widgetCardAlpha;
        o["finderAlpha"]        = m_cfg.finderAlpha;
        o["widgetsVisible"]     = m_cfg.widgetsVisible;
        o["dockIconSize"]       = m_cfg.dockIconSize;
        o["genieEnabled"]       = m_cfg.genieEnabled;
        o["minimizeStyle"]      = m_cfg.minimizeStyle;
        o["genieDurationMs"]    = m_cfg.genieDurationMs;
        o["genieStrips"]        = m_cfg.genieStrips;
        o["genieStagger"]       = m_cfg.genieStagger;
        o["genieCurve"]         = m_cfg.genieCurve;
        o["genieFadeStart"]     = m_cfg.genieFadeStart;
        o["screenOffTimeoutMin"] = m_cfg.screenOffTimeoutMin;
        o["sleepTimeoutMin"]     = m_cfg.sleepTimeoutMin;
        o["lowPowerMode"]        = m_cfg.lowPowerMode;
        o["ccShowWiFi"]          = m_cfg.ccShowWiFi;
        o["ccShowBluetooth"]     = m_cfg.ccShowBluetooth;
        o["ccShowMedia"]         = m_cfg.ccShowMedia;
        o["ccShowToggles"]       = m_cfg.ccShowToggles;
        o["ccShowSound"]         = m_cfg.ccShowSound;
        o["ccShowDisplay"]       = m_cfg.ccShowDisplay;
        o["lockTimeoutMin"]      = m_cfg.lockTimeoutMin;
        o["sshEnabled"]          = m_cfg.sshEnabled;
        o["firewallEnabled"]     = m_cfg.firewallEnabled;
        o["lockOnSleep"]         = m_cfg.lockOnSleep;
        o["lockOnScreenOff"]     = m_cfg.lockOnScreenOff;
        o["lockGraceSeconds"]    = m_cfg.lockGraceSeconds;
        o["lockShowClock"]       = m_cfg.lockShowClock;
        o["lockShowDate"]        = m_cfg.lockShowDate;
        o["keyboardLayout"]      = m_cfg.keyboardLayout;
        o["keyboardVariant"]     = m_cfg.keyboardVariant;
        o["keyboardRepeatDelay"] = m_cfg.keyboardRepeatDelay;
        o["keyboardRepeatRate"]  = m_cfg.keyboardRepeatRate;
        o["keyboardCapsLock"]    = m_cfg.keyboardCapsLock;
        o["mouseAccel"]          = m_cfg.mouseAccel;
        o["touchpadNatural"]     = m_cfg.touchpadNatural;
        o["touchpadTap"]         = m_cfg.touchpadTap;
        o["touchpadTwoFinger"]   = m_cfg.touchpadTwoFinger;
        o["touchpadDisableWhileTyping"] = m_cfg.touchpadDisableWhileTyping;
        o["touchpadScrollMethod"] = m_cfg.touchpadScrollMethod;
        o["systemLanguage"]      = m_cfg.systemLanguage;
        o["backupDestination"]   = m_cfg.backupDestination;
        o["backupSchedule"]      = m_cfg.backupSchedule;
        o["shellReduceMotion"]   = m_cfg.shellReduceMotion;
        o["shellHighContrast"]   = m_cfg.shellHighContrast;
        o["shellTextScale"]      = m_cfg.shellTextScale;
        o["cursorSize"]          = m_cfg.cursorSize;
        o["visualAlerts"]        = m_cfg.visualAlerts;
        o["notifTimeoutSec"]     = m_cfg.notifTimeoutSec;
        o["locationEnabled"]     = m_cfg.locationEnabled;
        o["telemetryEnabled"]    = m_cfg.telemetryEnabled;
        o["spotCalcEnabled"]     = m_cfg.spotCalcEnabled;
        o["spotAppsEnabled"]     = m_cfg.spotAppsEnabled;
        o["spotFilesEnabled"]    = m_cfg.spotFilesEnabled;
        o["spotWebEnabled"]      = m_cfg.spotWebEnabled;
        o["spotMaxResults"]      = m_cfg.spotMaxResults;
        o["focusMode"]           = m_cfg.focusMode;
        o["focusPolicy"]         = m_cfg.focusPolicy;
        o["focusScheduleOn"]     = m_cfg.focusScheduleOn;
        o["focusStartHour"]      = m_cfg.focusStartHour;
        o["focusEndHour"]        = m_cfg.focusEndHour;
        o["screensaverEnabled"]  = m_cfg.screensaverEnabled;
        o["screensaverTimeoutMin"] = m_cfg.screensaverTimeoutMin;
        o["screensaverStyle"]    = m_cfg.screensaverStyle;
        o["screensaverImageDir"] = m_cfg.screensaverImageDir;
        o["screensaverVideoPath"]= m_cfg.screensaverVideoPath;
        o["shellFontFamily"]     = m_cfg.shellFontFamily;
        o["shellMonoFontFamily"] = m_cfg.shellMonoFontFamily;
        o["shellBaseFontPt"]     = m_cfg.shellBaseFontPt;
        o["boomtubeFolder"]      = m_cfg.boomtubeFolder;
        o["boomtubeShuffle"]     = m_cfg.boomtubeShuffle;
        o["boomtubeLoop"]        = m_cfg.boomtubeLoop;
        o["desktopIconPos"]      = QJsonObject::fromVariantMap(
                                       m_cfg.desktopIconPos);
        o["launcherEnabled"]     = m_cfg.launcherEnabled;
        o["spotlightEnabled"]    = m_cfg.spotlightEnabled;
        o["launcherHasSearch"]   = m_cfg.launcherHasSearch;
        o["spotlightBarWidth"]   = m_cfg.spotlightBarWidth;
        o["spotlightBarHeight"]  = m_cfg.spotlightBarHeight;
        o["launcherIconSize"]    = m_cfg.launcherIconSize;
        {
            QJsonArray arr;
            for (const QString &s : m_cfg.dockApps)     arr << s;
            o["dockApps"] = arr;
        }
        {
            QJsonArray arr;
            for (const QString &s : m_cfg.launcherApps) arr << s;
            o["launcherApps"] = arr;
        }
        {
            QJsonArray arr;
            for (const QString &s : m_cfg.desktopApps)  arr << s;
            o["desktopApps"] = arr;
        }
        o["shellFontFamily"]     = m_cfg.shellFontFamily;
        o["shellMonoFontFamily"] = m_cfg.shellMonoFontFamily;
        o["shellBaseFontPt"]     = m_cfg.shellBaseFontPt;
        o["lockWallpaper"]       = m_cfg.lockWallpaper;
        o["dndEnabled"]         = m_cfg.dndEnabled;
        o["notificationBannerMs"] = m_cfg.notificationBannerMs;
        o["dockPosition"]       = m_cfg.dockPosition;
        o["clock24Hour"]         = m_cfg.clock24Hour;
        o["clockShowSeconds"]    = m_cfg.clockShowSeconds;
        o["blurRefreshMs"]      = m_cfg.blurRefreshMs;
        o["dockMagnifyMax"]     = m_cfg.dockMagnifyMax;
        o["dockSigma"]          = m_cfg.dockSigma;
        QFile f(path());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
        f.close();
    }

    qint64 lastModified() const {
        return QFileInfo(path()).lastModified().toMSecsSinceEpoch();
    }

private:
    static QString path() {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::ConfigLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.config";
        return dir + "/apokolips/visual.json";
    }
    VisualConfig m_cfg;
    QList<Callback> m_callbacks;
};

#endif // APOKOLIPS_VISUALCONFIG_H
