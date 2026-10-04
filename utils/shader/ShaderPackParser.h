/**
 * @file   ShaderPackParser.h
 * @brief  光影包选项解析器：按 Iris / OptiFine 规则解析光影包内 shaders.properties
 *         与着色器源文件，生成可配置选项列表，并兼容写入 Iris
 *         (config/iris.properties + shaderpacks/<pack>.txt) 与 OptiFine
 *         (optionsshaders.txt) 配置文件。
 * @author BlockBox Team
 * @date   2026-08-01
 */

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 光影包选项解析器
 *
 * 解析规则对齐 Iris 源码 (OptionAnnotatedSource / StringOption / ShaderProperties)：
 *  - 布尔选项：#define NAME 或 //#define NAME，需被 #ifdef/#ifndef 引用才确认
 *  - 字符串选项：#define NAME value // 描述 [v1 v2 v3]
 *  - const 选项：const int/float/bool NAME = value; // 描述 [v1 v2]（需在白名单内）
 *  - screen / sliders 指令来自未预处理的 shaders.properties（ISO-8859-1）
 *  - lang 文件（UTF-8）提供显示名 option.<name> / 说明 option.<name>.comment
 */
class ShaderPackParser : public QObject
{
    Q_OBJECT

public:
    /** @brief 单个光影包选项 */
    struct Option
    {
        QString name;           ///< 选项键名（写入配置文件使用）
        QString displayName;    ///< 显示名（lang 文件覆盖，否则用原名）
        QString description;    ///< 提示说明
        QString type;           ///< bool / string（Iris 只区分这两类）
        QString defaultValue;   ///< 默认值
        QString currentValue;   ///< 当前值（来自配置文件或默认值）
        QStringList values;     ///< 允许值列表
        bool isSlider = false;  ///< 是否应以滑块显示（shaders.properties 中 sliders 指定）
        int order = 0;          ///< 显示顺序（来自 screen 指令）
        QString screen;         ///< 所属分类（screen.<key> 的 key，空串表示未归属任何分类）
        QMap<QString, QString> valueLabels; ///< value.<name>.<value> 值显示名
        QString valuePrefix;    ///< prefix.<name>
        QString valueSuffix;    ///< suffix.<name>
    };

    /** @brief 一个配置分类（对应 shaders.properties 中 screen.<key> 指令） */
    struct ScreenCategory
    {
        QString key;            ///< 分类键（如 world / lighting，主屏为空串）
        QString displayName;    ///< 显示名（lang 中 screen.<key> 翻译，否则用 key）
        QStringList options;    ///< 该分类直接包含的选项名（按声明顺序）
        QStringList subScreens; ///< 以 [xxx] 链接的子分类键（按声明顺序）
    };

    explicit ShaderPackParser(QObject *parent = nullptr);

    /** @brief 解析光影包，返回是否成功 */
    bool parsePack(const QString &packPath);

    /** @brief 加载当前配置值，返回是否成功 */
    bool loadCurrentValues(const QString &gameDir);

    /** @brief 保存配置，返回是否成功 */
    bool saveConfig(const QString &gameDir);

    /** @brief 是否使用 Iris 配置（存在 config/iris.properties） */
    bool isIrisMode() const;

    /** @brief 配置目标描述（Iris / OptiFine） */
    QString configTargetName() const;

    /** @brief 光影包文件名（含扩展名） */
    QString packFileName() const;

    /** @brief 光影包显示名（去掉扩展名） */
    QString packDisplayName() const;

    /** @brief 选项列表（按 screen 顺序排序，未归属分类的排最末） */
    const QList<Option> &options() const;

    /** @brief 分类列表（主屏排最前，其余按声明顺序） */
    const QList<ScreenCategory> &screens() const;

    /** @brief 主屏分类（screen 指令，key 为空串）；无 screen 指令时返回空 */
    const ScreenCategory &mainScreen() const;

