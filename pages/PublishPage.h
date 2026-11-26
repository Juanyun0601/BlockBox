/**
 * @file   PublishPage.h
 * @brief  发布页 —— 左侧导航「发布」入口，选择发布资源、帖子还是教程
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 页面放五张入口卡片：
 *   资源 → 发布资源页（选择资源类型后跳 CurseForge / Modrinth，或进插件发布页）
 *   帖子 → 发布帖子页（选择板块后填写标题正文）
 *   教程 → 教程发布页（固定发到「教程分享」，正文自动生成目录，可挂资料附件）
 *   问答 → 有奖问答发布页（固定发到「有奖问答」，赏金发布时托管）
 *   导航 → 导航发布页（按类型发到「网站导航 / 服务器导航」，分享网址一键直达）
 */
#pragma once

#include <QWidget>

class PublishPage : public QWidget
{
    Q_OBJECT

public:
    explicit PublishPage(QWidget *parent = nullptr);

signals:
    /** 点了「资源」卡片 */
    void resourcesRequested();
    /** 点了「帖子」卡片 */
    void postsRequested();
    /** 点了「教程」卡片 */
    void tutorialsRequested();
    /** 点了「问答」卡片 */
    void questionsRequested();
    /** 点了「导航」卡片 */
    void navigationRequested();

private:
    void buildUi();
};
