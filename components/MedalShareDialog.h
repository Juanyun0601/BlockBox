/**
 * @file   MedalShareDialog.h
 * @brief  勋章分享图预览弹窗：看清楚再决定保存、复制还是直接分享
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 账户页「勋章」里点「生成分享图」后弹出本窗口。图片本身由
 * MedalShareCard 渲染，这里只负责预览和三个出口：
 *   - 保存图片：存成 PNG（默认落在图片文件夹）
 *   - 复制图片：进剪贴板，可以直接粘到 QQ / 微信 / 论坛
 *   - 分享…（仅安卓）：走系统分享面板发给别的应用
 */
#pragma once

#include <QImage>
#include <QString>

#include "AppDialogBase.h"

class QLabel;
class QPushButton;

class MedalShareDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开分享图预览
     * @param poster          MedalShareCard::buildPoster 画好的图
     * @param suggestedName   保存对话框里默认的文件名（不含目录）
     */
    static void show(QWidget *parent, const QImage &poster, const QString &suggestedName);

    explicit MedalShareDialog(const QImage &poster, const QString &suggestedName,
                              QWidget *parent = nullptr);

protected:
    /** 点预览图直接复制：比来回找按钮顺手 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void initStyle();
    /** 把海报缩到弹窗里能放下的尺寸，并切圆角 */
    void refreshPreview();
    void saveImage();
    void copyImage();
    void shareImage();

    QImage m_poster;
    QString m_suggestedName;
    QLabel *m_preview = nullptr;
    QLabel *m_sizeHint = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_shareButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
