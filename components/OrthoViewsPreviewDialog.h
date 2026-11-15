/**
 * @file   OrthoViewsPreviewDialog.h
 * @brief  投影三视图预览与保存对话框
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 展示 SchematicOrthoViewRenderer 生成的正视图 / 侧视图 / 俯视图三张图，
 * 确认效果后可一次保存为三张 PNG（文件名带视图名）；直接关窗则不落盘。
 */
#pragma once

#include <QDialog>
#include <QImage>
#include <QString>

class QLabel;

class OrthoViewsPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param parent    父窗口
     * @param front     正视图
     * @param left      侧视图
     * @param top       俯视图
     * @param baseName  保存文件名前缀（一般取投影文件名）
     */
    OrthoViewsPreviewDialog(QWidget *parent, const QImage &front, const QImage &left,
                            const QImage &top, const QString &baseName);

private slots:
    void onSaveClicked();

private:
    /** 预览列：标题 + 等比缩放的图片标签 */
    QWidget *makeViewCard(const QString &title, const QImage &image);

    QImage m_front;
    QImage m_left;
    QImage m_top;
    QString m_baseName;
    QLabel *m_hintLabel = nullptr;
};
