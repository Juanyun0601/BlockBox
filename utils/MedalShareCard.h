/**
 * @file   MedalShareCard.h
 * @brief  勋章墙分享图：把「我的勋章」排成一张可以直接发群聊 / 发社区的竖版海报
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 只负责画图，不联网、不读配置：账户页把 /api/medals 回包整理成 Data 传进来，
 * 这里按固定版式渲染成 QImage（宽 1080，高度随勋章数量变化）。
 * 分享图不跟界面主题走，固定用深绿底 + 亮绿点缀，浅色 / 深色主题下导出同一张图，
 * 发出去不会因为对方主题不同而变得难认。
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QVector>

namespace MedalShareCard {

/** 分享图里的一枚已点亮勋章（论坛勋章或游戏成就） */
struct Item
{
    QString name;      ///< 勋章名 / 成就名
    QString rarity;    ///< common / rare / epic / legendary / mythic / game
    QString iconId;    ///< 论坛勋章 id；游戏成就传 advancement id
    bool game = false; ///< true 时用游戏成就的分类图标
};

/** 生成分享图需要的全部数据（由账户页从勋章墙回包里整理） */
struct Data
{
    QString displayName;    ///< 昵称
    QString username;       ///< 账号名（画成 @账号名）
    QPixmap avatar;         ///< 方形头像原图；空则按昵称画字母头像
    qint64 joinedAt = 0;    ///< 注册时间（秒级时间戳），0 表示不显示
    int earned = 0;         ///< 已点亮总数（论坛勋章 + 游戏成就）
    int total = 0;          ///< 勋章墙总数（论坛勋章总数 + 已获得成就数）
    int forumEarned = 0;    ///< 论坛勋章已点亮数
    int forumTotal = 0;     ///< 论坛勋章总数
    int gameEarned = 0;     ///< 已同步的游戏成就数
    QVector<Item> items;    ///< 已点亮条目：论坛勋章在前、游戏成就在后
    Item next;              ///< 下一枚要解锁的勋章（空表示不展示「下一枚目标」）
    QString nextProgress;   ///< 下一枚的进度文案，例如「发布 5 个作品 · 4 / 5」
    QColor brand = QColor(0x2e, 0x7d, 0x32); ///< 启动器主题绿，用来推点缀色
    QString siteUrl;        ///< 官网地址（不是论坛地址），画在底部
};

/**
 * @brief 渲染分享图
 * @return 宽 1080 的竖版海报；data 里没有已点亮勋章时返回一张「还没点亮勋章」的引导图
 */
QImage buildPoster(const Data &data);

/** 建议的保存文件名（不含目录），例如「方块盒子-勋章墙-20261122-1650.png」 */
QString suggestedFileName();

} // namespace MedalShareCard
