/**
 * @file   ModelSelectDialog.h
 * @brief  AI 模型选择对话框（先选服务商，再从服务商模型列表中选择）
 * @author BlockBox Team
 * @date   2026-06-23
 */

#ifndef MODELSELECTDIALOG_H
#define MODELSELECTDIALOG_H

#include "components/AppDialogBase.h"

#include <QHash>
#include <QJsonObject>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/AiModelListFetcher.h"
#include "utils/AiService.h"

class LocalModelDialog;
class QLabel;
class QScrollArea;

/**
 * @brief 模型选择对话框
 *
 * 交互流程：先在左栏选择服务商（内置服务商 / 自定义供应商），
 * 右栏再展示该服务商的模型列表。模型列表优先读取本地缓存；
 * 没有缓存时自动向服务商请求，点「刷新列表」可强制重新获取。
 * 选中模型后立即生效并关闭窗口。
 */
class ModelSelectDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit ModelSelectDialog(QWidget* parent = nullptr);
    ~ModelSelectDialog() override;

    void setModels(const QList<AiModel>& models);
    void setCurrentModel(const QString& modelId);
    AiModel selectedModel() const;

    /**
     * @brief 返回对话框当前维护的完整模型列表（含本地模型条目）
     *
     * 供上层在收到 localModelsChanged 信号后同步刷新自己的 m_models。
     */
    QList<AiModel> allModels() const { return m_allModels; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

signals:
    void modelSelected(const AiModel& model);
    void modelConfigured(const AiModel& model);
    void modelRemoved(const QString& modelId);
    /**
     * @brief 本地模型清单发生变化（拉取/删除完成）
     */
    void localModelsChanged();
    void openSettingsRequested();

private:
    /// 一个可选的模型来源（内置服务商或自定义供应商）
    struct ProviderEntry
    {
        QString rowId;             ///< "preset:<key>" / "custom:<uuid>"
        QString name;              ///< 展示名
        QString apiUrl;            ///< 有效 API 端点
        QString apiKey;            ///< 已保存的密钥（可为空）
        QString selectedModel;     ///< 该服务商已选的默认模型
        QString selectedModelName; ///< 该默认模型的展示名
        QString configKey;         ///< 内置服务商设置键前缀
        QString urlHint;           ///< 预设模型匹配特征
        QString providerId;        ///< 自定义供应商 id
        bool isCustom = false;     ///< 是否为自定义供应商
    };

    void initUI();
    void rebuildAll();
    void rebuildProviderList();
    void rebuildModelList();
    void selectProvider(const QString& rowId, bool force = false);
    void fetchModels(bool force);
    void onFetchFinished(const AiModelListResult& result);
    void applyIds(const QStringList& ids);
    void chooseModel(const QString& id);
    void addManualModel();
    void updateStatusLabel(const QString& text, bool error = false);
    void onLocalModelClicked();

    QList<ProviderEntry> providerEntries() const;
    ProviderEntry providerForRow(const QString& rowId) const;
    ProviderEntry currentProvider() const;

    AiModel makeModel(const ProviderEntry& provider, const QString& id) const;
    QString displayNameFor(const ProviderEntry& provider, const QString& id) const;
    QStringList fallbackIds(const ProviderEntry& provider) const;
    void saveProviderDefault(const ProviderEntry& provider, const QString& id);
    QString providerOfModel(const AiModel& model) const;
    static QString normalizedUrl(const QString& url);
    static bool isLocalEndpoint(const QString& url);

    // 数据
    QList<AiModel> m_allModels;
    QString m_currentModelId;
    QString m_selectedRowId;              ///< 当前选中的服务商行
    QString m_loadedRowId;                ///< 当前右栏内容对应的服务商行
    QString m_fetchingRowId;              ///< 正在拉取模型列表的服务商行
    QStringList m_currentIds;             ///< 当前右栏展示的模型 ID
    QHash<QString, QString> m_knownNames; ///< 模型 ID → 预置展示名（来自内置清单/已配置模型）
    QSet<QString> m_autoFetched;          ///< 已自动发起过拉取的服务商（避免反复请求）
    QTimer* m_searchTimer;                ///< 搜索输入防抖定时器
    AiModelListFetcher* m_fetcher;

    // UI：左栏服务商
    QScrollArea* m_providerScroll;
    QWidget* m_providerHost;
    QVBoxLayout* m_providerLayout;
    QLineEdit* m_searchEdit;

    // UI：右栏模型
    QLabel* m_providerTitle;
    QLabel* m_statusLabel;
    QPushButton* m_refreshBtn;
    QScrollArea* m_modelScroll;
    QWidget* m_modelHost;
    QVBoxLayout* m_modelLayout;
    QLineEdit* m_manualEdit;
    QPushButton* m_manualBtn;

    // 本地模型管理
    QPushButton* m_localModelBtn;
    LocalModelDialog* m_localModelDialog;
};

#endif // MODELSELECTDIALOG_H
