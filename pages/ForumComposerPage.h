/**
 * @file   ForumComposerPage.h
 * @brief  发布详情页：顶部切换「帖子 / 资源 / 教程」
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从社区页 / 板块列表页 / 发布中心进入；需要登录论坛账号，且账号要先通过入站答题。
 * 顶部有五种发布类型：
 *   帖子 —— 选一个社区板块，纯交流讨论（「插件与开发」也只讨论，不带插件包）
 *   资源 —— 先选资源分类（目前有插件、投影），必须带对应文件：
 *           插件上传 .BlockBox 包发到「插件分享」板块；投影上传 .litematic / .schem
 *           等投影文件发到「投影分享」板块
 *   教程 —— 固定发到「教程分享」板块，正文支持目录、折叠块、思维导图等
 *           教程语法，可另挂 pdf / 压缩包等资料附件（最多 10 个）
 *   导航 —— 分享一个网址：选类型（网站 / 服务器）并填地址，
 *           按类型固定发到「网站导航 / 服务器导航」板块
 * 发布成功后发 threadCreated(新帖 id)，由主窗口直接打开新帖。
 */
#pragma once

#include <QWidget>

#include <QHash>
#include <QVector>

#include "components/ForumLinkRowsEditor.h"
#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"
#include "utils/ForumLobbyService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;

class ForumCarouselEditor;
class ForumTagEditor;

class ForumComposerPage : public QWidget
{
    Q_OBJECT

public:
    /** 顶部发布类型 */
    enum class Mode
    {
        Post = 0,   ///< 帖子：选板块，讨论 / 交流 / 作品
        Resource,   ///< 资源：选分类（插件 / 投影），要带文件
        Tutorial,   ///< 教程：固定板块，支持目录与资料附件
        Question,   ///< 问答：固定板块，先支付方块赏金并由服务端托管
        Navigation  ///< 导航：选类型（网站 / 服务器）并填网址，按类型固定板块
    };

    explicit ForumComposerPage(QWidget *parent = nullptr);

    /** 以「帖子」模式开始写一篇新帖，可预选板块 slug（从板块列表页进入时带上） */
    void startNewThread(const QString &preselectSlug = QString());

    /** 以「资源」模式开始发布，可预选资源分类 slug（plugin / schematic） */
    void startNewResource(const QString &kindSlug = QStringLiteral("plugin"));

    /** 以「教程」模式开始写一篇新教程（固定发到教程分享板块） */
    void startNewTutorial();

    /** 以「问答」模式开始发一条有奖问答（固定发到有奖问答板块，赏金发布时托管） */
    void startNewQuestion();

    /** 以「导航」模式开始分享一个网址（按类型固定发到网站导航 / 服务器导航板块） */
    void startNewNavigation();
    /**
     * @brief 预填内容开始发一条导航帖（「开服」页发布到服务器导航走这里）
     * @param kindSlug website（网站）/ server（服务器），其余值按服务器处理
     * @param url      网址 / 服务器地址，填进网址输入框
     * @param title    标题，空则留空让用户自己写
     * @param content  正文（Markdown），空则留空
     * @param tags     预置标签（最多 4 个，超出按服务端规则截断）
     */
    void startNewNavigation(const QString &kindSlug, const QString &url,
                            const QString &title = QString(),
                            const QString &content = QString(),
                            const QStringList &tags = QStringList());

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
    /** 拉一次论坛热门标签，作为标签推荐的兜底；失败也不影响发布 */
    void loadTagSuggestions();
    /** 按当前发布类型（帖子 / 插件 / 投影 / 教程）换一组推荐标签 */
    void applyTagRecommendations();
    void setPreviewMode(bool preview);
    /** 弹「预设」菜单：按当前模式插入帖子 / 教程的 Markdown 模板 */
    void showPresetMenu();
    void setMode(Mode mode);
    void applyMode();
    /** 按当前来源胶囊（原创 / 转载）展开或收起转载两栏 */
    void updateRepostFields();
    void resetForm();
    void clearPluginAttachment();
    void pickPluginFile();
    /** 清空全部投影附件（换模式 / 重开表单时用） */
    void clearSchematicAttachment();
    /** 投影附件：选文件（可一次选多份）、逐个上传、刷新版本行 */
    void pickSchematicFiles();
    void uploadSchematicEntry(int key);
    void removeSchematicEntry(int key);
    void refreshSchematicRows();
    bool anySchematicUploading() const;
    /** 教程资料附件：选文件、逐个上传、刷新附件行 */
    void pickTutorialAttachments();
    void uploadTutorialAttachment(const QString &path);
    void refreshTutorialAttachmentRows();
    void pickImageToUpload();
    /** 封面：选图 → 传图床 → 预览；清空 / 换模式时回到「还没选」 */
    void pickCoverImage();
    void clearCover();
    void refreshCoverPreview();
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

