/**
 * @file   PublishResourcePage.h
 * @brief  发布资源页 —— 选择要发布的资源类型
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 从发布页点「资源」进入：模组 / 整合包 / 资源包 / 光影包 / 数据包 / 世界 / 插件。
 * 除插件外都进入平台跳转页（CurseForge / Modrinth）；插件进入启动器内的插件发帖页。
 */
#pragma once

#include <QWidget>

#include "components/PublishKit.h"

class PublishResourcePage : public QWidget
{
    Q_OBJECT

public:
    explicit PublishResourcePage(QWidget *parent = nullptr);

signals:
    /** 选了某个资源类型 */
    void kindSelected(PublishKit::ResourceKind kind);

private:
    void buildUi();
};
