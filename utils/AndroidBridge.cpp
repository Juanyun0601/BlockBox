/**
 * @file   AndroidBridge.cpp
 * @brief  AndroidBridge 实现 — Android 走 QJniObject Intent，桌面为空实现
 */
#include "AndroidBridge.h"

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJniObject>
#include <QJniEnvironment>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <atomic>
#include <functional>

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr auto kBedrockPackage = "com.mojang.minecraftpe"_L1;
constexpr auto kAuthority = "com.blockbox.launcher.fileprovider"_L1;

QJniObject activityContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

/// 把一段 JNI 操作调度到 Android UI 线程执行。
/// ViewRootImpl / InsetsController 等只能由 UI 线程触碰，在 Qt 主线程
/// 直接调用会抛 CalledFromWrongThreadException（沉浸模式此前一直踩这里）。
void runOnUiThread(std::function<void()> work)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread(std::move(work));
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

// ---- 沉浸模式（隐藏系统栏）的公共小工具 ----

/// Android 11 起才有 WindowInsetsController；更老的版本用 setSystemUiVisibility
constexpr int kInsetsControllerApi = 30;

int androidSdkInt()
{
    return QNativeInterface::QAndroidApplication::sdkVersion();
}

/// WindowInsets.Type.statusBars() / navigationBars() 都是静态方法，返回类型掩码 (I)
jint statusBarsType()
{
    return QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "statusBars", "()I");
}

jint navigationBarsType()
{
    return QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "navigationBars", "()I");
}

/// 旧接口常量（API 24-29 兜底）：
/// LAYOUT_STABLE | LAYOUT_HIDE_NAVIGATION | LAYOUT_FULLSCREEN |
/// HIDE_NAVIGATION | FULLSCREEN | IMMERSIVE_STICKY
constexpr jint kLegacyImmersiveFlags =
    0x100 | 0x200 | 0x400 | 0x2 | 0x4 | 0x1000;

QJniObject activityWindow()
{
    QJniObject activity = activityContext();
    if (!activity.isValid())
        return {};
    return activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
}

