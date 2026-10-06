#include "ThemeManager.h"
#include "BackgroundManager.h"
#include "utils/plugin/PluginManager.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>

ThemeManager* ThemeManager::m_instance = nullptr;

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent),
      m_currentTheme(LightTheme),
      m_currentThemeName("light"),
      m_currentThemeColor("#2E7D32"),
      m_currentTextColor("#333333"),
      m_currentSecondaryThemeColor("#256428"),
      m_sidebarBgColor(""),
      m_textBorderColor("#e8e8e8"),
      m_borderColor("#e8e8e8"),
      m_infoAccentColor("#2196F3"),
      m_textColorCustomized(false),
      m_textBorderColorCustomized(false),
      m_borderColorCustomized(false),
      m_secondaryColorCustomized(false)
{
    m_commonThemeColors << "#F5F5F5"
                        << "#212121"
                        << "#9E9E9E"
                        << "#F44336"
                        << "#FF9800"
                        << "#FFC107"
                        << "#2E7D32"
                        << "#2196F3"
                        << "#9C27B0"
                        << "#E91E63"
                        << "#795548"
                        << "#00BCD4"
                        << "#3F51B5"
                        << "#FF4081"
                        << "#03A9F4"
                        << "#8BC34A";

    loadThemeFromSettings();
    preloadThemeFiles();
}

ThemeManager::~ThemeManager()
{
    saveTheme();
}

ThemeManager* ThemeManager::instance()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!m_instance) {
        m_instance = new ThemeManager();
    }
    return m_instance;
}

void ThemeManager::loadTheme(ThemeType theme)
{
    m_currentTheme = theme;

    switch (theme) {
    case LightTheme:
        m_currentThemeName = "light";
        break;
    case DarkTheme:
        m_currentThemeName = "dark";
        break;
    case CustomTheme:
        m_currentThemeName = "custom";
        break;
    default:
        m_currentThemeName = "light";
        m_currentTheme = LightTheme;
        break;
    }

    applyThemeColorDefaults();
    applyCurrentThemeStyle();
    saveTheme();
    emit themeChanged(theme);
}

QString ThemeManager::defaultTextColor(ThemeType theme) const
{
    return (theme == LightTheme) ? QStringLiteral("#333333") : QStringLiteral("#e8e8e8");
}

QString ThemeManager::defaultTextBorderColor(ThemeType theme) const
{
    return (theme == LightTheme) ? QStringLiteral("#e8e8e8") : QStringLiteral("#666666");
}

QString ThemeManager::defaultBorderColor(ThemeType theme) const
{
    return (theme == LightTheme) ? QStringLiteral("#e8e8e8") : QStringLiteral("#444444");
}

QString ThemeManager::defaultSecondaryColor(ThemeType theme) const
{
    return (theme == LightTheme) ? QStringLiteral("#256428") : QStringLiteral("#4caf6e");
}

void ThemeManager::applyThemeColorDefaults()
{
    // 仅在用户未显式自定义对应颜色时，才按当前主题套用适配套默认值，
    // 保证深色主题下默认文字为浅色（而非沿用浅色主题的 #333333 导致深底深字不可读）。
    if (!m_textColorCustomized)
        m_currentTextColor = defaultTextColor(m_currentTheme);
    if (!m_textBorderColorCustomized)
        m_textBorderColor = defaultTextBorderColor(m_currentTheme);
    if (!m_borderColorCustomized)
        m_borderColor = defaultBorderColor(m_currentTheme);
    if (!m_secondaryColorCustomized)
        m_currentSecondaryThemeColor = defaultSecondaryColor(m_currentTheme);
}

void ThemeManager::loadTheme(const QString& themeName)
{
    if (themeName == "dark") {
        loadTheme(DarkTheme);
    } else if (themeName == "custom") {
        loadTheme(CustomTheme);
    } else {
        loadTheme(LightTheme);
    }
}

ThemeManager::ThemeType ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

QString ThemeManager::currentThemeName() const
{
    return m_currentThemeName;
}

