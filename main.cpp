/**
 * @file   main.cpp
 * @brief  Application entry point
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "mainwindow.h"

#include <atomic>
#include <functional>
#include <memory>

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QDir>
#include <QFuture>
#include <QFutureWatcher>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QScrollArea>
#include <QResource>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QThread>
#include <QtConcurrent>
#include <QTimer>
#include <QTranslator>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "platform.h"
#include "components/AppMessageBox.h"
#include "components/QuizReminder.h"
#include "components/UserAgreementDialog.h"

#include "utils/AndroidBridge.h"
#include "utils/AsyncInitializer.h"
#include "utils/ContentFavoriteSync.h"
#include "utils/CrashReporter.h"
#include "utils/ForumAccountService.h"
#include "utils/GameLauncher.h"
#include "utils/LanguageManager.h"
#include "utils/LauncherLogger.h"
#include "utils/LowConfigMode.h"
#include "utils/PerformanceMonitor.h"
#include "utils/QuickLaunchManager.h"
#include "utils/QuizBank.h"
#include "utils/ResourceManager.h"
#include "utils/SettingsManager.h"
#include "utils/StartupProgressWidget.h"
#include "utils/Telemetry.h"
#include "utils/ThemeManager.h"
#include "utils/UpdateChecker.h"
#include "utils/news/AnnouncementFeed.h"

namespace {

/// blockbox:// 一键加入链接的 IPC 通道名：浏览器为新链接再起的进程靠它转交给已运行的实例
constexpr char kJoinIpcServerName[] = "BlockBox-join-ipc";

/// 转发桌面 .blockbox 快捷启动文件时加的行前缀（与一键加入链接共用一条 IPC 通道）
constexpr char kQuickLaunchFilePrefix[] = "blockbox-file:";

/// 从命令行参数里找出 blockbox:// 一键加入链接（没有返回空串）
QString findJoinUrlArgument(const QStringList &arguments)
{
    for (const QString &argument : arguments)
    {
        if (argument.startsWith(QStringLiteral("blockbox://"), Qt::CaseInsensitive))
            return argument;
    }
    return QString();
}

/// 从命令行参数里找出 .blockbox 快捷启动文件（没有返回空串）
QString findQuickLaunchFileArgument(const QStringList &arguments)
{
    for (const QString &argument : arguments)
    {
        // 跳过 --xxx 之类的开关参数
        if (argument.startsWith(QLatin1Char('-'))
            || !argument.endsWith(QStringLiteral(".blockbox"), Qt::CaseInsensitive))
            continue;
        const QFileInfo info(argument);
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
    }
    return QString();
}

/// 已有方块盒子在跑时把一行文本（链接 / 文件路径）转交给它；连不上（没有在跑）返回 false
bool forwardToRunningInstance(const QString &line)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kJoinIpcServerName));
    if (!socket.waitForConnected(500))
        return false;
    // 约定一行一条，接收端凑齐换行才处理
    socket.write(line.toUtf8() + '\n');
    socket.flush();
    return socket.waitForBytesWritten(500);
}

#ifdef Q_OS_WIN
/**
 * @brief 注册 blockbox:// 协议（HKCU，无需管理员）
 *
 * 网页 join.html 的「通过方块盒子一键加入」按钮靠它唤起方块盒子。
 * 每次启动都重写一遍：更新 / 换安装目录后注册的路径也能保持正确。
 */
