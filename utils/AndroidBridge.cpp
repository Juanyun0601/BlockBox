/**
 * @file   AndroidBridge.cpp
 * @brief  AndroidBridge 实现 — Android 走 QJniObject Intent，桌面为空实现
 */
#include "AndroidBridge.h"

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QDebug>
#include <QJniObject>
#include <QJniEnvironment>

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr auto kBedrockPackage = "com.mojang.minecraftpe"_L1;
constexpr auto kAuthority = "com.blockbox.launcher.fileprovider"_L1;

QJniObject activityContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

/// 组装并启动 Intent；失败时清理挂起的 JNI 异常
bool startActivity(const QJniObject &intent)
{
    QJniObject activity = activityContext();
    if (!activity.isValid() || !intent.isValid())
        return false;

    QJniEnvironment env;
    // NEW_TASK 标志：从非 Activity 上下文启动必需
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                            jint(0x10000000) /* FLAG_ACTIVITY_NEW_TASK */);
    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V",
                              intent.object<jobject>());
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    return true;
}

} // namespace

namespace AndroidBridge {

bool isAndroidRuntime() { return true; }

bool launchApp(const QString &packageName)
{
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    QJniObject pm = activity.callObjectMethod(
        "getPackageManager", "()Landroid/content/pm/PackageManager;");
    if (!pm.isValid())
        return false;

    QJniObject intent = pm.callObjectMethod(
        "getLaunchIntentForPackage",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString(packageName).object<jstring>());
    if (!intent.isValid())
        return false; // 未安装：getLaunchIntentForPackage 返回 null

    return startActivity(intent);
}

bool launchBedrock() { return launchApp(kBedrockPackage); }

bool installApk(const QString &apkPath)
{
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    // 优先走 FileProvider content:// URI（targetSdk>=24 禁止 file:// 跨应用共享）。
    // 若宿主 gradle 未引入 androidx.core:core，getUriForPackage 静态类缺失时
    // QJniObject 返回无效对象，此处降级为 false，由 UI 层提示手动安装。
    QJniObject file("java/io/File", "(Ljava/lang/String;)V",
                    QJniObject::fromString(apkPath).object<jstring>());
    if (!file.isValid())
        return false;

    QJniObject uri;
    QJniEnvironment env;
    jclass providerClass = env->FindClass("androidx/core/content/FileProvider");
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false; // 无 androidx FileProvider
    }
    env->DeleteLocalRef(providerClass);

    uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        activity.object(),
        QJniObject::fromString(kAuthority).object<jstring>(),
        file.object());
    if (!uri.isValid())
        return false;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
                      QJniObject::fromString("android.intent.action.VIEW").object<jstring>(),
                      uri.object());
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(),
                            QJniObject::fromString("application/vnd.android.package-archive").object<jstring>());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                            jint(0x10000000) | jint(0x80000000) /* GRANT_READ */);
    // SDK>=26：需要先授予"安装未知应用"权限，跳转到设置页
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 26)
    {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Z)Landroid/content/Intent;",
            QJniObject::fromString("android.intent.extra.NOT_UNKNOWN_SOURCE").object<jstring>(),
            jboolean(true));
    }
    return startActivity(intent);
}

bool shareFile(const QString &filePath, const QString &mimeType)
{
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    QJniObject file("java/io/File", "(Ljava/lang/String;)V",
                    QJniObject::fromString(filePath).object<jstring>());
    if (!file.isValid())
        return false;

    QJniEnvironment env;
    jclass providerClass = env->FindClass("androidx/core/content/FileProvider");
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    env->DeleteLocalRef(providerClass);

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        activity.object(),
        QJniObject::fromString(kAuthority).object<jstring>(),
        file.object());
    if (!uri.isValid())
        return false;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
                      QJniObject::fromString("android.intent.action.SEND").object<jstring>(),
                      uri.object());
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(mimeType).object<jstring>());
    // SEND 需要 EXTRA_STREAM
    intent.callObjectMethod(
        "putExtra",
        "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
        QJniObject::fromString("android.intent.extra.STREAM").object<jstring>(),
        uri.object());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                            jint(0x80000000) /* FLAG_GRANT_READ_URI_PERMISSION */);
    // SEND 用 startActivity 包裹 chooser
    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent", "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(), nullptr);
    if (!chooser.isValid())
        chooser = intent;
    return startActivity(chooser);
}

bool isAppInstalled(const QString &packageName)
{
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    QJniObject pm = activity.callObjectMethod(
        "getPackageManager", "()Landroid/content/pm/PackageManager;");
    if (!pm.isValid())
        return false;

    QJniEnvironment env;
    // getLaunchIntentForPackage 未安装时返回 null
    QJniObject launch = pm.callObjectMethod(
        "getLaunchIntentForPackage",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString(packageName).object<jstring>());
    const bool ok = launch.isValid();
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    return ok;
}

