/**
 * @file   ProjectionMaterialDialog.h
 * @brief  投影材料清单弹窗（只读统计整份投影用到的方块与数量）
 * @author BlockBox Team
 * @date   2026-11-21
 *
 * 从论坛「投影帖详情页」的「材料列表」按钮进来：不用先把投影存到电脑，
 * 弹窗会先把文件取回本机缓存（和「投影预览」共用同一份缓存），再在后台
 * 线程数一遍每个方块在整份投影里出现多少次，列成一张可筛选、可复制的清单。
 *
 * 只做统计，不改文件：
 *   - .litematic 直接读，.schematic / .schem 先转成临时 .litematic 再读
 *   - 清单里优先显示「中文名 (英文ID)」，能取到实例贴图时还带上小图标
 *   - 顶部可输入名字或英文 ID 就地筛选，底部一键把整份清单复制到剪贴板
 */

#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "AppDialogBase.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTreeWidget;

/**
 * @brief 材料清单里的一行（方块 ID + 整份投影中的数量）
 */
struct ProjectionMaterialEntry
{
    QString blockId;   ///< 方块 ID，如 "minecraft:oak_log"
    int count = 0;     ///< 整份投影中该方块的总数（不含空气）
};

/**
 * @brief 投影材料清单弹窗
 */
class ProjectionMaterialDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开材料清单（阻塞到用户关闭）
     * @param parent       父窗口（弹窗铺在它上面）
     * @param filePath     投影文件绝对路径（.litematic / .schematic / .schem）
     * @param instancePath 可选：方块中文名与图标的来源实例，空则只显示英文 ID
     * @param displayName  可选：标题里显示的名称，空则取文件名
     */
    static void open(QWidget *parent, const QString &filePath,
                     const QString &instancePath = QString(),
                     const QString &displayName = QString());

    explicit ProjectionMaterialDialog(QWidget *parent, const QString &filePath,
                                      const QString &instancePath = QString(),
                                      const QString &displayName = QString());
    ~ProjectionMaterialDialog() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    /** 后台统计结果：失败时 entries 为空、error 说明原因 */
    struct LoadResult
    {
        QList<ProjectionMaterialEntry> entries;
        int totalBlocks = 0;   ///< 非空气方块总数（清单各项之和）
        QString error;
    };

    /**
     * @brief 数一份投影的材料（工作线程执行，不碰界面）
     *
     * 需要时先把 .schematic / .schem 转成临时 .litematic，读完立即删掉临时文件。
     */
    static LoadResult loadMaterials(const QString &filePath);

    void initUI();
    /** 启动后台统计 */
    void startLoad();
    /** 统计完成：填表、写汇总，或显示失败原因 */
    void applyResult(const LoadResult &result);
    /** 按当前条目重建表格（应用筛选词） */
    void fillTree();
    /** 按筛选词把表格行显示 / 隐藏 */
    void applyFilter(const QString &keyword);
    /** 把整份清单复制到剪贴板 */
    void copyList();
    /** 清单里的显示文案：优先「中文名 (英文ID)」，没有中文名就只留英文 ID */
    QString displayNameFor(const QString &blockId) const;

    QString m_filePath;
    QString m_instancePath;
    QString m_displayName;

    bool m_loadStarted = false;
    bool m_loaded = false;
    QList<ProjectionMaterialEntry> m_entries;
    int m_totalBlocks = 0;

    /** 方块中文名（小写 "ns:id" -> 中文名），取不到时为空 */
    QHash<QString, QString> m_blockCnMap;

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    /** 底部说明的默认文案（筛选 / 复制时临时改成提示，清空后回落到它） */
    QString m_defaultHint;
};
