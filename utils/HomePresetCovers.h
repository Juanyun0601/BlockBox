/**
 * @file   HomePresetCovers.h
 * @brief  首页预设封面图（不内置，首次使用时按需下载）
 *
 * 以前首页轮播的 8 张风景封面图是打进程序里的，安装包因此大了约 6 MB。
 * 现在改成放在官网，用户第一次进首页、封面区还是空的时候点「下载预设图」
 * 再逐张下下来，存到应用数据目录的 home_covers/：
 *  - 下好的图片下次启动直接用，不再联网；
 *  - 中途取消或断网，已经下好的会保留，下次点下载接着下；
 *  - 8 张全部下完才算「已下载」，缺任何一张首页都继续显示下载入口；
 *  - 取图优先走 CDN（见 utils/AssetUrls.h），CDN 没取到再回退官网直连。
 */

#ifndef HOMEPRESETCOVERS_H
#define HOMEPRESETCOVERS_H

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class HomePresetCovers : public QObject
{
    Q_OBJECT

public:
    /** 全局唯一实例（首次使用时创建，生命周期随程序结束） */
    static HomePresetCovers *instance();

    /** 预设封面共有几张 */
    static int presetCount();

    /** 预设封面存放目录（不存在时由下载流程按需创建） */
    static QString coverDir();

    /** 第 index 张（从 1 开始）封面在本机的完整路径（不保证已下载） */
    static QString localFile(int index);

    /**
     * @brief 本机已下载的全部预设封面
     * @return 8 张齐全时按顺序返回路径；少一张就返回空列表
     */
    static QStringList localFiles();

    /** 8 张预设封面是否都已经下载好 */
    static bool isDownloaded();

    bool isDownloading() const { return m_downloading; }
    /** 本次已下好的张数（用于显示「正在下载 3 / 8」） */
    int downloadedCount() const { return m_done; }
    int totalCount() const { return m_total; }
    /** 最近一次失败原因（面向用户的中文描述） */
    QString lastError() const { return m_lastError; }

    /** 开始（或继续）下载；正在下载时重复调用会被忽略 */
    void download();

    /** 取消本次下载，已下好的文件保留 */
    void cancel();

signals:
    /** 进度变化（done 为已完成张数，total 为总张数） */
    void progressChanged(int done, int total);
    /** 全部下载完成 */
    void finished(const QStringList &files);
    /** 下载失败（已下好的文件保留，可重试） */
    void failed(const QString &error);
    /** 下载状态变化（开始 / 结束） */
    void downloadingChanged(bool downloading);

private:
    explicit HomePresetCovers(QObject *parent = nullptr);
    ~HomePresetCovers() override;

    void startNext();
    /** 用当前候选来源（CDN / 官网）请求第 index 张 */
    void requestFile(int index);
    /** 当前来源失败时换下一个来源；都失败才算这轮下载失败 */
    void tryNextSource(int index, const QString &reason);
    void finishWithSuccess();
    void finishWithError(const QString &error);
    void abortReply();

    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_downloading = false;
    int m_total = 0;      ///< 本次要下的总张数
    int m_nextIndex = 1;  ///< 下一张要下的序号（从 1 开始）
    int m_done = 0;       ///< 本次已下好的张数
    int m_sourceIndex = 0; ///< 当前用的取图来源下标（见 coverBaseUrls()，下载成功后沿用）
    QString m_lastError;
};

#endif // HOMEPRESETCOVERS_H
