/**
 * @file   ForumComposerPage.h
 * @brief  发布详情页：顶部切换「帖子 / 资源」，帖子选板块、资源选分类（目前只有插件）
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从社区页 / 板块列表页 / 发布中心进入；需要登录论坛账号，且账号要先通过入站答题。
 * 顶部有两种发布类型：
 *   帖子 —— 选一个社区板块，纯交流讨论（「插件与开发」也只讨论，不带插件包）
 *   资源 —— 先选资源分类，目前只有「插件」，必须上传 .BlockBox 插件包，发到「插件分享」板块
 * 发布成功后发 threadCreated(新帖 id)，由主窗口直接打开新帖。
 */
#pragma once

#include <QWidget>

#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

class ForumComposerPage : public QWidget
{
    Q_OBJECT

public:
    /** 顶部发布类型 */
    enum class Mode
    {
        Post = 0, ///< 帖子：选板块，讨论 / 教程 / 作品
        Resource  ///< 资源：选分类（目前只有插件），要带文件
    };

    explicit ForumComposerPage(QWidget *parent = nullptr);

    /** 以「帖子」模式开始写一篇新帖，可预选板块 slug（从板块列表页进入时带上） */
    void startNewThread(const QString &preselectSlug = QString());

    /** 以「资源」模式开始发布，可预选资源分类 slug（目前只有 plugin） */
    void startNewResource(const QString &kindSlug = QStringLiteral("plugin"));

signals:
    /** 发布成功，带上新帖 id */
    void threadCreated(int threadId);

    /** 未登录想发帖：请求跳到论坛账户页登录 */
    void loginRequested();

private:
    void buildUi();
    void updateState();
    void openQuizDialog();
    void loadBoards();
    void setPreviewMode(bool preview);
    /** 弹「预设」菜单：把常用 Markdown 正文模板插进正文 */
    void showPresetMenu();
    void setMode(Mode mode);
    void applyMode();
    void resetForm();
    void clearPluginAttachment();
    void pickPluginFile();
    void submit();

    QVector<ForumBoard> m_boards;
    QString m_preselectSlug;
    bool m_boardsLoaded = false;

    Mode m_mode = Mode::Post;
    /** 资源分类 slug：目前只有插件（plugin） */
    QString m_kindSlug = QStringLiteral("plugin");

    QWidget *m_formHost = nullptr;
    QWidget *m_loginHost = nullptr;
    QWidget *m_quizHost = nullptr;

    // ── 顶部「帖子 / 资源」切换 ──
    QWidget *m_modeBar = nullptr;
    QPushButton *m_postModeButton = nullptr;
    QPushButton *m_resourceModeButton = nullptr;

    QLabel *m_formTitleLabel = nullptr;
    /** 板块行（帖子模式可见） */
    QWidget *m_boardRow = nullptr;
    QComboBox *m_boardCombo = nullptr;
    /** 资源分类行（资源模式可见） */
    QWidget *m_kindRow = nullptr;
    QVector<QPushButton *> m_kindButtons;

    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_tagsEdit = nullptr;
    QPlainTextEdit *m_contentEdit = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    ForumPostBrowser *m_previewBrowser = nullptr;
    QPushButton *m_previewButton = nullptr;
    QPushButton *m_presetButton = nullptr;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    // ── 插件附件（资源模式的「插件」分类才可见） ──
    QWidget *m_pluginCard = nullptr;
    QLabel *m_pluginIconLabel = nullptr;
    QLabel *m_pluginNameLabel = nullptr;
    QLabel *m_pluginMetaLabel = nullptr;
    QPushButton *m_pluginRemoveButton = nullptr;
    QPushButton *m_pluginAddButton = nullptr;
    QLabel *m_pluginHintLabel = nullptr;
    QString m_pluginFilePath;         ///< 选中的 .BlockBox 本机路径
    ForumPluginUpload m_pluginUpload; ///< 上传成功后的服务端回执（发帖用 uploadId）
    bool m_pluginUploading = false;
};
