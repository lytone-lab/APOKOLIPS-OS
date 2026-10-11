#ifndef APOKOLIPS_GLOBALS_H
#define APOKOLIPS_GLOBALS_H

#include "shell.h"

static bool restoreGenieForDock(const QString &dockName);
static QString discoverSwaySock();

static volatile sig_atomic_t g_spotlightToggle = 0;
static volatile sig_atomic_t g_calcToggle = 0;
static volatile sig_atomic_t g_notesToggle = 0;
static volatile sig_atomic_t g_mcToggle = 0;
static volatile sig_atomic_t g_minimizeToggle = 0;

static QString g_swaySock;
static int     g_trackedConId = -1;
static QString g_trackedAppId;
static QString g_trackedTitle;
static QRect   g_trackedRect;

#endif // APOKOLIPS_GLOBALS_H
