/**
 * @file   MapExportDialog.h
 * @brief  存档地图「高级导出」：选项弹窗 + 导出图合成器
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图页顶栏「导出」菜单 →「高级导出…」打开本弹窗：选导出范围
 * （整张地图 / 当前视野）、清晰度（当前画布 / 高清重渲）、要画进图里的
 * 标注（结构 / 我的标记 / 矿物点 / 玩家位置 / 区块网格）与信息栏、格式。
 *
 * MapExportComposer::compose() 是纯函数（只用 QImage / QPainter，不碰任何
 * 控件），可在工作线程执行；页面负责在后台任务里调它并把结果写盘。
 */
#ifndef MAPEXPORTDIALOG_H
#define MAPEXPORTDIALOG_H

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

#include "AppDialogBase.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

/** 一次导出要画进图里的标注快照（页面在 UI 线程收集，值拷贝到后台） */
struct MapExportAnnotations
{
    /** 一个结构标记（type = StructureFinder::allStructures() 下标） */
    struct StructureMark
    {
        int type = -1;
        qint64 x = 0;
        qint64 z = 0;
        bool done = false;   ///< 已标记完成 → 半透明画
    };

    /** 一个矿物点（ore = OreFinder::allOres() 下标） */
    struct OreMark
    {
        int ore = -1;
        qint64 x = 0;
        qint64 z = 0;
    };

    /** 一个用户手放的标记点（icon = userMarkerIconDefs() 下标） */
    struct UserMark
    {
        int icon = 0;
        qint64 x = 0;
        qint64 z = 0;
        QString name;
    };

    QVector<StructureMark> structures;
    QVector<OreMark> ores;
    QVector<UserMark> userMarkers;
    /** 结构图标（与 allStructures() 下标对齐；预取好的像素图，后台线程可用） */
    QVector<QImage> structureIcons;
    /** 矿物点代表色（与 allOres() 下标对齐） */
    QVector<QRgb> oreColors;
    /** 用户标记的白色图形（与 userMarkerIconDefs() 下标对齐） */
    QVector<QImage> userIcons;
    /** 用户标记的圆底颜色（与 userMarkerIconDefs() 下标对齐） */
    QVector<QRgb> userColors;
    bool hasPlayer = false;
    double playerX = 0.0;
    double playerZ = 0.0;
    double playerYaw = 0.0; ///< MC yaw：0 = 南(+Z)，90 = 西(-X)
    QRgb accent = 0;        ///< 主题色（玩家标记 / 指北针用）
};

/** 合成一份导出图（页面在后台线程调用；只做像素操作，线程安全） */
struct MapExportComposeInput
{
    QImage base;               ///< 底图（透明像素 = 未生成区块）
    qint64 minBlockX = 0;      ///< 底图左上角对应的世界方块坐标 X
    qint64 minBlockZ = 0;      ///< 底图左上角对应的世界方块坐标 Z
    int bpp = 1;               ///< 每像素代表的方块数
    bool png = true;           ///< true = PNG（保留未生成区域的透明）；false = JPG（填底色）
    QColor voidColor;          ///< JPG 的未生成区域底色（与画布底色一致）
    MapExportAnnotations ann;
    bool structures = true;
    bool userMarkers = true;
    bool ores = false;
    bool player = false;
    bool grid = false;
    bool infoBar = true;
    QString title;             ///< 信息栏左侧：存档名 · 维度
    QString meta;              ///< 信息栏右侧：种子 · 导出日期
};

namespace MapExportComposer {

/** 组装导出图：底图 + 标注 + 指北针 / 比例尺 / 信息栏（须在工作线程调用） */
QImage compose(const MapExportComposeInput &input);

} // namespace MapExportComposer

/** 高级导出弹窗：收集选项，导出动作由页面执行（返回 Accepted 后读 options） */
class MapExportDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /** 范围模式（Options::rangeMode 取值） */
    enum
    {
        RangeWhole = 0,  ///< 整张地图（当前渲染画布）
        RangeView = 1,   ///< 当前视野
        RangeCustom = 2  ///< 自定义方块矩形（坐标输入 / 地图上框选）
    };

    /** 「重新框选」的自定义返回码：done(ReselectResult) 表示要先去地图上框范围 */
    static constexpr int ReselectResult = 2;

    /** 几种「范围 × 清晰度」组合的预估像素尺寸（0 尺寸 = 该组合不可用） */
    struct SizeEstimates
    {
        QSize wholeLo;  ///< 整张地图 · 当前画布
        QSize wholeHi;  ///< 整张地图 · 高清重渲
        QSize viewLo;   ///< 当前视野 · 当前画布
        QSize viewHi;   ///< 当前视野 · 高清重渲
        // 自定义区域的预估由弹窗按坐标实时算，不在这里预置
    };

    struct Options
    {
        int rangeMode = RangeWhole; ///< RangeWhole / RangeView / RangeCustom
        QRect customRect;           ///< rangeMode == RangeCustom 时的方块范围
        bool hiRes = false;         ///< true = 从存档重新高清渲染
        bool structures = true;
        bool userMarkers = true;
        bool ores = false;
        bool player = false;
        bool grid = false;
        bool infoBar = true;
        bool png = true;            ///< false = JPG
    };

    MapExportDialog(QWidget *parent, const SizeEstimates &estimates,
                    const QRect &canvasBlocks, int canvasBpp,
                    const Options &initial, const QRect &initialCustomRect);

    /** 弹窗选中的选项（exec() 返回 Accepted 后读） */
    Options options() const { return m_opt; }

private:
    void initUI();
    /** 把当前界面勾选读进 m_opt（导出与「重新框选」两个出口共用） */
    void collectOptions();
    /** 从输入框读出自定义区域（钳到画布范围；无效返回空矩形） */
    QRect customRectFromInputs() const;
    /** 按当前勾选刷新底部尺寸预估说明（无效范围会禁用导出按钮） */
    void updateEstimate();

    Options m_opt;
    SizeEstimates m_est;
    QRect m_canvasBlocks; ///< 画布覆盖的方块范围（自定义坐标校验用）
    int m_canvasBpp = 1;

    QRadioButton *m_wholeBtn = nullptr;
    QRadioButton *m_viewBtn = nullptr;
    QRadioButton *m_customBtn = nullptr;
    QWidget *m_customRow = nullptr;     ///< 自定义坐标输入行（勾自定义时显示）
    QLineEdit *m_cX0 = nullptr;
    QLineEdit *m_cX1 = nullptr;
    QLineEdit *m_cZ0 = nullptr;
    QLineEdit *m_cZ1 = nullptr;
    QPushButton *m_reselectBtn = nullptr;
    QRadioButton *m_loBtn = nullptr;
    QRadioButton *m_hiBtn = nullptr;
    QCheckBox *m_structChk = nullptr;
    QCheckBox *m_userChk = nullptr;
    QCheckBox *m_oreChk = nullptr;
    QCheckBox *m_playerChk = nullptr;
    QCheckBox *m_gridChk = nullptr;
    QCheckBox *m_infoChk = nullptr;
    QRadioButton *m_pngBtn = nullptr;
    QRadioButton *m_jpgBtn = nullptr;
    QLabel *m_estimateLabel = nullptr;
    QPushButton *m_okBtn = nullptr;
};

#endif // MAPEXPORTDIALOG_H
