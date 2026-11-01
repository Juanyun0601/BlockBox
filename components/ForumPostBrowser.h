/**
 * @file   ForumPostBrowser.h
 * @brief  论坛帖子正文的 Markdown 渲染控件
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 把帖子 / 楼层的 Markdown 正文渲染成富文本：
 *   · 站内图片（/uploads/...）自动下载并嵌入正文，加载完成后自动重排；
 *   · 图片包了链接，点击弹出大图预览；
 *   · 站内链接与外部链接都通过 linkClicked 交给上层处理
 *     （论坛内链转成启动器内跳转，外部链接才开系统浏览器）。
 * 高度自动跟随内容，由外层页面滚动区统一滚动。
 */
#pragma once

#include "AutoHeightTextBrowser.h"

#include <QSet>
#include <QStringList>

class ForumPostBrowser : public AutoHeightTextBrowser
{
    Q_OBJECT

public:
    explicit ForumPostBrowser(QWidget *parent = nullptr);

    /** 渲染一段 Markdown 正文（重复调用会整体替换） */
    void setPostMarkdown(const QString &markdown);

    /** 弹出大图预览（path 为站内相对路径或完整地址；未下载时会先取图） */
    static void showImagePreview(const QString &path, QWidget *parent = nullptr);

signals:
    /** 正文里的链接被点击（含图片），由上层决定内跳还是开浏览器 */
    void linkClicked(const QUrl &url);

    /** 点击了正文图片，path 是站内相对路径（/uploads/...）或完整地址 */
    void imageClicked(const QString &path);

protected:
    void changeEvent(QEvent *event) override;

private:
    void renderDocument();
    void requestPendingImages();

    QString m_markdown;
    QString m_html;              ///< 渲染出的 HTML（图片到位后整体重设）
    QStringList m_imagePaths;    ///< 正文里出现的全部图片路径
    QSet<QString> m_resolvedImages; ///< 已经处理过（成功或失败）的图片，防止循环重排
};
