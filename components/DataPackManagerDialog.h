/**
 * @file   DataPackManagerDialog.h
 * @brief  本地数据包管理弹窗声明
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 单个世界（存档）的数据包管理界面：
 *   - 左侧列表：世界 datapacks/ 下的数据包，开关启用状态、上下调整加载顺序；
 *   - 右侧详情：描述、兼容性、内容清单、冲突文件、文件信息（只读）；
 *   - 从实例数据包库或本地文件导入、删除数据包、打开数据包文件夹；
 *   - 任何改动立即写回世界 level.dat，失败时重新扫描回滚到真实状态。
 *
 * 数据层见 utils/Saves/DataPackManager。
 */
#ifndef DATAPACKMANAGERDIALOG_H
#define DATAPACKMANAGERDIALOG_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

#include "AppDialogBase.h"
#include "utils/Saves/DataPackManager.h"
#include "utils/mod/ModData.h"

class ModrinthAPI;
class QNetworkAccessManager;

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QSplitter;
class QVBoxLayout;
class QWidget;

class DataPackManagerDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开指定世界的本地数据包管理弹窗
     * @param parent          宿主窗口内的任意控件（弹窗将覆盖其顶层窗口）
     * @param instancePath    实例（版本文件夹）绝对路径
     * @param worldName       世界目录名
     * @param gameVersionHint 实例的 MC 版本，用于 level.dat 缺少版本信息时兜底
     */
    static void openForWorld(QWidget *parent, const QString &instancePath,
                             const QString &worldName, const QString &gameVersionHint);

private:
    explicit DataPackManagerDialog(QWidget *parent, const QString &instancePath,
                                   const QString &worldName, const QString &gameVersionHint);

    void initUI();
    /** 重新扫描世界并整体刷新界面（回滚 / 导入 / 删除后调用） */
    void reload();
    /** 按当前扫描结果与搜索词重建左侧行卡片 */
    void rebuildRows();
    /** 重建右侧只读详情面板 */
    void rebuildDetailPanel();
    /** 刷新副标题：世界名 / 版本 / 数量统计 */
    void refreshHeader();
    /** 刷新上移、下移、删除按钮的可用状态 */
    void refreshActionState();
    /** 所选行的高亮描边 */
    void updateRowSelectionStyles();
    /** 按标签实际宽度重新省略过长的数据包名 */
    void updateNameElide();
    /** 窄屏下把左右分栏改为上下堆叠、底部按钮换行 */
    void applyResponsiveLayout();
    /** 底部按钮按宽度重排（宽屏一行、窄屏两行） */
    void layoutBottomBar();
    /** 创建一行数据包卡片（orderNumber > 0 表示启用包的加载序号） */
    QWidget *createPackRow(int index, int orderNumber);
    void selectPack(int index);
    /** 勾选/取消勾选一个数据包并立即写回 level.dat */
    void togglePack(int index, bool enabled);
    /** 在启用包之间上调/下调加载顺序 */
    void movePack(int index, bool up);
    void removeSelectedPack();
    /** 工具条「导入数据包」的弹出菜单：实例数据包库 + 从电脑选择文件 */
    void showImportMenu();
    void chooseImportFile();
    void importPackFrom(const QString &sourcePath);
    void openDatapacksFolder();
    bool packMatchesFilter(int index) const;
    /** 把界面上的启停状态与顺序写回 level.dat，失败时回滚 */
    void persistStates(const QString &successText);

    // ── 在线检查更新（Modrinth 哈希比对，逻辑与资源包页一致） ──
    /** 算出世界的文件型数据包哈希并批量检查是否有新版本 */
    void checkUpdates();
    void onHashesMatched(const QMap<QString, ModVersionFile> &versions);
    void onUpdatesChecked(const QMap<QString, ModVersionFile> &versions);
    void onUpdateCheckFailed(const QString &error);
    /** 依次把有更新的数据包换成新文件（同名覆盖，保留启用状态与加载顺序） */
    void updateCheckedPacks();
    void startNextPackUpdate();
    /** 按可更新缓存刷新「更新」按钮与状态提示 */
    void refreshUpdateState();
    const DataPackInfo *selectedPack() const;
    int indexOfPack(const QString &key) const;

    /** 大小 + 格式的展示文本 */
    static QString formatText(const DataPackInfo &info);
    /** 文件大小（文件夹包显示「文件夹」） */
    static QString sizeText(const DataPackInfo &info);
    /** 小徽标的内联样式（无效颜色 = 中性灰） */
    static QString chipStyle(const QColor &color);
    /** 兼容性结论对应的语义色 */
    static QColor compatColor(DataPackCompat compat);
    /** 兼容性徽标文案（区分偏新 / 偏旧 / 不匹配） */
    QString compatBadgeText(const DataPackInfo &info) const;

    void resizeEvent(QResizeEvent *event) override;

    // ── 数据 ──
    QString m_instancePath;    ///< 实例路径
    QString m_worldName;       ///< 世界目录名
    QString m_worldDir;        ///< 世界文件夹绝对路径
    QString m_gameVersionHint; ///< 实例 MC 版本（兜底）
    DataPackWorld m_world;     ///< 最近一次扫描结果
    QList<DataPackInfo> m_packs; ///< 界面列表（启用在前、禁用在后）
    int m_worldFormatMajor = -1; ///< 世界数据包格式主版本（用于判断偏新偏旧）
    int m_worldFormatMinor = 0;  ///< 世界数据包格式次版本
    int m_selected = -1;         ///< 当前选中的 m_packs 下标
    QString m_search;            ///< 搜索词（名称 / 描述过滤）
    bool m_rebuilding = false;   ///< 正在重建行卡片，忽略构建期信号
    bool m_compact = false;      ///< 窄屏（按钮换行、分栏改纵向）
    bool m_splitterVertical = false; ///< 分栏当前是否纵向

    // ── UI ──
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_runningLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;
    QPushButton *m_updateBtn = nullptr;
    QPushButton *m_toolbarFolderBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_folderBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QGridLayout *m_bottomGrid = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_leftPane = nullptr;
    QWidget *m_rightPane = nullptr;
    QScrollArea *m_listScroll = nullptr;
    QScrollArea *m_detailScroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QWidget *m_emptyWidget = nullptr;   ///< 空世界提示（含「导入数据包」按钮）
    QLabel *m_noMatchLabel = nullptr;   ///< 搜索无结果提示
    QList<QWidget *> m_rowWidgets;      ///< 当前可见的行卡片
    QList<QLabel *> m_rowNameLabels;    ///< 行卡片里的名称标签（用于省略过长的名字）

    // ── 在线更新检查 ──
    ModrinthAPI *m_updateApi = nullptr;      ///< 哈希匹配 / 更新检查
    QNetworkAccessManager *m_net = nullptr;  ///< 下载更新文件
    QHash<QString, QString> m_sha1ByPath;    ///< 数据包文件路径 → SHA-1
    QMap<QString, ModVersionFile> m_installedBySha1; ///< 哈希 → 已安装版本
    QHash<QString, ModVersionFile> m_updateByPath;    ///< 文件路径 → 可更新到的版本
    QStringList m_pendingUpdatePaths;        ///< 待更新的数据包路径队列
    bool m_checkingUpdate = false;
    bool m_updatingPacks = false;
};

#endif // DATAPACKMANAGERDIALOG_H
