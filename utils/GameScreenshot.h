/**
 * @file   GameScreenshot.h
 * @brief  抓取游戏画面（供 AI 助手识图聊天使用）
 * @author BlockBox Team
 * @date   2026-11-21
 */

#pragma once

#include <QImage>
#include <QList>
#include <QString>

class QWidget;

/**
 * @brief 游戏画面截图工具
 *
 * 实例助手是浮在游戏上的小窗，直接截图会把助手自己也拍进去，所以流程是：
 * 先把调用方窗口临时藏起来（让游戏画面露出来）→ 抓屏 → 再把窗口恢复原样。
 * 之所以抓屏幕而不是直接抓窗口内容：Minecraft 用 OpenGL 渲染，
 * PrintWindow / BitBlt 这类「抓窗口自身」的做法在它身上基本只能拿到黑图。
 *
 * Windows 上会先定位 javaw.exe 的游戏窗口，只截它覆盖的那块屏幕；
 * 其它平台或没找到游戏窗口时退回整屏截图（此时 windowTitle 为空）。
 */
class GameScreenshot
{
public:
    /**
     * @brief 一次截图的结果
     */
    struct Result
    {
        QImage image;        ///< 截到的画面（失败时为空）
        QString windowTitle; ///< 截到的游戏窗口标题（空 = 整屏，用于提示用户）
        QString error;       ///< 失败原因（中文，可直接展示给用户）
    };

    /**
     * @brief 抓取当前游戏画面（同步，约 200ms）
     * @param windowsToHide 截图期间需要临时隐藏的窗口（调用方顶层窗口、手机端输入弹窗等）
     * @return 截图结果
     *
     * 窗口隐藏后会给系统约 200ms 时间把游戏画面重绘出来再抓屏，
     * 结束后无论成功失败都会把窗口恢复成隐藏前的样子。
     */
    static Result capture(const QList<QWidget *> &windowsToHide = {});

    /**
     * @brief 把截图压成可直接塞进请求的 JPEG data URL
     * @param image    原始图像
     * @param outBytes 可选输出：压缩后的 JPEG 原始字节（便于同时落盘缓存）
     * @return data:image/jpeg;base64,...（失败返回空字符串）
     *
     * 最长边超过 1280 时等比缩小，并在体积过大时逐级降质，
     * 保证单张图转 base64 后不会把请求撑得过大。
     */
    static QString toJpegDataUrl(const QImage &image, QByteArray *outBytes = nullptr);
};
