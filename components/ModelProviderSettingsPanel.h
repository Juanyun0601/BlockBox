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

#include "utils/AiModelListFetcher.h"

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
 *  - 内置服务商：API Key / API 地址 + 模型列表（从服务商在线获取，点击设为默认模型）
 *  - 自定义供应商：名称 / 端点 / Key / API 格式 + 模型列表（在线获取）
 *  - 添加供应商：名称、端点、Key、API 格式（模型留到使用时再选，无需预先添加）
 *
 * 模型列表优先读取本地缓存（utils/AiModelListFetcher），没有缓存或点「刷新」时
 * 才向服务商请求，获取成功后写回缓存。
 *
 * 存储保持与旧消费方（AiChatPage / ModelSelectDialog）兼容：
 *  - 内置服务商配置键：ai/provider_<key>/{url,key,selectedModel,selectedModelName}
 *  - 自定义模型扁平表：ai/customModels（自定义供应商的模型带 providerId 标记展开写入）
 *  - 自定义供应商：ai/customProviders（含 models 子数组与 apiFormat）
 *  - 模型列表缓存：ai/modelListCache
 */
class ModelProviderSettingsPanel : public QFrame
{
    Q_OBJECT

public:
    explicit ModelProviderSettingsPanel(QWidget *parent = nullptr);

signals:
    /// 服务商配置发生变化（设置页据此刷新默认模型下拉框）
    void providersChanged();

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

    // ── 模型列表（在线获取 + 缓存）──
    /// 某个服务商行的有效 API 端点（内置取已保存 url 或默认 url，自定义取记录里的 url）
    QString providerUrlForRow(const QString &rowId) const;
    /// 发起模型列表请求（结果写入缓存后重建当前页）
    void fetchModelList(const QString &rowId, const QString &apiUrl, const QString &apiKey);
    /// 在页面标题旁生成「刷新模型列表」按钮
    QPushButton *createFetchButton(const QString &rowId, const QString &apiUrl, const QString &apiKey);

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
    QPushButton *m_addCreateBtn = nullptr;

    // 模型列表在线获取
    AiModelListFetcher *m_fetcher = nullptr;
    QString m_fetchingRowId;
};

#endif // MODELPROVIDERSETTINGSPANEL_H
