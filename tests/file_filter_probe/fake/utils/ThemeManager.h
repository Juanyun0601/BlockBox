/**
 * 探针用 ThemeManager 桩：只提供 InstanceFilePage 及其依赖链
 * 真正引触到的接口，避免拉入 BackgroundManager / PluginManager 依赖链。
 */
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum ThemeType { LightTheme, DarkTheme };

    static ThemeManager *instance()
    {
        static ThemeManager m;
        return &m;
    }

    QString currentThemeColor() const { return QStringLiteral("#2E7D32"); }
    QString currentTextBorderColor() const { return QStringLiteral("#e8e8e8"); }
    QString getInfoColorHover() const { return QStringLiteral("#2196F3"); }
    QString getThemeColorHover() const { return QStringLiteral("#256428"); }
    QString currentInfoAccentColor() const { return QStringLiteral("#2196F3"); }
    QString getInfoColorPressed() const { return QStringLiteral("#1769aa"); }
    QString getInfoColorLight() const { return QStringLiteral("#e3f2fd"); }
    QString getInfoColorLightAccent() const { return QStringLiteral("#bbdefb"); }
    QString getInfoColorMediumAccent() const { return QStringLiteral("#1769aa"); }
    ThemeType currentTheme() const
    {
        return qEnvironmentVariableIsSet("BB_PROBE_DARK") ? DarkTheme : LightTheme;
    }

signals:
    void textBorderColorChanged(const QString &color);
    void themeChanged(ThemeType themeType);
    void themeColorChanged(const QString &color);
};

#endif // THEMEMANAGER_H