void registerBlockboxUrlScheme()
{
    const QString command = QStringLiteral("\"%1\" \"%2\"")
                                .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
                                     QStringLiteral("%1"));

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\blockbox", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr)
        == ERROR_SUCCESS)
    {
        const wchar_t empty[] = L"";
        RegSetValueExW(key, L"URL Protocol", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(empty), sizeof(empty));
        RegCloseKey(key);
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\blockbox\\shell\\open\\command",
                        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr)
        == ERROR_SUCCESS)
    {
        const std::wstring commandWide = command.toStdWString();
        RegSetValueExW(key, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(commandWide.c_str()),
                       static_cast<DWORD>((commandWide.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}
#endif

/**
 * @brief 启动后在右下角显示入站答题提醒
 *
 * 答题结果和论坛账号绑定：账号还没通过时每次启动都会在主窗口右下角弹一个小提醒，
 * 用户可以选择「开始答题」，也可以「跳过」直接使用启动器（发帖和评论需要先通过答题）。
 * 提醒窗挂在主窗口上，随主窗口一起销毁；已通过答题则不再出现。
 */
void showQuizReminder(QWidget *parent)
{
    if (!parent || QuizBank::hasPassed()
        || ForumAccountService::instance()->hasPassedQuiz())
        return;

    QuizReminder *reminder = new QuizReminder(parent);
    if (MainWindow *mainWindow = qobject_cast<MainWindow *>(parent))
    {
        // 未登录点「去登录」：直接打开账号页的登录表单
        QObject::connect(reminder, &QuizReminder::loginRequested, mainWindow,
                         &MainWindow::openForumAccountLogin);
    }
    reminder->showReminder();
}

/// 这条重要公告是不是已经被用户勾选过「不再弹出」
bool isImportantAnnouncementDismissed(const QString &id)
{
    return !id.isEmpty()
           && SettingsManager::instance()->getProperty("announcement/important_dismissed_id").toString() == id;
}

/// 记住用户不想再看到的这条重要公告（公告更新后 id 会变，新公告仍会弹出）
void dismissImportantAnnouncement(const QString &id)
{
    if (!id.isEmpty())
        SettingsManager::instance()->setProperty("announcement/important_dismissed_id", id);
}

/// 弹出启动重要公告：正文按 Markdown 渲染，底部可勾选「不再弹出这条公告」
void showImportantAnnouncement(QWidget *parent, const ImportantAnnouncement &notice)
{
    AppMessageBox box(parent);
    box.setWindowTitle(QCoreApplication::translate("MainWindow", "重要公告"));
    box.setIcon(AppMessageBox::Information);

    const QString title = notice.title.trimmed().isEmpty()
                              ? QCoreApplication::translate("MainWindow", "重要公告")
                              : notice.title;
    box.setText(title);

    // 后台可以单独填一个跳转链接；正文里没写链接时，在末尾补一条可点击的「查看详情」
    QString markdown = notice.markdown;
    if (!notice.link.trimmed().isEmpty())
        markdown += QStringLiteral("\n\n[%1](%2)")
                        .arg(QCoreApplication::translate("MainWindow", "查看详情"), notice.link);
    box.setInformativeMarkdown(markdown);

    box.setCardWidthRange(560, 760);
    box.setInfoBrowserHeightRange(180, 440);
    box.setInfoBrowserFullWidth();
    box.setCheckBox(QCoreApplication::translate("MainWindow", "不再弹出这条公告"));
    box.setStandardButtons(AppMessageBox::Ok);
    box.setButtonText(AppMessageBox::Ok, QCoreApplication::translate("MainWindow", "我知道了"));
    box.exec();

    if (box.isCheckBoxChecked())
        dismissImportantAnnouncement(notice.id);
}

/**
 * @brief 启动时拉取并展示重要公告
 *
 * 弹窗只显示一次：拉取完成（成功或失败）后看当前数据，公告已被勾选
 * 「不再弹出」就跳过。网络很慢时先用本地缓存兜底，不让弹窗一直迟到。
 * 无论是否弹出，结束后都会调用 finished，让启动检查更新等后续动作
 * 排在公告之后，避免两个弹窗叠在一起。
 */
void maybeShowImportantAnnouncement(QWidget *parent, const std::function<void()> &finished)
{
    AnnouncementFeed *feed = AnnouncementFeed::instance();
    auto done = std::make_shared<bool>(false);
    auto showing = std::make_shared<bool>(false);

    auto complete = [done, finished]() {
        if (*done)
            return;
        *done = true;
        if (finished)
            finished();
    };

    // 展示当前数据里的重要公告（没有 / 已勾选不再弹出时直接收尾）
    auto present = [feed, parent, showing, complete]() {
        if (*showing) {
            complete();
            return;
        }
        const ImportantAnnouncement notice = feed->important();
        if (!notice.isValid() || isImportantAnnouncementDismissed(notice.id)) {
            complete();
            return;
        }
        *showing = true;
        showImportantAnnouncement(parent, notice);
        complete();
    };

    QObject::connect(feed, &AnnouncementFeed::loadingChanged, parent, [present](bool loading) {
        if (!loading)
            present();
    });

    // 网络迟迟没结果时先用上一次的缓存弹出，等真拿到新数据也已经弹过了
    QTimer::singleShot(3500, parent, [feed, parent, done, showing, complete]() {
        if (*done || *showing)
            return;
        const ImportantAnnouncement notice = feed->important();
        if (!notice.isValid() || isImportantAnnouncementDismissed(notice.id))
            return;
        *showing = true;
        showImportantAnnouncement(parent, notice);
        complete();
    });

    // 每次启动都强制拉一次，保证重要公告的时效性
    feed->refresh(true);
}

/// 启动后按设置检查更新（静默模式：仅发现新版本时提示，失败不打扰）
void maybeCheckUpdates(QWidget *parent)
{
    if (!SettingsManager::instance()->getProperty("auto_check_update", true).toBool())
        return;

    QTimer::singleShot(3000, [parent]() {
        UpdateChecker::instance()->checkForUpdates(parent, true);
    });
}

/**
 * @brief 安卓端启动权限引导：首次打开（或一直没授权时）先把游戏文件所需的
 *        存储权限要到手，再继续新手引导 / 公告 / 更新检查，避免装实例、导入
 *        存档时才发现没权限。授权过之后每次启动静默通过，不打扰用户。
 *
 * 两个分支按系统版本自动二选一：
 *  - 安卓 11+：「所有文件访问」没有系统弹窗，只能说明用途后跳系统设置页；
 *  - 旧版本：说明用途后再弹一次系统的存储权限授权框。
 * 桌面端直接返回。
 */
void requestAndroidStartupPermissions(QWidget *parent)
{
    if (!AndroidBridge::isAndroidRuntime())
        return;

    const QString purpose = QCoreApplication::translate("MainWindow",
        "安装实例、模组和导入导出存档都需要访问存储");

    if (!AndroidBridge::hasAllFilesAccess())
    {
        const auto answer = AppMessageBox::question(
            parent,
            QCoreApplication::translate("MainWindow", "需要「所有文件访问」权限"),
            QCoreApplication::translate("MainWindow",
                                        "%1。\n\n"
                                        "是否前往系统设置授权？授权后返回即可继续使用。")
                .arg(purpose),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (answer == AppMessageBox::Yes)
            AndroidBridge::ensureAllFilesAccess();
        return;
    }

    if (!AndroidBridge::hasLegacyStoragePermission())
    {
        const auto answer = AppMessageBox::question(
            parent,
            QCoreApplication::translate("MainWindow", "需要存储权限"),
            QCoreApplication::translate("MainWindow",
                                        "%1。\n\n请在接下来的系统弹窗中允许。")
                .arg(purpose),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (answer == AppMessageBox::Yes)
            AndroidBridge::requestLegacyStoragePermissions();
    }
}

} // namespace

namespace {

/**
 * @brief 启动轨迹要尝试写入的候选路径
 *
 * 优先写应用自己的媒体目录 /sdcard/Android/media/<包名>/：这个目录在手机
 * 文件管理器里能直接看到（Android/data 看不到），启动就闪退时用户可以把
 * 文件发过来。部分定制系统（如鸿蒙）对这条路径有限制，所以再准备两个备用
 * 位置，总有一个写得进去。
 */
QStringList bbTraceDirCandidates()
{
#ifdef Q_OS_ANDROID
    QStringList dirs;
    dirs << QStringLiteral("/sdcard/Android/media/com.blockbox.launcher");
    dirs << QStringLiteral("/storage/emulated/0/Android/media/com.blockbox.launcher");
    dirs << (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/logs"));
    return dirs;
#else
    return QStringList(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                       + QStringLiteral("/BlockBox"));
#endif
}

/// 找到第一个能写进去的目录（找不到就返回空，后面的写入直接跳过）
QString bbTraceDir()
{
    static const QString dir = [] {
        const QStringList candidates = bbTraceDirCandidates();
        for (const QString &candidate : candidates) {
            if (!QDir().mkpath(candidate))
                continue;
            QFile probe(candidate + QStringLiteral("/.write_probe"));
            if (probe.open(QIODevice::WriteOnly)) {
                probe.close();
                probe.remove();
                return candidate;
            }
        }
        return QString();
    }();
    return dir;
}

/// 追加一行启动轨迹（truncate=true 时先清空，只在启动第一行用）
void bbStartupTrace(const QString &line, bool truncate = false)
{
    static const QString dir = bbTraceDir();
    static bool loggedPath = false;
    if (dir.isEmpty()) {
        if (!loggedPath)
            qWarning() << "[Trace] 没有可写的启动轨迹目录";
        loggedPath = true;
        return;
    }
    const QString path = dir + QStringLiteral("/BlockBox启动日志.txt");
    QFile file(path);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    mode |= truncate ? QIODevice::Truncate : QIODevice::Append;
    if (!file.open(mode))
        return;
    loggedPath = true;
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        << QStringLiteral("  ") << line << QLatin1Char('\n');
}

} // namespace

int main(int argc, char *argv[])
{
    // ── 启动轨迹：把启动过程中的关键节点写进一个用户找得到的文件 ──
    // 安卓启动就闪退时，普通用户拿不到 logcat、也进不去 Android/data，
    // 所以额外写一份到应用自己的媒体目录（文件管理器里看得见）：
    //   /sdcard/Android/media/com.blockbox.launcher/BlockBox启动日志.txt
    // 文件最后一行就是崩溃前走到的那一步，便于定位。
    bbStartupTrace(QStringLiteral("========== 进程启动 (main 入口) 构建时间 %1 ==========")
                       .arg(QStringLiteral(__DATE__ " " __TIME__)), true);


    // 移动端界面缩放：把整套 UI 按 0.6 倍缩小后再渲染。
    // 界面是按桌面宽屏（最小 1200×800）设计的，手机横屏逻辑高度通常只有 ~360，
    // 等比缩放后 1200×800 → 720×480，布局骨架（侧边栏/顶栏/卡片/弹窗）全部同步变小，
    // 避免逐页手工改尺寸，也保证不出现内容超出屏幕。
    // 必须在 QApplication 构造前设置环境变量；只对移动形态（手机/平板）生效：
    // 桌面 Windows/macOS/Linux 与鸿蒙电脑（2in1）都按 1.0 渲染电脑版布局。
    // 若用户设备上已手动设过该变量，则以用户设置为准。
#if defined(Q_OS_ANDROID) || defined(HARMONY_OS)
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")
        && Platform::getFormFactor() == Platform::FormFactor::Mobile)
        qputenv("QT_SCALE_FACTOR", "0.6");
#endif

    // 设置 OpenGL 表面格式（必须在 QApplication 之前）
    // 桌面端用 OpenGL 3.3 Core Profile；安卓 / 鸿蒙只有 OpenGL ES（版本号最高 3.2），
    // 沿用 3.3 Core 会让 EGL 直接报 EGL_BAD_CONFIG（Unknown ES version 3.3），
    // 拿不到 GL 上下文后整个窗口一片白，应用看起来就是「卡在白屏」。
    QSurfaceFormat glFormat;
#if defined(Q_OS_ANDROID) || defined(HARMONY_OS)
    glFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    glFormat.setVersion(3, 0);
    glFormat.setProfile(QSurfaceFormat::NoProfile);
#else
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
#endif
    glFormat.setDepthBufferSize(24);
    glFormat.setStencilBufferSize(0);
    glFormat.setSamples(0);
    QSurfaceFormat::setDefaultFormat(glFormat);

    QApplication a(argc, argv);
    bbStartupTrace(QStringLiteral("QApplication 已创建"));

    // 设置应用程序图标
    a.setWindowIcon(QIcon(":/Images/icon.ico"));

    // ── 外部打开请求：blockbox:// 一键加入链接 / 桌面 .blockbox 快捷启动文件，
    //    必要时转交给已运行的实例 ──
    const QStringList appArguments = QApplication::arguments();
    const QString joinUrlArgument = findJoinUrlArgument(appArguments);
    const QString quickLaunchFileArgument = findQuickLaunchFileArgument(appArguments);
#ifndef Q_OS_ANDROID
    if (!joinUrlArgument.isEmpty() || !quickLaunchFileArgument.isEmpty())
    {
        bool forwarded = true;
        if (!joinUrlArgument.isEmpty())
            forwarded = forwardToRunningInstance(joinUrlArgument);
        if (forwarded && !quickLaunchFileArgument.isEmpty())
            forwarded = forwardToRunningInstance(
                QString::fromLatin1(kQuickLaunchFilePrefix)
                + QDir::fromNativeSeparators(quickLaunchFileArgument));
        if (forwarded)
        {
            // 已有方块盒子在跑：请求交给它处理，本进程直接退出，不重复开窗口
            return 0;
        }
    }
#endif
#ifdef Q_OS_WIN
    // 网页端「通过方块盒子一键加入」靠这个协议唤起方块盒子
    registerBlockboxUrlScheme();
    // 桌面 .blockbox 快捷启动文件：每次启动重写一遍关联，
    // 更新 / 换安装目录后双击 .blockbox 文件依然会用方块盒子打开
    QuickLaunchManager::registerFileAssociation();
#endif

#if defined(Q_OS_ANDROID)
    // 沉浸模式：隐藏系统状态栏（时间/电量/信号）和导航条，界面铺满整屏。
    // 系统会在切前后台、窗口重新获得焦点、旋转、软键盘收放后自己把栏放回来，
    // 所以交给维持器：启动先隐藏一次，之后按这些时机自动补回，并低频自检。
    AndroidBridge::installImmersiveMode();

    // 触屏上点输入框后软键盘不一定自动弹出（实测搜索框、AI 输入框都出现过
    // 点了没反应的情况）。应用级兜底：可编辑文本控件拿到焦点时主动唤起输入法。
    // 只读控件（QTextBrowser 等）拿到焦点不弹，避免公告弹窗一打开就蹦键盘。
    class ImeAutoShowFilter : public QObject
    {
    public:
        explicit ImeAutoShowFilter(QObject *parent) : QObject(parent) {}

        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::FocusIn) {
                bool editable = false;
                if (auto *edit = qobject_cast<QLineEdit *>(watched))
                    editable = !edit->isReadOnly();
                else if (auto *textEdit = qobject_cast<QTextEdit *>(watched))
                    editable = !textEdit->isReadOnly();
                else if (auto *plain = qobject_cast<QPlainTextEdit *>(watched))
                    editable = !plain->isReadOnly();
                if (editable)
                    QGuiApplication::inputMethod()->show();
            }
            return QObject::eventFilter(watched, event);
        }
    };
    a.installEventFilter(new ImeAutoShowFilter(&a));

    // 安卓返回键分级处理：应用级事件过滤器拦下 Qt::Key_Back，交给主窗口
    // 按优先级消化（关弹窗 → 收覆盖层 → 收子导航 → 收键盘 → 返回上页 →
    // 弹窗确认退出）。事件被消化后 Qt 不再把它交给系统默认行为——否则
    // 系统会直接把整个应用退掉（弹窗开着、键盘开着也照退不误）。
    // 过滤器在主窗口构造之后安装（见下方 MainWindow w 创建处）。
    class BackKeyFilter : public QObject
    {
    public:
        explicit BackKeyFilter(MainWindow *target, QObject *parent)
            : QObject(parent), m_target(target) {}

        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::KeyPress) {
                const auto *keyEvent = static_cast<QKeyEvent *>(event);
                if (keyEvent->key() == Qt::Key_Back && m_target
                    && m_target->handleAndroidBack())
                    return true; // 已消化：不再让系统把应用退掉
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        QPointer<MainWindow> m_target;
    };

    // 软键盘遮挡兜底：键盘弹出后若盖住焦点输入框，把它滚进可视范围。
    // adjustResize 在部分机型（全屏窗口 + 旧版本安卓）不生效，系统只做平移；
    // 输入框位于滚动区里时由这里主动滚动到位，滚动区外的场景靠窗口 resize。
    class KeyboardOcclusionGuard : public QObject
    {
    public:
        explicit KeyboardOcclusionGuard(QObject *parent) : QObject(parent)
        {
            const auto trigger = [this]() { revealFocusedInput(); };
            // 部分输入法先到位键盘矩形、后置可见性，两个信号都接
            connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
                    this, trigger);
            connect(QGuiApplication::inputMethod(), &QInputMethod::visibleChanged,
                    this, trigger);
        }

    private:
        void revealFocusedInput()
        {
            QInputMethod *im = QGuiApplication::inputMethod();
            if (!im || !im->isVisible())
                return;
            QWidget *focus = QApplication::focusWidget();
            if (!focus)
                return;
            if (!qobject_cast<QLineEdit *>(focus) && !qobject_cast<QTextEdit *>(focus)
                && !qobject_cast<QPlainTextEdit *>(focus))
                return;

            // 键盘矩形是窗口坐标，输入框也换算到窗口坐标再判交叠
            const QRect inputRect(focus->mapTo(focus->window(), QPoint(0, 0)), focus->size());
            if (!inputRect.intersects(im->keyboardRectangle().toRect()))
                return;

            // 找最近的滚动区祖先（列表/树等 item view 本身也是 QScrollArea）滚动到可见
            for (QWidget *parent = focus->parentWidget(); parent; parent = parent->parentWidget()) {
                if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                    scroll->ensureWidgetVisible(focus, 0, 24);
                    break;
                }
            }
        }
    };
#endif

    // 初始化运行日志：尽早装上，把启动过程也记进去
    LauncherLogger::install();

    // 崩溃捕获要接在日志之后：闪退时它会把最后一段 launcher.log 一并留下；
    // 匿名统计只读取设置，用户开启后才会真正联网
    CrashReporter::instance()->install();
    Telemetry::instance()->install();

    QObject::connect(&a, &QCoreApplication::aboutToQuit, []() {
        CrashReporter::instance()->setAppPhase(QCoreApplication::translate("MainWindow", "退出中"));
        Telemetry::instance()->shutdown();
        CrashReporter::instance()->shutdown();
        LauncherLogger::shutdown();
    });

    // 初始化低配置模式检测
    LowConfigMode* lowConfigMode = LowConfigMode::instance();

    // 显示启动进度窗口
    StartupProgressWidget* startupWidget = StartupProgressWidget::instance();

    // 根据低配置模式设置动画启用状态
    startupWidget->setAnimationsEnabled(lowConfigMode->areAnimationsEnabled());

    startupWidget->showStartup();

    // 主题在启动窗口显示之后再初始化并套用：QSS 的解析与套用会明显拖慢
    // 首个窗口的出现，先让用户看到启动窗口，再上主题样式
    startupWidget->applyTheme();
    qDebug() << "[App]" << "主题管理器已初始化并应用样式表";

    // 开始测量应用启动时间
    PerformanceMonitor::instance()->startMeasurement("应用启动");

    // 首次运行判定：以持久化的 onboarding/completed 标记为准（而非数据文件夹是否存在）。
    // 数据文件夹由 getDataDirectory() 在启动早期创建，不能作为首次判据——
    // 否则引导一旦被中断（语言切换重启 / 用户关闭 / 崩溃），下次启动文件夹已存在、引导就再也不出现。
    // 只有用户走完引导到达完成页（OnboardingWizard::goDone，含跳过）才会置该标记为 true；
    // 语言切换会重启且不置标记，故重启后引导会再次弹出以继续。
    const bool isFirstRun = !QSettings("BlockBox", "BlockBox")
                                .value("onboarding/completed", false).toBool();

    // 初始化应用数据目录，确保BlockBox和.minecraft文件夹被创建
    startupWidget->updateProgress(10, QCoreApplication::translate("MainWindow", "正在初始化数据目录..."));
    PerformanceMonitor::instance()->startMeasurement("数据目录初始化");
    Platform::getDataDirectory();
    PerformanceMonitor::instance()->endMeasurement("数据目录初始化");

    // 同步加载语言设置（在创建任何 Widget 之前，确保 tr() 使用正确的翻译）
    startupWidget->updateProgress(15, QCoreApplication::translate("MainWindow", "正在加载语言设置..."));
    LanguageManager::instance()->loadLanguageSync();

    qDebug() << "[App]" << "语言管理器已同步初始化";

    // 主题已在启动窗口显示后初始化并套用（见上方 applyTheme），
    // 保证后续创建的主窗口与各页面直接带上主题样式

    // 检查资源文件是否正确加载
    qDebug() << "[App]" << "资源路径：" << QDir::searchPaths(":");
    QDir resourceDir(":/translations");
    qDebug() << "[App]" << "资源中的翻译文件：" << resourceDir.entryList();
    
    // 创建主窗口（但不显示，等待异步初始化完成）
    startupWidget->updateProgress(20, QCoreApplication::translate("MainWindow", "正在创建主窗口..."));
    PerformanceMonitor::instance()->startMeasurement("主窗口初始化");
    MainWindow w;
    PerformanceMonitor::instance()->endMeasurement("主窗口初始化");

#if defined(Q_OS_ANDROID)
    // 返回键过滤器要在主窗口存在之后安装（要拿到窗口实例）；
    // 挂在应用上，任何页面/弹窗里的返回键都会先经过这里
    a.installEventFilter(new BackKeyFilter(&w, &a));
    a.installEventFilter(new KeyboardOcclusionGuard(&a));
#endif

#ifndef Q_OS_ANDROID
    // 接收后续 blockbox:// 一键加入链接的转发：浏览器会为新链接再起一个进程，
    // 那个进程把链接写进这里就退出，由这个已经跑着的实例来完成加入
    QLocalServer *joinIpcServer = new QLocalServer(&a);
    QLocalServer::removeServer(QString::fromLatin1(kJoinIpcServerName));
    if (joinIpcServer->listen(QString::fromLatin1(kJoinIpcServerName)))
    {
        QObject::connect(joinIpcServer, &QLocalServer::newConnection, [&w, joinIpcServer]() {
            while (QLocalSocket *connection = joinIpcServer->nextPendingConnection())
            {
                QObject::connect(connection, &QLocalSocket::disconnected,
                                 connection, &QLocalSocket::deleteLater);
                QObject::connect(connection, &QLocalSocket::readyRead, [&w, connection]() {
                    // 一行一条链接：凑齐换行才处理，链接较大分片到达时不会被截断
                    QByteArray buffer = connection->property("joinBuffer").toByteArray();
                    buffer += connection->readAll();
                    if (!buffer.contains('\n'))
                    {
                        connection->setProperty("joinBuffer", buffer);
                        return;
                    }
                    connection->setProperty("joinBuffer", QByteArray());
                    const QString line = QString::fromUtf8(
                        buffer.left(buffer.indexOf('\n')).trimmed());
                    if (line.isEmpty())
                        return;
                    // 窗口可能正收在系统托盘里：先把主窗口叫回前台再处理
                    // （直接 raise/activate 对隐藏窗口不起作用）
                    w.showFromTray();
                    const QString filePrefix = QString::fromLatin1(kQuickLaunchFilePrefix);
                    if (line.startsWith(filePrefix))
                    {
                        // 另一个进程双击了桌面 .blockbox 文件，路径转发过来由本实例启动
                        const QString filePath = line.mid(filePrefix.size());
                        if (!filePath.isEmpty())
                            w.handleExternalQuickLaunchFile(filePath);
                    }
                    else
                    {
                        w.handleExternalJoinUrl(line);
                    }
                });
            }
        });
    }
    else
    {
        joinIpcServer->deleteLater();
    }
#endif

    // 在线资源收藏 ↔ 论坛账号 同步器（监听收藏增删并在登录后对账补传）
    ContentFavoriteSync::instance();

    // 初始化Java缓存（首次启动全盘扫描，后续从缓存加载）
    startupWidget->updateProgress(25, QCoreApplication::translate("MainWindow", "正在检测Java..."));
    PerformanceMonitor::instance()->startMeasurement("Java 缓存初始化");
    GameLauncher::instance()->initJavaCache();
    PerformanceMonitor::instance()->endMeasurement("Java 缓存初始化");
    qDebug() << "[App]" << "Java 缓存初始化完成";



    // 创建异步初始化器
    AsyncInitializer initializer;

    // 连接异步初始化完成信号
    QObject::connect(&initializer, &AsyncInitializer::allInitializationFinished, [&]() {
        PerformanceMonitor::instance()->endMeasurement("异步初始化");
        qDebug() << "[App]" << "异步初始化完成后显示主窗口";
        
        // 启动完成
        startupWidget->updateProgress(100, QCoreApplication::translate("MainWindow", "启动完成"));
        
        // 短暂延迟后隐藏启动窗口并显示主窗口
        QTimer::singleShot(300, [&w, startupWidget, isFirstRun, joinUrlArgument,
                                 quickLaunchFileArgument]() {
            bbStartupTrace(QStringLiteral("准备显示主窗口"));
            startupWidget->hideStartup();
            w.show();
            CrashReporter::instance()->setAppPhase(QCoreApplication::translate("MainWindow", "运行中"));
            bbStartupTrace(QStringLiteral("主窗口已显示"));

            // 首次使用（以及本次更新后第一次打开）先确认使用协议与隐私政策。
            // 只有用户明确点「不同意并退出」才退出软件：安卓上点图标启动的那一下
            // 触摸、系统抢焦点都可能把弹窗直接关掉，那不算用户拒绝——
            // 否则表现就是点图标一闪而过、软件根本起不来。
            UserAgreementDialog::Decision agreement =
                UserAgreementDialog::decide(UserAgreementDialog::FirstLaunch, &w);
            bbStartupTrace(QStringLiteral("协议确认结果: %1")
                               .arg(agreement == UserAgreementDialog::Accepted
                                        ? QStringLiteral("已同意")
                                        : (agreement == UserAgreementDialog::Declined
                                               ? QStringLiteral("用户拒绝")
                                               : QStringLiteral("被关闭"))));
            for (int retry = 0;
                 agreement == UserAgreementDialog::Dismissed && retry < 2; ++retry) {
                QCoreApplication::processEvents();
                QThread::msleep(250);
                agreement = UserAgreementDialog::decide(UserAgreementDialog::FirstLaunch, &w);
            }
            if (agreement == UserAgreementDialog::Declined) {
                QTimer::singleShot(0, qApp, []() { QCoreApplication::quit(); });
                return;
            }
            if (agreement == UserAgreementDialog::Dismissed) {
                // 仍被关掉：先保证软件能起来，稍后再请用户确认一次
                qWarning() << "[App]" << "协议确认窗未能停留，稍后重试";
                QTimer::singleShot(1500, &w, [&w]() {
                    UserAgreementDialog::ensureAccepted(UserAgreementDialog::FirstLaunch, &w);
                });
            }

            // 安卓端先把系统权限要到手（首次启动会弹出说明 + 授权跳转），
            // 之后再进新手引导或弹公告 / 提醒，保证引导期间就有存储可用
            requestAndroidStartupPermissions(&w);
            bbStartupTrace(QStringLiteral("启动流程完成（后续引导 / 公告 / 检查更新）"));

            // 协议确认之后再开始统计会话（匿名统计默认关闭，这里只记录会话起点）
            Telemetry::instance()->start();
            CrashReporter::instance()->setCurrentPage(QStringLiteral("home"));
            Telemetry::pageView(QStringLiteral("home"));

            // 通过 blockbox:// 链接 / 桌面 .blockbox 文件启动的：窗口就绪后自动处理
            const auto handlePendingExternalActions = [&w, joinUrlArgument, quickLaunchFileArgument]() {
                if (!joinUrlArgument.isEmpty())
                    QTimer::singleShot(600, &w, [&w, joinUrlArgument]() {
                        w.handleExternalJoinUrl(joinUrlArgument);
                    });
                if (!quickLaunchFileArgument.isEmpty())
                    QTimer::singleShot(900, &w, [&w, quickLaunchFileArgument]() {
                        // 双击快捷启动文件时软件本来就在后台：确保窗口先出来再启动游戏
                        w.showFromTray();
                        w.handleExternalQuickLaunchFile(quickLaunchFileArgument);
                    });
            };

            // 上次异常退出的话，先问用户要不要发崩溃报告，处理完再继续检查更新，
            // 避免两个弹窗叠在一起（只弹一份最新的报告，积压的可在设置里一起发）
            const auto reportCrashThen = [&w](std::function<void()> next) {
                CrashReporter::instance()->processPendingReports(&w, std::move(next));
            };

            // 首次运行（未走完引导）：直接进入新手引导页
            if (isFirstRun) {
                QTimer::singleShot(400, [&w, handlePendingExternalActions, reportCrashThen]() {
                    // 引导页走完后再做首次启动的后续动作（入站答题提醒 / 崩溃上报 / 检查更新）。
                    // onboardingFinished() 只在首次启动进入引导时发出一次，设置里再次打开不会重复打扰。
                    QObject::connect(&w, &MainWindow::onboardingFinished, &w,
                                     [&w, handlePendingExternalActions, reportCrashThen](bool) {
                        // 新手引导之后在右下角提醒入站答题（可直接跳过，不阻塞使用）
                        showQuizReminder(&w);
                        // 重要公告弹窗结束后先处理崩溃报告，再检查更新，避免多个弹窗叠在一起
                        maybeShowImportantAnnouncement(&w, [&w, reportCrashThen]() {
                            reportCrashThen([&w]() { maybeCheckUpdates(&w); });
                        });
                        handlePendingExternalActions();
                    });
                    w.showOnboardingPage(true);
                });
            } else {
                showQuizReminder(&w);
                maybeShowImportantAnnouncement(&w, [&w, reportCrashThen]() {
                    reportCrashThen([&w]() { maybeCheckUpdates(&w); });
                });
                handlePendingExternalActions();
            }
        });

        // 结束测量应用启动时间
        PerformanceMonitor::instance()->endMeasurement("应用启动");
    });

    // 启动异步初始化
    PerformanceMonitor::instance()->startMeasurement("异步初始化");
    initializer.startAsyncInitialization();

    return a.exec();
}

