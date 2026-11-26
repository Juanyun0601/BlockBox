#include "LanguageManager.h"

#include "utils/AppVersion.h"
#include "utils/AssetUrls.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

namespace {

/// 语言包在官网 / CDN 上的目录（域名见 utils/AssetUrls.h）
const char *const kLanguagePackPath = "/assets/translations/";

/// 语言包在本机存放目录（应用数据目录下的子目录）
const char *const kLanguagePackDirName = "translations";

/// 单个语言包请求超时
constexpr int kPackTimeoutMs = 30000;

} // namespace

LanguageManager *LanguageManager::m_instance = nullptr;

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_translator(new QTranslator(this))
    , m_currentLanguage(Chinese)
    , m_initialized(false)
{
}

LanguageManager::~LanguageManager()
{
    abortLanguagePackReply();

    QMutexLocker locker(&m_mutex);
    if (m_translator && !m_translator->isEmpty()) {
        qApp->removeTranslator(m_translator);
    }
    m_instance = nullptr;
}

LanguageManager *LanguageManager::instance()
{
    if (!m_instance) {
        static QMutex instanceMutex;
        QMutexLocker locker(&instanceMutex);
        if (!m_instance) {
            m_instance = new LanguageManager();
        }
    }
    return m_instance;
}

void LanguageManager::setLanguage(Language lang)
{
    QMutexLocker locker(&m_mutex);

    if (m_currentLanguage == lang && !m_translator->isEmpty()) {
        return;
    }

    locker.unlock();
    setLanguageInternal(lang);
}

static const char *languageFileName(LanguageManager::Language lang)
{
    switch (lang) {
    case LanguageManager::English:             return "BlockBox_en.qm";
    case LanguageManager::ChineseTraditional:   return "BlockBox_zh_Hant.qm";
    case LanguageManager::Spanish:             return "BlockBox_es.qm";
    case LanguageManager::Japanese:            return "BlockBox_ja.qm";
    case LanguageManager::Russian:             return "BlockBox_ru.qm";
    case LanguageManager::Chinese:
    default:                                   return "BlockBox_zh.qm";
    }
}

static const char *languageCode(LanguageManager::Language lang)
{
    switch (lang) {
    case LanguageManager::English:             return "en";
    case LanguageManager::ChineseTraditional:   return "zh_Hant";
    case LanguageManager::Spanish:             return "es";
    case LanguageManager::Japanese:            return "ja";
    case LanguageManager::Russian:             return "ru";
    case LanguageManager::Chinese:
    default:                                   return "zh";
    }
}

/// QSettings 里存的语言代号 → 枚举（未知代号一律回退简体中文）
static LanguageManager::Language languageFromCode(const QString &code)
{
    if (code == "en")       return LanguageManager::English;
    if (code == "zh_Hant")  return LanguageManager::ChineseTraditional;
    if (code == "es")       return LanguageManager::Spanish;
    if (code == "ja")       return LanguageManager::Japanese;
    if (code == "ru")       return LanguageManager::Russian;
    return LanguageManager::Chinese;
}

// ============================================================================
// 语言包：内置 / 本机 / 下载
// ============================================================================

bool LanguageManager::isBundled(Language lang)
{
    // 只有简体中文和英文的翻译随安装包分发，其余语言走按需下载
    return lang == Chinese || lang == English;
}

QString LanguageManager::languagePackDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QLatin1Char('/') + QLatin1String(kLanguagePackDirName);
}

QString LanguageManager::languagePackPath(Language lang)
{
    return languagePackDir() + QLatin1Char('/')
           + QString::fromLatin1(languageFileName(lang));
}

/// 语言包下载时记下的软件版本（用来判断软件升级后语言包是否该刷新）
static QString languagePackVersionPath(LanguageManager::Language lang)
{
    return LanguageManager::languagePackPath(lang) + QStringLiteral(".version");
}

