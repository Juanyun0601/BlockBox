/**
 * @file   QrCode.h
 * @brief  二维码编解码（电脑端生成登录二维码 / 手机端扫码识别）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 只解决一件事：把一小段文本（登录二维码里的地址）画成二维码，以及在手机端
 * 从相机取景图 / 相册图片里把这段文本读回来。为了不额外引第三方库，编码器和
 * 解码器都在本工程内实现：
 *   - 编码：字节模式，纠错等级 M（放不下时退到 L），覆盖版本 1-10；
 *   - 解码：自适应二值化 → 定位三个回字 → 透视校正 → 网格采样 → 格式信息纠错 →
 *           去掩码 → 分块 → 里德-所罗门纠错 → 按模式取回文本（版本 1-10，L / M）。
 * 超出上面范围的二维码会返回空串，调用方按「不是登录二维码」提示即可。
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

namespace QrCode
{
/** 二维码点阵：true = 深色模块；不含四周静区 */
using Matrix = QVector<QVector<bool>>;

/**
 * @brief 把文本编码成二维码点阵
 * @param text  要编码的文本（按 UTF-8 字节编码，最长约 250 字节）
 * @param error 失败原因（可为空），例如「内容太长，放不进二维码」
 * @return 边长 = 4 * 版本 + 17 的方阵；失败时返回空矩阵
 */
Matrix encodeMatrix(const QString &text, QString *error = nullptr);

/**
 * @brief 编码并渲染成图片
 * @param text   要编码的文本
 * @param sizePx 期望的图片边长（像素）；四周额外留 4 个模块的静区
 * @param dark   深色模块颜色
 * @param light  浅色背景颜色
 * @param error  失败原因（可为空）
 * @return 白底黑块的 RGB32 图片；失败时返回空图片
 */
QImage render(const QString &text, int sizePx, const QColor &dark = QColor(0, 0, 0),
              const QColor &light = QColor(0xFF, 0xFF, 0xFF), QString *error = nullptr);

/**
 * @brief 二维码的外观参数：圆角模块 + 品牌渐变 + 中心方块徽标
 *
 * 让登录二维码看起来像方块盒子自己的东西，而不是一张黑白方块图。
 * 只改外观，码里的内容一个字节都不变；圆角和徽标都控制得比较克制，
 * 保证手机相机在各种光线下依然能读出来。
 */
struct Style
{
    QColor light = QColor(0xFF, 0xFF, 0xFF); ///< 底色（必须是浅色，扫码硬要求）
    QColor dark = QColor(0, 0, 0);           ///< 主色（渐变起点）
    QColor dark2;                            ///< 渐变终点；无效色表示纯色
    qreal moduleRadius = 0.34;               ///< 单个模块的圆角（占模块边长比例，0 = 方角）
    qreal eyeRadius = 0.34;                  ///< 三个「回」字定位图形的圆角比例
    int emblemModules = 0;                   ///< 中心方块徽标占用的模块边长（奇数，0 = 不加）
};

/**
 * @brief 按 Style 渲染一张「有个性」的二维码
 *
 * 比模块还小的时候（太小的图）会自动退回方角、去掉徽标，避免为了好看牺牲识别率。
 */
QImage renderStyled(const QString &text, int sizePx, const Style &style, QString *error = nullptr);

/**
 * @brief 从图片里找到二维码并读出文本
 *
 * 图片可以带背景、倾斜、缩放、模糊；识别不出来时返回空串（不抛异常）。
 */
QString decode(const QImage &image);

/** 从图片文件解码（相册选图 / 截图都走这里） */
QString decodeFile(const QString &path);

/** 文字里是否像一张方块盒子扫码登录码（生成方与识别方共用的判断，避免误识别别的二维码） */
bool isLoginPayload(const QString &text);

/** 从扫码登录码里取出票据字符串；不是登录码时返回空串 */
QString ticketFromPayload(const QString &text);

/** 拼一张扫码登录码的文本（电脑端编码用，手机端解析用，两边必须一致） */
QString makeLoginPayload(const QString &baseUrl, const QString &ticket);

} // namespace QrCode
