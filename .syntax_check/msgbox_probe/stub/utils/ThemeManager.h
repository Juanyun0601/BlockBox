#ifndef STUB_THEMEMANAGER_H
#define STUB_THEMEMANAGER_H

// 探针用桩：只提供 AppMessageBox 实际用到的最小主题接口，
// 让弹窗能脱离整个启动器单独渲染出来做尺寸/截图验证。
#include <QString>

class ThemeManager
{
public:
    enum Theme { LightTheme, DarkTheme };

    static ThemeManager *instance();
    QString currentThemeColor() const;
    QString getThemeColorHover() const;
    Theme currentTheme() const;
};

#endif // STUB_THEMEMANAGER_H
