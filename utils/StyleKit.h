/**
 * @file   StyleKit.h
 * @brief  集中式内联样式工厂 — 消灭跨文件复制的 QSS 字符串
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * StyleKit 收编了散落在各页面/组件中反复出现的内联样式原型
 * （主按钮、描边按钮、标题、弱化说明、输入框、卡片、状态胶囊等）。
 *
 * 使用规则：
 *  1. 给控件设置样式时，先在这里找原型，找不到才允许写一次性 QSS；
 *  2. 一次性 QSS 中禁止出现硬编码颜色，一律使用 @TOKEN@ 占位符并
 *     通过 StyleKit::resolve() 解析（与全局 style.qss 同一套令牌）；
 *  3. 所有返回值在"调用时"解析当前主题颜色，需要跟随主题色变更的
 *     控件请在 themeColorChanged 槽里重新调用相应函数并 setStyleSheet。
 *
 * 示例：
 *   btn->setStyleSheet(StyleKit::primaryButton());
 *   lbl->setStyleSheet(StyleKit::mutedLabel());
 *   w->setStyleSheet(StyleKit::resolve(
 *       "color: @WARNING_TEXT@; font-size: 12px;"));
 */
#ifndef STYLEKIT_H
#define STYLEKIT_H

#include <QString>

class QWidget;

namespace StyleKit
{
    /**
     * @brief 解析样式片段中的 @TOKEN@ 占位符（转发 ThemeManager::resolveTokens）
     * @param raw 含占位符的 QSS 片段
     * @return QString 替换完成后的 QSS
     */
    QString resolve(const QString& raw);

    /* ───────────────────────────── 文本 ───────────────────────────── */

    /** 页面主标题：18px 粗体，主文字色 */
    QString pageTitle();

    /** 分区标题：14px 粗体，主文字色 */
    QString sectionTitle();

    /** 卡片内标题：15px 粗体，透明背景无边框 */
    QString cardTitle();

    /** 正文：13px，主文字色 */
    QString bodyLabel();

    /** 次要说明：13px，次要文字色 */
    QString secondaryLabel();

    /** 弱化说明：12px，三级文字色，透明背景 */
    QString mutedLabel();

    /** 脚注/提示：11px，三级文字色，透明背景 */
    QString captionLabel();

    /** 成功语义文本：13px，@SUCCESS@ 色 */
    QString successLabel();

    /** 危险语义文本：13px，@DANGER@ 色 */
    QString dangerLabel();

    /* ───────────────────────────── 按钮 ───────────────────────────── */

    /** 主操作按钮：实心主题色、白字、含 hover/pressed/disabled 态 */
    QString primaryButton();

    /** 小型主操作按钮：实心主题色，13px 字号 + 紧凑内边距（跟随输入框的小按钮） */
    QString smallPrimaryButton();

    /** 迷你中性按钮：12px 字号、中性描边（全选/取消全选等工具性小按钮） */
    QString miniButton();

    /** 次操作按钮：主题色描边、透明底、含 hover/pressed/disabled 态 */
    QString outlineButton();

    /** 危险操作按钮：实心危险色、白字、含 hover 态 */
    QString dangerButton();

    /** 幽灵按钮：无边框透明底，仅文字，hover 出现浅底 */
    QString ghostButton();

    /* ───────────────────────────── 输入 ───────────────────────────── */

    /** 单行输入框：主题适配边框 + 聚焦主题色描边 */
    QString lineEdit();

    /** 多行输入框（QTextEdit）：与 lineEdit 同一套几何与配色 */
    QString textEdit();

    /** 复选框：仅统一字号，其余交给全局 QSS */
    QString checkBox();

    /* ───────────────────────────── 容器 ───────────────────────────── */

    /** 卡片容器：@BG_CARD@ 底 + @BORDER@ 描边 + 12px 圆角 */
    QString card();

    /** 无边框平卡片：@BG_CARD@ 底 + 12px 圆角，无描边 */
    QString flatCard();

    /** 完全透明容器（去掉全局 QSS 继承下来的底色/边框） */
    QString transparent();

    /**
     * @brief 状态胶囊（动态配色）
     * @param bg     背景色（如 @SUCCESS_BG@ 解析结果或任意运行时颜色）
     * @param fg     文字颜色
     * @param border 可选描边颜色，空则不描边
     * @return QString QSS（11px 字号 + 10px 圆角 + 3px 10px 内边距）
     */
    QString chip(const QString& bg, const QString& fg, const QString& border = QString());

    /**
     * @brief 色块预览按钮（色板/颜色选择器用）
     * @param color 当前色值（运行时动态）
     * @return QString QSS（带 1px 中性描边 + 6px 圆角）
     */
    QString colorSwatch(const QString& color);

    /* ─────────────────────────── 工具函数 ─────────────────────────── */

    /**
     * @brief 设置/更新控件的 class 动态属性并触发重新抛光
     *
     * 供基于全局 QSS 属性选择器（如 QPushButton[class~="btn-primary"]）
     * 的控件在运行时切换类使用；仅在构造期设置时直接 setProperty 即可。
     * @param w     目标控件
     * @param value 类名（多个类用空格分隔）
     */
    void setClass(QWidget* w, const QString& value);

    /**
     * @brief 搜索关键词高亮：把 text 中所有不区分大小写匹配 keyword 的
     *        片段包裹为主题绿色（@SUCCESS_BG@ 底 + @SUCCESS@ 字）的 span
     * @param text    原始纯文本
     * @param keyword 搜索关键词；为空或不匹配时仅返回 HTML 转义后的文本
     * @return 富文本 HTML（所有片段均经过转义），配合
     *         QLabel::setTextFormat(Qt::RichText) 使用
     */
    QString highlightKeyword(const QString& text, const QString& keyword);
}

#endif // STYLEKIT_H
