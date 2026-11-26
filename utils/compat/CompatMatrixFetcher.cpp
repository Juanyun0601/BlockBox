/**
 * @file   CompatMatrixFetcher.cpp
 * @brief  实例体检数据获取器实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "CompatMatrixFetcher.h"

#include <QHash>
#include <QSet>
#include <QtConcurrent>

#include "utils/McimHelper.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/mod/ModScanner.h"

namespace
{
/** 网络阶段的总超时：批量请求正常几秒内返回，超过这个时间按「数据源不可用」降级 */
const int kNetworkTimeoutMs = 45000;
} // namespace

CompatMatrixFetcher::CompatMatrixFetcher(QObject *parent)
    : QObject(parent)
    , m_api(new ModrinthAPI(this))
    , m_watcher(new QFutureWatcher<LocalResult>(this))
{
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(kNetworkTimeoutMs);

    connect(m_watcher, &QFutureWatcher<LocalResult>::finished,
            this, &CompatMatrixFetcher::onLocalFinished);
    connect(&m_timeoutTimer, &QTimer::timeout,
            this, &CompatMatrixFetcher::onNetworkTimeout);
    connect(m_api, &ModrinthAPI::hashesMatched,
            this, &CompatMatrixFetcher::onHashesMatched);
    connect(m_api, &ModrinthAPI::hashesMatchFailed,
            this, &CompatMatrixFetcher::onHashMatchFailed);
    connect(m_api, &ModrinthAPI::projectsResolved,
            this, &CompatMatrixFetcher::onProjectsResolved);
    connect(m_api, &ModrinthAPI::projectsResolveFailed,
            this, &CompatMatrixFetcher::onProjectsFailed);
}

CompatMatrixFetcher::~CompatMatrixFetcher() = default;

void CompatMatrixFetcher::start(const QString &instancePath,
                                const QString &gameVersion,
                                const QString &loaderType)
{
    stopTimer();
    m_instancePath = instancePath;
    m_gameVersion = gameVersion;
    m_loaderType = loaderType;
    m_localReport = CompatReport();
    m_modList = LocalModList();
    m_installed.clear();
    m_running = true;

    if (m_instancePath.isEmpty())
    {
        m_running = false;
        emit failed(tr("实例路径为空"));
        return;
    }

    if (m_watcher->isRunning())
        m_watcher->cancel();

    emit progress(tr("正在扫描模组、资源包与光影包…"));

    const QString path = m_instancePath;
    const QString version = m_gameVersion;
    const QString loader = m_loaderType;

    // 本地检查与模组扫描都在后台线程完成：不联网也能先出「体检报告」
    m_watcher->setFuture(QtConcurrent::run([path, version, loader]() {
        LocalResult result;
        result.report = LocalCompatibilityChecker::checkInstance(path, version, loader);
        ModScanner scanner;
        scanner.scanModsSync(path, result.mods);
        return result;
    }));
}

void CompatMatrixFetcher::onLocalFinished()
{
    if (!m_running || !m_watcher->isFinished())
        return;
    if (m_watcher->isCanceled())
        return;

    const LocalResult result = m_watcher->result();
    m_localReport = result.report;
    m_modList = result.mods;

    emit localReady(m_localReport, m_modList.mods);

    // 收集本地文件哈希：没有哈希就无法在下载源定位项目
    QList<QString> sha1s;
    QSet<QString> seen;
    for (const ModInfo &mod : m_modList.mods)
    {
        const QString h = mod.sha1Hash.toLower();
        if (h.isEmpty() || seen.contains(h))
            continue;
        seen.insert(h);
        sha1s.append(h);
    }

    if (sha1s.isEmpty())
    {
        emitLocalOnlyMatrix(tr("实例里没有可查询的模组文件"));
        return;
    }

    // 与「检查更新」走同一条通路：跟随全局镜像设置，官方接口连不上时自动回退
    m_api->setBaseUrl(McimHelper::modrinthApiBaseUrl());
    emit progress(tr("正在向 Modrinth 核对 %1 个模组…").arg(sha1s.size()));
    m_timeoutTimer.start();
    m_api->matchHashes(sha1s);
}