void ThemeManager::saveTheme()
{
    QSettings settings("BlockBox", "Settings");
    settings.setValue("theme", m_currentThemeName);
    settings.setValue("themeColor", m_currentThemeColor);
    settings.setValue("textColor", m_currentTextColor);
    settings.setValue("secondaryThemeColor", m_currentSecondaryThemeColor);
    settings.setValue("sidebarBgColor", m_sidebarBgColor);
    settings.setValue("textBorderColor", m_textBorderColor);
    settings.setValue("borderColor", m_borderColor);
    settings.setValue("textColorCustomized", m_textColorCustomized);
    settings.setValue("textBorderColorCustomized", m_textBorderColorCustomized);
    settings.setValue("borderColorCustomized", m_borderColorCustomized);
    settings.setValue("secondaryColorCustomized", m_secondaryColorCustomized);
}

void ThemeManager::setTheme(QApplication* app, ThemeType theme)
{
    ThemeManager* mgr = instance();
    mgr->m_currentTheme = theme;
    switch (theme) {
    case DarkTheme:
        mgr->m_currentThemeName = "dark";
        break;
    case CustomTheme:
        mgr->m_currentThemeName = "custom";
        break;
    default:
        mgr->m_currentThemeName = "light";
        mgr->m_currentTheme = LightTheme;
        break;
    }
    mgr->applyThemeColorDefaults();
    mgr->applyCurrentThemeStyle();
}

void ThemeManager::setTheme(QApplication* app, const QString& themeName)
{
    ThemeType theme = LightTheme;
    if (themeName == "dark") {
        theme = DarkTheme;
    } else if (themeName == "custom") {
        theme = CustomTheme;
    }
    setTheme(app, theme);
}

void ThemeManager::setThemeColor(const QString& color)
{
    m_currentThemeColor = color;
    applyCurrentThemeStyle();
    saveTheme();
    emit themeColorChanged(color);
}

QString ThemeManager::currentThemeColor() const
{
    return m_currentThemeColor;
}

QStringList ThemeManager::commonThemeColors() const
{
    return m_commonThemeColors;
}

QList<ThemeManager::PresetColor> ThemeManager::presetColors() const
{
    return {
        {tr("白色"), "#F5F5F5"},
        {tr("黑色"), "#212121"},
        {tr("灰色"), "#9E9E9E"},
        {tr("红色"), "#F44336"},
        {tr("橙色"), "#FF9800"},
        {tr("黄色"), "#FFC107"},
        {tr("绿色"), "#2E7D32"},
        {tr("蓝色"), "#2196F3"},
        {tr("紫色"), "#9C27B0"},
        {tr("粉色"), "#E91E63"},
        {tr("棕色"), "#795548"},
        {tr("青色"), "#00BCD4"},
        {tr("靛色"), "#3F51B5"},
        {tr("玫红"), "#FF4081"},
        {tr("湖蓝"), "#03A9F4"},
        {tr("草绿"), "#8BC34A"}
    };
}

QString ThemeManager::getThemeColorHover() const
{
    return QColor(m_currentThemeColor).darker(110).name();
}

QString ThemeManager::getThemeColorPressed() const
{
    return QColor(m_currentThemeColor).darker(125).name();
}

QString ThemeManager::getThemeColorLight() const
{
    return QColor(m_currentThemeColor).lighter(180).name();
}

QString ThemeManager::getThemeColorLightAccent() const
{
    return QColor(m_currentThemeColor).lighter(150).name();
}

QString ThemeManager::getThemeColorMediumAccent() const
{
    return QColor(m_currentThemeColor).lighter(120).name();
}

QString ThemeManager::currentInfoAccentColor() const
{
    return m_infoAccentColor;
}

QString ThemeManager::getInfoColorHover() const
{
    return QColor(m_infoAccentColor).darker(110).name();
}

QString ThemeManager::getInfoColorPressed() const
{
    return QColor(m_infoAccentColor).darker(125).name();
}

QString ThemeManager::getInfoColorLight() const
{
    return QColor(m_infoAccentColor).lighter(180).name();
}

QString ThemeManager::getInfoColorLightAccent() const
{
    return QColor(m_infoAccentColor).lighter(150).name();
}

