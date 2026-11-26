/**
 * @file   ProjectionPreviewDialog.h
 * @brief  投影三维预览弹窗（只读查看整份投影）
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 从「投影详情页」和论坛「投影帖详情页」的「投影预览」按钮进来：
 * 后台线程解析投影文件（.litematic 直接读，.schematic / .schem 先转成
 * .litematic 再读），解析完成后在 VoxelViewWidget 里按方块真实形状与实例
 * 材质渲染整份投影，可自由飞行观察，也能按层剖开看内部。
 *
 * 这里的定位是「看一眼」，所以不带任何编辑能力：破坏 / 放置都不启用，
 * 只保留拖动转向、WASD 飞行、滚轮前后这类浏览操作，避免误改本地投影。
 *
 * 浏览辅助：
 *   - 视角预设（等距 / 正前 / 正后 / 左右侧 / 俯视 / 仰视）+ 重置视角
 *   - 自动旋转（转台，手一拖就停），转速可选慢 / 标准 / 快
 *   - 整窗全屏：按钮、双击视图或 F 键切换，Esc 先退全屏
 *   - 保存当前画面截图（P / Ctrl+S），或直接把画面复制到剪贴板（Ctrl+C）
 *   - 层数条：只看某一层 / 该层及以下，掀开屋顶看内部；还能一键逐层下扫
 *   - 剖切方向可切水平 / 左右 / 前后，竖向结构也能剖开看
 *   - 内置材料清单、快捷键说明
 *   - 材质来源可切「跟随实例」或「纯色块」
 *   - 同一份投影的解析结果会缓存，关掉再开秒开
 */

#pragma once

#include <QHash>
#include <QString>

#include <memory>

#include "AppDialogBase.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;
class SchematicDocument;
class VoxelViewWidget;

/**
 * @brief 投影三维预览弹窗
 */
class ProjectionPreviewDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /** 材质来源：实例客户端贴图，或纯色块（没装对应版本时的兜底） */
    enum class MaterialMode
    {
        Instance = 0,   ///< 取当前实例的方块贴图与真实形状
        Plain = 1       ///< 只用方块颜色，最简单也最快
    };

    /**
     * @brief 打开投影预览（阻塞到用户关闭）
     * @param parent       父窗口（弹窗铺在它上面）
     * @param filePath     投影文件绝对路径（.litematic / .schematic / .schem）
     * @param instancePath 可选：材质来源实例（根目录或 versions/<版本>），空则用纯色块
     * @param displayName  可选：标题里显示的名称，空则取文件名
     */
    static void preview(QWidget *parent, const QString &filePath,
                        const QString &instancePath = QString(),
                        const QString &displayName = QString());

    explicit ProjectionPreviewDialog(QWidget *parent, const QString &filePath,
                                     const QString &instancePath = QString(),
                                     const QString &displayName = QString());
    ~ProjectionPreviewDialog() override;

    /** 预览是否已铺满整个窗口 */
    bool isFullscreenPreview() const { return m_fullscreen; }

    /** 进入 / 退出铺满整个窗口的预览（没有可用模型时忽略进入请求） */
    void setFullscreenPreview(bool on);

    /** 当前材质来源 */
    MaterialMode materialMode() const { return m_materialMode; }

    /** 切换材质来源（拿不到实例贴图时会自动退回纯色块） */
    void setMaterialMode(MaterialMode mode);

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** 后台解析结果：失败时 doc 为空、error 说明原因 */
    struct LoadResult
    {
        std::shared_ptr<SchematicDocument> doc;
        QString error;
        int sizeX = 0;
        int sizeY = 0;
        int sizeZ = 0;
        qint64 cells = 0;       ///< 包围盒格数
        qint64 blocks = 0;      ///< 非空气方块数
        int materialKinds = 0;  ///< 用到的方块状态种类
        bool fromCache = false; ///< 命中上次的解析缓存
    };

    /**
     * @brief 解析一份投影（工作线程执行，不碰界面）
     *
     * 命中缓存时直接返回；需要时先把 .schematic / .schem 转成临时 .litematic，
     * 读完立即删掉临时文件。
     */
    static LoadResult loadDocument(const QString &filePath);

    void initUI();
    /** 启动后台解析（超大投影被拦下后，用户可点「仍然加载」再进一次） */
    void startLoad();
    /** 用户坚持要看这个超大投影 */
    void forceLoad();
    /** 解析完成：挂材质、交给三维视图，或显示失败原因 */
    void applyDocument(const LoadResult &result);
    /** 按当前材质来源给视图挂贴图 / 形状加载器（纯色块则摘掉）；返回是否被迫退回纯色块 */
    bool applyMaterialToView();
    /** 生物开关的可用状态与文案（没有实体贴图时灰掉） */
    void refreshEntityUi();
    /** 预热本次投影用到的方块形状，避免首帧满 jar 找模型 */
    void prewarmShapes();
    /** 后台读方块中文名（悬停提示用；读完之前先显示英文 ID） */
    void ensureBlockNames();
    /** 按层数控件当前值过滤预览（0 = 全部，1..N = 沿当前剖切轴的第 N 层） */
    void applyLayer(int layer);
    /** 层数控件数值文案与箭头可用状态 */
    void refreshLayerUi();
    /** 当前剖切轴上的层数（水平层 = sizeY，左右 / 前后 = sizeX / sizeZ） */
    int layerSliceCount() const;
    /** 切换剖切轴：0 = 水平（Y），1 = 左右（X），2 = 前后（Z） */
    void applyLayerAxis(int axis);
    /** 逐层扫描开关：从最顶层自动往下掀开 */
    void toggleLayerScan(bool on);
    /** 停止逐层扫描并恢复按钮外观 */
    void stopLayerScan();
    /** 自动旋转每帧转过的角度（按转速下拉框更新） */
    void applySpinSpeed();
    /** 打开材料清单（复用投影材料弹窗，不重复解析文件） */
    void openMaterialList();
    /** 把当前画面复制到剪贴板（Ctrl+C） */
    void copyScreenshot();
    /** 快捷键与操作说明 */
    void showShortcutHelp();
    /** 刷新标题行右侧的尺寸 / 方块数 / 材料种类 / 面数 */
    void updateStatsLabel();
    /** 模型没就绪时把操作控件灰掉 */
    void setControlsEnabled(bool enabled);
    /** 显示常驻提示（加载中 / 失败 / 操作说明） */
    void setHint(const QString &text);
    /** 没有画面时：藏起空画面框，让提示撑满留白并居中（加载中 / 解析失败 / 超大投影） */
    void setPlaceholderState(bool on);
    /** 显示一次性提示（截图已保存等），几秒后自动消失 */
    void showTransient(const QString &text);
    /** 自动旋转开关 */
    void toggleAutoSpin(bool on);
    /** 保存当前画面为 PNG */
    void saveScreenshot();
    /** 回到默认等距视角并把整份投影重新收进画面 */
    void resetViewToDefault();
    /** 按视角下拉框下标切预设 */
    void applyViewPresetIndex(int index);
    /** 标题里显示的投影名 */
    QString subjectName() const;

    QString m_filePath;
    QString m_instancePath;
    QString m_displayName;

    bool m_loadStarted = false;
    bool m_loading = false;
    bool m_forced = false;      ///< 用户已确认「仍然加载」
    bool m_docReady = false;
    bool m_fullscreen = false;

    MaterialMode m_materialMode = MaterialMode::Instance;
    std::shared_ptr<SchematicDocument> m_doc;

    // 解析统计（超大投影被拦下时也要能显示尺寸）
    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    qint64 m_cells = 0;
    qint64 m_blocks = 0;
    int m_materialKinds = 0;
    int m_entityCount = 0;   ///< 投影里保存的生物 / 实体数量
    bool m_entityVisible = true;   ///< 用户是否要显示生物（没有实体贴图时临时关掉，恢复后按这个来）

    // 卡片与布局（全屏时要放开尺寸、藏起标题行）
    QWidget *m_card = nullptr;
    QVBoxLayout *m_outer = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QWidget *m_titleBar = nullptr;
    QWidget *m_toolBar = nullptr;    ///< 工具栏面板（分组控件，窄窗口自动换行）
    QWidget *m_stage = nullptr;      ///< 三维画面的外框（全屏时一起去掉圆角描边）
    QWidget *m_infoBar = nullptr;    ///< 提示行容器（没画面时撑满留白、提示居中）

    QLabel *m_titleLabel = nullptr;
    QLabel *m_titleBadge = nullptr;  ///< 标题前的「3D」小徽标
    QLabel *m_statsLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_closeBtn = nullptr;

    // 视角 / 材质 / 全屏 / 截图
    QComboBox *m_viewCombo = nullptr;
    QComboBox *m_materialCombo = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QToolButton *m_resetBtn = nullptr;
    QToolButton *m_spinBtn = nullptr;
    QToolButton *m_entityBtn = nullptr;   ///< 显示 / 隐藏投影里的生物
    QToolButton *m_fullscreenBtn = nullptr;
    QToolButton *m_shotBtn = nullptr;
    QToolButton *m_moreBtn = nullptr;

    // 超大投影的「仍然加载」
    QPushButton *m_forceBtn = nullptr;

    // 层数条（模型没建好时整条隐藏）
    QWidget *m_layerBar = nullptr;
    QSlider *m_layerSlider = nullptr;
    QToolButton *m_layerDown = nullptr;
    QToolButton *m_layerUp = nullptr;
    QToolButton *m_scanBtn = nullptr;
    QLabel *m_layerValue = nullptr;
    QToolButton *m_belowToggle = nullptr;

    QTimer *m_spinTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_scanTimer = nullptr;
    VoxelViewWidget *m_view = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_hoverLabel = nullptr;   ///< 悬停方块信息（名称 + 坐标）

    // 观察参数
    float m_spinDegreesPerTick = 0.45f;   ///< 自动旋转每 30ms 转过的角度
    int m_layerAxis = 0;                  ///< 0 = 水平（Y），1 = 左右（X），2 = 前后（Z）
    bool m_scanning = false;              ///< 正在逐层下扫

    // 方块中文名（小写 ID -> 中文名；后台读好后填进来）
    QHash<QString, QString> m_cnMap;
    bool m_cnMapLoading = false;
};