void CompatMatrixFetcher::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    if (!m_running)
        return;

    m_installed = versions;

    QStringList projectIds;
    QSet<QString> seen;
    for (auto it = versions.constBegin(); it != versions.constEnd(); ++it)
    {
        const QString pid = it.value().projectId;
        if (pid.isEmpty() || seen.contains(pid))
            continue;
        seen.insert(pid);
        projectIds.append(pid);
    }

    if (projectIds.isEmpty())
    {
        stopTimer();
        emitLocalOnlyMatrix(tr("本地模组都没有在 Modrinth 匹配到记录"));
        return;
    }

    emit progress(tr("正在读取 %1 个资源页的支持版本…").arg(projectIds.size()));
    m_api->fetchProjectsByIds(projectIds);
}

void CompatMatrixFetcher::onHashMatchFailed(const QString &error)
{
    if (!m_running)
        return;
    stopTimer();
    emitLocalOnlyMatrix(error);
}

void CompatMatrixFetcher::onProjectsResolved(const QList<ModInfo> &projects)
{
    if (!m_running)
        return;
    stopTimer();

    QList<CompatMatrixRow> rows = buildRows(m_installed, projects);
    CompatMatrix::finalizeRows(rows, m_gameVersion, m_loaderType);

    QString note;
    if (rows.isEmpty())
        note = tr("实例里没有模组");
    else
        note = tr("按 Modrinth 资源页声明的支持版本统计；未收录的模组不计入建议");
    const CompatMatrixReport report =
        CompatMatrix::buildReport(m_gameVersion, m_loaderType, rows, true, note);
    m_running = false;
    emit matrixReady(report);
}

void CompatMatrixFetcher::onProjectsFailed(const QString &error)
{
    if (!m_running)
        return;
    stopTimer();
    emitLocalOnlyMatrix(error);
}

void CompatMatrixFetcher::onNetworkTimeout()
{
    if (!m_running)
        return;
    emitLocalOnlyMatrix(tr("查询超时"));
}

void CompatMatrixFetcher::emitLocalOnlyMatrix(const QString &note)
{
    stopTimer();

    QList<CompatMatrixRow> rows = buildRows(QMap<QString, ModVersionFile>(), QList<ModInfo>());
    CompatMatrix::finalizeRows(rows, m_gameVersion, m_loaderType);

    CompatMatrixReport report =
        CompatMatrix::buildReport(m_gameVersion, m_loaderType, rows, false, note);
    // 网络不可用时也要让「本地问题报告」里的模组能被看到：状态统一按未收录处理
    report.unknownCount = report.modCount;
    m_running = false;
    emit matrixReady(report);
}

QList<CompatMatrixRow> CompatMatrixFetcher::buildRows(
    const QMap<QString, ModVersionFile> &installed,
    const QList<ModInfo> &projects) const
{
    QHash<QString, ModInfo> projectById;
    for (const ModInfo &p : projects)
    {
        if (!p.id.isEmpty())
            projectById.insert(p.id, p);
    }

    QList<CompatMatrixRow> rows;
    rows.reserve(m_modList.mods.size());

    for (const ModInfo &mod : m_modList.mods)
    {
        CompatMatrixRow row;
        row.fileName = mod.fileName;
        row.modId = mod.id;
        row.enabled = mod.enabled;
        row.displayName = !mod.name.isEmpty() ? mod.name : mod.fileName;

        const QString sha1 = mod.sha1Hash.toLower();
        if (!sha1.isEmpty() && installed.contains(sha1))
        {
            const ModVersionFile vf = installed.value(sha1);
            row.localVersion = !vf.version.isEmpty() ? vf.version : vf.fileName;

            const ModInfo project = projectById.value(vf.projectId);
            if (!project.id.isEmpty())
            {
                row.matched = true;
                row.projectId = project.id;
                row.projectName = project.name;
                row.projectUrl = project.pageUrl;
                row.supportedGameVersions = project.gameVersions;
                row.supportedLoaders = project.loaders;
                row.projectUpdated = project.dateModified;
                row.archived = (project.projectStatus.compare(QStringLiteral("archived"),
                                                              Qt::CaseInsensitive) == 0);
                if (!project.name.isEmpty())
                    row.displayName = project.name;
            }
        }
        rows.append(row);
    }
    return rows;
}

void CompatMatrixFetcher::stopTimer()
{
    if (m_timeoutTimer.isActive())
        m_timeoutTimer.stop();
}