QString ThemeManager::getInfoColorMediumAccent() const
{
    return QColor(m_infoAccentColor).lighter(120).name();
}

void ThemeManager::applyThemeColor()
{
    applyCurrentThemeStyle();
}

void ThemeManager::onThemeColorChanged(const QString& color)
{
    setThemeColor(color);
}

void ThemeManager::loadThemeFromSettings()
{
    QSettings settings("BlockBox", "Settings");
    QString themeName = settings.value("theme", "light").toString();
    QString themeColor = settings.value("themeColor", "#2E7D32").toString();
    QString textColor = settings.value("textColor", "#333333").toString();
    QString secondaryThemeColor = settings.value("secondaryThemeColor", "#256428").toString();
    QString sidebarBgColor = settings.value("sidebarBgColor", "").toString();
    QString textBorderColor = settings.value("textBorderColor", "#e8e8e8").toString();
    QString borderColor = settings.value("borderColor", "#e8e8e8").toString();

    m_currentThemeColor = themeColor;
    m_currentTextColor = textColor;
    m_currentSecondaryThemeColor = secondaryThemeColor;
    m_sidebarBgColor = sidebarBgColor;
    m_textBorderColor = textBorderColor;
    m_borderColor = borderColor;

    m_textColorCustomized      = settings.value("textColorCustomized", false).toBool();
    m_textBorderColorCustomized = settings.value("textBorderColorCustomized", false).toBool();
    m_borderColorCustomized    = settings.value("borderColorCustomized", false).toBool();
    m_secondaryColorCustomized = settings.value("secondaryColorCustomized", false).toBool();

    loadTheme(themeName);
}

QString ThemeManager::getThemeFilePath(ThemeType theme) const
{
    Q_UNUSED(theme)
    return ":/styles/style.qss";
}

QString ThemeManager::getThemeFilePath(const QString& themeName) const
{
    Q_UNUSED(themeName)
    return ":/styles/style.qss";
}

void ThemeManager::preloadThemeFiles()
{
    QString style = loadThemeFromFileInternal(":/styles/style.qss");
    if (!style.isEmpty()) {
        m_themeCache[":/styles/style.qss"] = style;
    }
    qDebug() << "[ThemeManager]" << "Style file loaded:" << style.size() << "bytes";
}

QString ThemeManager::loadThemeFromFileInternal(const QString& filePath) const
{
    QFile themeFile(filePath);
    if (themeFile.open(QFile::ReadOnly)) {
        QString style = QString::fromUtf8(themeFile.readAll());
        themeFile.close();
        return style;
    }
    qDebug() << "[ThemeManager]" << "Failed to load style file:" << filePath;
    return "";
}

QString ThemeManager::loadThemeFromFile(const QString& filePath) const
{
    if (m_themeCache.contains(filePath)) {
        return m_themeCache[filePath];
    }
    QString style = loadThemeFromFileInternal(filePath);
    if (!style.isEmpty()) {
        const_cast<ThemeManager*>(this)->m_themeCache[filePath] = style;
    }
    return style;
}

