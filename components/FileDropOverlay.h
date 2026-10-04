/**
 * @file   FileDropOverlay.h
 * @brief  全屏拖拽覆盖层：拖入文件时显示半透明遮罩与文件预览
 * @author BlockBox Team
 * @date   2026-08-24
 */
#ifndef FILEDROPOVERLAY_H
#define FILEDROPOVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

/**
 * @brief 拖拽覆盖层
 *
 * 当用户从外部拖文件到主窗口时，覆盖全屏显示半透明遮罩，
 * 提示用户释放以导入，并显示文件数量和类型预览。
 * 该控件不处理实际拖放事件，仅做视觉反馈；
 * 实际 dropEvent 由 MainWindow 处理。
 */
class FileDropOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit FileDropOverlay(QWidget *parent = nullptr);

    /** 激活覆盖层，显示文件数量 */
    void activate(int fileCount);

    /** 更新文件类型预览文字 */
    void updatePreview(const QString &typeSummary);

    /** 停用覆盖层 */
    void deactivate();

    bool isActive() const { return m_active; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_iconLabel;
    QLabel *m_hintLabel;
    QLabel *m_countLabel;
    QVBoxLayout *m_layout;
    bool m_active = false;
};

#endif // FILEDROPOVERLAY_H
