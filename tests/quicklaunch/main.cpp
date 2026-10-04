/**
 * @file   main.cpp
 * @brief  桌面快捷启动（.blockbox）功能独立验证程序
 * @author BlockBox Team
 * @date   2026-09-06
 *
 * 链接真实的 utils/QuickLaunchManager.cpp（SettingsManager 用伪实现替换），
 * 覆盖以下关键路径：
 *   1. createDesktopShortcut 在（测试重定向的）桌面生成 <实例名>.blockbox
 *   2. Windows 系统能从 .blockbox 文件本身提取出内嵌图标（ICO+载荷合成格式）
 *   3. parseQuickLaunchFile 正确回读实例路径（载荷 round-trip）
 *   4. 注册表关联键值（.blockbox → BlockBox.InstanceFile → open command / DefaultIcon）
 *   5. 实例改名后 refreshDesktopShortcut 同步更新桌面文件（旧文件删除）
 * 结束后清理桌面文件、注册表键与临时目录。
 */
#include <QGuiApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>

#include <cstdio>

#include "utils/QuickLaunchManager.h"
#include "utils/SettingsManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char **argv)
{
    // QGuiApplication：QuickLaunchManager 内部使用 QPixmap，需要 GUI 平台插件
    QGuiApplication app(argc, argv);

    // 桌面目录重定向到 ~/.qttest，不污染真实桌面
    QStandardPaths::setTestModeEnabled(true);

    int failures = 0;
    auto check = [&failures](bool ok, const QString &what)
    {
        const QString line = (ok ? QStringLiteral("[PASS] ") : QStringLiteral("[FAIL] "))
            + what + QLatin1Char('\n');
        const QByteArray lineBytes = line.toUtf8();
        std::fwrite(lineBytes.constData(), 1, lineBytes.size(), stderr);
        std::fflush(stderr);
        if (!ok)
            ++failures;
    };

    // ===== 1. 构造测试实例目录与非方形彩色图标（120x80，验证居中裁剪）=====
    const QString instancePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/blockbox_ql_test_instance";
    QDir(instancePath).mkpath(".");
    const QString iconPath = instancePath + "/icon.png";
    QImage sourceImage(120, 80, QImage::Format_ARGB32);
    sourceImage.fill(QColor(30, 144, 255));
    sourceImage.save(iconPath, "PNG");
    SettingsManager::instance()->setProperty("instance/" + instancePath + "/iconPath", iconPath);

    // ===== 2. 创建桌面快捷启动文件 =====
    QString error;
    const bool created = QuickLaunchManager::createDesktopShortcut(instancePath, &error);
    check(created, QStringLiteral("createDesktopShortcut %1").arg(created ? QStringLiteral("成功") : error));
    if (!created)
    {
        check(false, QStringLiteral("提前结束"));
        return failures;
    }

    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString shortcut = desktop + "/blockbox_ql_test_instance.blockbox";
    check(QFile::exists(shortcut), QStringLiteral("桌面文件已生成: %1").arg(shortcut));

    // ===== 3. Windows 系统级图标提取（证明资源管理器能渲染内嵌图标）=====
#ifdef Q_OS_WIN
    const std::wstring shortcutW = shortcut.toStdWString();
    HICON largeIcon = nullptr;
    const UINT extracted = ExtractIconExW(shortcutW.c_str(), 0, &largeIcon, nullptr, 1);
    check(extracted == 1 && largeIcon != nullptr,
          QStringLiteral("成功从 .blockbox 文件提取图标句柄（got=%1）").arg(extracted));

    // 把提取到的图标画进 32 位 DIB，校验中心像素是否为实例图标的蓝色（30,144,255）
    bool iconColorMatched = false;
    if (largeIcon)
    {
        HDC screenDC = GetDC(nullptr);
        BITMAPV5HEADER bmi = {};
        bmi.bV5Size = sizeof(BITMAPV5HEADER);
        bmi.bV5Width = 32;
        bmi.bV5Height = -32;
        bmi.bV5Planes = 1;
        bmi.bV5BitCount = 32;
        bmi.bV5Compression = BI_BITFIELDS;
        bmi.bV5RedMask = 0x00FF0000;
        bmi.bV5GreenMask = 0x0000FF00;
        bmi.bV5BlueMask = 0x000000FF;
        bmi.bV5AlphaMask = 0xFF000000;
        quint8 *pixels = nullptr;
        HDC memDC = CreateCompatibleDC(screenDC);
        void *bits = nullptr;
        HBITMAP dib = CreateDIBSection(memDC, reinterpret_cast<BITMAPINFO *>(&bmi),
                                       DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits)
        {
            const HGDIOBJ oldBmp = SelectObject(memDC, dib);
            if (DrawIconEx(memDC, 0, 0, largeIcon, 32, 32, 0, nullptr, DI_NORMAL))
            {
                const quint8 *center = static_cast<const quint8 *>(bits) + (16 * 32 + 16) * 4;
                // DIB 为 BGRA 顺序
                iconColorMatched = qAbs(int(center[2]) - 30) < 24
                    && qAbs(int(center[1]) - 144) < 24
                    && qAbs(int(center[0]) - 255) < 24;
            }
            SelectObject(memDC, oldBmp);
        }
        if (dib)
            DeleteObject(dib);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        DestroyIcon(largeIcon);
    }
    check(iconColorMatched, QStringLiteral("提取出的图标颜色与实例图标一致（中心像素校验）"));
#endif

    // ===== 4. 载荷回读 =====
    QString parsedPath, parseError;
    const bool parsed = QuickLaunchManager::parseQuickLaunchFile(shortcut, &parsedPath, &parseError);
    check(parsed && parsedPath == QDir::fromNativeSeparators(instancePath),
          QStringLiteral("parseQuickLaunchFile 回读: %1 %2").arg(parsedPath, parseError));

    // ===== 5. 注册表关联 =====
    QSettings extensionKey("HKEY_CURRENT_USER\\Software\\Classes\\.blockbox", QSettings::NativeFormat);
    check(extensionKey.value(".").toString() == "BlockBox.InstanceFile",
          QStringLiteral(".blockbox → %1").arg(extensionKey.value(".").toString()));
    QSettings commandKey("HKEY_CURRENT_USER\\Software\\Classes\\BlockBox.InstanceFile\\shell\\open\\command",
                         QSettings::NativeFormat);
    const QString openCommand = commandKey.value(".").toString();
    // 形如 "<exe 路径>" "%1"（验证程序路径为测试 exe 本身）
    check(openCommand.startsWith(QLatin1Char('"'))
          && openCommand.endsWith(QStringLiteral("\" \"%1\""))
          && openCommand.contains(QStringLiteral("test_quicklaunch.exe")),
          QStringLiteral("open command = %1").arg(openCommand));
    QSettings iconKey("HKEY_CURRENT_USER\\Software\\Classes\\BlockBox.InstanceFile\\DefaultIcon",
                      QSettings::NativeFormat);
    check(iconKey.value(".").toString() == QStringLiteral("\"%1\",0"),
          QStringLiteral("DefaultIcon = %1").arg(iconKey.value(".").toString()));

    // ===== 6. 改名后刷新：旧文件删除、新文件按新名生成 =====
    const QString newDisplayName = QStringLiteral("测试实例甲");
    SettingsManager::instance()->setProperty("instance/" + instancePath + "/displayName", newDisplayName);
    QuickLaunchManager::refreshDesktopShortcut(instancePath);
    const QString renamed = desktop + "/" + newDisplayName + ".blockbox";
    check(QFile::exists(renamed) && !QFile::exists(shortcut),
          QStringLiteral("改名后桌面文件同步: 新文件存在=%1 旧文件已删=%2")
              .arg(QFile::exists(renamed)).arg(!QFile::exists(shortcut)));

    // ===== 7. 清理：桌面文件 / 注册表键 / 临时目录 =====
    QFile::remove(renamed);
#ifdef Q_OS_WIN
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.blockbox");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\BlockBox.InstanceFile");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\Applications\\BlockBox.exe");
#endif
    QDir(instancePath).removeRecursively();
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + "/blockbox_ql_test_settings.ini");

    check(failures == 0, failures == 0 ? QStringLiteral("=== 全部验证通过 ===")
                                       : QStringLiteral("=== %1 项验证失败 ===").arg(failures));
    return failures;
}
