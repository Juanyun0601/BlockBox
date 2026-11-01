/**
 * @file   StructureDetailDialog.h
 * @brief  结构详情弹窗（顶部「简介 / 预览 / 战利品」三个选项）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 入口是存档地图里结构信息小窗的「详情」按钮：弹出后铺满模糊遮罩，
 * 居中卡片里先给出结构图标与名称；简介页展示坐标与维度，战利品页按原版
 * 战利品表推算箱内物品的出现概率，预览页用投影预览同一套三维渲染按种子
 * 重建结构主体。
 */

#ifndef STRUCTUREDETAILDIALOG_H
#define STRUCTUREDETAILDIALOG_H

#include <QList>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStringList>

#include "AppDialogBase.h"
#include "utils/WorldMap/StructureLoot.h"

class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class QWidget;
class FlowLayout;
class SchematicDocument;
class VoxelViewWidget;

/**
 * @brief 结构详情弹窗：居中卡片 + 顶部三页选项
 *
 * 顶部页签固定为「简介 / 预览 / 战利品」三页。简介页填当前结构的
 * 基本信息（坐标 / 维度 / Wiki 名），战利品页展示各物品的出现概率，
 * 预览页复用 VoxelViewWidget 按种子重建结构的三维方块模型，并把模型里的
 * 藏宝箱（箱子 / 木桶 / 潜影盒）高亮出来。
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
     * @param rare      是否稀有/重要结构（标题旁打标签）
     * @param loot      战利品结果（展示在「战利品」页）
     * @param seed      世界种子（预览页按它重建模型）
     * @param hasSeed   是否读到世界种子
     * @param instancePath 实例目录（预览页取客户端贴图 / 方块形状；空则用颜色回退）
     * @param mcVersion 游戏版本（如 "1.21.4"；预览页避开过新的方块）
     */
    StructureDetailDialog(const QString &title, const QString &wikiTitle,
                          const QPixmap &icon, const QString &structKey,
                          qint64 x = 0, qint64 z = 0, bool hasCoords = false,
                          const QString &dimName = QString(), bool rare = false,
                          const StructureLoot::Result &loot =
                              StructureLoot::Result(),
                          quint64 seed = 0, bool hasSeed = false,
                          const QString &instancePath = QString(),
                          const QString &mcVersion = QString(),
                          QWidget *parent = nullptr);

    ~StructureDetailDialog() override;

    /** 结构稳定标识：后续三页按它取内容 */
    QString structKey() const { return m_structKey; }
    /** Wiki 条目标题：后续简介页取资料用 */
    QString wikiTitle() const { return m_wikiTitle; }
    /** 页签容器（探针 / 后续填充内容用） */
    QTabWidget *tabs() const { return m_tabs; }
    /** 切到指定页签 */
    void setCurrentPage(Page page);

    /**
     * @brief 预览页是否正在全屏预览（铺满整个窗口）
     */
    bool isPreviewFullscreen() const { return m_previewFs; }

    /**
     * @brief 进入 / 退出预览页的全屏预览
     *
     * 全屏时居中卡片撑满整个窗口、顶部标题行与页签条隐藏，只留结构预览
     * 本身（层数条、藏宝箱高亮、说明文字照常可用）；再按一次按钮或 Esc 退出。
     * 没有可用模型时忽略进入请求。
     */
    void setPreviewFullscreen(bool on);

protected:
    /** Esc：全屏时先退出全屏，不直接关弹窗 */
    void keyPressEvent(QKeyEvent *event) override;