bool LanguageManager::needsLanguagePackRefresh(Language lang)
{
    if (isBundled(lang) || !isInstalled(lang))
        return false;

    QFile stamp(languagePackVersionPath(lang));
    if (!stamp.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;   // 早期版本下载的语言包没有版本标记：补一次
    const QString stampVersion = QString::fromUtf8(stamp.readAll()).trimmed();
    return stampVersion != AppVersion::current();
}

bool LanguageManager::isInstalled(Language lang)
{
    if (isBundled(lang))
        return true;

    const QFileInfo info(languagePackPath(lang));
    return info.exists() && info.size() > 0;
}

QString LanguageManager::nativeName(Language lang)
{
    switch (lang) {
    case Language::Chinese:            return QStringLiteral("\u7B80\u4F53\u4E2D\u6587");
    case Language::ChineseTraditional: return QStringLiteral("\u7E41\u9AD4\u4E2D\u6587");
    case Language::English:            return QStringLiteral("English");
    case Language::Spanish:            return QStringLiteral("Espa\u00F1ol");
    case Language::Japanese:           return QStringLiteral("\u65E5\u672C\u8A9E");
    case Language::Russian:            return QStringLiteral("\u0420\u0443\u0441\u0441\u043A\u0438\u0439");
    }
    return QStringLiteral("\u7B80\u4F53\u4E2D\u6587");
}

bool LanguageManager::hasPendingLanguagePack() const
{
    QMutexLocker locker(&m_mutex);
    return m_hasPendingPack;
}

LanguageManager::Language LanguageManager::pendingLanguagePack() const
{
    QMutexLocker locker(&m_mutex);
    return m_pendingPackLanguage;
}

void LanguageManager::setLanguagePreference(Language lang)
{
    // 仅写 QSettings，不安装翻译器、不触发 languageChanged。
    // 下次进程启动时由 loadLanguageSync() 读取并真正应用。
    QSettings settings("BlockBox", "BlockBox");
    settings.setValue("language", QString::fromLatin1(languageCode(lang)));
    settings.sync();
}

void LanguageManager::setLanguageInternal(Language lang)
{
    // 记下用户真正想要的语言：语言包缺失时会回落成简体中文，
    // 回落的那次不能覆盖已保存的语言偏好。
    const Language requested = lang;

    QMutexLocker locker(&m_mutex);

    if (!m_translator->isEmpty()) {
        qApp->removeTranslator(m_translator);
        m_translator->deleteLater();
        m_translator = new QTranslator(this);
    }

    const QString translationFileName = QString::fromLatin1(languageFileName(lang));
    locker.unlock();

    // 内置语言读资源，其余语言读本机下好的语言包
    bool loaded = false;
    if (isBundled(lang)) {
        loaded = m_translator->load(QStringLiteral(":/translations/") + translationFileName);
    } else {
        const QString packPath = languagePackPath(lang);
        if (QFileInfo(packPath).size() > 0) {
            loaded = m_translator->load(packPath);
            if (!loaded) {
                // 文件坏了（下载中断、磁盘异常）：删掉，界面上就能重新下载
                qWarning() << "[LanguageManager] 语言包损坏，已删除：" << packPath;
                QFile::remove(packPath);
                QFile::remove(languagePackVersionPath(lang));
            }
        }
    }

    if (!loaded && !isBundled(lang)) {
        // 偏好语言的语言包不在本机（新装机器、清理过数据、从旧版本升级）：
        // 偏好照旧留着，先用内置简体中文显示，界面会提示下载语言包，装好后重启即可。
        qWarning() << "[LanguageManager] 语言包未安装：" << translationFileName;
        {
            QMutexLocker stateLock(&m_mutex);
            m_hasPendingPack = true;
            m_pendingPackLanguage = lang;
        }
        loaded = m_translator->load(QStringLiteral(":/translations/BlockBox_zh.qm"));
        if (loaded)
            lang = Chinese;
    }

    locker.relock();

    if (loaded) {
        locker.unlock();
        qApp->installTranslator(m_translator);
        locker.relock();

        m_currentLanguage = lang;
        if (lang == requested) {
            // 真正切换到用户选择（或上次保存）的语言才回写偏好、清掉「缺语言包」标记；
            // 回落成简体中文时保留原来的选择，语言包装好重启即可恢复。
            m_hasPendingPack = false;
            QSettings settings("BlockBox", "BlockBox");
            settings.setValue("language", QString::fromLatin1(languageCode(lang)));
            settings.sync();
        }

        locker.unlock();
        emit languageChanged();
        return;
    }

    qWarning() << "[LanguageManager] 加载失败：" << translationFileName;
}

void LanguageManager::loadLanguageSync()
{
    QSettings settings("BlockBox", "BlockBox");
    const Language lang = languageFromCode(settings.value("language", "zh").toString());
    setLanguageInternal(lang);
    {
        QMutexLocker locker(&m_mutex);
        m_initialized = true;
    }
}

LanguageManager::Language LanguageManager::currentLanguage() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentLanguage;
}

QFuture<void> LanguageManager::initializeAsync()
{
    return QtConcurrent::run([this]() {
        QSettings settings("BlockBox", "BlockBox");
        const Language lang = languageFromCode(settings.value("language", "zh").toString());

        QMetaObject::invokeMethod(this, [this, lang]() {
            if (m_translator->isEmpty())
                setLanguageInternal(lang);
            else
                setLanguage(lang);

            {
                QMutexLocker locker(&m_mutex);
                m_initialized = true;
            }

            emit initialized();
        }, Qt::QueuedConnection);
    });
}

bool LanguageManager::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

// ============================================================================
// 语言包下载
// ============================================================================

QNetworkAccessManager *LanguageManager::networkManager()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

