/**
 * @file   CreationWorkDetailPage.h
 * @brief  创作中心的作品详情页（管理视角）
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 创作中心列表点开一个作品进来（创作中心内部子页，不是公开的帖子详情）。
 * 集中看这一个作品的数据（浏览 / 回复 / 下载）、插件作品的历史版本，
 * 并提供编辑、上传新版本、删除，以及跳到公开的帖子详情。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/ForumContentService.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ForumPostBrowser;

class CreationWorkDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit CreationWorkDetailPage(QWidget *parent = nullptr);

    /** 展示一个作品：拉详情刷新数据与正文，插件作品再拉版本历史 */
    void openWork(const ForumThreadInfo &thread);

    /**
     * @brief 编辑作品的公共弹窗（标题 + 标签 + 正文），创作中心列表也用它
     * @return 是否确认保存（false = 取消或标题为空）
     */
    static bool runEditDialog(QWidget *parent, const QString &title, const QStringList &tags,
                              const QString &content, QString *outTitle, QStringList *outTags,
                              QString *outContent);

signals:
    /** 「返回作品列表」：回到创作中心列表 */
    void backRequested();

    /** 「查看公开帖子」：在启动器内打开公开的帖子详情 */
    void threadOpenRequested(int threadId);

    /** 作品被编辑 / 上传了新版本：创作中心列表的数据跟着刷新 */
    void workChanged();

    /** 作品被删除：创作中心回列表并刷新 */
    void workDeleted();

private:
    void buildUi();
    void loadContent();
    void loadVersions();
    void renderWork();
    void renderVersions(const QVector<ForumPluginVersionInfo> &versions);
    void editWork();
    void deleteWork();
    void uploadNewVersion();
    void setStatus(const QString &text, bool error = false);

    ForumThreadInfo m_thread;         ///< 当前作品（列表带入，详情刷新后覆盖）
    int m_firstPostId = 0;            ///< 首楼 id（改正文用）
    QString m_firstPostContent;       ///< 首楼正文原文（编辑弹窗预填 / 对比变化）
    bool m_uploading = false;
    bool m_loading = false;

    QScrollArea *m_scroll = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_backButton = nullptr;

    // 头部卡
    ForumKit::ElidedLabel *m_titleLabel = nullptr;
    QHBoxLayout *m_badgeLayout = nullptr;
    QLabel *m_metaLabel = nullptr;

    // 数据卡
    QLabel *m_statViews = nullptr;
    QLabel *m_statReplies = nullptr;
    QLabel *m_statDownloads = nullptr;
    QFrame *m_downloadTile = nullptr;

    // 版本卡（插件作品才有）
    QFrame *m_versionsCard = nullptr;
    QVBoxLayout *m_versionsLayout = nullptr;
    QLabel *m_versionStatus = nullptr;

    // 正文卡
    ForumPostBrowser *m_contentBrowser = nullptr;
};
