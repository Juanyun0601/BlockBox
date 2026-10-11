/**
 * 探针用 ThemeManager 桩：只提供 InstanceOverviewPage / OutlinedLabel
 * 链接时真正引用到的接口，避免拉入 BackgroundManager / PluginManager 依赖链。
 */
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager *instance()
    {
        static ThemeManager m;
        return &m;
    }

    QString currentThemeColor() const { return QStringLiteral("#2E7D32"); }
    QString currentTextBorderColor() const { return QStringLiteral("#e8e8e8"); }
    QString getInfoColorHover() const { return QStringLiteral("#2196F3"); }
    int currentTheme() const { return 0; }

signals:
    void textBorderColorChanged(const QString &color);
};

#endif // THEMEMANAGER_H
