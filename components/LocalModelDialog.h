/**
 * @file   LocalModelDialog.h
 * @brief  本地 AI 模型管理对话框 - Ollama 服务与模型的可视化管理
 * @author BlockBox Team
 * @date   2026-08-06
 *
 * 由 ModelSelectDialog 的"本地模型"入口弹出，提供：
 * - Ollama 服务状态显示与启动/停止
 * - 未安装时引导用户下载并运行 Ollama 安装器
 * - 已下载的本地模型列表，支持删除与"使用此模型"
 * - 可下载模型预设列表（Gemma 3 / Qwen3 系列），点击拉取
 * - 实时拉取进度
 */

#ifndef LOCALMODELDIALOG_H
#define LOCALMODELDIALOG_H

#include "components/AppDialogBase.h"

#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/AiService.h"
#include "utils/LocalModelManager.h"

class LocalModelDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit LocalModelDialog(QWidget* parent = nullptr);

    /**
     * @brief 当前已下载的本地模型列表（最近一次刷新结果）
     */
    QList<AiModel> localAiModels() const { return m_localAiModels; }

protected:
    void showEvent(QShowEvent* event) override;

signals:
    /**
     * @brief 用户点击"使用此模型"
     * @param model 转换好的 AiModel（isLocal=true，apiUrl 已填入 Ollama OpenAI 端点）
     */
    void localModelSelected(const AiModel& model);

    /**
     * @brief 本地模型清单发生变化（拉取完成、删除完成等）
     *
     * 上层（ModelSelectDialog / AiChatPage）可据此刷新 m_models 中本地模型条目。
     */
    void localModelsChanged();

private slots:
    void onRefreshClicked();
    void onStartStopServiceClicked();
    void onInstallClicked();
    void onRunInstallerClicked();
    void onPullPreset(const LocalModelPreset& preset);
    void onCancelPullClicked();
    void onUseModelClicked(const QString& tag);
    void onDeleteModelClicked(const QString& tag);

    // 来自 LocalModelManager 的信号
    void onServiceStatusChecked(LocalModelManager::ServiceStatus status);
    void onServiceStarted();
    void onServiceStopped(const QString& errorMessage);
    void onPullProgress(const QString& tag, int percent, const QString& speedText,
                        const QString& statusText);
    void onPullFinished(const QString& tag);
    void onPullFailed(const QString& tag, const QString& errorMessage);
    void onModelListReady(const QList<LocalModelInfo>& models);
    void onModelRemoved(const QString& tag, bool success, const QString& errorMessage);
    void onInstallerProgress(int percent, qint64 received, qint64 total);
    void onInstallerDownloaded(const QString& localPath);
    void onInstallerDownloadFailed(const QString& errorMessage);

private:
    void initUI();
    void refreshServiceStatusUI();
    void rebuildInstalledModelsSection();
    void rebuildPresetSection();
    void setPullInProgress(bool inProgress, const QString& tag = {});

    // 数据
    LocalModelManager* m_manager;
    QList<LocalModelInfo> m_installedModels;
    QList<AiModel> m_localAiModels;
    LocalModelManager::ServiceStatus m_currentStatus = LocalModelManager::ServiceStatus::Unknown;
    QString m_currentPullTag;

    // UI 组件
    QWidget* m_card;
    QVBoxLayout* m_mainLayout;

    // 服务状态区
    QWidget* m_statusSection;
    QLabel* m_statusIconLabel;
    QLabel* m_statusTextLabel;
    QLabel* m_statusDetailLabel;
    QPushButton* m_startStopBtn;
    QPushButton* m_installBtn;
    QPushButton* m_refreshBtn;
    QProgressBar* m_installerProgress;

    // 已下载模型区
    QWidget* m_installedSection;
    QVBoxLayout* m_installedLayout;
    QLabel* m_installedEmptyLabel;

    // 拉取进度区
    QWidget* m_pullSection;
    QLabel* m_pullTagLabel;
    QLabel* m_pullStatusLabel;
    QProgressBar* m_pullProgress;
    QPushButton* m_cancelPullBtn;

    // 预设模型区
    QWidget* m_presetSection;
    QVBoxLayout* m_presetLayout;
};

#endif // LOCALMODELDIALOG_H
