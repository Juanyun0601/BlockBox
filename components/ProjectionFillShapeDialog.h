/**
 * @file   ProjectionFillShapeDialog.h
 * @brief  投影编辑器「填充方块」的形状选择弹窗（内嵌居中样式）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 投影编辑器侧栏的「填充方块」按钮点开后弹出本弹窗：先选一个形状
 * （实心 / 空心 / 框架 / 倾斜地板 / 方形 / 三角形 / 圆形 / 圆柱），
 * 再返回调用方去 3D 视图里点两个对角方块生成。
 *
 * 基于 AppDialogBase：全屏暗色模糊遮罩 + 居中圆角卡片，与 AppMessageBox
 * 等内嵌弹窗观感一致。构造函数只建 UI、不 exec()，由调用方 exec() 后
 * 读 mode() / selectedShape() / airOnly() 决定下一步。
 *
 * 用法：
 *   ProjectionFillShapeDialog dlg(this);
 *   if (dlg.exec() == QDialog::Accepted) {
 *       switch (dlg.mode()) {
 *       case ProjectionFillShapeDialog::Mode::PickTwoPoints: ... // 等用户点两个对角方块
 *       case ProjectionFillShapeDialog::Mode::Coordinates:   ... // 弹坐标输入
 *       case ProjectionFillShapeDialog::Mode::Cancel:        break;
 *       }
 *   }
 */

#pragma once

#include "components/AppDialogBase.h"
#include "utils/Schematic/ProjectionFillShape.h"

class QButtonGroup;
class QCheckBox;
class QColor;
class QIcon;
class QKeyEvent;
class QLabel;
class QPushButton;
class QToolButton;

/**
 * @brief 「填充方块」形状选择弹窗
 */
class ProjectionFillShapeDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /// 用户的选择结果：取消 / 开始选点 / 按坐标填充
    enum class Mode
    {
        Cancel,        ///< 取消、遮罩点击或 Esc
        PickTwoPoints, ///< 「开始选点」：调用方进入两点拾取
        Coordinates    ///< 「按坐标填充…」：调用方进入坐标输入
    };

    /**
     * @brief 构造函数：只搭建界面，不 exec()，也不阻塞
     * @param parent 父窗口（AppDialogBase 用它定位遮罩与居中卡片）
     */
    explicit ProjectionFillShapeDialog(QWidget *parent = nullptr);

    /// 当前选中的形状（默认实心）
    ProjectionFillShape selectedShape() const;

    /// 用户点的哪个出口（默认 Cancel）
    Mode mode() const { return m_mode; }

    /// 是否勾选「只填充空气（不覆盖已有方块）」（默认不勾选）
    bool airOnly() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void initUI();
    void initStyle(QWidget *card);
    /// 跟随选中形状刷新网格下方的动态说明
    void updateDescription();
    /// 记录出口并 accept()
    void commit(Mode mode);

    /// 生成一个形状卡片按钮（checkable、加入互斥组、手绘剪影图标）
    QToolButton *makeShapeButton(ProjectionFillShape shape, QWidget *parent);
    /// 生成形状剪影图标：Off = 次要文字色，On = 主题色
    QIcon makeShapeIcon(ProjectionFillShape shape) const;
    /// 在 32×32 设计空间里手绘形状剪影（颜色全部来自 StyleKit 令牌）
    QPixmap shapePixmap(ProjectionFillShape shape, int px, const QColor &ink) const;
    /// 顶部标题左侧的等轴方块小图标
    QPixmap makeHeaderPixmap() const;

    Mode m_mode = Mode::Cancel;
    QButtonGroup *m_shapeGroup = nullptr;
    /// 按 ProjectionFillShape 的下标保存按钮，便于回读选中项
    QToolButton *m_shapeButtons[static_cast<int>(ProjectionFillShape::Count)] = {};
    QLabel *m_descLabel = nullptr;
    QCheckBox *m_airBox = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_coordBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
};
