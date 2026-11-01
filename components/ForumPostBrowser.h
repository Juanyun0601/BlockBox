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
 *
 * 帖子正文用默认样式；个人简介这类短文本用 setCompactMode(true)：字号更小、
 * 段落更紧，并保留作者在输入框里按下的换行位置。
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

    /**
     * @brief 紧凑模式：个人简介、签名这类一两行的小段文字用
     *
     * 字号比帖子正文小一档、标题不放大、段落间距收紧；同时把单个换行当成
     * 硬换行（用户在哪里按回车就在哪里断行），避免多行简介被并成一段。
     */
    void setCompactMode(bool on);
    bool compactMode() const { return m_compact; }

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
    /** 用当前样式把 m_html 灌进文档（紧凑模式下再压一遍段落间距） */
    void applyDocument();
    /** 正文 CSS：帖子正文的默认样式 + 紧凑模式的补充规则 */
    QString documentStyle() const;
    /** 把每个段落的上 / 下外边距统一收成指定值（Qt 的 Markdown 默认间距偏松） */
    void tightenParagraphs(int top, int bottom);

    QString m_markdown;
    QString m_html;              ///< 渲染出的 HTML（图片到位后整体重设）
    QStringList m_imagePaths;    ///< 正文里出现的全部图片路径
    QSet<QString> m_resolvedImages; ///< 已经处理过（成功或失败）的图片，防止循环重排
    bool m_compact = false;      ///< 紧凑模式（简介 / 签名）
};
