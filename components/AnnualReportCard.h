/**
 * @file   AnnualReportCard.h
 * @brief  「我的 MC 年度报告」分享海报：把一年的游玩数据排成一张能直接发群 / 发动态的竖版长图
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 只负责画图：不联网、不读配置（品牌行用安装包里内嵌的官方 logo 资源，字体取系统默认族 +
 * 粗体），传进来一份 PlaytimeAnnualReport 就能出图。
 *
 * 海报固定宽 900、高 1500（竖版），深色渐变底 + 主题绿点缀，不跟界面主题走：
 * 浅色主题下导出的也是同一张图，发出去不会因为对方主题不同而看错。
 * 需要保存 / 复制 / 分享时，把返回的 QImage 交给 MedalShareDialog 即可。
 */
#pragma once

#include <QImage>
#include <QString>

#include "utils/PlaytimeStats.h"

namespace AnnualReportCard
{
/** 海报宽度（像素） */
constexpr int kPosterWidth = 900;

/** 海报高度（像素，竖版 3:5，落在建议的 1400~1600 区间内） */
constexpr int kPosterHeight = 1500;

/**
 * @brief 生成年度报告海报
 * @param report      一年的游玩数据（本机或账号云端都可以；没有数据也能画，数字为 0）
 * @param displayName 昵称，画在页脚「方块盒子 · 昵称」；空则用「方块盒子玩家」
 * @param subtitle    数据来源小字（例如「本机数据」「账号云端 · 含所有设备」），空则不画
 * @return 宽 900、高 1500 的竖版 QImage
 */
QImage buildPoster(const PlaytimeAnnualReport &report, const QString &displayName,
                   const QString &subtitle);

/** 建议的保存文件名（不含目录），例如「方块盒子-MC年度报告-2026-20261122-1936.png」 */
QString suggestedFileName(int year);

} // namespace AnnualReportCard
