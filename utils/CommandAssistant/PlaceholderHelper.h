/**
 * @file   PlaceholderHelper.h
 * @brief  快捷指令占位符（<xxx>）解析工具
 * @author BlockBox Team
 * @date   2026-09-13
 *
 * 快捷指令支持在中文输入中用尖括号标记"待填参数"（占位符），例如：
 *   给予 @自己 <钻石块> <2>
 * 其中 <钻石块> 表示此处需填一个方块（尖括号内的文字是当前提示值），
 * <2> 表示此处需填一个数字。尖括号本身不进入最终英文指令——
 * 转换器会把占位符剥离后按参数类型解析（方块经注册表转为英文 ID，
 * 数字直接校验），确保输出如 "/give @s minecraft:diamond_block 2"。
 *
 * 语法约定：
 *   - 占位符必须在一对尖括号之间，如 <钻石块>；内部文字去除首尾空白
 *   - 一个占位符对应一个空格分隔的词（内部不能含空格，坐标除外：
 *     pos 参数的三个词由转换器整体消费，不属于本工具管理范围）
 *   - 未闭合的 '<' 不是占位符，原样保留
 */

#pragma once

#include <QList>
#include <QString>

/**
 * @brief 输入文本中的一个占位符词元
 */
struct PlaceholderToken
{
    int start;    ///< 词元起始下标（含 '<'），闭区间起点
    int end;      ///< 词元结束下标（含 '>'）+1，即 Qt 风格右开边界
    QString inner; ///< 尖括号内的提示值（已去除首尾空白）
};

/**
 * @brief 占位符解析工具（纯静态方法）
 */
class PlaceholderHelper
{
public:
    /**
     * @brief 解析文本中的全部占位符（按出现顺序）
     * @param text 输入文本
     * @return 占位符词元列表；未闭合的 '<' 及空占位符（<>）不收录
     */
    static QList<PlaceholderToken> parse(const QString &text);

    /**
     * @brief 判断一个词是否为占位符（同时以 '<' 开头、'>' 结尾）
     * @param value 单个词（不含空格）
     */
    static bool isPlaceholder(const QString &value);

    /**
     * @brief 剥离占位符尖括号，返回内部提示值
     *
     * 非占位符原样返回（仅去除首尾空白）；"<钻石块>" 返回 "钻石块"。
     *
     * @param value 单个词
     */
    static QString stripWrappers(const QString &value);

    /**
     * @brief 将提示值包装为占位符词（"<" + inner + ">"）
     */
    static QString wrap(const QString &inner);

    /**
     * @brief 将文本中 [start, end) 区间的词元替换为新的占位符
     * @param text     原文本
     * @param start    词元起始下标
     * @param end      词元结束下标（右开）
     * @param newInner 新的内部提示值
     * @return 替换后的文本
     */
    static QString replaceToken(const QString &text, int start, int end, const QString &newInner);
};