QString ThemeManager::rgbaString(const QColor& color, const QString& alphaStr) const
{
    return QString("rgba(%1, %2, %3, %4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(alphaStr);
}

QMap<QString, QString> ThemeManager::buildTokenTable() const
{
    QMap<QString, QString> T;

    const bool isLight = (m_currentTheme == LightTheme);
    const bool isDark  = (m_currentTheme == DarkTheme);

    // ──────────────────────────── 静态表面 / 边框 / 文字 ────────────────────────────
    if (isLight) {
        T["@BG_BASE@"]         = "#f5f5f5";
        T["@BG_CARD@"]         = "#ffffff";
        T["@BG_CONTENT@"]      = "#fafafa";
        T["@BG_MUTED@"]        = "#f8f9fa";
        T["@BG_HOVER@"]        = "#eaeaea";
        T["@BG_PRESSED@"]      = "#e0e0e0";
        T["@BG_TRACK@"]        = "#e8e8e8";
        T["@BG_TAB@"]          = "#f0f0f0";
        T["@BG_DISABLED@"]     = "#d5d7db";
        T["@BG_MSG@"]          = "#edf0f4";

        T["@BORDER@"]          = "#e8eaed";
        T["@BORDER_STRONG@"]   = "#dadce0";
        T["@BORDER_HOVER@"]    = "#bdbdbd";
        T["@BORDER_DASHED@"]   = "#c4c7c5";
        T["@BORDER_DISABLED@"] = "#b0b0b0";
        T["@BORDER_LIGHT@"]    = "#f0f1f3";

        T["@TEXT_SECONDARY@"]  = "#666666";
        T["@TEXT_TERTIARY@"]   = "#888888";
        T["@TEXT_DISABLED@"]   = "#bbbbbb";

        T["@DANGER_BG@"]       = "#ffebee";
        T["@DANGER_BORDER@"]   = "#ffcdd2";
        T["@DANGER_TEXT@"]     = "#c62828";

        T["@WARNING_BG@"]      = "#fff8e1";
        T["@WARNING_BORDER@"]  = "#ffe082";
        T["@WARNING_TEXT@"]    = "#e65100";
        T["@WARNING_TEXT_DARK@"] = "#856404";

        T["@SUCCESS@"]         = "#059669";

        T["@THINK_BG@"]        = "#fffde7";
    } else if (isDark) {
        T["@BG_BASE@"]         = "#1a1a1a";
        T["@BG_CARD@"]         = "#2d2d2d";
        T["@BG_CONTENT@"]      = "#1f1f1f";
        T["@BG_MUTED@"]        = "#2d2d2d";
        T["@BG_HOVER@"]        = "#3d3d3d";
        T["@BG_PRESSED@"]      = "#4a4a4a";
        T["@BG_TRACK@"]        = "#4a4a4a";
        T["@BG_TAB@"]          = "#252525";
        T["@BG_DISABLED@"]     = "#3d3d3d";
        T["@BG_MSG@"]          = "#1a1a1a";

        T["@BORDER@"]          = "#3a3a3a";
        T["@BORDER_STRONG@"]   = "#3a3a3a";
        T["@BORDER_HOVER@"]    = "#444444";
        T["@BORDER_DASHED@"]   = "#555555";
        T["@BORDER_DISABLED@"] = "#444444";
        T["@BORDER_LIGHT@"]    = "#444444";

        T["@TEXT_SECONDARY@"]  = "#b0b0b0";
        T["@TEXT_TERTIARY@"]   = "#808080";
        T["@TEXT_DISABLED@"]   = "#808080";

        T["@DANGER_BG@"]       = "#5d2d2d";
        T["@DANGER_BORDER@"]   = "#8d4d4d";
        T["@DANGER_TEXT@"]     = "#ff6b6b";

        T["@WARNING_BG@"]      = "#4d3d2d";
        T["@WARNING_BORDER@"]  = "#8d7d3d";
        T["@WARNING_TEXT@"]    = "#ff9800";
        T["@WARNING_TEXT_DARK@"] = "#e0c080";

        T["@SUCCESS@"]         = "#10B981";

        T["@THINK_BG@"]        = "#2d2818";
    } else {  // CustomTheme
        T["@BG_BASE@"]         = "#2a2a2a";
        T["@BG_CARD@"]         = "#333333";
        T["@BG_CONTENT@"]      = "#2a2a2a";
        T["@BG_MUTED@"]        = "#2a2a2a";
        T["@BG_HOVER@"]        = "#3d3d3d";
        T["@BG_PRESSED@"]      = "#444444";
        T["@BG_TRACK@"]        = "#444444";
        T["@BG_TAB@"]          = "#2d2d2d";
        T["@BG_DISABLED@"]     = "#444444";
        T["@BG_MSG@"]          = "#222222";

        T["@BORDER@"]          = "#404040";
        T["@BORDER_STRONG@"]   = "#404040";
        T["@BORDER_HOVER@"]    = "#444444";
        T["@BORDER_DASHED@"]   = "#555555";
        T["@BORDER_DISABLED@"] = "#444444";
        T["@BORDER_LIGHT@"]    = "#444444";

        T["@TEXT_SECONDARY@"]  = "#b0b0b0";
        T["@TEXT_TERTIARY@"]   = "#808080";
        T["@TEXT_DISABLED@"]   = "#808080";

        T["@DANGER_BG@"]       = "#5d2d2d";
        T["@DANGER_BORDER@"]   = "#8d4d4d";
        T["@DANGER_TEXT@"]     = "#ff6b6b";

        T["@WARNING_BG@"]      = "#4d3d2d";
        T["@WARNING_BORDER@"]  = "#8d7d3d";
        T["@WARNING_TEXT@"]    = "#f57c00";
        T["@WARNING_TEXT_DARK@"] = "#e0c080";

        T["@SUCCESS@"]         = "#10B981";

        T["@THINK_BG@"]        = "#3d3525";
    }

    // 恒定色（不随主题变化）
    T["@BORDER_ON_ACCENT@"] = "#ffffff";
    T["@TEXT_ON_PRIMARY@"]  = "#ffffff";

    // 语义别名（部分区块按语义命名引用，取值随主题；漏登记会令 Qt 解析中止、其后样式全部丢弃）
    T["@BG_SECONDARY@"]   = T.value("@BG_MUTED@");
    T["@BORDER_DEFAULT@"] = T.value("@BORDER@");

    // 玻璃态微透明卡片底色（页面顶部过滤条/底部翻页栏等，透出页面/自定义背景）
    T["@BG_GLASS@"] = isLight ? "rgba(255, 255, 255, 0.65)"
                    : (isDark ? "rgba(45, 45, 45, 0.65)"
                              : "rgba(42, 42, 42, 0.65)");

    // ──────────────────────────── 动态文字 / 次要主题色 ────────────────────────────
    T["@TEXT_PRIMARY@"] = m_currentTextColor;
    T["@TEXT_BORDER@"]  = m_textBorderColor;
    T["@SECONDARY@"]    = m_currentSecondaryThemeColor;

    // ──────────────────────────── 主色族（由 m_currentThemeColor 派生） ────────────────────────────
    const QColor pc(m_currentThemeColor);
    T["@PRIMARY@"]           = pc.name();
    T["@PRIMARY_HOVER@"]     = pc.darker(110).name();
    T["@PRIMARY_PRESSED@"]   = pc.darker(125).name();
    T["@PRIMARY_DARK@"]      = pc.darker(130).name();
    T["@PRIMARY_GRAD1@"]     = pc.lighter(130).name();
    T["@PRIMARY_GRAD2@"]     = pc.lighter(150).name();
    T["@PRIMARY_GRAD3@"]     = pc.lighter(140).name();
    T["@PRIMARY_MEDIUM@"]    = pc.lighter(120).name();
    T["@PRIMARY_LIGHT@"]     = pc.lighter(150).name();
    T["@PRIMARY_LIGHTER@"]   = pc.lighter(160).name();
    T["@PRIMARY_BG@"]        = isLight ? pc.lighter(180).name()
                                : (isDark ? "#2d4d2d" : "#3d5d3d");
    T["@PRIMARY_BG_SOFT@"]   = pc.lighter(190).name();

    // 强调文字（亮色主题深绿，暗色/自定义主题浅绿以保证可读性）
    T["@TEXT_ACCENT@"]       = isLight ? pc.darker(140).name()
                                : pc.lighter(120).name();
    T["@TEXT_ACCENT_DARK@"]  = isLight ? pc.darker(170).name()
                                : pc.lighter(150).name();
    // 主色背景上的说明文字（modifyBanner / modifyInfoBar 等）
    T["@PRIMARY_TEXT@"]      = T.value("@TEXT_ACCENT@");

    // 主色 rgba（box-shadow / 半透明背景共用）
    static const QStringList primaryAlphas = {
        "8", "10", "12", "15", "18", "20", "26", "30", "31", "38", "40",
        "46", "51", "56", "64", "76", "89", "102", "0.1", "0.15", "0.2"
    };
    for (const QString& a : primaryAlphas) {
        T[QString("@PRIMARY_RGBA_%1@").arg(a)] = rgbaString(pc, a);
    }

    // ──────────────────────────── 信息强调色族（由 m_infoAccentColor 派生） ────────────────────────────
    const QColor ic(m_infoAccentColor);
    T["@INFO@"]             = isLight ? ic.name() : ic.lighter(130).name();
    T["@INFO_HOVER@"]       = ic.darker(110).name();
    T["@INFO_PRESSED@"]     = ic.darker(125).name();
    T["@INFO_BG@"]          = isLight ? ic.lighter(180).name()
                                : (isDark ? "#2d3d4d" : "#3d4d5d");
    T["@INFO_BG_DEEP@"]     = isLight ? ic.lighter(150).name()
                                : (isDark ? "#0d2f4d" : "#1a2f3d");
    T["@INFO_BG_SOFT@"]     = ic.lighter(190).name();
    T["@INFO_BORDER@"]      = isLight ? ic.lighter(120).name()
                                : (isDark ? "#3d5080" : "#4d6080");
    T["@INFO_BORDER_LIGHT@"]= ic.lighter(160).name();
    T["@INFO_DARK@"]        = ic.lighter(130).name();

    static const QStringList infoAlphas = { "20", "26", "38", "51" };
    for (const QString& a : infoAlphas) {
        T[QString("@INFO_RGBA_%1@").arg(a)] = rgbaString(ic, a);
    }

    // ──────────────────────────── 危险 / 警告 静态色族 ────────────────────────────
    T["@DANGER@"]           = "#f44336";
    T["@DANGER_HOVER@"]     = "#d32f2f";
    T["@DANGER_PRESSED@"]   = "#b71c1c";
    T["@DANGER_LIGHT@"]     = "#ff5252";

    static const QStringList dangerAlphas = { "26", "31", "38", "51" };
    for (const QString& a : dangerAlphas) {
        T[QString("@DANGER_RGBA_%1@").arg(a)] = rgbaString(QColor("#f44336"), a);
    }

    T["@WARNING@"]          = "#ff9800";
    T["@WARNING_HOVER@"]    = isLight ? "#fb8c00" : (isDark ? "#ffb74d" : "#ff9800");

    static const QStringList warningAlphas = { "26", "31", "38", "64" };
    for (const QString& a : warningAlphas) {
        T[QString("@WARNING_RGBA_%1@").arg(a)] = rgbaString(QColor("#ff9800"), a);
    }

    T["@THINK_RGBA_51@"]    = isLight ? "rgba(255, 224, 130, 51)"
                                : "rgba(255, 183, 77, 51)";

    // ──────────────────────────── 链接色族（静态） ────────────────────────────
    T["@LINK@"]             = "#1a73e8";
    T["@LINK_HOVER@"]       = "#409eff";
    T["@LINK_PRESSED@"]     = "#1765cc";
    T["@LINK_RGBA_0.10@"]   = "rgba(26, 115, 232, 0.10)";

    // ──────────────────────────── 编辑器 / 代码块（固定不随主题变化） ────────────────────────────
    T["@EDITOR_BG@"]        = "#1e1e1e";
    T["@EDITOR_BG_ALT@"]    = "#2a2a2a";
    T["@EDITOR_BORDER@"]    = "#3a3a3a";
    T["@EDITOR_SELECTION@"] = "#264f78";
    T["@EDITOR_TEXT@"]      = "#d4d4d4";

    T["@CODE_BG@"]          = "#f6f8fa";
    T["@CODE_BORDER@"]      = "#e1e4e8";
    T["@CODE_TEXT@"]        = "#c7254e";

    // ──────────────────────────── 杂项固定色 ────────────────────────────
    T["@INDIGO@"]           = "#5c6bc0";
    T["@INDIGO_RGBA_20@"]   = "rgba(92, 107, 192, 20)";
    T["@INDIGO_RGBA_46@"]   = "rgba(92, 107, 192, 46)";

    T["@TEAL_RGBA_80@"]     = "rgba(0, 200, 200, 80)";
    T["@TEAL_RGBA_150@"]    = "rgba(0, 200, 200, 150)";

    T["@WHITE_RGBA_30@"]    = "rgba(255, 255, 255, 30)";
    T["@WHITE_RGBA_235@"]   = isLight ? "rgba(255, 255, 255, 235)"
                                : (isDark ? "rgba(45, 45, 45, 242)"
                                          : "rgba(51, 51, 51, 242)");

    // ──────────────────────────── 顶部栏 / 侧边栏默认灰色面 ────────────────────────────
T["@BAR_BG@"]           = isLight ? "#E0E0E0"
                                : (isDark ? "#2E2E2E"
                                          : "#343434");

    // ──────────────────────────── 黑色 rgba（暗色主题下翻转为白色，用于滚动条/悬停遮罩） ────────────────────────────
    static const QStringList blackIntAlphas = { "5", "8", "10", "15", "20", "31", "64", "115" };
    for (const QString& a : blackIntAlphas) {
        T[QString("@BLACK_RGBA_%1@").arg(a)] = "rgba(0, 0, 0, " + a + ")";
    }

    static const struct { QString dec; QString white; } blackDecAlphas[] = {
        { "0.06", "0.08" }, { "0.08", "0.10" }, { "0.12", "0.12" }, { "0.13", "0.15" },
        { "0.18", "0.18" }, { "0.20", "0.20" }, { "0.25", "0.25" }, { "0.32", "0.32" }
    };
    for (const auto& b : blackDecAlphas) {
        T[QString("@BLACK_RGBA_%1@").arg(b.dec)] =
                isLight ? "rgba(0, 0, 0, " + b.dec + ")"
                        : "rgba(255, 255, 255, " + b.white + ")";
    }

    return T;
}

QString ThemeManager::resolveTokens(const QString& style) const
{
    QMap<QString, QString> table = buildTokenTable();

    static const QRegularExpression re("@([A-Z0-9_.]+)@");
    QString result;
    int last = 0;

    QRegularExpressionMatchIterator it = re.globalMatch(style);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        result += style.mid(last, m.capturedStart() - last);
        result += table.value(m.captured(0), m.captured(0));
        last = m.capturedEnd();
    }
    result += style.mid(last);
    return result;
}

