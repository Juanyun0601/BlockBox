/**
 * @file   ThemeManager.h
 * @brief  主题管理器类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QApplication>
#include <QColor>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @class ThemeManager
 * @brief 主题管理类，负责主题的加载、切换和保存
 * 
 * ThemeManager是一个单例类，提供全局的主题管理功能，包括：
 * - 主题的加载和切换
 * - 主题设置的保存和读取
 * - 主题色管理
 * - 提供静态方法供全局调用
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @enum ThemeType
     * @brief 主题类型枚举
     */
    enum ThemeType {
        LightTheme,  // 亮色主题
        DarkTheme,   // 深色主题
        CustomTheme  // 自定义主题
    };

    /**
     * @brief 获取ThemeManager的单例实例
     * @return ThemeManager* 单例实例指针
     */
    static ThemeManager* instance();

    /**
     * @brief 加载主题
     * @param theme 主题类型
     */
    void loadTheme(ThemeType theme);

    /**
     * @brief 加载主题
     * @param themeName 主题名称
     */
    void loadTheme(const QString& themeName);

    /**
     * @brief 获取当前主题类型
     * @return ThemeType 当前主题类型
     */
    ThemeType currentTheme() const;

    /**
     * @brief 获取当前主题名称
     * @return QString 当前主题名称
     */
    QString currentThemeName() const;

    /**
     * @brief 保存当前主题设置
     */
    void saveTheme();

    /**
     * @brief 静态方法，用于直接设置主题
     * @param app QApplication指针
     * @param theme 主题类型
     */
    static void setTheme(QApplication* app, ThemeType theme);

    /**
     * @brief 静态方法，用于直接设置主题
     * @param app QApplication指针
     * @param themeName 主题名称
     */
    static void setTheme(QApplication* app, const QString& themeName);

    /**
     * @brief 设置主题色
     * @param color 主题色
     */
    void setThemeColor(const QString& color);

    /**
     * @brief 获取当前主题色
     * @return QString 当前主题色
     */
    QString currentThemeColor() const;

    /**
     * @brief 设置文字颜色
     * @param color 文字颜色
     */
    void setTextColor(const QString& color);

    /**
     * @brief 获取当前文字颜色
     * @return QString 当前文字颜色
     */
    QString currentTextColor() const;

    /**
     * @brief 设置次要主题色
     * @param color 次要主题色
     */
    void setSecondaryThemeColor(const QString& color);

    /**
     * @brief 获取当前次要主题色
     * @return QString 当前次要主题色
     */
    QString currentSecondaryThemeColor() const;

    /**
     * @brief 设置侧边栏背景颜色
     * @param color 侧边栏背景颜色
     */
    void setSidebarBgColor(const QString& color);

    /**
     * @brief 获取当前侧边栏背景颜色
     * @return QString 当前侧边栏背景颜色
     */
    QString currentSidebarBgColor() const;

    /**
     * @brief 设置文字边框颜色
     * @param color 文字边框颜色
     */
    void setTextBorderColor(const QString& color);

    /**
     * @brief 获取当前文字边框颜色
     * @return QString 当前文字边框颜色
     */
    QString currentTextBorderColor() const;

    /**
     * @brief 应用文字边框颜色到当前主题
     */
    void applyTextBorderColor();

    /**
     * @brief 设置边框颜色
     * @param color 边框颜色
     */
    void setBorderColor(const QString& color);

    /**
     * @brief 获取当前边框颜色
     * @return QString 当前边框颜色
     */
    QString currentBorderColor() const;

    /**
     * @brief 应用边框颜色到当前主题
     */
    void applyBorderColor();

    /**
     * @brief 重新合成并应用全局样式（含当前已启用插件贡献的样式）
     *
     * 插件导入/删除/启用/停用后由宿主调用，使插件样式贡献立即生效或移除。
     */
    void reloadPluginStyles();

    /**
     * @brief 预设颜色信息结构体
     */
    struct PresetColor {
        QString name;     // 中文名称
        QString color;    // 十六进制颜色值
    };

    /**
     * @brief 获取常用主题色列表
     * @return QStringList 常用主题色列表
     */
    QStringList commonThemeColors() const;

    /**
     * @brief 获取预设颜色列表（16种）
     * @return QList<PresetColor> 预设颜色列表
     */
    QList<PresetColor> presetColors() const;

    /**
     * @brief 获取主题色 hover 变体（用于按钮hover状态）
     * @return QString hover颜色
     */
    QString getThemeColorHover() const;

    /**
     * @brief 获取主题色 pressed 变体（用于按钮pressed状态）
     * @return QString pressed颜色
     */
    QString getThemeColorPressed() const;

    /**
     * @brief 获取主题色浅色背景变体（用于hover背景等）
     * @return QString 浅色背景颜色
     */
    QString getThemeColorLight() const;

    /**
     * @brief 获取主题色浅色强调色变体（lighter(150)，用于按钮pressed、进度条chunk、列表选中项等）
     * @return QString 浅色强调色
     */
    QString getThemeColorLightAccent() const;

    /**
     * @brief 获取主题色中等强调色变体（lighter(120)，用于按钮pressed、进度条chunk、列表选中项等）
     * @return QString 中等强调色
     */
    QString getThemeColorMediumAccent() const;

    /**
     * @brief 获取信息强调色（默认 #2196F3）
     * @return QString 信息强调色
     */
    QString currentInfoAccentColor() const;

    /**
     * @brief 获取信息强调色 hover 变体
     * @return QString 信息强调色 hover 颜色
     */
    QString getInfoColorHover() const;

    /**
     * @brief 获取信息强调色 pressed 变体
     * @return QString 信息强调色 pressed 颜色
     */
    QString getInfoColorPressed() const;

    /**
     * @brief 获取信息强调色浅色背景变体
     * @return QString 信息强调色浅色背景
     */
    QString getInfoColorLight() const;

    /**
     * @brief 获取信息强调色浅色强调色变体
     * @return QString 信息强调色浅色强调色
     */
    QString getInfoColorLightAccent() const;

    /**
     * @brief 获取信息强调色中等强调色变体
     * @return QString 信息强调色中等强调色
     */
    QString getInfoColorMediumAccent() const;

    /**
     * @brief 应用主题色到当前主题
     */
    void applyThemeColor();

    /**
     * @brief 应用文字颜色到当前主题
     */
    void applyTextColor();

    /**
     * @brief 应用次要主题色到当前主题
     */
    void applySecondaryThemeColor();

