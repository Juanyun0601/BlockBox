/**
 * @file   CompatMatrix.h
 * @brief  兼容矩阵 / 实例体检的数据结构与聚合算法
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 本地检测（LocalCompatibilityChecker）只能看出实例「当下」的问题；
 * 兼容矩阵再往前一步：把每个模组映射到下载源（Modrinth）上真实的项目，
 * 用作者声明的支持版本，算出「这个整合包最好停在哪个版本」以及
 * 每个模组是「当前版本可用 / 仅支持旧版本 / 已停更 / 未收录」。
 *
 * 设计约束（见《方块盒子功能扩展调研报告》B2）：
 *  - 数据源查不到时必须显示「未知」，绝不猜测；
 *  - 只有本地文件哈希能在下载源匹配到项目时，才给出线上结论。
 */
#ifndef COMPATMATRIX_H
#define COMPATMATRIX_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "utils/compat/LocalCompatibilityChecker.h"

/**
 * @brief 兼容矩阵中的一行（一个本地模组）
 */
struct CompatMatrixRow
{
    enum Status
    {
        StatusOk = 0,             ///< 当前游戏版本仍被作者声明支持
        StatusOutdated = 1,       ///< 作者仍在维护，但当前版本已不支持（仅旧版本可用）
        StatusArchived = 2,       ///< 下载源项目已归档 / 停止维护
        StatusUnknown = 3,        ///< 本地文件在下载源查不到，无法判断
        StatusLoaderMismatch = 4  ///< 加载器与项目声明不符（数据不可信，按未知处理）
    };

    QString displayName;    ///< 展示名（优先下载源项目名，其次 JAR 内名称，最后文件名）
    QString fileName;       ///< 本地文件名
    QString modId;          ///< 模组 ID（JAR 元数据）
    QString localVersion;   ///< 本地安装的版本号（来自下载源对哈希的匹配结果，可能为空）
    bool enabled = true;    ///< 是否启用（禁用模组不参与升级可行性统计）

    bool matched = false;           ///< 是否在下载源匹配到项目
    QString projectId;
    QString projectName;
    QString projectUrl;             ///< 资源页链接
    QStringList supportedGameVersions;  ///< 项目声明支持的全部 MC 版本
    QStringList supportedLoaders;       ///< 项目声明支持的全部加载器
    QDateTime projectUpdated;       ///< 项目最后更新时间
    bool archived = false;          ///< 项目是否已归档
    bool loaderSupported = true;    ///< 项目声明的加载器是否覆盖当前实例加载器

    bool supportsCurrent = false;       ///< 当前实例版本是否有对应版本
    QString highestSupportedVersion;    ///< 作者声明支持的最高 MC 版本（含快照/预览，尽力而为）
    int status = StatusUnknown;
};

/**
 * @brief 一个候选升级目标版本的支持情况
 */
struct CompatMatrixTarget
{
    QString gameVersion;
    int supported = 0;  ///< 该版本有对应版本的已启用模组数
    int total = 0;      ///< 参与统计的已启用模组数

    double coverage() const
    {
        return total > 0 ? static_cast<double>(supported) / static_cast<double>(total) : 0.0;
    }
    bool complete() const { return total > 0 && supported >= total; }
};

/**
 * @brief 兼容矩阵报告
 */
struct CompatMatrixReport
{
    QString gameVersion;
    QString loaderType;

    int modCount = 0;        ///< 本地模组总数
    int activeCount = 0;     ///< 参与升级统计的已启用模组数（已启用且已匹配）
    int matchedCount = 0;    ///< 在下载源匹配到的数量
    int unknownCount = 0;    ///< 未收录数量
    int archivedCount = 0;   ///< 已停更数量
    int outdatedCount = 0;   ///< 仅支持旧版本的数量
    int currentSupported = 0;///< 当前版本仍有对应版本的数量

    bool networkOk = false;  ///< 是否成功拿到下载源数据
    QString note;            ///< 数据源说明（失败原因 / 查询范围），UI 原样展示

    QList<CompatMatrixRow> rows;
    QList<CompatMatrixTarget> targets;  ///< 高于当前版本的候选目标，按版本升序

    QString recommendedVersion;   ///< 高于当前版本、且所有模组都支持的最高版本；空 = 无法整体升级
    bool hasRecommendation = false;

    /**
     * @brief 所有参与统计的模组都声明支持的最高版本（可能低于当前版本）
     *
     * 「这个包建议停在哪个版本」用它回答：当前版本跑不动时，往回退到这个版本最稳。
     * stableComplete=false 表示没有任何版本能被全部模组支持。
     */
    QString stableVersion;
    bool stableComplete = false;
};

/**
 * @brief 兼容矩阵聚合算法（纯逻辑，无网络、无 UI）
 */
class CompatMatrix
{
public:
    /**
     * @brief 是否是可用于目标版本判断的正式版本号（1.21.11 / 26.1 等）
     *
     * 排除 24w14a 这类快照、1.21.2-pre1 这类预览，避免推荐到玩家装不上的版本。
     */
    static bool isReleaseVersion(const QString &version);

    /** 该项目是否声明支持指定 MC 版本 */
    static bool supportsVersion(const CompatMatrixRow &row, const QString &gameVersion);

    /**
     * @brief 归纳候选升级目标：所有项目声明支持、且高于实例当前版本的正式版本
     * @param rows    矩阵行
     * @param current 实例当前版本（可空）
     */
    static QList<CompatMatrixTarget> buildTargets(const QList<CompatMatrixRow> &rows,
                                                  const QString &current);

    /** 填充行上的 supportsCurrent / highestSupportedVersion / status 字段 */
    static void finalizeRows(QList<CompatMatrixRow> &rows,
                             const QString &gameVersion,
                             const QString &loaderType);

    /**
     * @brief 组装完整报告（统计 + 目标 + 推荐）
     * @param networkOk 是否真的从下载源拿到了数据
     * @param note      数据源说明
     */
    static CompatMatrixReport buildReport(const QString &gameVersion,
                                          const QString &loaderType,
                                          const QList<CompatMatrixRow> &rows,
                                          bool networkOk,
                                          const QString &note);

    /** 统计「当前版本仍有对应版本」的已启用模组数 */
    static int supportedAtVersion(const QList<CompatMatrixRow> &rows, const QString &gameVersion);

    /** 行状态对应的中文名（UI 与日志共用） */
    static QString statusText(int status);
};

#endif // COMPATMATRIX_H
