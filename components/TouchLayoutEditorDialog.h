#ifndef TOUCHLAYOUTEDITORDIALOG_H
#define TOUCHLAYOUTEDITORDIALOG_H

#include <QPoint>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "AppDialogBase.h"
#include "utils/MobileControlSettings.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;

/**
 * @brief 键位布局预览画布
 * @author BlockBox Team
 *
 * 按「操作设置」悬浮层同样的比例画出当前触屏布局，手指 / 鼠标拖动即可
 * 改按钮位置；画布持机方式镜像、按钮大小都和真实游戏内一致，所见即所得。
 */
class TouchLayoutCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit TouchLayoutCanvas(QWidget *parent = nullptr);

    void setLayout(const QVector<MobileControlSettings::TouchControl> &layout);
    void setSelectedIndex(int index);
    int selectedIndex() const { return m_selected; }
    /// 预览是否按左手持机镜像（与悬浮层保持一致）
    void setMirror(bool mirror);

    QPointF controlCenter(const MobileControlSettings::TouchControl &control) const;
    qreal controlRadius(const MobileControlSettings::TouchControl &control) const;

signals:
    void selectionChanged(int index);
    void controlMoved(int index, qreal x, qreal y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    int hitControl(const QPointF &pos) const;
    void beginDrag(int index, const QPointF &pos);
    void dragTo(const QPointF &pos);
    void endDrag();

    QVector<MobileControlSettings::TouchControl> m_layout;
    bool m_mirror = false;
    int m_selected = -1;
    int m_dragIndex = -1;
    QPointF m_dragOffset;   ///< 按下点到控件中心的偏移，拖动时保持手感
    QPointF m_lastPos;
    int m_touchPointId = -1; ///< 正在拖画布的触点 id（多点时只取第一个）
};

/**
 * @brief 可视化电脑键盘选键控件
 * @author BlockBox Team
 *
 * 渲染一块电脑键盘（功能键行 + 主键区 + 底部方向键），点按即选中 / 取消，
 * 选中键位按主题色高亮；选中多个键就是组合键（按下时同时发出）。
 * 手机触屏和桌面鼠标都能点。
 */
class TouchKeyKeyboard : public QWidget
{
    Q_OBJECT

public:
    explicit TouchKeyKeyboard(QWidget *parent = nullptr);

    /// 当前高亮的键（canonical 键名集合，来自选中按钮的 comboKeys）
    void setSelectedKeys(const QStringList &keys);
    /// 是否允许点选（未选中按钮或选中摇杆时置灰）
    void setKeysEnabled(bool enabled);

signals:
    /** 点按后选中集合变化（canonical 键名，按点选顺序） */
    void selectionChanged(const QStringList &keys);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    /// 一个键位：显示名 + canonical 键名 + 相对宽度单位
    struct KeyCap
    {
        QString display;
        QString key;
        qreal units = 1.0;
        QRectF rect; // 布局时算出来
    };

    void buildKeyRows();
    void relayoutKeys();
    int hitKey(const QPointF &pos) const;
    void toggleKey(int index);

    QVector<QVector<KeyCap>> m_rows; ///< 每行一组键帽
    QStringList m_selectedKeys;
    bool m_keysEnabled = true;
};

/**
 * @brief 自定义触屏键位布局编辑器（操作设置 > 触屏 > 自定义键位）
 * @author BlockBox Team
 *
 * 左侧预览画布拖动改位置，右侧改标签、尺寸、透明度；底部渲染一块
 * 电脑键盘，点键即选、选中高亮，多选即组合键；可以添加 / 删除自己的
 * 按钮，保存后游戏内悬浮键位按新布局显示。
 */
class TouchLayoutEditorDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit TouchLayoutEditorDialog(QWidget *parent = nullptr);

private slots:
    void onSelectionChanged(int index);
    void onAddButton();
    void onDeleteButton();
    void onResetLayout();
    void onSave();
    void onKeyboardSelectionChanged(const QStringList &keys);
    void onClearKeys();

private:
    void refreshPropertyPanel();
    void refreshKeyboardSelection();
    void updateCanvas();
    void initStyle();

    TouchLayoutCanvas *m_canvas = nullptr;
    TouchKeyKeyboard *m_keyKeyboard = nullptr;
    QLabel *m_controlTitle = nullptr;
    QLabel *m_defaultHint = nullptr;
    QLineEdit *m_labelEdit = nullptr;
    QSlider *m_sizeSlider = nullptr;
    QLabel *m_sizeValue = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QPushButton *m_clearKeysBtn = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;

    QVector<MobileControlSettings::TouchControl> m_layout;
    int m_selected = -1;
    bool m_updatingPanel = false; ///< 属性面板回填时抑制信号反向改数据
};

/** 键名的显示名（Space → 空格，方向键 → 箭头），编辑器与键盘控件共用 */
QString touchKeyDisplayName(const QString &key);

#endif // TOUCHLAYOUTEDITORDIALOG_H