/**
     * @brief 重置所有自定义颜色标记并套用当前主题默认值
     */
    void resetColorOverrides();

public slots:
    /**
     * @brief 主题色变更槽
     * @param color 新的主题色
     */
    void onThemeColorChanged(const QString& color);

    /**
     * @brief 文字颜色变更槽
     * @param color 新的文字颜色
     */
    void onTextColorChanged(const QString& color);

    /**
     * @brief 次要主题色变更槽
     * @param color 新的次要主题色
     */
    void onSecondaryThemeColorChanged(const QString& color);

    /**
     * @brief 侧边栏背景颜色变更槽
     * @param color 新的侧边栏背景颜色
     */
    void onSidebarBgColorChanged(const QString& color);

    /**
     * @brief 文字边框颜色变更槽
     * @param color 新的文字边框颜色
     */
    void onTextBorderColorChanged(const QString& color);

    /**
     * @brief 边框颜色变更槽
     * @param color 新的边框颜色
     */
    void onBorderColorChanged(const QString& color);

signals:
    /**
     * @brief 主题切换信号
     * @param themeType 新的主题类型
     */
    void themeChanged(ThemeType themeType);

    /**
     * @brief 主题色变更信号
     * @param color 新的主题色
     */
    void themeColorChanged(const QString& color);

    /**
     * @brief 文字颜色变更信号
     * @param color 新的文字颜色
     */
    void textColorChanged(const QString& color);

    /**
     * @brief 次要主题色变更信号
     * @param color 新的次要主题色
     */
    void secondaryThemeColorChanged(const QString& color);

    /**
     * @brief 侧边栏背景颜色变更信号
     * @param color 新的侧边栏背景颜色
     */
    void sidebarBgColorChanged(const QString& color);

    /**
     * @brief 文字边框颜色变更信号
     * @param color 新的文字边框颜色
     */
    void textBorderColorChanged(const QString& color);

    /**
     * @brief 边框颜色变更信号
     * @param color 新的边框颜色
     */
    void borderColorChanged(const QString& color);

private:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit ThemeManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~ThemeManager() override;

    /**
     * @brief 从配置文件加载主题设置
     */
    void loadThemeFromSettings();

    /**
     * @brief 获取主题文件路径
     * @param theme 主题类型
     * @return QString 主题文件路径
     */
    QString getThemeFilePath(ThemeType theme) const;

    /**
     * @brief 获取主题文件路径
     * @param themeName 主题名称
     * @return QString 主题文件路径
     */
    QString getThemeFilePath(const QString& themeName) const;

    /**
     * @brief 从文件加载主题样式
     * @param filePath 文件路径
     * @return QString 主题样式
     */
    QString loadThemeFromFile(const QString& filePath) const;
    
    /**
     * @brief 从文件加载主题样式（内部方法）
     * @param filePath 文件路径
     * @return QString 主题样式
     */
    QString loadThemeFromFileInternal(const QString& filePath) const;

    /**
     * @brief 构建当前主题状态下的完整 token -> 颜色 映射表
     * @return QMap<QString, QString> token 表（key 为 @TOKEN@）
     */
    QMap<QString, QString> buildTokenTable() const;

    /**
     * @brief 将样式表中所有 @TOKEN@ 占位符替换为对应颜色值
     * @param style 含占位符的样式表
     * @return QString 替换完成后的样式表
     */
    QString resolveTokens(const QString& style) const;

    /**
     * @brief 生成 rgba 颜色字符串
     * @param color 颜色
     * @param alphaStr 透明度字符串（如 "51" 或 "0.15"）
     * @return QString rgba(...)
     */
    QString rgbaString(const QColor& color, const QString& alphaStr) const;

    /**
     * @brief 应用侧边栏背景颜色覆盖
     * @param style 样式表
     */
    void applySidebarBgOverride(QString& style) const;

    /**
     * @brief 在深色/自定义主题下覆盖顶栏/侧边栏/子导航的玻璃态白色渐变
     * QSS 内 TopBar/SideBar/SubNavPanel 硬编码 rgba(255,255,255,…) 玻璃态，
     * 在深色主题下底色仍为浅白，导致滚动条 handle（白色 0.18 alpha）几乎不可见。
     * 此处统一在非浅色主题下追加 override，让 bar 背景随主题转深。
     * @param style 样式表（会被追加规则）
     */
    void applyDarkBarOverride(QString& style) const;

    /**
     * @brief 使用当前主题状态重新构建并应用完整样式表
     * 统一入口：加载 -> token 解析 -> 侧边栏覆盖 -> 背景覆盖 -> setStyleSheet
     */
    void applyCurrentThemeStyle() const;

private:
    static ThemeManager* m_instance;  // 单例实例
    ThemeType m_currentTheme;         // 当前主题类型
    QString m_currentThemeName;       // 当前主题名称
    QString m_currentThemeColor;      // 当前主题色
    QString m_currentTextColor;       // 当前文字颜色
    QString m_currentSecondaryThemeColor;  // 当前次要主题色
    QString m_sidebarBgColor;          // 当前侧边栏背景颜色
    QString m_textBorderColor;         // 当前文字边框颜色
    QString m_borderColor;             // 当前边框颜色
    QString m_infoAccentColor;          // 当前信息强调色
    QStringList m_commonThemeColors;   // 常用主题色列表
    QMap<QString, QString> m_themeCache;  // 主题文件缓存

    bool m_textColorCustomized;         // 文字颜色是否被用户显式自定义（否则随主题使用自动默认值）
    bool m_textBorderColorCustomized;   // 文字边框颜色是否被用户显式自定义
    bool m_borderColorCustomized;       // 边框颜色是否被用户显式自定义
    bool m_secondaryColorCustomized;    // 次要主题色是否被用户显式自定义

    /**
     * @brief 获取主题默认文字颜色
     * @param theme 主题类型
     * @return QString 默认文字颜色
     */
    QString defaultTextColor(ThemeType theme) const;

    /**
     * @brief 获取主题默认文字边框颜色
     * @param theme 主题类型
     * @return QString 默认文字边框颜色
     */
    QString defaultTextBorderColor(ThemeType theme) const;

    /**
     * @brief 获取主题默认边框颜色
     * @param theme 主题类型
     * @return QString 默认边框颜色
     */
    QString defaultBorderColor(ThemeType theme) const;

    /**
     * @brief 获取主题默认次要主题色
     * @param theme 主题类型
     * @return QString 默认次要主题色
     */
    QString defaultSecondaryColor(ThemeType theme) const;

    /**
     * @brief 为未自定义的颜色应用当前主题的适配套默认值
     * 深色/自定义主题下确保文字为浅色、边框为柔和的深灰，避免浅色默认值造成深色底上不可读。
     */
    void applyThemeColorDefaults();

    /**
     * @brief 预加载主题文件到缓存
     */
    void preloadThemeFiles();
};

#endif // THEMEMANAGER_H
