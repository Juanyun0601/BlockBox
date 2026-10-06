/**
 * 探针用 ThemeManager 桩：只提供 ContentViewSwitch 链接时真正引用到的接口，
 * 避免拉入 BackgroundManager / PluginManager 依赖链。
 * setThemeForProbe 会触发与真实 ThemeManager 相同的 themeChanged 信号。
 */
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum ThemeType { LightTheme, DarkTheme, CustomTheme };
    Q_ENUM(ThemeType)

    static ThemeManager *instance()
    {
        static ThemeManager m;
        return &m;
    }

    ThemeType currentTheme() const { return m_theme; }

    /* 探针专用：切换桩主题并发出 themeChanged */
    void setThemeForProbe(ThemeType t)
    {
        if (m_theme == t)
            return;
        m_theme = t;
        emit themeChanged(t);
    }

signals:
    void themeChanged(ThemeManager::ThemeType themeType);

private:
    ThemeManager() = default;
    ThemeType m_theme = LightTheme;
};

#endif // THEMEMANAGER_H
