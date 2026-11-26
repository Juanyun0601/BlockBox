/**
 * @file   LanguageManager.h
 * @brief  多语言管理器类声明
 * @author BlockBox Team
 * @date   2026-05-10
 *
 * 简体中文与英文的界面翻译随安装包内置，装好即可用；
 * 繁體中文 / Español / 日本語 / Русский 不占安装包体积，用户在「设置 > 语言」里
 * 选中时再联网下载语言包，下好的语言包存在应用数据目录，之后断网也能直接切换。
 */
#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QApplication>
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QtConcurrent>
#include <QTranslator>

class QNetworkAccessManager;
class QNetworkReply;

class LanguageManager : public QObject
{
    Q_OBJECT

public:
    // 注意：枚举顺序与「设置 > 语言」下拉框、新手引导语言卡片的顺序一一对应，
    // 新增语言只能追加在末尾，不能插在中间，否则已保存的语言偏好会错位。
    enum Language {
        Chinese,
        ChineseTraditional,
        English,
        Spanish,
        Japanese,
        Russian
    };

    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager();

    static LanguageManager *instance();
    void setLanguage(Language lang);
    Language currentLanguage() const;

    // 仅持久化语言偏好（写入 QSettings），不安装翻译器、不发送 languageChanged。
    // 供首次引导"语言设置"步骤使用：选定后重启应用，由下次启动 loadLanguageSync() 干净加载，
    // 避免在已构建的向导上触发 LanguageChange 实时重译导致崩溃。
    void setLanguagePreference(Language lang);

    // 同步加载语言——在创建任何 UI 之前调用，确保 tr() 使用正确的翻译
    void loadLanguageSync();

    // 异步初始化
    QFuture<void> initializeAsync();

    // 检查是否已初始化完成
    bool isInitialized() const;

    // ── 语言包（不随安装包分发的语言）──────────────────────────────
    /** 该语言是否随安装包内置（内置语言不需要联网下载） */
    static bool isBundled(Language lang);

    /** 该语言现在能不能直接用：内置语言恒为 true，其余看本机有没有下好语言包 */
    static bool isInstalled(Language lang);

    /** 本机存放语言包的目录（应用数据目录下的 translations） */
    static QString languagePackDir();

    /** 语言包在本机的完整路径 */
    static QString languagePackPath(Language lang);

    /**
     * @brief 已装好的语言包是否需要更新
     *
     * 语言包是随版本走的：软件升级后界面文案会变，旧语言包缺的句子会退回中文。
     * 这里按下载时记下的版本号判断，需要时由启动流程在后台补一次。
     */
    static bool needsLanguagePackRefresh(Language lang);

    /** 语言的母语名（不随界面语言变化，用于语言列表与下载提示） */
    static QString nativeName(Language lang);

    /** 启动时因为语言包不在本机而回落成简体中文的那个语言；没有这种情况返回 false */
    bool hasPendingLanguagePack() const;
    Language pendingLanguagePack() const;

    /**
     * @brief 联网下载语言包
     * @param lang  目标语言
     * @param force true=本机已有语言包也重新下载（升级后刷新翻译用）
     *
     * 内置语言、正在下载时直接忽略；force=false 时已装好也不重复下载。
     */
    void downloadLanguagePack(Language lang, bool force = false);
    bool isDownloadingLanguagePack() const;
    Language downloadingLanguage() const;

signals:
    // 初始化完成信号
    void initialized();

    // 语言已变更（UI 可连接此信号执行 retranslateUi）
    void languageChanged();

    /** 语言包开始下载 */
    void languagePackDownloadStarted(LanguageManager::Language lang);
    /** 语言包下载进度（total<=0 表示总大小未知） */
    void languagePackDownloadProgress(LanguageManager::Language lang, qint64 received, qint64 total);
    /** 语言包下载完成并已存到本机（重启后生效） */
    void languagePackDownloadFinished(LanguageManager::Language lang);
    /** 语言包下载失败（已换过所有下载源） */
    void languagePackDownloadFailed(LanguageManager::Language lang, const QString &error);

private:
    static LanguageManager *m_instance;
    QTranslator *m_translator;
    Language m_currentLanguage;

    // 线程安全
    mutable QMutex m_mutex;
    bool m_initialized;

    // 启动时缺语言包的偏好（保留偏好不回写，等语言包装好再重启即可用）
    bool m_hasPendingPack = false;
    Language m_pendingPackLanguage = Chinese;

    // 语言包下载
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_packReply = nullptr;
    bool m_packDownloading = false;
    Language m_packLanguage = Chinese;
    int m_packSourceIndex = 0;

    void setLanguageInternal(Language lang);

    QNetworkAccessManager *networkManager();
    /** 按第 sourceIndex 个下载源取语言包 */
    void requestLanguagePack(Language lang, int sourceIndex);
    /** 当前下载源失败：换下一个；都试完就报失败 */
    void retryNextSource(Language lang, int failedIndex, const QString &reason);
    void handleLanguagePackReply(QNetworkReply *reply, Language lang, int sourceIndex);
    void abortLanguagePackReply();
};

#endif // LANGUAGEMANAGER_H