void ThemeManager::applyCurrentThemeStyle() const
{
    QString style = loadThemeFromFile(":/styles/style.qss");
    style = resolveTokens(style);

    applySidebarBgOverride(style);
    applyDarkBarOverride(style);

    QString bgOverride = BackgroundManager::instance()->backgroundStyleSheet();
    if (!bgOverride.isEmpty())
        style += "\n" + bgOverride;

    // 追加「已启用」插件贡献的启动器样式（放最后，可覆盖内置主题）。
    // 与主样式一致，插件 QSS 同样支持 @TOKEN@ 占位符（resolveTokens 一并解析）。
    const QStringList pluginStyles = PluginManager::instance()->activePluginStyles();
    for (const QString &pluginStyle : pluginStyles) {
        if (!pluginStyle.trimmed().isEmpty())
            style += "\n" + resolveTokens(pluginStyle);
    }

    qApp->setStyleSheet(style);
}

void ThemeManager::reloadPluginStyles()
{
    applyCurrentThemeStyle();
}

void ThemeManager::resetColorOverrides()
{
    m_textColorCustomized = false;
    m_textBorderColorCustomized = false;
    m_borderColorCustomized = false;
    m_secondaryColorCustomized = false;
    applyThemeColorDefaults();
    applyCurrentThemeStyle();
    saveTheme();
}

