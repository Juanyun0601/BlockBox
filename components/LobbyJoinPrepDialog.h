/**
 * @file   LobbyJoinPrepDialog.h
 * @brief  「一键加入联机房间」的准备对话框：挑实例 + 勾选要补装的内容 + 下载进度
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 大厅里点「一键加入」后，主窗口先用匹配器算出计划：
 *   - 本机哪个实例最合适（可能有好几个备选）；
 *   - 缺哪些必装模组、辅助模组、资源包、光影包；
 *   - 完全没实例时提示要装哪个版本。
 * 有缺东西就把这份计划交给本对话框：
 *   1. 顶部下拉框可以在备选实例之间切换（显示版本 / 加载器 / 缺几项）；
 *   2. 下面按「必装模组 / 辅助模组 / 资源包 / 光影包」分组列出来，逐项勾选，
 *      默认勾上必装模组和资源包，辅助模组与光影包默认不勾（体积大、可不装）；
 *   3. 点「下载并加入」后对话框原地变成进度页，边下边报进度，全部完成后 accept()，
 *      失败项再列一遍，让用户决定「仍然加入」还是取消。
 * 什么都不缺时主窗口根本不会打开它，一键加入还是一步到位。
 */

#pragma once

#include "utils/Lobby/LobbyContentFetcher.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class QStackedWidget;

class LobbyJoinPrepDialog : public QDialog
{
    Q_OBJECT

public:
    /** 「去安装实例」用的自定义结果码（exec() 返回值） */
    static constexpr int kResultInstall = 2;

    explicit LobbyJoinPrepDialog(QWidget *parent = nullptr);
    ~LobbyJoinPrepDialog() override;

    /**
     * @brief 填入房间要求与匹配计划
     * @param plan      匹配器给出的计划（含备选实例与各类缺失内容）
     * @param roomTitle 房间名（只用于标题，可空）
     * @param joinTarget 邀请码 / 地址（只用于副标题，可空）
     * @param description 房主写的简要介绍（可空）
     */
    void setPlan(const LobbyInstanceMatcher::JoinPlan &plan, const QString &roomTitle,
                 const QString &joinTarget, const QString &description = QString());

    /** 用户最终选中的实例路径（没有可用实例时为空） */
    QString selectedInstancePath() const;

    /** 用户勾选、要下载的内容（点「下载并加入」时才有意义） */
    QList<LobbyFetchRequest> selectedRequests() const;

    /** 下载结果（失败项用于给用户提示） */
    QList<LobbyFetchResult> fetchResults() const;

    /** 用户是不是点了「去安装实例」 */
    bool installRequested() const { return m_installRequested; }

    /** 失败项数量（accept 之后主窗口用来说一句「还有 N 个没装上」） */
    int failedCount() const { return m_failedCount; }

protected:
    /** 下载中途关窗：先把下载停掉，避免装到一半 */
    void closeEvent(QCloseEvent *event) override;
    /** 首次显示时把高度压在屏幕范围内（缺十几项时内容会很长） */
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void applyStyles();
    void rebuildInstanceCombo();
    void rebuildContentGroups();
    void updateFooterState();
    /** 按清单内容重算列表区高度（换实例 / 加删勾选项后调用） */
    void updateContentHeight();
    void startDownload();
    void onProgress(int done, int total, const QString &message);
    void onItemFinished(int index, const LobbyFetchResult &result);
    void onFetchFinished(int okCount, int failCount);
    void requestInstall();
    /** 新建一个内容勾选项，并记录到对应的 group 列表里 */
    QCheckBox *makeCheckRow(QVBoxLayout *layout, const QString &fileName, bool checked,
                            const QString &tooltip);
    void clearGroup(QVBoxLayout *layout, QList<QCheckBox *> *boxes);

    LobbyInstanceMatcher::JoinPlan m_plan;
    QString m_roomTitle;
    QString m_joinTarget;
    QList<LobbyFetchRequest> m_pendingRequests; ///< 本次勾选待下载的项（含下标映射）

    LobbyContentFetcher *m_fetcher = nullptr;
    bool m_installRequested = false;
    bool m_downloading = false;
    int m_failedCount = 0;

    QStackedWidget *m_pages = nullptr;
    QWidget *m_card = nullptr;

    // ── 配置页 ──
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subTitleLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QLabel *m_instanceCaption = nullptr;
    QComboBox *m_instanceCombo = nullptr;
    QLabel *m_instanceHint = nullptr;
    QPushButton *m_installButton = nullptr;

    QWidget *m_scrollBody = nullptr;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    QVBoxLayout *m_requiredLayout = nullptr;
    QVBoxLayout *m_auxLayout = nullptr;
    QVBoxLayout *m_packLayout = nullptr;
    QVBoxLayout *m_shaderLayout = nullptr;
    QWidget *m_requiredGroup = nullptr;
    QWidget *m_auxGroup = nullptr;
    QWidget *m_packGroup = nullptr;
    QWidget *m_shaderGroup = nullptr;
    QList<QCheckBox *> m_requiredBoxes;
    QList<QCheckBox *> m_auxBoxes;
    QList<QCheckBox *> m_packBoxes;
    QList<QCheckBox *> m_shaderBoxes;

    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_skipButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QLabel *m_emptyHint = nullptr;

    // ── 进度页 ──
    QLabel *m_progressLabel = nullptr;
    QLabel *m_progressDetail = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_progressCancelButton = nullptr;
    QLabel *m_failListLabel = nullptr;
    QLabel *m_triviaLabel = nullptr;
    QPushButton *m_continueButton = nullptr;
    QList<QString> m_failNames;
};