bool openPathWith(const QString &packageName, const QString &path, const QString &mimeType)
{
    if (!isAppInstalled(packageName))
        return false;

    QJniObject file("java/io/File", "(Ljava/lang/String;)V",
                    QJniObject::fromString(path).object<jstring>());
    if (!file.isValid())
        return false;

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "fromFile",
        "(Ljava/io/File;)Landroid/net/Uri;",
        file.object());
    if (!uri.isValid())
        return false;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
                      QJniObject::fromString("android.intent.action.VIEW").object<jstring>(),
                      uri.object());
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(),
                            QJniObject::fromString(mimeType).object<jstring>());
    // 限定到目标应用；目标未注册该 Intent 时 startActivity 抛异常，由 startActivity 清理并返回 false
    intent.callObjectMethod("setPackage",
                            "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(packageName).object<jstring>());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                            jint(0x10000000) | jint(0x00000001) /* NEW_TASK | GRANT_READ */);
    return startActivity(intent);
}

void applyImmersiveMode()
{
    QJniObject activity = activityContext();
    if (!activity.isValid()) {
        qDebug() << "[Immersive] no activity context";
        return;
    }

    QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
    if (!window.isValid()) {
        qDebug() << "[Immersive] no window";
        return;
    }

    QJniEnvironment env;

    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        // API 30+：WindowInsetsController 隐藏状态栏；下滑可临时呼出并自动隐藏
        QJniObject controller = window.callObjectMethod(
            "getInsetsController", "()Landroid/view/WindowInsetsController;");
        if (controller.isValid()) {
            // WindowInsets.Type.statusBars()/navigationBars() 是静态方法，返回类型掩码 (I)。
            // 状态栏 + 导航条都隐藏：应用占满整屏，否则底部内容会被手势条区域盖住
            jint bars = QJniObject::callStaticMethod<jint>(
                "android/view/WindowInsets$Type", "statusBars", "()I");
            jint nav = QJniObject::callStaticMethod<jint>(
                "android/view/WindowInsets$Type", "navigationBars", "()I");
            qDebug() << "[Immersive] controller ok, bars =" << (bars | nav);
            controller.callMethod<void>("hide", "(I)V", jint(bars | nav));
            // 2 = BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE：边缘上滑临时呼出并自动隐藏
            controller.callMethod<void>("setSystemBarsBehavior", "(I)V", jint(2));
        } else {
            qDebug() << "[Immersive] no insets controller";
        }
    } else {
        // API 24-29 兜底：immersive sticky（LAYOUT_STABLE|LAYOUT_HIDE_NAVIGATION|
        // LAYOUT_FULLSCREEN|HIDE_NAVIGATION|FULLSCREEN|IMMERSIVE_STICKY）
        QJniObject decor = window.callObjectMethod("getDecorView", "()Landroid/view/View;");
        if (decor.isValid()) {
            decor.callMethod<void>("setSystemUiVisibility", "(I)V",
                                   jint(0x100 | 0x2 | 0x4 | 0x200 | 0x400 | 0x1000));
            qDebug() << "[Immersive] legacy systemUiVisibility set";
        }
    }

    if (env->ExceptionCheck()) {
        qDebug() << "[Immersive] JNI exception cleared";
        env->ExceptionClear();
    }
}

bool ensureAllFilesAccess()
{
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30)
        return true; // API 30 以下走 requestLegacyExternalStorage

    const bool granted = QJniObject::callStaticMethod<jboolean>(
        "android/os/Environment", "isExternalStorageManager", "()Z");
    if (granted)
        return true;

    // 跳转本应用的"所有文件访问权限"系统设置页
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    QJniObject packageName = activity.callObjectMethod(
        "getPackageName", "()Ljava/lang/String;");
    if (!packageName.isValid())
        return false;

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(QStringLiteral("package:") + packageName.toString()).object());
    if (!uri.isValid())
        return false;

    QJniObject intent("android/content/Intent",
                      "(Ljava/lang/String;)V",
                      QJniObject::fromString(
                          QStringLiteral("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION"))
                          .object());
    intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                            uri.object());
    startActivity(intent);
    return false; // 已发起授权跳转，当前尚未授权
}

} // namespace AndroidBridge

#else // !Q_OS_ANDROID — 桌面空实现

namespace AndroidBridge {

bool isAndroidRuntime() { return false; }
void applyImmersiveMode() {}
bool ensureAllFilesAccess() { return true; }
bool launchApp(const QString &) { return false; }
bool launchBedrock() { return false; }
bool installApk(const QString &) { return false; }
bool shareFile(const QString &, const QString &) { return false; }
bool isAppInstalled(const QString &) { return false; }
bool openPathWith(const QString &, const QString &, const QString &) { return false; }

} // namespace AndroidBridge

#endif // Q_OS_ANDROID
