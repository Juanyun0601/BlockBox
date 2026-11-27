/**
 * @file   AiMarkdownStyle.h
 * @brief  AI 回答正文的 Markdown 排版（标题 / 列表 / 代码 / 引用 / 表格 / 链接）
 * @author BlockBox Team
 *
 * AI 气泡原先直接把 Markdown 交给 QLabel 渲染，样式完全由 Qt 默认值决定：
 * 标题过大、代码块没有底色、行内代码只是换了等宽字体、段落和列表几乎贴在一起。
 * 这里把 Markdown 解析后逐块补齐排版，再输出富文本 HTML 交给 QLabel，正文的
 * 字号、行距、代码块底色、表格边框等就都受控了（样式表无法作用于 QLabel 内部文档）。
 */
#ifndef AIMARKDOWNSTYLE_H
#define AIMARKDOWNSTYLE_H

#include <QString>

class QLabel;

namespace AiMarkdownStyle
{

/// 把 Markdown 渲染成带内联样式的富文本 HTML（供 Qt::RichText 标签使用）
/// @param dark 深色主题下代码块/引用/表格换用深色底，避免出现亮白色块
/// @param compact 紧凑模式（实例助手 / 手机窄窗口）整体缩小一档字号
QString toRichText(const QString &markdown, bool dark, bool compact = false);

/// 把 Markdown 应用到标签：正文走富文本渲染，原始 Markdown / 纯文本存入属性
/// （bbAiMarkdown / bbAiPlain），供复制、引用等操作取回原文
void applyToLabel(QLabel *label, const QString &markdown, bool dark, bool compact = false);

/// 标签上保存的原始 Markdown；非 AI 正文标签返回空串
QString markdownOf(const QLabel *label);

/// 标签上保存的纯文本；非 AI 正文标签回退到 label->text()
QString plainTextOf(const QLabel *label);

} // namespace AiMarkdownStyle

#endif // AIMARKDOWNSTYLE_H



