#pragma once

#include <QMenu>
#include <QWidget>
#include "theme.h"
#include "visualconfig.h"

inline QString menuStyle(const Theme &)
{
    const auto &vc = VisualConfigManager::instance().cfg();
    const int bgAlpha = vc.menuOpacity * 255 / 100;
    // Calculate vertical padding to achieve the desired total item height.
    // Qt items include text (~16px), so subtract that from the total height.
    int padY = qMax(2, (vc.menuItemHeight - 16) / 2);
    
    return QString(
        "QMenu {"
        "  background: rgba(30, 30, 35, %1);"
        "  border: 1px solid rgba(255, 255, 255, 30);"
        "  border-radius: %2px;"
        "  padding: %3px %3px;"
        "  color: #f0f0f5;"
        "  font-size: 13px;"
        "}"
        "QMenu::item {"
        "  padding: %4px 28px %4px 16px;"
        "  border-radius: 6px;"
        "  background: transparent;"
        "  color: #f0f0f5;"
        "}"
        "QMenu::indicator {"
        "  width: 0px; height: 0px;"
        "}"
        "QMenu::item:selected {"
        "  background: rgba(255, 255, 255, 45);"
        "  color: #ffffff;"
        "}"
        "QMenu::item:disabled {"
        "  color: #6a6a72;"
        "  background: transparent;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: rgba(255, 255, 255, 40);"
        "  margin: 4px 8px;"
        "}"
        "QMenu::right-arrow {"
        "  image: none;"
        "  width: 0; height: 0; margin: 0;"
        "}")
        .arg(bgAlpha)
        .arg(vc.menuRadius)
        .arg(vc.menuPadding)
        .arg(padY);
}

class ApoMenu : public QMenu {
public:
    explicit ApoMenu(QWidget *parent = nullptr) : QMenu(parent) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet(menuStyle(ThemeManager::instance().current()));
    }
};
