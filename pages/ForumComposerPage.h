/**
 * @file   ForumComposerPage.h
 * @brief  发布详情页：顶部切换「帖子 / 资源 / 教程」
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从社区页 / 板块列表页 / 发布中心进入；需要登录论坛账号，且账号要先通过入站答题。
 * 顶部有三种发布类型：
 *   帖子 —— 选一个社区板块，纯交流讨论（「插件与开发」也只讨论，不带插件包）
 *   资源 —— 先选资源分类（目前有插件、投影），必须带对应文件：
 *           插件上传 .BlockBox 包发到「插件分享」板块；投影上传 .litematic / .schem
 *           等投影文件发到「投影分享」板块
 *   教程 —— 固定发到「教程分享」板块，正文支持目录、折叠块、思维导图等
 *           教程语法，可另挂 pdf / 压缩包等资料附件（最多 10 个）
 * 发布成功后发 threadCreated(新帖 id)，由主窗口直接打开新帖。
 */
#pragma once

#include <QWidget>

#include <QVector>

#include "components/ForumLinkRowsEditor.h"
#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

class ForumComposerPage : public QWidget
{
    Q_OBJECT

public:
    /** 顶部发布类型 */
    enum class Mode
    {
        Post = 0,   ///< 帖子：选板块，讨论 / 交流 / 作品
        Resource,   ///< 资源：选分类（插件 / 投影），要带文件
        Tutorial    ///< 教程：固定板块，支持目录与资料附件
    };

    explicit ForumComposerPage(QWidget *parent = nullptr);

    /** 以「帖子」模式开始写一篇新帖，可预选板块 slug（从板块列表页进入时带上） */
    void startNewThread(const QString &preselectSlug = QString());

    /** 以「资源」模式开始发布，可预选资源分类 slug（plugin / schematic） */
    void startNewResource(const QString &kindSlug = QStringLiteral("plugin"));

    /** 以「教程」模式开始写一篇新教程（固定发到教程分享板块） */
    void startNewTutorial();

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
    /** 弹「预设」菜单：按当前模式插入帖子 / 教程的 Markdown 模板 */
    void showPresetMenu();
    void setMode(Mode mode);
    void applyMode();
    void resetForm();
    void clearPluginAttachment();
    void pickPluginFile();
    void clearSchematicAttachment();
    void pickSchematicFile();
    /** 教程资料附件：选文件、逐个上传、刷新附件行 */
    void pickTutorialAttachments();
    void uploadTutorialAttachment(const QString &path);
    void refreshTutorialAttachmentRows();
    void pickImageToUpload();
    /** 工具栏格式命令：bold / italic / strike / heading / quote / code / codeblock / ul / ol / link / fold / table / mindmap */
    void runMdCommand(const QString &command);
    void submit();

    QVector<ForumBoard> m_boards;
    QString m_preselectSlug;
    bool m_boardsLoaded = false;

    Mode m_mode = Mode::Post;
    /** 资源分类 slug：plugin（插件）/ schematic（投影） */
    QString m_kindSlug = QStringLiteral("plugin");

    QWidget *m_formHost = nullptr;
    QWidget *m_loginHost = nullptr;
    QWidget *m_quizHost = nullptr;

    // ── 顶部「帖子 / 资源 / 教程」切换 ──
    QWidget *m_modeBar = nullptr;
    QPushButton *m_postModeButton = nullptr;
    QPushButton *m_resourceModeButton = nullptr;
    QPushButton *m_tutorialModeButton = nullptr;

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
    QPushButton *m_editModeButton = nullptr;   ///< 工具栏右侧「编辑 / 预览」分段切换
    QPushButton *m_previewModeButton = nullptr;
    QPushButton *m_imageButton = nullptr;      ///< 工具栏「图片」：选图传论坛图床后插进正文
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

    // ── 投影附件（资源模式的「投影」分类才可见） ──
    QWidget *m_schematicCard = nullptr;
    QLabel *m_schematicIconLabel = nullptr;
    QLabel *m_schematicNameLabel = nullptr;
    QLabel *m_schematicMetaLabel = nullptr;
    QPushButton *m_schematicRemoveButton = nullptr;
    QPushButton *m_schematicAddButton = nullptr;
    QLabel *m_schematicHintLabel = nullptr;
    QString m_schematicFilePath;          ///< 选中的投影文件本机路径
    ForumSchematicUpload m_schematicUpload; ///< 上传成功后的服务端回执（发帖用 uploadId）
    bool m_schematicUploading = false;

    // ── 资料附件（教程模式可见）：pdf / 压缩包等，一帖最多 10 个 ──
    QWidget *m_attachmentCard = nullptr;
    QVBoxLayout *m_attachmentRowsLayout = nullptr;
    QLabel *m_attachmentHintLabel = nullptr;
    QPushButton *m_attachmentAddButton = nullptr;
    QVector<ForumAttachmentUpload> m_attachments;   ///< 上传成功的附件回执（发帖用 uploadId）

    // ── 相关链接（资源模式可见）：绑官网 / 仓库 / 反馈等入口 ──
    QWidget *m_linksRow = nullptr;
    ForumLinkRowsEditor *m_linksEditor = nullptr;
};
