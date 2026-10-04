/**
 * @file   BedrockDownloadPage.h
 * @brief  基岩版版本列表页 — 多源（mcappx / mcapks / bbk）版本浏览与筛选
 * @author BlockBox Team
 *
 * 入口：侧边栏「基岩版模式 → 资源 → 安装新实例」
 *
 * 上方为筛选栏 + 版本列表（与 Java 版安装实例页一致）；
 * 点击版本卡片进入 BedrockVersionDetailPage 进行下载。
 */

#pragma once

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVector>
#include <QWidget>

#include "utils/bedrock/BedrockVersionService.h"

class BedrockDownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockDownloadPage(QWidget *parent = nullptr);
    ~BedrockDownloadPage() override;

    /// 拉取/刷新版本列表
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void backRequested();
    /// 请求打开某版本的详情页
    void versionDetailRequested(const BedrockVersionEntry &entry);

private slots:
    void onSourceFilterChanged();
    void onTypeFilterChanged();
    void onRefreshClicked();
    void onVersionListFetched(const QVector<BedrockVersionEntry> &versions);
    void onFetchError(const QString &sourceName, const QString &error);
    void onCardItemClicked(QListWidgetItem *item);

private:
    void initUI();
    void populateVersionList();
    void updateUIState();
    QString versionTypeName(BedrockVersionType type) const;
    /// 构建与 Java 版安装实例页一致的版本卡片
    QWidget *createVersionCard(const BedrockVersionEntry &entry, int index);
    /// 将选中态同步到列表卡片（selected 属性）
    void syncCardSelection();

    // 标题
    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;

    // 筛选栏
    QComboBox *m_sourceCombo;
    QComboBox *m_typeCombo;
    QPushButton *m_refreshBtn;
    QLabel *m_statusHintLabel;

    // 版本列表
    QListWidget *m_versionList;

    // 数据
    QVector<BedrockVersionEntry> m_allVersions;
    QVector<BedrockVersionEntry> m_filteredVersions;
    bool m_loaded; ///< 版本列表是否已加载过（避免每次进入页面都重新拉取）
};