private:
    /** 简介页：基本信息（坐标 / 维度 / Wiki） */
    QWidget *createIntroPage(const QString &title, const QString &wikiTitle,
                             qint64 x, qint64 z, bool hasCoords,
                             const QString &dimName, bool rare);
    /** 战利品页：各物品的出现概率芯片 */
    QWidget *createLootPage(const StructureLoot::Result &loot);
    /**
     * @brief 战利品页换分类：重建芯片区
     * @param chestIndex -1 = 任一宝箱；-2 = 全部合计；0..N-1 = 第 N 类箱子
     */
    void populateLootChips(int chestIndex);
    /**
     * @brief 造一条战利品芯片（图标 +「名称 ×期望数量」+ 出现概率）
     * @param item   单条战利品
     * @param fromSeed 是否已按种子定位到结构（芯片上打标记用）
     * @param parent 父控件
     */
    QWidget *createLootChip(const StructureLoot::Stack &item, bool fromSeed,
                            QWidget *parent);
    /** 战利品页首次显示时按客户端 jar 贴图补齐芯片图标（只补一次） */
    void ensureLootIcons();
    /** 预览页：三维模型 + 概要信息 */
    QWidget *createPreviewPage();
    /** 预览页首次显示时按种子构建模型（只建一次） */
    void ensurePreviewBuilt();
    /**
     * @brief 按层数控件当前值过滤预览视图
     * @param layer 0 = 全部；1..sizeY = 自下往上第 layer 层
     */
    void applyPreviewLayer(int layer);
    /** 层数控件的可用状态与数值显示（模型未建好时整条灰掉） */
    void refreshPreviewLayerUi();
    /** 收集模型里的藏宝箱（箱子 / 陷阱箱 / 木桶 / 潜影盒）格索引 */
    void collectPreviewChestCells();
    /** 根据找到的宝箱数刷新「高亮藏宝箱」按钮并默认打开高亮 */
    void refreshPreviewChestUi();
    /** 用户切换「高亮藏宝箱」 */
    void applyPreviewChestHighlight(bool on);

    QTabWidget *m_tabs = nullptr;
    QString m_structKey;
    QString m_wikiTitle;

    // 卡片与标题行（全屏预览时要撑满窗口 / 暂时藏起来）
    QWidget *m_card = nullptr;
    QVBoxLayout *m_outer = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QWidget *m_titleBar = nullptr;
    bool m_previewFs = false;

    // 预览页上下文
    qint64 m_x = 0;
    qint64 m_z = 0;
    quint64 m_seed = 0;
    bool m_hasSeed = false;
    QString m_instancePath;
    QString m_mcVersion;
    bool m_supportsPreview = false;

    VoxelViewWidget *m_previewView = nullptr;
    QLabel *m_previewVariant = nullptr;
    QLabel *m_previewStats = nullptr;
    QLabel *m_previewHint = nullptr;
    QToolButton *m_previewFsBtn = nullptr;
    SchematicDocument *m_previewDoc = nullptr;
    bool m_previewBuilt = false;

    // 预览页层数控制：滑块 0 = 全部，1..sizeY = 自下往上第 N 层
    QWidget *m_previewLayerBar = nullptr;
    QSlider *m_previewLayerSlider = nullptr;
    QToolButton *m_previewLayerDown = nullptr;
    QToolButton *m_previewLayerUp = nullptr;
    QLabel *m_previewLayerValue = nullptr;
    QToolButton *m_previewBelowToggle = nullptr;
    QToolButton *m_previewChestToggle = nullptr;
    QSet<int> m_previewChestCells;

    // 战利品页图标：与 m_lootItemIds 一一对应
    QList<QLabel *> m_lootIconLabels;
    QStringList m_lootItemIds;
    bool m_lootIconsBuilt = false;

    // 战利品页分类：任一宝箱 / 各类箱子（换分类时整片芯片重造）
    StructureLoot::Result m_loot;
    QWidget *m_lootChipWrap = nullptr;
    FlowLayout *m_lootFlow = nullptr;
    QList<QWidget *> m_lootChips;
    QLabel *m_lootEmptyHint = nullptr;
    QLabel *m_lootSummary = nullptr;
};

#endif // STRUCTUREDETAILDIALOG_H
