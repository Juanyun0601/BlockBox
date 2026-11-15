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
 *   · 教程增强语法：::: fold 折叠块（点标题展开 / 收起）、```mindmap
 *     思维导图（缩进树）、B 站视频链接自动变成播放卡片；一 / 二 / 三级
 *     标题自动带锚点，takeToc() 取出的目录可用来做章节跳转。
 * 高度自动跟随内容，由外层页面滚动区统一滚动。
 *
 * 帖子正文用默认样式；个人简介这类短文本用 setCompactMode(true)：字号更小、
 * 段落更紧，并保留作者在输入框里按下的换行位置。
 */
#pragma once

#include "AutoHeightTextBrowser.h"

#include <QSet>
#include <QStringList>
#include <QVector>

/** 教程目录条目：按正文中的一 / 二 / 三级标题自动生成（与网页版同一口径） */
struct ForumTocEntry
{
    int level = 1;      ///< 标题级别 1~3
    QString anchor;     ///< 文档内锚点名（sec-N），anchorY() 用它定位
    QString text;       ///< 纯文本标题
};

class ForumPostBrowser : public AutoHeightTextBrowser
{
    Q_OBJECT

public:
    explicit ForumPostBrowser(QWidget *parent = nullptr);

    /** 渲染一段 Markdown 正文（重复调用会整体替换） */
    void setPostMarkdown(const QString &markdown);

    /** 最近一次渲染生成的目录（标题为空或没有标题时为空列表） */
    const QVector<ForumTocEntry> &toc() const { return m_toc; }

    /**
     * @brief 锚点在正文里的 y 坐标（相对本控件），找不到返回 -1
     *
     * 本控件高度贴合内容、没有内部滚动条，页面滚动区拿这个值
     * 加上控件在内容里的偏移，就能滚到对应章节。
     */
    int anchorY(const QString &anchor) const;

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
    /** 一段普通 Markdown → HTML（目录锚点 / B 站视频卡在这一步处理） */
    QString renderMarkdownHtml(const QString &markdown, int &headingSeq);
    void requestPendingImages();
    /** 用当前样式把 m_html 灌进文档（紧凑模式下再压一遍段落间距） */
    void applyDocument();
    /** 正文 CSS：帖子正文的默认样式 + 紧凑模式的补充规则 */
    QString documentStyle() const;
    /** 把每个段落的上 / 下外边距统一收成指定值（Qt 的 Markdown 默认间距偏松） */
    void tightenParagraphs(int top, int bottom);
    /**
     * @brief 帖子正文的段落节奏：段落收紧、列表更紧、代码行贴齐
     *
     * 只调整块间距（不动行高），让正文读起来紧凑但不拥挤；
     * 代码块里的每一行原样挨着，不再被当段落拉开。
     */
    void tuneBodyRhythm();

    QString m_markdown;
    QString m_html;              ///< 渲染出的 HTML（图片到位后整体重设）
    QStringList m_imagePaths;    ///< 正文里出现的全部图片路径
    QSet<QString> m_resolvedImages; ///< 已经处理过（成功或失败）的图片，防止循环重排
    bool m_compact = false;      ///< 紧凑模式（简介 / 签名）
    QVector<ForumTocEntry> m_toc;    ///< 最近一次渲染生成的目录
    QVector<bool> m_foldOpen;    ///< 折叠块的展开状态（按下标记，false = 收起）
};
