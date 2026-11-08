// 探针用桩实现：固定深色主题、简体中文，只为让弹窗渲染出来量尺寸。
#include "utils/LanguageManager.h"
#include "utils/ThemeManager.h"

ThemeManager *ThemeManager::instance()
{
    static ThemeManager s_instance;
    return &s_instance;
}

QString ThemeManager::currentThemeColor() const
{
    return QStringLiteral("#4a9eff");
}

QString ThemeManager::getThemeColorHover() const
{
    return QStringLiteral("#6fb4ff");
}

ThemeManager::Theme ThemeManager::currentTheme() const
{
    return DarkTheme;
}

LanguageManager *LanguageManager::instance()
{
    static LanguageManager s_instance;
    return &s_instance;
}

LanguageManager::Language LanguageManager::currentLanguage() const
{
    return Chinese;
}
