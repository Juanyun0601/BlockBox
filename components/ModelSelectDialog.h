/**
 * @file   ModelSelectDialog.h
 * @brief  AI 模型选择与配置对话框
 * @author BlockBox Team
 * @date   2026-06-23
 */

#ifndef MODELSELECTDIALOG_H
#define MODELSELECTDIALOG_H

#include "components/AppDialogBase.h"

#include <QCheckBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/AiService.h"

class LocalModelDialog;

class ModelSelectDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit ModelSelectDialog(QWidget* parent = nullptr);

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
     *
     * 上层 AiChatPage 可据此刷新 m_models 中本地模型条目（清空旧的 isLocal=true 项后
     * 重新追加 LocalModelDialog::localAiModels() 的最新结果）。
     */
    void localModelsChanged();
    void openSettingsRequested();

private slots:
    void onProviderToggled(const QString& providerKey);
    void onModelClicked(const AiModel& model);
    void onCustomJsonClicked();
    void onSaveConfig();
    void onImportJsonFile();
    void onExportJsonFile();
    void onLocalModelClicked();

private:
    void initUI();
    void rebuildConfiguredSection();
    void rebuildProviderSections();
    QString buildSignature() const;
    void markRebuilt();
    void showConfigForm(const AiModel& model);
    void hideConfigForm();
    QString providerKey(const AiModel& model) const;
    void openProviderWebsite(const QString& providerKey);

    // 数据
    QList<AiModel> m_allModels;
    QString m_currentModelId;
    QString m_lastSignature;    ///< 上次完整重建时的渲染签名（用于跳过无变化重建）
    QTimer* m_searchTimer;      ///< 搜索输入防抖定时器

    // UI 组件
    QScrollArea* m_scrollArea;
    QWidget* m_contentWidget;
    QVBoxLayout* m_contentLayout;

    // 已配置模型区域
    QWidget* m_configuredSection;
    QVBoxLayout* m_configuredLayout;

    // 服务商区域
    QWidget* m_providerSection;
    QVBoxLayout* m_providerLayout;
    QMap<QString, QWidget*> m_providerContents;
    QMap<QString, QPushButton*> m_providerHeaders;

    // 配置表单
    QWidget* m_configForm;
    QLineEdit* m_configNameEdit;
    QLineEdit* m_configIdEdit;
    QLineEdit* m_configUrlEdit;
    QLineEdit* m_configKeyEdit;
    QCheckBox* m_configThinkingCheck;
    AiModel m_configModel;

    // 自定义 JSON
    QWidget* m_jsonSection;
    QTextEdit* m_jsonEdit;
    bool m_jsonVisible;

    // 搜索
    QLineEdit* m_searchEdit;

    // 网络
    QNetworkAccessManager* m_networkManager;

    // 本地模型管理
    QPushButton* m_localModelBtn;
    LocalModelDialog* m_localModelDialog;
};

#endif // MODELSELECTDIALOG_H