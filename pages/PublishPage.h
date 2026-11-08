/**
 * @file   PublishPage.h
 * @brief  发布页 —— 左侧导航「发布」入口，选择发布资源还是帖子
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 页面只放两张入口卡片：
 *   资源 → 发布资源页（选择资源类型后跳 CurseForge / Modrinth，或进插件发布页）
 *   帖子 → 发布帖子页（选择板块后填写标题正文）
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

private:
    void buildUi();
};
