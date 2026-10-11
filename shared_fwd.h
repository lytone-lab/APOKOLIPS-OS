#ifndef APOKOLIPS_SHARED_FWD_H
#define APOKOLIPS_SHARED_FWD_H

// Shared forward declarations for helpers still defined in main.cpp.
// Every app header includes this once. Default arguments live here
// only — never re-declare them elsewhere (C++ forbids it).

#include "shell.h"

static void  setShellWindowGeometry(QWidget *window,
                                    const QSize &preferred,
                                    const QPoint &offset = QPoint(),
                                    bool centered = true);
static QRect shellSafeArea();
static void  shellMinimizeFocusedWindow();
static void  shellCloseViaGenie(QWidget *w);
static void  shellRestoreWindow(QWidget *w,
                                const QString &title,
                                const QSize &preferred = QSize());

#endif // APOKOLIPS_SHARED_FWD_H
