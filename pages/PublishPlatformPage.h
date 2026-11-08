/**
 * @file   PublishPlatformPage.h
 * @brief  平台跳转页 —— 显示「跳转 CurseForge / 跳转 Modrinth」
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从发布资源页选择资源类型后进入（插件除外）。
 * 方块盒子不托管资源文件，这里列出对应平台分类页，点击后用浏览器打开。
 * 世界资源在 Modrinth 没有对应分类，该入口会置灰并说明原因。
 */
#pragma once

#include <QWidget>

#include "components/PublishKit.h"

class QLabel;
class QVBoxLayout;
class FlowLayout;

class PublishPlatformPage : public QWidget
{
    Q_OBJECT

public:
    explicit PublishPlatformPage(QWidget *parent = nullptr);

    /** 设置当前要发布的资源类型（刷新标题与两个平台入口） */
    void setKind(PublishKit::ResourceKind kind);

signals:
    /** 用户点了某个平台入口，url 已在浏览器打开 */
    void jumpRequested(const QString &url);

private:
    void buildUi();
    void rebuildPlatformCards();
    QWidget *buildTipsCard(QWidget *parent) const;

    PublishKit::ResourceKind m_kind = PublishKit::ResourceKind::Mod;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_subtitle = nullptr;
    QWidget *m_cardsHost = nullptr;
    FlowLayout *m_cardsLayout = nullptr;
};
