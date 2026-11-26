/**
 * @file   CompatMatrixFetcher.h
 * @brief  实例体检数据获取器：本地离线检查 + 下载源批量查询
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 一次体检分两段：
 *  1) 后台线程跑本地检测（LocalCompatibilityChecker）与模组扫描（ModScanner），
 *     先出「问题报告」，界面不用等网络；
 *  2) 主线程用本地文件的 SHA-1 去 Modrinth 批量匹配项目（两次批量请求，
 *     不按模组逐个查），合成「兼容矩阵」。
 *
 * 网络失败或本地文件在下载源查不到时，结果一律标记为「未收录 / 未知」，
 * 不猜版本、不编造支持情况。
 */
#ifndef COMPATMATRIXFETCHER_H
#define COMPATMATRIXFETCHER_H

#include <QFutureWatcher>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>

#include "utils/compat/CompatMatrix.h"
#include "utils/compat/LocalCompatibilityChecker.h"
#include "utils/mod/ModData.h"

class ModrinthAPI;

/**
 * @brief 体检数据获取器
 *
 * 生命周期：start() → localReady() → matrixReady()/matrixFailed()。
 * 同一时刻只允许一次体检，重复调用 start() 会先复位上一次的状态。
 */
class CompatMatrixFetcher : public QObject
{
    Q_OBJECT

public:
    explicit CompatMatrixFetcher(QObject *parent = nullptr);
    ~CompatMatrixFetcher() override;

    /** 开始一次体检（实例路径必填，版本/加载器可为空） */
    void start(const QString &instancePath,
               const QString &gameVersion,
               const QString &loaderType);

    bool isRunning() const { return m_running; }

signals:
    /** 本地离线检测完成（问题报告 + 模组列表），线上矩阵仍在查询中 */
    void localReady(const CompatReport &report, const QList<ModInfo> &mods);
    /** 查询进度文案（用于界面提示） */
    void progress(const QString &text);
    /** 兼容矩阵完成 */
    void matrixReady(const CompatMatrixReport &report);
    /** 体检过程异常（本地都失败时才会触发） */
    void failed(const QString &error);

private slots:
    void onLocalFinished();
    void onHashesMatched(const QMap<QString, ModVersionFile> &versions);
    void onHashMatchFailed(const QString &error);
    void onProjectsResolved(const QList<ModInfo> &projects);
    void onProjectsFailed(const QString &error);
    void onNetworkTimeout();

private:
    /** 只用本地数据出矩阵（网络不可用时的降级路径） */
    void emitLocalOnlyMatrix(const QString &note);
    /** 用匹配到的项目信息填充矩阵行 */
    QList<CompatMatrixRow> buildRows(const QMap<QString, ModVersionFile> &installed,
                                     const QList<ModInfo> &projects) const;
    void stopTimer();

    struct LocalResult
    {
        CompatReport report;
        LocalModList mods;
    };

    QString m_instancePath;
    QString m_gameVersion;
    QString m_loaderType;
    bool m_running = false;

    CompatReport m_localReport;
    LocalModList m_modList;
    QMap<QString, ModVersionFile> m_installed;

    ModrinthAPI *m_api = nullptr;
    QFutureWatcher<LocalResult> *m_watcher = nullptr;
    QTimer m_timeoutTimer;
};

#endif // COMPATMATRIXFETCHER_H
