/**
 * @file   PublishBoardPage.h
 * @brief  发布帖子页 —— 选择社区板块后进入发帖详情页
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从发布页点「帖子」进入：拉取论坛板块，用卡片网格展示。
 * 点某个板块后把 slug 交给发帖页（ForumComposerPage）预选板块。
 * 板块数据未登录也能读取；发帖本身仍由发帖页检查登录与人机验证。
 */
#pragma once

#include <QWidget>

#include "utils/ForumContentService.h"

class QLabel;
class QPushButton;
class FlowLayout;

class PublishBoardPage : public QWidget
{
    Q_OBJECT

public:
    explicit PublishBoardPage(QWidget *parent = nullptr);

    /** 进入页面时加载板块（每次进入都刷新，避免拿到旧数据） */
    void open();

    /** 顶栏刷新按钮调用 */
    Q_INVOKABLE void refresh();

signals:
    /** 选中一个板块：去发帖详情页（预选该板块） */
    void boardSelected(const QString &slug, const QString &boardName);

private:
    void buildUi();
    void loadBoards();
    void renderBoards(const QVector<ForumBoard> &boards);
    void renderStatus(const QString &text, bool withRetry);

    FlowLayout *m_cardsLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_retryButton = nullptr;
    bool m_loading = false;
};
