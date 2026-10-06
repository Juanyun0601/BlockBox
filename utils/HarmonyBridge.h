/**
 * @file   HarmonyBridge.h
 * @brief  鸿蒙运行时桥接 — 通过 Qt OHOS 移植层在 JS 线程调用系统 API
 * @author BlockBox Team
 * @date   2026-10-05
 *
 * 仅在 Q_OS_HARMONY（Qt for OpenHarmony）下有真实实现：
 * 借助 QtCore 私有接口 QOhosJsThreadGateway 切到 ArkTS JS 线程，
 * napi_load_module 加载 @ohos.bundle.installer 并发起 HAP 安装。
 * 其他平台接口安全返回失败，调用方无需关心平台差异。
 *
 * 注意：install() 需要 ohos.permission.INSTALL_BUNDLE（system_basic），
 * 由打包脚本注入 module.json5；在采用自定义签名并授予 ACL 的
 * OpenHarmony 设备上可成功，商用 HarmonyOS NEXT 将返回权限错误，
 * 调用方需提供 hdc 安装命令兜底。
 */
#ifndef HARMONYBRIDGE_H
#define HARMONYBRIDGE_H

#include <QObject>
#include <QString>

class HarmonyBridge : public QObject
{
    Q_OBJECT

public:
    static HarmonyBridge *instance();

    /** 发起 HAP 安装（异步）；结果通过 installFinished 返回 */
    void installHap(const QString &hapPath);

signals:
    void installFinished(bool success, const QString &error);
};

#endif // HARMONYBRIDGE_H
