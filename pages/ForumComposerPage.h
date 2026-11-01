/**
 * @file   ForumComposerPage.h
 * @brief  论坛发帖页：选板块 + 标题 + 标签 + Markdown 正文（带实时预览）
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 从社区页 / 板块列表页进入；需要登录论坛账号，且账号要先通过入站答题。
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
    explicit ForumComposerPage(QWidget *parent = nullptr);

    /** 开始写一篇新帖，可预选板块 slug（从板块列表页进入时带上） */
    void startNewThread(const QString &preselectSlug = QString());

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
    void submit();

    QVector<ForumBoard> m_boards;
    QString m_preselectSlug;
    bool m_boardsLoaded = false;

    QWidget *m_formHost = nullptr;
    QWidget *m_loginHost = nullptr;
    QWidget *m_quizHost = nullptr;
    QComboBox *m_boardCombo = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_tagsEdit = nullptr;
    QPlainTextEdit *m_contentEdit = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    ForumPostBrowser *m_previewBrowser = nullptr;
    QPushButton *m_previewButton = nullptr;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;
};