void ThemeManager::setTextColor(const QString& color)
{
    m_currentTextColor = color;
    m_textColorCustomized = true;
    applyCurrentThemeStyle();
    saveTheme();
    emit textColorChanged(color);
}

QString ThemeManager::currentTextColor() const
{
    return m_currentTextColor;
}

void ThemeManager::applyTextColor()
{
    applyCurrentThemeStyle();
}

void ThemeManager::onTextColorChanged(const QString& color)
{
    setTextColor(color);
}

void ThemeManager::setSecondaryThemeColor(const QString& color)
{
    m_currentSecondaryThemeColor = color;
    m_secondaryColorCustomized = true;
    applyCurrentThemeStyle();
    saveTheme();
    emit secondaryThemeColorChanged(color);
}

QString ThemeManager::currentSecondaryThemeColor() const
{
    return m_currentSecondaryThemeColor;
}

void ThemeManager::applySecondaryThemeColor()
{
    applyCurrentThemeStyle();
}

void ThemeManager::onSecondaryThemeColorChanged(const QString& color)
{
    setSecondaryThemeColor(color);
}

void ThemeManager::setSidebarBgColor(const QString& color)
{
    m_sidebarBgColor = color;
    applyCurrentThemeStyle();
    saveTheme();
    emit sidebarBgColorChanged(color);
}

