/**
 * @file   LocalModSettingsPage.h
 * @brief  本地模组设置页面:模组信息 + config 配置文件表单化编辑
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 嵌入实例管理页(InstanceManagePage)内容栈中,由模组列表卡片的
 * 「模组设置」按钮进入。页面自动在实例 config/ 目录匹配当前模组的
 * 配置文件(TOML / 旧版 Forge cfg / properties / JSON),将设置项渲染
 * 为表单(开关/下拉/数字/文本),保存时经 ModConfigParser 行级回写,
 * 保留注释与未编辑内容。
 *
 * 本页不含自己的返回按钮与页面标题,返回与当前页名称复用主界面
 * TopBar 的返回按钮与标题(由 MainWindow 在进入本页时设置)。
 */

#pragma once

#include <QWidget>
#include <QString>
#include <QList>

#include "../utils/mod/ModData.h"
#include "../utils/mod/ModConfigParser.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QDoubleSpinBox;
class QBoxLayout;
class QHBoxLayout;
class QVBoxLayout;
class CustomCheckBox;

/**
 * @brief 本地模组设置页面
 *
 * 顶部为模组信息与快捷操作(启用/禁用、打开文件夹);
 * 中部为配置文件选择与分节表单;底部为「恢复默认值」「保存设置」。
 */
class LocalModSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit LocalModSettingsPage(QWidget *parent = nullptr);
    ~LocalModSettingsPage() override;

    /**
     * @brief 指定要查看/编辑的模组并加载其配置文件
     * @param info         模组信息(需含 filePath/id/fileName)
     * @param instancePath 实例根路径(用于定位 config 目录)
     */
    void setCurrentMod(const ModInfo &info, const QString &instancePath);

    /** 重置为未加载状态(实例切换时调用,避免残留上个模组的信息) */
    void reset();

signals:
    /** 模组状态发生变化(启用/禁用),需要刷新模组列表 */
    void modSettingsChanged();

private slots:
    void onConfigFileChanged(int index);
    void onToggleEnabledClicked();
    void onOpenModFolderClicked();
    void onOpenConfigFolderClicked();
    void onSaveClicked();
    void onResetDefaultsClicked();

private:
    /** 表单控件类型 */
    enum class OptionKind
    {
        Unsupported,
        Bool,
        Enum,
        Int,
        Double,
        String,
        StringList
    };

    /** 控件与设置项的绑定记录(以 section/option 索引定位) */
    struct Binding
    {
        int sectionIndex = -1;
        int optionIndex = -1;
        OptionKind kind = OptionKind::Unsupported;
        CustomCheckBox *switchBox = nullptr;
        QComboBox *comboBox = nullptr;
        QSpinBox *spinBox = nullptr;
        QDoubleSpinBox *doubleSpinBox = nullptr;
        QLineEdit *lineEdit = nullptr; ///< String 与 StringList 共用
    };

    void initUI();
    void clearForm();
    void reloadConfigFiles();
    void parseCurrentFile();
    void buildForm();
    /** 渲染一个分节卡片;根节(空路径)标题为「常规设置」 */
    void renderSection(int sectionIndex, QVBoxLayout *parentLayout);
    /** 构建单个设置项行(键名 + 控件) */
    void addOptionRow(QVBoxLayout *parentLayout, int sectionIndex, int optionIndex);
    /** 将表单控件的当前值写回 Document(有改动返回 true) */
    bool collectConfigEdits();
    /** 有未保存修改时弹窗询问;返回 true 表示可继续(丢弃修改) */
    bool confirmDiscard();
    void refreshHeader();
    void refreshToggleState();
    /** 从 JAR 内提取模组 logo(带 QPixmapCache),失败返回空 */
    QPixmap loadModIcon(int size) const;
    QString configDir() const;
    QString modDisplayName() const;
    QString formatFileSize(qint64 bytes) const;
    QString optionTooltip(int sectionIndex, int optionIndex) const;

    // 设置卡片与设置行辅助（与设置页样式对齐）
    QVBoxLayout *createSettingsCard(QBoxLayout *parentLayout,
                                    const QString &title = QString(),
                                    bool startCollapsed = false);
    QHBoxLayout *appendSettingRow(QVBoxLayout *cardLayout,
                                  const QString &title,
                                  const QString &desc = QString(),
                                  bool isLast = false);

    ModInfo m_modInfo;
    QString m_instancePath;
    QString m_configDir;
    QStringList m_configFiles;  ///< 匹配到的配置文件绝对路径
    int m_currentFileIndex = -1; ///< 当前已加载的配置文件索引(切换取消时回退)
    ModConfigParser::Document m_doc;
    bool m_loaded = false;      ///< 是否已成功打开一个配置文件
    bool m_dirty = false;       ///< 表单有未保存修改
    bool m_updating = false;    ///< 回填控件时防触发 dirty

    // 顶部
    QLabel *m_iconLabel;         ///< 模组 logo(提取失败时显示首字母占位)
    QLabel *m_nameLabel;
    QLabel *m_metaLabel;
    QPushButton *m_toggleBtn;
    QPushButton *m_openModFolderBtn;
    QPushButton *m_openConfigFolderBtn;

    // 配置文件选择
    QLabel *m_fileLabel;
    QComboBox *m_fileCombo;

    // 表单区
    QWidget *m_formContainer;
    QVBoxLayout *m_formLayout;
    QLabel *m_emptyLabel;

    // 底部
    QPushButton *m_resetBtn;
    QPushButton *m_saveBtn;

    QList<Binding> m_bindings;
};