/// 取 decorView。顺带保证 decor 已创建：窗口内容还没挂上时，
/// API 30+ 的 getInsetsController() 有可能因为 decor 为空而拿到空对象。
QJniObject windowDecorView(const QJniObject &window)
{
    if (!window.isValid())
        return {};
    return window.callObjectMethod("getDecorView", "()Landroid/view/View;");
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

/// 最近一次在 UI 线程读到的系统栏可见状态（供非 UI 线程查询，允许滞后一拍）
std::atomic<int> g_cachedBarsState{static_cast<int>(SystemBarsState::Unknown)};

/// 在 Android UI 线程上检查并隐藏系统栏（状态栏 + 导航条）。
/// 读取状态和下发隐藏必须在同一线程里完成，拆到两个线程会有竞态：
/// 刚判定"已隐藏"就被系统放回来，或者反之重复下发触发窗口重排。
void ensureSystemBarsHiddenOnUi()
{
    QJniObject window = activityWindow();
    QJniObject decor = windowDecorView(window);
    if (!window.isValid() || !decor.isValid()) {
        g_cachedBarsState.store(static_cast<int>(SystemBarsState::Unknown));
        return;
    }

    QJniEnvironment env;
    bool hidden = false;
    bool known = false;

    if (androidSdkInt() >= kInsetsControllerApi) {
        // View.getRootWindowInsets() 在视图还没挂上时返回 null，这时状态未知
        QJniObject insets = decor.callObjectMethod(
            "getRootWindowInsets", "()Landroid/view/WindowInsets;");
        if (insets.isValid()) {
            const bool statusVisible =
                insets.callMethod<jboolean>("isVisible", "(I)Z", statusBarsType());
            const bool navVisible =
                insets.callMethod<jboolean>("isVisible", "(I)Z", navigationBarsType());
            known = true;
            hidden = !(statusVisible || navVisible);
        }

        if (!hidden) {
            // API 30+：WindowInsetsController 隐藏状态栏 + 导航条
            // （只隐藏状态栏时底部内容会被手势条盖住）
            QJniObject controller = window.callObjectMethod(
                "getInsetsController", "()Landroid/view/WindowInsetsController;");
            if (!controller.isValid()) {
                // decor 还没挂到 window 上时 window.getInsetsController() 可能拿不到，
                // 从 decorView 再取一次
                controller = decor.callObjectMethod(
                    "getWindowInsetsController", "()Landroid/view/WindowInsetsController;");
            }
            if (controller.isValid()) {
                // 2 = BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE：边缘滑动临时呼出、随后自动隐藏
                controller.callMethod<void>("setSystemBarsBehavior", "(I)V", jint(2));
                controller.callMethod<void>("hide", "(I)V",
                                            jint(statusBarsType() | navigationBarsType()));
                hidden = true;
                known = true;
            }
        }
    } else {
        // API 24-29（以及个别拿不到 controller 的机器）兜底：immersive sticky
        const jint visibility = decor.callMethod<jint>("getSystemUiVisibility", "()I");
        const bool fullscreen = (visibility & 0x4) != 0; // SYSTEM_UI_FLAG_FULLSCREEN
        const bool hideNav = (visibility & 0x2) != 0;    // SYSTEM_UI_FLAG_HIDE_NAVIGATION
        if (!fullscreen || !hideNav)
            decor.callMethod<void>("setSystemUiVisibility", "(I)V", kLegacyImmersiveFlags);
        known = true;
        hidden = true; // 下发 sticky 后按已隐藏处理，避免每拍重复下发
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        qWarning() << "[Immersive] 隐藏系统栏时发生 JNI 异常";
        g_cachedBarsState.store(static_cast<int>(SystemBarsState::Unknown));
        return;
    }

    if (known)
        g_cachedBarsState.store(static_cast<int>(hidden ? SystemBarsState::Hidden
                                                        : SystemBarsState::Visible));

    static bool logged = false;
    if (!logged && hidden) {
        logged = true;
        qDebug() << "[Immersive] 已隐藏系统栏，SDK" << androidSdkInt();
    }
}

bool applyImmersiveMode()
{
    runOnUiThread([]() { ensureSystemBarsHiddenOnUi(); });
    return true; // 已发起调度，实际结果异步生效
}

SystemBarsState systemBarsState()
{
    return static_cast<SystemBarsState>(g_cachedBarsState.load());
}

namespace {

/**
 * 沉浸模式守卫。
 *
 * Android 会在很多时机自己把状态栏/导航条放回来（窗口重新获得焦点、从后台
 * 切回、弹窗开合、旋转、软键盘收放，某些定制系统还会周期性改写），而 Qt 不
 * 提供对应回调，只在启动时下发一次必然时好时坏。这里补三件事：
 *  1) 窗口显示/激活、应用回到前台时重新下发，并分几拍补发——系统的"把栏放
 *     回来"往往发生在我们下发之后的那次窗口调整里，单发一次会被覆盖；
 *  2) 低频自检，发现栏又露出来就再隐藏一次（兜底厂商 ROM）；
 *  3) 切到后台时停掉自检，不做无谓的 JNI 调用。
 */
class ImmersiveGuard : public QObject
{
public:
    explicit ImmersiveGuard(QObject *parent) : QObject(parent)
    {
        m_heal.setInterval(2500);
        m_heal.setTimerType(Qt::CoarseTimer);
        connect(&m_heal, &QTimer::timeout, this, [this]() { heal(); });

        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::applicationStateChanged, this,
                    [this](Qt::ApplicationState state) {
                        if (state == Qt::ApplicationActive) {
                            burst();
                            m_heal.start();
                        } else {
                            m_heal.stop();
                        }
                    });
            // 事件过滤器装在应用上：任何窗口第一次显示、重新激活都算一次时机
            app->installEventFilter(this);
        }
    }

    void start()
    {
        applyNow();
        m_heal.start();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::WindowActivate:
        case QEvent::ActivationChange:
        case QEvent::WindowStateChange:
            if (isWindowObject(watched))
                burst();
            break;
        case QEvent::Resize:
            // 旋转、软键盘收放会走到这里：系统栏很容易在这之后自己冒出来
            if (qobject_cast<QWindow *>(watched))
                burst();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static bool isWindowObject(const QObject *object)
    {
        if (const auto *widget = qobject_cast<const QWidget *>(object))
            return widget->isWindow();
        return qobject_cast<const QWindow *>(object) != nullptr;
    }

    /// 立刻隐藏 + 随后两拍补发；同一轮里不重复排队，避免事件密集时堆一串调用
    void burst()
    {
        applyNow();
        if (m_burst.isValid() && m_burst.elapsed() < 500)
            return;
        m_burst.restart();
        QTimer::singleShot(120, this, [this]() { applyNow(); });
        QTimer::singleShot(400, this, [this]() { applyNow(); });
    }

    void applyNow()
    {
        if (m_throttle.isValid() && m_throttle.elapsed() < 120)
            return;
        m_throttle.restart();
        // 检查 + 隐藏整体调度到 Android UI 线程原子完成；
        // 那边发现已沉浸就不重复下发，免得白白触发一次窗口重新布局
        applyImmersiveMode();
    }

    void heal()
    {
        if (QGuiApplication::applicationState() != Qt::ApplicationActive)
            return;
        if (!anyWindowVisible())
            return;
        // 用户刚下滑临时呼出系统栏时不清屏太早：这里只管"确实露着"的情况，
        // 系统自身的 transient 自动隐藏照常生效，本定时器只是兜底
        if (systemBarsState() != SystemBarsState::Visible)
            return;
        static bool logged = false;
        if (!logged) {
            logged = true;
            qDebug() << "[Immersive] 检测到系统栏被放回，已重新隐藏";
        }
        applyImmersiveMode();
    }

    static bool anyWindowVisible()
    {
        const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
        for (const QWindow *window : windows) {
            if (window->isVisible())
                return true;
        }
        return false;
    }

    QTimer m_heal;
    QElapsedTimer m_burst;
    QElapsedTimer m_throttle;
};

} // namespace