QString ThemeManager::currentSidebarBgColor() const
{
    return m_sidebarBgColor;
}

void ThemeManager::onSidebarBgColorChanged(const QString& color)
{
    setSidebarBgColor(color);
}

void ThemeManager::applySidebarBgOverride(QString& style) const
{
    if (!m_sidebarBgColor.isEmpty()) {
        // 自定义侧边栏色以半透明玻璃方式呈现，让自定义背景（纯色/图片/必应壁纸）
        // 能透出侧边栏，避免不透明色块把壁纸完全挡住、看不出透明效果。
        const QColor c(m_sidebarBgColor);
        if (c.isValid()) {
            style += QString("\nSideBar {\n    background-color: rgba(%1, %2, %3, 0.65);\n}\n")
                         .arg(c.red()).arg(c.green()).arg(c.blue());
        } else {
            style += QString("\nSideBar {\n    background-color: %1;\n}\n").arg(m_sidebarBgColor);
        }
    }
}

void ThemeManager::applyDarkBarOverride(QString& style) const
{
    // 仅在深色 / 自定义主题下追加。
    // QSS 中 TopBar/SideBar/SubNavPanel 硬编码了 rgba(255,255,255,…) 玻璃态渐变，
    // 浅色主题下视觉效果良好；但深色主题下这是浅白底色，会让滚动条 handle
    // （深色主题下被 @BLACK_RGBA_*@ 翻转为 rgba(255,255,255,0.18)）几乎不可见。
    if (m_currentTheme == LightTheme)
        return;

    // 颜色与 ThemeManager::buildTokenTable 中深色主题的 BAR_BG 系列保持近似，
    // 顶栏稍亮（区分于侧栏）、子导航介于两者之间，保留半透明玻璃质感。
    static const char* kDarkBarOverride = R"(
/* === dark bar override (auto) === */
TopBar {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(48, 48, 48, 0.62),
        stop:1 rgba(40, 40, 40, 0.68));
    border-bottom: 1px solid #444444;
}
SideBar {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(42, 42, 42, 0.62),
        stop:1 rgba(36, 36, 36, 0.68));
    border-right: 1px solid #444444;
}
SubNavPanel {
    background-color: rgba(45, 45, 45, 0.7);
    border: 1px solid #444444;
    border-radius: 12px;
}
QWidget#aiChatTopBar {
    background: rgba(42, 42, 42, 0.72);
    border-bottom: 1px solid #444444;
}
QFrame#aiChatLeftPanel {
    background-color: rgba(42, 42, 42, 0.7);
    border: 1px solid #444444;
}
)";
    style += "\n" + QString(kDarkBarOverride);
}

void ThemeManager::setTextBorderColor(const QString& color)
{
    m_textBorderColor = color;
    m_textBorderColorCustomized = true;
    applyCurrentThemeStyle();
    saveTheme();
    emit textBorderColorChanged(color);
}

QString ThemeManager::currentTextBorderColor() const
{
    return m_textBorderColor;
}

void ThemeManager::applyTextBorderColor()
{
    applyCurrentThemeStyle();
}

void ThemeManager::onTextBorderColorChanged(const QString& color)
{
    setTextBorderColor(color);
}

void ThemeManager::setBorderColor(const QString& color)
{
    m_borderColor = color;
    m_borderColorCustomized = true;
    applyCurrentThemeStyle();
    saveTheme();
    emit borderColorChanged(color);
}

QString ThemeManager::currentBorderColor() const
{
    return m_borderColor;
}

void ThemeManager::applyBorderColor()
{
    applyCurrentThemeStyle();
}

void ThemeManager::onBorderColorChanged(const QString& color)
{
    setBorderColor(color);
}
