/**
 * @file   AuthlibInjector.h
 * @brief  第三方登录（Yggdrasil / authlib-injector）游戏内注入支持
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 第三方外置登录（方块皮肤站 / LittleSkin 等 Yggdrasil 兼容认证服务器）在启动器里
 * 能登录、能拿皮肤，但游戏进程本身并不知道这个认证服务器：只按离线身份进游戏时，
 * 第三方服务器会判定"未通过身份验证"直接拒绝进入，游戏内也没有皮肤。
 *
 * 解决办法与 HMCL / PCL2 一致：启动时把 authlib-injector 作为 javaagent 注入 JVM，
 * 并把 minecraft.api.* 指到该认证服务器，游戏内就会用这个服务器的身份校验、拉皮肤。
 * 本类负责三件事：
 *   1. 把 authlib-injector.jar 下载到本地（多源 + SHA-256 校验 + 版本标记）；
 *   2. 判断某个认证服务器地址能不能注入；
 *   3. 拼出注入用的 JVM 参数。
 *
 * 只依赖 QtCore / QtNetwork，方便被启动流程和测试探针直接复用。
 */
#ifndef AUTHLIBINJECTOR_H
#define AUTHLIBINJECTOR_H

#include <functional>

#include <QString>
#include <QStringList>

class AuthlibInjector
{
public:
    /** 下载/解压进度回调，参数是一句面向用户的中文说明（可为空） */
    using ProgressCallback = std::function<void(const QString &)>;

    /** 本地存放目录：<数据目录>/BlockBox/runtime/authlib-injector */
    static QString installDir();

    /** 本地 jar 路径：<installDir>/authlib-injector.jar */
    static QString jarPath();

    /** jar 是否已就绪（文件存在且不是空壳） */
    static bool isInstalled();

    /** 已安装的版本号（读版本标记文件，读不到时为空） */
    static QString installedVersion();

    /**
     * @brief 确保 authlib-injector.jar 已就绪
     *
     * jar 已存在时立即返回 true；否则依次尝试官方源、BMCLAPI 镜像、GitHub Release，
     * 下载完成后校验 SHA-256，再原子替换到目标位置并写版本标记。
     *
     * 这是阻塞调用（内部 QEventLoop，单源超时），只会在第一次用第三方账户启动时
     * 走一次；成功后本次运行期间不再检查。下载失败后会有冷却期，避免每次启动都
     * 卡在同一个网络问题上。
     *
     * @param errorOut 失败原因（面向用户的中文短句，调用方可直接展示）
     * @param progress 进度回调，可为空
     * @return jar 是否已就绪
     */
    static bool ensureInstalled(QString *errorOut = nullptr,
                                const ProgressCallback &progress = ProgressCallback());

    /** 最近一次失败原因（没失败过时为空） */
    static QString lastError();

    /** 清掉"下载失败"冷却，下次 ensureInstalled 会重新尝试（设置里点重试时用） */
    static void resetFailureCooldown();

    /**
     * @brief 这个地址能不能作为外置登录服务器注入
     *
     * 只接受 http / https 的 Yggdrasil API 根地址；Mojang 官方接口、空地址一律拒绝。
     */
    static bool supportsServer(const QString &serverUrl);

    /**
     * @brief 生成注入用的 JVM 参数
     *
     * 返回（serverUrl 会先补上结尾斜杠）：
     *   -javaagent:<jar>=<服务器根地址>
     *   -Dminecraft.api.env=custom
     *   -Dminecraft.api.auth.host=<根>/authserver
     *   -Dminecraft.api.account.host=<根>/sessionserver
     *   -Dminecraft.api.session.host=<根>/sessionserver
     *   -Dminecraft.api.services.host=<根>/minecraftservices
     *
     * jar 未就绪或地址不可注入时返回空列表（调用方据此退回离线身份）。
     */
    static QStringList jvmArgs(const QString &serverUrl);
};

#endif // AUTHLIBINJECTOR_H
