/**
 * @file   HarmonyBridge.cpp
 * @brief  鸿蒙运行时桥接实现
 * @author BlockBox Team
 * @date   2026-10-05
 */
#include "utils/HarmonyBridge.h"

#include <QCoreApplication>
#include <QMetaObject>

#ifdef Q_OS_HARMONY
// Qt for OpenHarmony 的 JS 线程通道与 NAPI 封装（QtCore 私有接口）。
// 先单独引入 <napi.h>：此时 NAPI_VERSION 取 SDK 兼容值（8），
// 绕开 qt-ohos 自带 node-addon-api 中引用的本 SDK 未提供的
// 实验性符号（node_api_create_syntax_error 等）；
// 随后 qnapi_p.h 的 include 因头文件守卫不再重复展开。
#include <napi.h>
#include <napi/native_api.h>
#include <QtCore/private/qcore_ohos_p.h>
#endif

HarmonyBridge *HarmonyBridge::instance()
{
    static HarmonyBridge s_instance;
    return &s_instance;
}

#ifdef Q_OS_HARMONY

namespace {

// 从 JS 线程把安装结果投递回 Qt 主线程
void notifyInstallResultFromJsThread(bool success, const QString &error)
{
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [success, error]() {
            emit HarmonyBridge::instance()->installFinished(success, error);
        },
        Qt::QueuedConnection);
}

} // namespace

void HarmonyBridge::installHap(const QString &hapPath)
{
    const std::string path = hapPath.toStdString();

    // 切到 ArkTS JS 线程执行（N-API 只能在创建 env 的线程使用）
    QOhosJsThreadGateway::invoke([path](QOhosJsState &js) {
        try {
            Napi::Env env(js.env());
            Napi::HandleScope scope(env);

            // 加载系统包管理安装模块（等价于 ETS 侧 import installer from '@ohos.bundle.installer'）
            napi_value moduleValue = nullptr;
            const napi_status status =
                napi_load_module(js.env(), "@ohos.bundle.installer", &moduleValue);
            if (status != napi_ok || moduleValue == nullptr) {
                notifyInstallResultFromJsThread(
                    false, QObject::tr("无法加载系统包管理安装模块 (status=%1)")
                               .arg(static_cast<int>(status)));
                return;
            }
            Napi::Object installer(env, moduleValue);

            Napi::Value installValue = installer.Get("install");
            if (!installValue.IsFunction()) {
                notifyInstallResultFromJsThread(
                    false, QObject::tr("系统包管理安装模块缺少 install 接口"));
                return;
            }
            Napi::Function installFn = installValue.As<Napi::Function>();

            // installer.install([hapPath], (err) => { ... })
            Napi::Array paths = Napi::Array::New(env, 1);
            paths.Set(0u, Napi::String::New(env, path));
            auto callback = Napi::Function::New(
                env,
                [](const Napi::CallbackInfo &info) -> Napi::Value {
                    // 回调约定：err 为 undefined/null 表示成功
                    const bool ok = info[0].IsUndefined() || info[0].IsNull();
                    QString error;
                    if (!ok && info[0].IsObject()) {
                        Napi::Object errObj = info[0].As<Napi::Object>();
                        Napi::Value message = errObj.Get("message");
                        if (message.IsString())
                            error = QString::fromStdString(
                                message.As<Napi::String>().Utf8Value());
                    }
                    notifyInstallResultFromJsThread(ok, error);
                    return info.Env().Undefined();
                });

            installFn.Call(installer, {paths, callback});

            // Call 走的是 Promise/回调异步路径，发起成功即返回；
            // 若宿主运行时抛了同步异常（如权限拒绝），在此捕获上报
            if (env.IsExceptionPending()) {
                Napi::Error pending = env.GetAndClearPendingException();
                notifyInstallResultFromJsThread(
                    false, QString::fromStdString(pending.Message()));
            }
        } catch (const Napi::Error &e) {
            notifyInstallResultFromJsThread(
                false, QString::fromStdString(e.Message()));
        } catch (const std::exception &e) {
            notifyInstallResultFromJsThread(false, QString::fromUtf8(e.what()));
        }
    });
}

#else // 非鸿蒙平台：安全桩

void HarmonyBridge::installHap(const QString &hapPath)
{
    Q_UNUSED(hapPath);
    emit installFinished(false, tr("当前平台不支持应用内安装"));
}

#endif // Q_OS_HARMONY
