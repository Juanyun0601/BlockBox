/**
 * @file   ShaderPackSettingsPage.h
 * @brief  光影包设置页面：可视化编辑光影包选项并写入配置文件
 * @author BlockBox Team
 * @date   2026-08-02
 *
 * 解析指定光影包 zip 内的选项（shaders.properties / 着色器源文件），
 * 以表单形式展示并可编辑，支持 OptiFine (optionsshaders.txt) 与
 * Iris (config/iris.properties + shaderpacks/<pack>.txt) 两种写入方式。
 *
 * 本页嵌入实例管理页（InstanceManagePage）的内容栈中，通过
 * setCurrentPack() 指定要编辑的光影包。修改通过 ShaderPackParser 写入配置文件。
 *
 * 本页不含自己的返回按钮与页面标题，返回与当前页名称复用主界面
 * TopBar 的返回按钮与标题（由 MainWindow 在进入本页时设置）。
 */

#pragma once

#include <QList>
#include <QSet>
#include <QWidget>

#include "components/OutlinedLabel.h"
#include "utils/shader/ShaderPackParser.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QComboBox;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLineEdit;
class QTabWidget;
class QGroupBox;
class BlurLoadingOverlay;

/**
 * @brief 光影包设置页面
 *  - bool    -> 滑块开关（CustomCheckBox）
 *  - enum    -> QComboBox
 *  - int     -> QSpinBox
 *  - float   -> QDoubleSpinBox（带范围时用滑块）
 *  - string  -> QComboBox / 编辑框
 * 提供「保存」「恢复默认值」操作，保存时通过 ShaderPackParser 写入配置文件。
 *
 * 界面按分类分页（对齐 Iris OptionMenuContainer）：每个顶层分类一个
 * QTabWidget 页，页内直属选项直接展示，嵌套子分类（[xxx] 链接）用
 * QGroupBox 分组递归渲染。
 */
class ShaderPackSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ShaderPackSettingsPage(QWidget *parent = nullptr);
    ~ShaderPackSettingsPage() override;

    /**
     * @brief 指定要编辑的光影包并重新解析加载
     * @param packPath 光影包 zip 的完整路径
     * @param gameDir  游戏根目录（用于读写配置文件）
     */
    void setCurrentPack(const QString &packPath, const QString &gameDir);

    /** @brief 重置为未加载状态（实例切换时调用，避免残留上个光影包的选项） */
    void reset();

signals:
    /** 设置已成功写入配置文件 */
    void settingsSaved();

private:
    void initUI();
    void buildTabs();
    void clearForm();
    /** @brief 递归渲染一个分类：直属选项行 + 嵌套子分类分组（visited 防循环引用） */
    void renderCategory(const ShaderPackParser::ScreenCategory &cat, QVBoxLayout *parentLayout,
                        QSet<QString> visited, bool isLastInParent);
    /** @brief 渲染单个选项的行为控件并绑定（isLast 指示是否为本卡片最后一行） */
    void renderOptionRow(const ShaderPackParser::Option &opt, QVBoxLayout *parentLayout,
                         bool isLast);
    /** @brief 按 key 查找分类（返回分类指针，未找到返回空） */
    const ShaderPackParser::ScreenCategory *findScreen(const QString &key) const;
    void updateStatsLabel();
    void onValueChanged();
    void onSaveClicked();
    void onResetClicked();

    /** @brief 控件与选项名的绑定记录 */
    struct ControlBinding
    {
        QString optionName;
        class CustomCheckBox *switchBox = nullptr; ///< 布尔选项滑块开关
        QComboBox *comboBox = nullptr;
        QSpinBox *spinBox = nullptr;
        QDoubleSpinBox *doubleSpinBox = nullptr;
        QSlider *slider = nullptr;
        QLineEdit *lineEdit = nullptr;
        QLabel *sliderValueLabel = nullptr;
    };

    QString m_packPath;
    QString m_gameDir;
    ShaderPackParser *m_parser;
    bool m_loaded = false; ///< 当前是否已成功解析一个光影包
    int m_parseGeneration = 0; ///< 解析代次，作废过期的延迟解析回调

    // UI
    QLabel *m_headerLabel;
    QLabel *m_statsLabel;
    QTabWidget *m_tabWidget;
    QLabel *m_emptyLabel;
    QPushButton *m_resetBtn;
    QPushButton *m_saveBtn;
    BlurLoadingOverlay *m_loadingOverlay; ///< 解析进度遮罩（复用现有加载进度条组件）

    QList<ControlBinding> m_bindings;
    bool m_updating = false; ///< 防止回写时触发 onValueChanged
};
