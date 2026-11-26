/**
 * @file   language_pack_download_probe.cpp
 * @brief  语言包按需下载自测：本地起一个静态服务器（见同名 .md 说明），
 *         走 LanguageManager 的下载通道，验证落盘、校验与加载。
 *
 * 用法：
 *   python -m http.server 18765 --directory translations
 *   set BLOCKBOX_LANGUAGE_PACK_BASE_URL=http://127.0.0.1:18765/
 *   qmake language_pack_download_probe.pro && mingw32-make && .\release\language_pack_download_probe.exe
 */
#include "utils/LanguageManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QTranslator>

namespace {

int failures = 0;

void check(bool ok, const QString &what)
{
    if (ok) {
        qInfo().noquote() << "  [OK]  " << what;
    } else {
        qCritical().noquote() << "  [FAIL]" << what;
        ++failures;
    }
}

/// 跑到 languagePackDownloadFinished / Failed 或超时为止
bool waitForDownload(LanguageManager *manager, LanguageManager::Language lang,
                     QString *error, bool force = false, int timeoutMs = 40000)
{
    QEventLoop loop;
    bool done = false;
    bool ok = false;

    QObject::connect(manager, &LanguageManager::languagePackDownloadFinished, &loop,
                     [&](LanguageManager::Language finishedLang) {
        if (finishedLang != lang)
            return;
        done = true;
        ok = true;
        loop.quit();
    });
    QObject::connect(manager, &LanguageManager::languagePackDownloadFailed, &loop,
                     [&](LanguageManager::Language failedLang, const QString &reason) {
        if (failedLang != lang)
            return;
        done = true;
        ok = false;
        if (error)
            *error = reason;
        loop.quit();
    });

    QTimer::singleShot(timeoutMs, &loop, [&]() {
        if (!done)
            loop.quit();
    });

    manager->downloadLanguagePack(lang, force);
    loop.exec();
    return ok;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBoxLanguagePackProbe"));

    qInfo().noquote() << "语言包下载自测开始";
    qInfo().noquote() << "  下载来源:" << qEnvironmentVariable("BLOCKBOX_LANGUAGE_PACK_BASE_URL");
    qInfo().noquote() << "  本机目录:" << LanguageManager::languagePackDir();

    LanguageManager *manager = LanguageManager::instance();

    // 清掉上次运行留下的语言包，保证从零开始
    QFile::remove(LanguageManager::languagePackPath(LanguageManager::Spanish));

    // ── 1. 内置 / 需下载的判定 ──
    check(LanguageManager::isBundled(LanguageManager::Chinese), "简体中文内置");
    check(LanguageManager::isBundled(LanguageManager::English), "英文内置");
    check(!LanguageManager::isBundled(LanguageManager::Spanish), "西班牙语不走内置");
    check(LanguageManager::isInstalled(LanguageManager::English), "内置语言视为已可用");
    check(!LanguageManager::isInstalled(LanguageManager::Spanish), "没下过时西班牙语不可用");

    // 失败路径：来源里没有这个文件时，必须报失败且不留下半截文件
    if (QCoreApplication::arguments().contains(QStringLiteral("--expect-fail"))) {
        QString failError;
        const bool failedAsExpected =
            !waitForDownload(manager, LanguageManager::Spanish, &failError);
        check(failedAsExpected, "下载不到时报失败：" + failError);
        check(!QFile::exists(LanguageManager::languagePackPath(LanguageManager::Spanish)),
              "失败后没有留下语言包文件");
        qInfo().noquote() << (failures == 0 ? "全部通过" : QStringLiteral("失败 %1 项").arg(failures));
        return failures == 0 ? 0 : 1;
    }

    // ── 2. 下载西班牙语语言包 ──
    QString error;
    const bool ok = waitForDownload(manager, LanguageManager::Spanish, &error);
    check(ok, QStringLiteral("下载完成（%1）").arg(ok ? QStringLiteral("成功") : error));
    if (!ok)
        return 1;

    const QString packPath = LanguageManager::languagePackPath(LanguageManager::Spanish);
    check(QFile(packPath).exists() && QFileInfo(packPath).size() > 0,
          "语言包已落盘：" + packPath);
    check(LanguageManager::isInstalled(LanguageManager::Spanish), "下载后可判定为可用");
    check(!LanguageManager::needsLanguagePackRefresh(LanguageManager::Spanish),
          "下载时记下了软件版本，不需要再刷新");

    // ── 3. 落盘的文件确实是能用的翻译 ──
    QTranslator translator;
    check(translator.load(packPath), "语言包能被 QTranslator 加载");
    const QString translated =
        translator.translate("SettingsPage", "下载语言包");
    check(translated == QStringLiteral("Descargar paquete de idioma"),
          "翻译内容正确：" + translated);

    // ── 4. 重复调用不应再次下载（已是可用状态） ──
    const QString before = QString::number(QFileInfo(packPath).lastModified().toSecsSinceEpoch());
    manager->downloadLanguagePack(LanguageManager::Spanish);
    const QString after = QString::number(QFileInfo(packPath).lastModified().toSecsSinceEpoch());
    check(before == after, "已装好的语言不会重复下载");

    // ── 5. 语言包版本过旧时能强制刷新 ──
    QFile::remove(packPath + QStringLiteral(".version"));
    check(LanguageManager::needsLanguagePackRefresh(LanguageManager::Spanish),
          "没有版本标记时判定需要更新");
    QString refreshError;
    check(waitForDownload(manager, LanguageManager::Spanish, &refreshError, true),
          "强制刷新下载成功：" + refreshError);
    check(!LanguageManager::needsLanguagePackRefresh(LanguageManager::Spanish),
          "刷新后版本标记已更新");

    qInfo().noquote() << (failures == 0 ? "全部通过" : QStringLiteral("失败 %1 项").arg(failures));
    return failures == 0 ? 0 : 1;
}
