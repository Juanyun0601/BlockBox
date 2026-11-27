/**
 * 探针用 ThemeManager 桩：只提供 MemoryOptimizePage / OutlinedLabel /
 * CollapsibleSectionCard / CustomCheckBox 真正引用到的接口，
 * 避免拉入 BackgroundManager / PluginManager 这条重依赖链。
 */
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum ThemeType {
        LightTheme = 0,
        DarkTheme = 1,
        CustomTheme = 2
    };
    Q_ENUM(ThemeType)

    static ThemeManager *instance()
    {
        static ThemeManager m;
        return &m;
    }

    ThemeType currentTheme() const { return m_theme; }
    void setCurrentTheme(ThemeType theme)
    {
        if (m_theme == theme)
            return;
        m_theme = theme;
        // 真实 ThemeManager 切主题会发这两条信号（箭头图标等靠它们重刷）
        emit textColorChanged(currentTextColor());
        emit textBorderColorChanged(currentTextBorderColor());
        emit themeChanged();
    }

    QString currentTextColor() const
    {
        return m_theme == LightTheme ? QStringLiteral("#333333") : QStringLiteral("#e8e8e8");
    }

    QString currentTextBorderColor() const
    {
        return m_theme == LightTheme ? QStringLiteral("#e8eaed") : QStringLiteral("#3a3a3a");
    }

    QString currentThemeColor() const { return QStringLiteral("#2E7D32"); }

signals:
    void textColorChanged(const QString &color);
    void textBorderColorChanged(const QString &color);
    void themeChanged();

private:
    ThemeType m_theme = LightTheme;
};

#endif // THEMEMANAGER_H
