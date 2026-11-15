/**
 * @file   ModRelationDialog.h
 * @brief  本地模组所属关系弹窗：依赖树 / 关系图 两种模式查看前置与被依赖关系
 * @author BlockBox Team
 * @date   2026-11-14
 */
#ifndef MODRELATIONDIALOG_H
#define MODRELATIONDIALOG_H

#include <QColor>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include "components/AppDialogBase.h"
#include "utils/mod/ModData.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QPushButton;

/**
 * @brief 关系图自绘控件：前置依赖（左）→ 当前模组（中）→ 被依赖模组（右）三列布局
 *
 * 节点为圆角卡片，连线带箭头；未安装的前置用虚线框灰显。
 * 鼠标悬停显示提示，点击已安装节点可切换当前查看的模组。
 * 颜色取自控件调色板（自适应深浅主题）+ 主题色。
 */
class ModRelationGraphWidget : public QWidget
{
    Q_OBJECT

public:
    struct GraphNode
    {
        QString title;       // 主标题（模组名或 modId）
        QString subtitle;    // 副标题（版本 / 未安装 等）
        bool installed = true;   // 是否本地已安装
        bool required = true;    // 是否必需前置（建议依赖灰显）
        int modIndex = -1;       // 本地模组索引（未安装为 -1）
        QRectF rect;             // 布局时回填，用于命中测试
    };

    explicit ModRelationGraphWidget(QWidget *parent = nullptr);

    /** 填充三列节点并重排布局 */
    void setGraph(const QList<GraphNode> &deps,
                  const GraphNode &center,
                  const QList<GraphNode> &dependents);

    QSize minimumSizeHint() const override;

signals:
    /** 点击已安装节点，modIndex 为本地模组索引 */
    void nodeClicked(int modIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void relayout();
    /** 命中测试：返回节点所在的列（0=前置 1=当前 2=被依赖），-1 表示未命中 */
    int hitNode(const QPointF &pos, int *indexInColumn = nullptr) const;
    void drawNode(QPainter &p, const GraphNode &node, bool highlighted) const;
    void drawEdge(QPainter &p, const QPointF &from, const QPointF &to,
                  bool highlighted, bool dashed) const;

    QList<GraphNode> m_deps;        // 左列：前置依赖
    GraphNode m_center;             // 中列：当前模组
    QList<GraphNode> m_dependents;  // 右列：被依赖模组
    bool m_hasCenter = false;
    int m_hoverCol = -1;            // 悬停所在列
    int m_hoverIdx = -1;            // 悬停节点在列内的下标
    int m_pressedCol = -1;          // 按下时的列（点击判定用）
    int m_pressedIdx = -1;
};

/**
 * @brief 模组所属关系弹窗（居中卡片，AppDialogBase）
 *
 * 左侧为当前实例的模组列表（带搜索框），右侧按模式显示：
 *  - 依赖树：两棵树——「前置依赖」（可逐级展开查看前置的前置，带已安装/未安装状态）
 *            与「被这些模组依赖」
 *  - 关系图：三列节点图（前置 | 当前模组 | 被依赖），点节点可跳转查看
 * 底部状态栏汇总前置/缺失/被依赖数量。
 */
class ModRelationDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit ModRelationDialog(const QList<ModInfo> &mods, QWidget *parent = nullptr);

private slots:
    void onModSelected(int index);
    void onSearchChanged(const QString &text);
    void onGraphNodeClicked(int modIndex);

private:
    void initUI();
    void rebuildModList();
    void rebuildTree();
    void rebuildGraph();
    void updateSummary();
    /** modId / 文件名（去 .jar）→ 本地模组索引；未收录返回 -1 */
    int findLocalIndex(const QString &depName) const;
    QString modDisplayKey(const ModInfo &info) const;
    /** 依赖树递归展开：把 owner 的前置追加到 parent 下（限深 + 防环） */
    void addDepTreeItems(QTreeWidgetItem *parent, const ModInfo &owner, int depth);

    QList<ModInfo> m_mods;
    QMap<QString, int> m_indexByKey;  // modId / 文件名小写 → m_mods 下标
    int m_currentIndex = -1;
    QSet<QString> m_treeVisited;      // 依赖树当前展开路径（防环）

    // 左侧模组列表
    QLineEdit *m_searchEdit;
    QListWidget *m_modListWidget;

    // 模式切换与右侧内容
    QPushButton *m_treeModeBtn;
    QPushButton *m_graphModeBtn;
    QStackedWidget *m_modeStack;
    QTreeWidget *m_treeWidget;
    ModRelationGraphWidget *m_graphWidget;

    QLabel *m_summaryLabel;
};

#endif // MODRELATIONDIALOG_H
