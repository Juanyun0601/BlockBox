/**
 * @file   StructureDetailDialog.h
 * @brief  结构详情弹窗（顶部「简介 / 预览 / 战利品」三个选项）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 入口是存档地图里结构信息小窗的「详情」按钮：弹出后铺满模糊遮罩，
 * 居中卡片里先给出结构图标与名称；简介页展示当前这条结构的坐标、
 * 维度与按种子掷取的战利品，预览页内容后续再补。
 */

#ifndef STRUCTUREDETAILDIALOG_H
#define STRUCTUREDETAILDIALOG_H

#include <QPixmap>
#include <QString>

#include "AppDialogBase.h"
#include "utils/WorldMap/StructureLoot.h"

class QTabWidget;
class QWidget;

/**
 * @brief 结构详情弹窗：居中卡片 + 顶部三页选项
 *
 * 顶部页签固定为「简介 / 预览 / 战利品」三页。简介页填当前结构的
 * 基本信息（坐标 / 维度 / Wiki 名 / 按种子掷取的战利品），预览与战利品页仍是占位。
 */
class StructureDetailDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /** 页签下标（与页签顺序一致） */
    enum Page
    {
        IntroPage = 0,   ///< 简介
        PreviewPage = 1, ///< 预览
        LootPage = 2     ///< 战利品
    };
    Q_ENUM(Page)

    /**
     * @param title     结构中文名（弹窗标题）
     * @param wikiTitle Wiki 条目标题（简介页展示 + 取资料用）
     * @param icon      结构图标（空图时不占位）
     * @param structKey 结构稳定标识（StructureFinder 的 key）
     * @param x         该结构实例的方块坐标 X（无坐标时传 false）
     * @param z         该结构实例的方块坐标 Z
     * @param hasCoords 是否有坐标可展示（地图预测命中才有）
     * @param dimName   当前维度中文名（主世界 / 下界 / 末地；空则不显示）
     * @param rare      是否稀有/重要结构（简介页打标签）
     * @param loot      战利品结果（种子掷取或概览回退；空则不显示区块）
     */
    StructureDetailDialog(const QString &title, const QString &wikiTitle,
                          const QPixmap &icon, const QString &structKey,
                          qint64 x = 0, qint64 z = 0, bool hasCoords = false,
                          const QString &dimName = QString(), bool rare = false,
                          const StructureLoot::Result &loot =
                              StructureLoot::Result(),
                          QWidget *parent = nullptr);

    /** 结构稳定标识：后续三页按它取内容 */
    QString structKey() const { return m_structKey; }
    /** Wiki 条目标题：后续简介页取资料用 */
    QString wikiTitle() const { return m_wikiTitle; }
    /** 页签容器（探针 / 后续填充内容用） */
    QTabWidget *tabs() const { return m_tabs; }
    /** 切到指定页签 */
    void setCurrentPage(Page page);

private:
    /** 简介页：基本信息 + 战利品 + 稀有标记 */
    QWidget *createIntroPage(const QString &title, const QString &wikiTitle,
                             qint64 x, qint64 z, bool hasCoords,
                             const QString &dimName, bool rare,
                             const StructureLoot::Result &loot);
    /** 占位页：居中放一行标题 + 一行说明（预览 / 战利品页） */
    QWidget *createPlaceholderPage(const QString &title, const QString &hint);

    QTabWidget *m_tabs = nullptr;
    QString m_structKey;
    QString m_wikiTitle;
};

#endif // STRUCTUREDETAILDIALOG_H
