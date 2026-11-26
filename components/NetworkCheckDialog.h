/**
 * @file   NetworkCheckDialog.h
 * @brief  网络连通性自检对话框：逐项检测启动器依赖的远程服务
 * @author BlockBox Team
 * @date   2026-11-22
 */
#ifndef NETWORKCHECKDIALOG_H
#define NETWORKCHECKDIALOG_H

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>

#include "AppDialogBase.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QNetworkReply;

/**
 * @brief 检测启动器依赖的各远程服务（Mojang 官方 / BMCLAPI / MCIM /
 *        Modrinth / CurseForge CDN / 微软登录）的连通性与延迟
 *
 * 所有请求并发发出，任一服务收到 HTTP 回复（含 4xx/5xx）即视为"通"，
 * 只有 DNS / 连接 / 超时等网络层错误才判"不通"。
 */
class NetworkCheckDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit NetworkCheckDialog(QWidget *parent = nullptr);

    /// 便捷入口：创建对话框并立即开始检测，模态执行
    static void runCheck(QWidget *parent);

private slots:
    void startCheck();
    void onReplyFinished();

private:
    struct Endpoint
    {
        QString name;     ///< 展示名，如「Mojang 官方源」
        QString url;      ///< 被检测的完整地址
        QString hint;     ///< 检测失败时给用户的一条建议
    };
    struct CheckState
    {
        bool finished = false;
        bool ok = false;
        qint64 latencyMs = 0;
        QString errorText;  ///< 失败原因（中文）
    };

    void initUI();
    void initStyle();
    void buildRows();
    QWidget *createCheckCard(const Endpoint &endpoint, int index);
    void setCardState(int index, const CheckState &state);
    void updateSummary();
    QString chineseError(int errorCode) const;

    QLabel *m_metaLabel;
    QLabel *m_okChip;
    QLabel *m_failChip;
    QLabel *m_runningChip;
    QLabel *m_verdictLabel;
    QScrollArea *m_scrollArea;
    QWidget *m_rowsContainer;
    QVBoxLayout *m_rowsLayout;
    QPushButton *m_recheckBtn;

    QList<Endpoint> m_endpoints;
    QList<CheckState> m_states;
    QHash<QNetworkReply *, int> m_replyIndex;  ///< 进行中的回复 → 检测项下标
    QList<QElapsedTimer> m_timers;
    int m_pendingCount = 0;
};

#endif // NETWORKCHECKDIALOG_H
