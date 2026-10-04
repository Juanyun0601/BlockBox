/**
 * @file   ModelProviderSettingsPanel.h
 * @brief  模型服务商设置面板（双栏布局）
 * @author BlockBox Team
 * @date   2026-09-18
 */
#ifndef MODELPROVIDERSETTINGSPANEL_H
#define MODELPROVIDERSETTINGSPANEL_H

#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStackedWidget>
#include <functional>

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

/**
 * @brief 模型服务商设置面板
 *
 * 左栏为服务商分组列表（内置服务商 / 自定义供应商，行尾状态点标记是否已配置），
 * 底部为“添加供应商”入口；右栏为当前选中项的内联页面：
 *  - 内置服务商：API Key / API 地址 + 预设模型列表（点击设为默认模型）
 *  - 自定义供应商：名称 / 端点 / Key / API 格式 + 模型增删
 *  - 添加供应商：名称、端点、Key、API 格式与初始模型表单（至少一个模型才能创建）
 *
 * 存储保持与旧消费方（AiChatPage / ModelSelectDialog）兼容：
 *  - 内置服务商配置键：ai/provider_<key>/{url,key,selectedModel,selectedModelName}
 *  - 自定义模型扁平表：ai/customModels（自定义供应商的模型带 providerId 标记展开写入）
 *  - 自定义供应商：ai/customProviders（本面板新增，含 models 子数组与 apiFormat）
 */
class ModelProviderSettingsPanel : public QFrame
{
    Q_OBJECT

public:
    explicit ModelProviderSettingsPanel(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    struct PresetInfo
    {
        QString key;         // 短名（DeepSeek / GLM ...）
        QString name;        // 显示名
        QString defaultUrl;  // 默认 API 端点
        QString configKey;   // 设置键前缀
    };
    QList<PresetInfo> presetProviders();
    static QString presetUrlHint(const QString &key);

    // ── 数据访问 ──
    QJsonArray loadCustomProviders() const;
    void saveCustomProviders(const QJsonArray &providers);
    QJsonArray loadFlatCustomModels() const;
    void saveFlatCustomModels(const QJsonArray &models);
    /// 用自定义供应商记录重建 ai/customModels 中带 providerId 标记的条目
    void syncFlattenedCustomModels();
    /// 追加一条扁平自定义模型（无 providerId，用于内置服务商的自定义模型）
    void appendFlatCustomModel(const QJsonObject &entry);

    // ── 左栏列表 ──
    void rebuildProviderList();
    QLabel *createGroupLabel(const QString &text) const;
    QPushButton *createProviderRow(const QString &name, bool active, const QString &rowId);
    QLabel *createStatusDot(bool active) const;
    void selectProvider(const QString &rowId, bool force = false);

    // ── 右栏页面 ──
    QWidget *wrapScroll(QWidget *content) const;
    QWidget *buildPresetPage(const PresetInfo &info);
    QWidget *buildCustomProviderPage(const QJsonObject &prov);
    QWidget *buildAddProviderPage();
    void refreshAddModelsCard();

    // ── 通用小件 ──
    QHBoxLayout *createFormRow(const QString &labelText, QWidget *field, QWidget *trailing = nullptr) const;
    QWidget *createModelListCard(QVBoxLayout *&rowsLayout) const;
    QWidget *createModelRow(const QString &name, const QString &subtitle, bool checked,
                            const std::function<void()> &onClick,
                            const std::function<void()> &onRemove = {}) const;

private:
    // 左栏
    QWidget *m_sideList = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
    QButtonGroup *m_rowGroup = nullptr;
    QPushButton *m_addProviderBtn = nullptr;

    // 右栏
    QStackedWidget *m_stack = nullptr;
    QWidget *m_page = nullptr;          // 当前服务商页（每次选中重建）
    QWidget *m_addPage = nullptr;       // 添加供应商页（常驻）
    QString m_currentId;                // "preset:<key>" / "custom:<uuid>" / "__add__"

    // 添加供应商页（常驻，保存成员以便表单状态跨刷新保留）
    QLineEdit *m_addNameEdit = nullptr;
    QLineEdit *m_addUrlEdit = nullptr;
    QLineEdit *m_addKeyEdit = nullptr;
    QComboBox *m_addFormatCombo = nullptr;
    QVBoxLayout *m_addModelsLayout = nullptr;
    QPushButton *m_addCreateBtn = nullptr;
    QJsonArray m_stagedModels;
};

#endif // MODELPROVIDERSETTINGSPANEL_H
