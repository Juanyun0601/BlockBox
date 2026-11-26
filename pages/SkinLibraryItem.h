/**
 * @file   SkinLibraryItem.h
 * @brief  方块皮肤站皮肤库的一件作品（皮肤库列表页与作品详情页共用）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 字段与方块皮肤站 /api/library、/api/textures/{id} 的回包对齐，
 * 也保持与 SkinStationPage 内部的 SkinItem 一致，避免两处各写一份结构体。
 */
#pragma once

#include <QString>

/** 皮肤库作品 */
struct SkinLibraryItem
{
    int id = -1;
    QString kind;     ///< skin / cape（皮肤库列表页只展示 skin）
    QString name;
    QString model;    ///< classic / slim
    QString url;      ///< 站内相对地址，例如 /textures/xxxx.png
    QString ownerName;
    QString createdAt; ///< 上传日期（yyyy-MM-dd，详情接口补全后才有）
    int likes = 0;
    int downloads = 0;
    int comments = 0;  ///< 评论数（详情接口补全后才有）
    int favorites = 0; ///< 收藏数（详情接口补全后才有）
    bool mine = false;
    bool isPublic = true;
};