/// 可用的下载来源：CDN 优先、官网兜底（本地调试可用环境变量顶到最前）
static QStringList languagePackBaseUrls()
{
    QStringList urls;

    const QString custom = qEnvironmentVariable("BLOCKBOX_LANGUAGE_PACK_BASE_URL").trimmed();
    if (!custom.isEmpty()) {
        QString base = custom;
        while (base.endsWith(QLatin1Char('/')))
            base.chop(1);
        urls.append(base + QLatin1Char('/'));
    }

    urls.append(AssetUrls::baseCandidates(QString::fromLatin1(kLanguagePackPath)));
    return urls;
}

void LanguageManager::downloadLanguagePack(Language lang, bool force)
{
    if (m_packDownloading || m_packReply)
        return;
    if (isBundled(lang))
        return;
    if (!force && isInstalled(lang))
        return;

    if (!QDir().mkpath(languagePackDir())) {
        emit languagePackDownloadFailed(lang, tr("无法创建语言包存放目录，请检查磁盘空间"));
        return;
    }

    m_packDownloading = true;
    m_packLanguage = lang;
    m_packSourceIndex = 0;
    emit languagePackDownloadStarted(lang);
    requestLanguagePack(lang, 0);
}

bool LanguageManager::isDownloadingLanguagePack() const
{
    return m_packDownloading;
}

LanguageManager::Language LanguageManager::downloadingLanguage() const
{
    return m_packLanguage;
}

void LanguageManager::requestLanguagePack(Language lang, int sourceIndex)
{
    const QStringList bases = languagePackBaseUrls();
    if (sourceIndex < 0 || sourceIndex >= bases.size()) {
        m_packDownloading = false;
        m_packReply = nullptr;
        emit languagePackDownloadFailed(lang, tr("网络连接失败，请检查网络后重试"));
        return;
    }

    m_packSourceIndex = sourceIndex;
    // 带上版本号查询串：每个版本的语言包 URL 都不同，CDN 不会把旧翻译长期缓存给新版客户端
    QUrl url(bases.at(sourceIndex) + QString::fromLatin1(languageFileName(lang)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), AppVersion::current());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setTransferTimeout(kPackTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // 语言包随版本更新，不要命中 Qt 的 HTTP 缓存
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBox/%1").arg(AppVersion::current()));

    QNetworkReply *reply = networkManager()->get(request);
    m_packReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, lang](qint64 received, qint64 total) {
        if (!m_packDownloading || m_packLanguage != lang)
            return;
        emit languagePackDownloadProgress(lang, received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, lang, sourceIndex]() {
        handleLanguagePackReply(reply, lang, sourceIndex);
    });
}

void LanguageManager::handleLanguagePackReply(QNetworkReply *reply, Language lang, int sourceIndex)
{
    if (m_packReply == reply)
        m_packReply = nullptr;

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (!m_packDownloading || m_packLanguage != lang)
        return;   // 下载已被放弃，丢弃这次回调

    if (networkError != QNetworkReply::NoError) {
        retryNextSource(lang, sourceIndex, networkErrorText);
        return;
    }
    if (status > 0 && status != 200) {
        retryNextSource(lang, sourceIndex, tr("服务器返回 HTTP %1").arg(status));
        return;
    }
    if (payload.isEmpty()) {
        retryNextSource(lang, sourceIndex, tr("下载到的内容是空的"));
        return;
    }

    const QString path = languagePackPath(lang);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(payload) != payload.size()
        || !file.commit()) {
        m_packDownloading = false;
        emit languagePackDownloadFailed(lang, tr("语言包保存失败，请检查磁盘空间"));
        return;
    }

    // 落盘后确认文件确实能被当翻译装上，避免把错误页存成语言包
    {
        QTranslator probe;
        if (!probe.load(path)) {
            QFile::remove(path);
            retryNextSource(lang, sourceIndex, tr("下载到的文件不是有效的语言包"));
            return;
        }
    }

    // 记下这次语言包对应的软件版本：升级后据此在后台补一次新翻译
    QFile stamp(languagePackVersionPath(lang));
    if (stamp.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        stamp.write(AppVersion::current().toUtf8());

    m_packDownloading = false;
    emit languagePackDownloadProgress(lang, payload.size(), payload.size());
    emit languagePackDownloadFinished(lang);
}

void LanguageManager::retryNextSource(Language lang, int failedIndex, const QString &reason)
{
    const QStringList bases = languagePackBaseUrls();
    if (failedIndex >= 0 && failedIndex < bases.size()) {
        qWarning() << "[LanguageManager] 语言包从" << bases.at(failedIndex)
                   << "下载失败，换下一个来源:" << reason;
    }
    requestLanguagePack(lang, failedIndex + 1);
}

void LanguageManager::abortLanguagePackReply()
{
    if (!m_packReply)
        return;

    QNetworkReply *reply = m_packReply;
    m_packReply = nullptr;
    m_packDownloading = false;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}