    /** @brief 按名称查找选项 */
    Option *findOption(const QString &name);

    /** @brief 设置某个选项的值 */
    void setValue(const QString &name, const QString &value);

    /** @brief 恢复全部选项为默认值 */
    void resetToDefaults();

    /** @brief 可用语言代码列表（lang 文件名的前半部分，如 en_US / zh_CN） */
    QStringList languages() const;

    /** @brief 当前语言代码（未设置时为光影包默认语言） */
    QString language() const;

    /** @brief 切换显示语言，成功返回 true */
    bool setLanguage(const QString &code);

    /** @brief 根据软件界面语言代码自动选择光影包语言并应用（对齐 Iris 语言回退：精确匹配，否则回退 en_us）
     *  @param appLangCode 软件语言代码（en / zh / zh_Hant / es），传入空串表示保持默认 */
    void autoSetLanguage(const QString &appLangCode);

signals:
    /** @brief 解析进度（0-100），phase 为当前阶段描述 */
    void progressChanged(int percent, const QString &phase);

private:
    /** @brief 定位 zip 内 shaders 目录前缀（根部 shaders/ 优先，否则取以 "shaders" 结尾的目录） */
    QString locateShadersDir() const;
    /** @brief 收集 shaders 目录下全部 GLSL 源文件（含递归 #include），返回相对 shaders 的路径列表 */
    QStringList collectSourceFiles();
    /** @brief 从一条 #include 指令解析目标路径（返回相对 shaders 的路径，空表示无效） */
    QString resolveInclude(const QString &includeLine, const QString &currentDir) const;
    void parseShadersProperties(const QString &content);
    void parseLangFile(const QString &langCode, const QString &langContent);
    void parseShaderSource(const QString &source);
    void sortOptions();
    /** @brief 应用当前语言（m_activeLang）的翻译到选项显示名 / 说明 / 值标签 / 分类名 */
    void applyLanguage();
    /** @brief 解析 #define 选项（布尔 / 字符串） */
    void parseDefineOption(const QString &name, const QString &value,
                           const QString &comment, bool hasLeadingComment);
    /** @brief 解析 const 常量选项 */
    void parseConstOption(const QString &type, const QString &name,
                          const QString &value, const QString &comment);
    /** @brief const 选项名称白名单（与 Iris VALID_CONST_OPTION_NAMES 一致） */
    static bool isConstOptionName(const QString &name);
    /** @brief 从注释中提取 [v1 v2 ...] 值列表与描述文本 */
    static bool extractAllowedValues(const QString &comment, QString &desc,
                                     QStringList &values);
    /** @brief 解析 screen 指令，构建分类结构并分配选项归属 */
    void buildScreens();
    /** @brief 按 key 获取分类显示名（lang 中 screen.<key>），否则原样返回 key */
    QString screenDisplayName(const QString &key) const;

    QString m_packPath;
    QString m_packFileName;
    QString m_shadersDir;           ///< zip 内 shaders 目录前缀（如 "shaders/" 或 "MyPack/shaders/"）
    QList<Option> m_options;
    QMap<QString, int> m_optionIndex;   ///< 名称 -> 下标
    QStringList m_sliderList;           ///< sliders 指令中的选项
    QList<ScreenCategory> m_screens;    ///< 分类列表（主屏排最前）
    QMap<QString, int> m_screenIndex;   ///< 分类 key -> 下标
    QMap<QString, int> m_screenOrderMap;///< 选项名 -> 主屏顺序
    QMap<QString, QString> m_screenTranslations; ///< screen.<key> -> 显示名
    QMap<QString, QMap<QString, QString>> m_langMaps; ///< 语言代码 -> 完整翻译键值表
    QString m_activeLang;           ///< 当前激活语言代码（空表示未设置）
    QStringList m_ifdefRefs;            ///< #ifdef/#ifndef 中引用的名称
    bool m_hasIrisConfig = false;
};