    // ── 顶部「帖子 / 资源 / 教程 / 问答 / 导航」切换 ──
    QWidget *m_modeBar = nullptr;
    QPushButton *m_postModeButton = nullptr;
    QPushButton *m_resourceModeButton = nullptr;
    QPushButton *m_tutorialModeButton = nullptr;
    QPushButton *m_questionModeButton = nullptr;
    QPushButton *m_navigationModeButton = nullptr;

    QLabel *m_formTitleLabel = nullptr;
    /** 板块行（帖子模式可见） */
    QWidget *m_boardRow = nullptr;
    QComboBox *m_boardCombo = nullptr;
    /** 资源分类行（资源模式可见） */
    QWidget *m_kindRow = nullptr;
    QVector<QPushButton *> m_kindButtons;

    QLineEdit *m_titleEdit = nullptr;
    ForumTagEditor *m_tagsEditor = nullptr;
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

    // ── 投影附件（资源模式的「投影」分类才可见）：可挂多份，每份自己命名 ──
    /** 发布页里的一份投影：本地路径、版本名（主世界 / 下界…）、上传回执 */
    struct SchematicEntry
    {
        int key = 0;                    ///< 稳定标识：上传回调按它找行，删除 / 重排都不会串
        QString filePath;
        QString label;                  ///< 版本名（默认取投影名 / 文件名）
        ForumSchematicUpload upload;    ///< 上传成功后的服务端回执
        bool uploading = false;
    };

    QWidget *m_schematicCard = nullptr;
    QVBoxLayout *m_schematicRowsLayout = nullptr;
    QLabel *m_schematicHintLabel = nullptr;
    QPushButton *m_schematicAddButton = nullptr;
    QVector<SchematicEntry> m_schematics;
    QHash<int, QLabel *> m_schematicMetaLabels;   ///< 每行的说明文字（进度 / 大小）
    int m_schematicKeySeed = 0;

    // ── 资料附件（教程模式可见）：pdf / 压缩包等，一帖最多 10 个 ──
    QWidget *m_attachmentCard = nullptr;
    QVBoxLayout *m_attachmentRowsLayout = nullptr;
    QLabel *m_attachmentHintLabel = nullptr;
    QPushButton *m_attachmentAddButton = nullptr;
    QVector<ForumAttachmentUpload> m_attachments;   ///< 上传成功的附件回执（发帖用 uploadId）

    // ── 相关链接（资源模式可见）：绑官网 / 仓库 / 反馈等入口 ──
    QWidget *m_linksRow = nullptr;
    ForumLinkRowsEditor *m_linksEditor = nullptr;

    // ── 轮播图（资源 / 导航模式可见）：一组图片 / B 站视频，展示在帖子顶部 ──
    QWidget *m_carouselCard = nullptr;
    ForumCarouselEditor *m_carouselEditor = nullptr;

    // ── 封面（教程 / 资源 / 导航模式可见）：板块瀑布流里展示的那张图 ──
    QWidget *m_coverCard = nullptr;
    QLabel *m_coverPreviewLabel = nullptr;
    QLabel *m_coverHintLabel = nullptr;
    QPushButton *m_coverPickButton = nullptr;
    QPushButton *m_coverClearButton = nullptr;
    ForumImageUpload m_coverUpload;   ///< 上传成功后的图床回执（发帖用 uploadId）
    bool m_coverUploading = false;

    // ── 作品来源（资源模式可见）：原创 / 转载二选一，转载要填原链接和说明 ──
    QWidget *m_sourceRow = nullptr;
    QPushButton *m_originalSourceButton = nullptr;
    QPushButton *m_repostSourceButton = nullptr;
    QWidget *m_repostFields = nullptr;
    QLineEdit *m_repostUrlEdit = nullptr;
    QLineEdit *m_repostNoteEdit = nullptr;

    // ── 导航（导航模式可见）：选类型（网站 / 服务器）并填网址 ──
    QWidget *m_navRow = nullptr;
    QVector<QPushButton *> m_navTypeButtons;   ///< 类型胶囊（website 网站 / server 服务器）
    QLineEdit *m_navUrlEdit = nullptr;         ///< 网址输入框
    /** 导航类型 slug：website（网站）/ server（服务器） */
    QString m_navKind = QStringLiteral("website");

    // ── 赏金（问答模式可见）：发布时先从账户里托管，采纳后发给回答者 ──
    QWidget *m_bountyRow = nullptr;
    QSpinBox *m_bountySpin = nullptr;
    QLabel *m_bountyBalanceLabel = nullptr;
    QLabel *m_bountyHintLabel = nullptr;
    QVector<QPushButton *> m_bountyChips;   ///< 快捷金额（5 / 10 / 20 / 50 方块）
};
