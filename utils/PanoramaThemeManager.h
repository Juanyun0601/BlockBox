/**
 * @file   PanoramaThemeManager.h
 * @brief  旋转全景主题素材的按需下载与本地缓存
 * @author BlockBox Team
 *
 * 旋转背景使用的六张全景图不再编进安装包，而是首次选中某个主题时
 * 从发布仓库的 CDN 下载到本机缓存，之后离线也能直接使用。
 */
#ifndef PANORAMATHEMEMANAGER_H
#define PANORAMATHEMEMANAGER_H

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief 旋转全景主题下载管理器（单例）
 *
 * 每个主题固定六张立方体面贴图（panorama_0..5.jpg），下载完成后存到
 * {数据目录}/panorama/themes/<主题名>/。同一时间只下载一个主题，
 * 切到别的主题时会取消上一个未完成的下载。
 */
class PanoramaThemeManager : public QObject
{
    Q_OBJECT

public:
    static PanoramaThemeManager* instance();

    /// 内置主题标识列表（与设置页「全景场景」下拉一一对应）
    static QStringList themeIds();
    /// 默认主题（自定义图片未指定图片时兜底使用）
    static QString defaultThemeId();
    /// 是否为已知的内置主题
    static bool isBuiltinTheme(const QString &themeId);
    /// 每个主题的图片数量（固定六张）
    static int fileCount();
    /// 主题素材的本地目录；主题名非法时返回空串
    static QString themeDir(const QString &themeId);
    /// 六张图是否都已下载且文件头有效
    static bool isThemeReady(const QString &themeId);
    /// 中文显示名（仅用于日志等兜底，界面文案仍由设置页提供）
    static QString displayName(const QString &themeId);

    /// 指定主题是否正在下载
    bool isDownloading(const QString &themeId) const;
    /// 当前正在下载的主题（没有则为空）
    QString activeTheme() const { return m_activeTheme; }
    /// 当前主题已完成的图片数量
    int completedFiles() const { return m_completedFiles; }

    /**
     * @brief 确保主题素材已就绪（幂等）
     *
     * 已下载完成时立即发出 themeReady；否则创建缓存目录并开始下载。
     * replaceActive 为 true（用户主动选主题）时会取消正在下载的另一个主题；
     * 为 false（后台自动补素材）时不动正在进行的下载，避免把用户选的主题取消掉。
     */
    void ensureTheme(const QString &themeId, bool replaceActive = false);

    /// 取消下载：themeId 为空时取消当前任务
    void cancelTheme(const QString &themeId = QString());

signals:
    /// 下载进度：completed/total 为已完成图片数，currentFilePercent 为当前这张的百分比
    void themeProgress(const QString &themeId, int completed, int total,
                       qint64 receivedBytes, int currentFilePercent);
    /// 主题六张图全部就绪，可以应用
    void themeReady(const QString &themeId);
    /// 下载失败，reason 为给用户看的提示
    void themeFailed(const QString &themeId, const QString &reason);

private:
    explicit PanoramaThemeManager(QObject *parent = nullptr);

    void startNextFile();
    void requestFile(int index);
    void handleReplyFinished();
    void handleDownloadProgress(qint64 received, qint64 total);
    void tryNextSourceOrFail(const QString &reason);
    void finishTheme();
    void failTheme(const QString &reason);
    void resetActiveState();

    static QString fileNameFor(int index);
    static QStringList sourceBaseUrls();
    static bool fileLooksValid(const QString &path);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeout = nullptr;

    QString m_activeTheme;
    int m_fileIndex = 0;
    int m_completedFiles = 0;
    int m_sourceIndex = 0;
    int m_currentFilePercent = 0;
    qint64 m_receivedBytes = 0;

    static PanoramaThemeManager* m_instance;
    static QMutex m_instanceMutex;
};

#endif // PANORAMATHEMEMANAGER_H