void installImmersiveMode()
{
    static ImmersiveGuard *guard = nullptr;
    if (guard)
        return;
    guard = new ImmersiveGuard(QCoreApplication::instance());
    guard->start();
}

bool hasAllFilesAccess()
{
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30)
        return true; // API 30 以下走 requestLegacyExternalStorage

    return QJniObject::callStaticMethod<jboolean>(
        "android/os/Environment", "isExternalStorageManager", "()Z");
}

bool ensureAllFilesAccess()
{
    if (hasAllFilesAccess())
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
    if (startActivity(intent))
        return false; // 已发起本应用授权页

    // 部分定制系统没有「本应用」授权页，退回全局「所有文件访问」列表
    QJniObject generic("android/content/Intent",
                       "(Ljava/lang/String;)V",
                       QJniObject::fromString(
                           QStringLiteral("android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION"))
                           .object());
    startActivity(generic);
    return false; // 已发起授权跳转，当前尚未授权
}

bool hasLegacyStoragePermission()
{
    const int sdk = QNativeInterface::QAndroidApplication::sdkVersion();
    if (sdk > 32)
        return true; // API 33+ 传统存储权限已失效，存储走「所有文件访问」/媒体权限

    QJniObject activity = activityContext();
    if (!activity.isValid())
        return true; // 拿不到上下文时不拦用户，由具体功能自行兜底

    QJniEnvironment env;
    auto granted = [&activity, &env](const QString &permission) {
        const jint state = activity.callMethod<jint>(
            "checkSelfPermission", "(Ljava/lang/String;)I",
            QJniObject::fromString(permission).object<jstring>());
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return true;
        }
        return state == 0 /* PackageManager.PERMISSION_GRANTED */;
    };

    // 清单里 WRITE 声明到 API 28、READ 到 32：按系统版本只检查还在清单里的权限
    if (sdk <= 28 && !granted(QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE")))
        return false;
    return granted(QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"));
}

bool requestLegacyStoragePermissions()
{
    const int sdk = QNativeInterface::QAndroidApplication::sdkVersion();
    if (sdk < 23 || sdk > 32)
        return false; // API 23 以下无需运行时申请，33+ 无传统权限可申请

    QJniObject activity = activityContext();
    if (!activity.isValid())
        return false;

    QJniEnvironment env;
    if (!env.isValid())
        return false;

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }

    // 只申请当前系统版本下清单里还声明着的权限（申请未声明权限会被系统直接拒绝）
    const int count = sdk <= 28 ? 2 : 1;
    jobjectArray permissions = env->NewObjectArray(count, stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (!permissions || env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }

    auto setPermission = [&env, permissions](int index, const QString &value) {
        jobject text = env->NewStringUTF(value.toUtf8().constData());
        env->SetObjectArrayElement(permissions, index, text);
        env->DeleteLocalRef(text);
    };
    if (sdk <= 28)
        setPermission(0, QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE"));
    setPermission(sdk <= 28 ? 1 : 0, QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"));

    activity.callMethod<void>("requestPermissions", "([Ljava/lang/String;I)V",
                              permissions, jint(1001));
    env->DeleteLocalRef(permissions);

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    return true; // 系统授权框已弹出，结果异步返回
}

} // namespace AndroidBridge

#else // !Q_OS_ANDROID — 桌面空实现

namespace AndroidBridge {

bool isAndroidRuntime() { return false; }
bool applyImmersiveMode() { return false; }
SystemBarsState systemBarsState() { return SystemBarsState::Hidden; }
void installImmersiveMode() {}
bool hasAllFilesAccess() { return true; }
bool ensureAllFilesAccess() { return true; }
bool hasLegacyStoragePermission() { return true; }
bool requestLegacyStoragePermissions() { return false; }
bool launchApp(const QString &) { return false; }
bool launchBedrock() { return false; }
bool installApk(const QString &) { return false; }
bool shareFile(const QString &, const QString &) { return false; }
bool isAppInstalled(const QString &) { return false; }
bool openPathWith(const QString &, const QString &, const QString &) { return false; }

} // namespace AndroidBridge

#endif // Q_OS_ANDROID
