/**
 * @file   SelfIntegrityChecker.h
 * @brief  本地文件自校验：把程序目录里的文件与官方公示清单逐个比对
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 规则（见 .tmp/trust-spec.md 2.2）：
 *   - 以 QCoreApplication::applicationDirPath() 为根，清单里的 path 都是相对它的正斜杠路径；
 *   - 四类结果：正常 / 被改动 / 缺失 / 清单外多出的文件（只查根层 .exe / .dll，
 *     这两类文件是 DLL 劫持最常被替换的目标，递归报噪音没有意义）；
 *   - 清单里没有当前版本时不是错误：照样把本机主程序哈希算出来给用户看。
 *
 * 全部是静态纯函数，可以在后台线程跑（QtConcurrent::run），不碰界面。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "utils/trust/TrustManifest.h"

/** 单个文件的比对结果 */
struct IntegrityFileResult
{
    QString path;           ///< 相对程序目录（正斜杠）
    QString expectedSha256; ///< 清单里的哈希（可能为空：只公示了大小）
    QString actualSha256;   ///< 本机算出来的哈希（算不出来时为空）
    qint64 expectedSize = 0;
    qint64 actualSize = 0;
};

/** 一次自校验的完整结果 */
struct IntegrityReport
{
    bool manifestAvailable = false; ///< 是否拿到了当前版本的文件清单
    QString version;                ///< 清单里的版本号
    QString platform;               ///< 清单里的平台键
    QString arch;                   ///< 清单里的架构键
    QString generatedAt;            ///< 清单生成时间
    QString rootDir;                ///< 实际校验的程序目录
    QString mainProgramPath;        ///< 本机主程序完整路径
    QString mainProgramSha256;      ///< 本机主程序哈希（读不到时为空）

    QVector<IntegrityFileResult> okFiles;        ///< 与清单一致
    QVector<IntegrityFileResult> modifiedFiles;  ///< 哈希 / 大小对不上
    QVector<IntegrityFileResult> missingFiles;   ///< 清单里有、本机没有
    QStringList extraFiles;                      ///< 清单外多出的根层 .exe / .dll
    QStringList unreadableFiles;                 ///< 存在但读不出来（权限 / 占用）

    /** 参与比对的文件数 */
    int checkedCount() const
    {
        return okFiles.size() + modifiedFiles.size() + missingFiles.size()
               + unreadableFiles.size();
    }
    /** 是否有异常（被改动 / 缺失 / 清单外多出 / 读不出来） */
    bool hasAnomaly() const
    {
        return !modifiedFiles.isEmpty() || !missingFiles.isEmpty() || !extraFiles.isEmpty()
               || !unreadableFiles.isEmpty();
    }

    /** 一句话结论（状态文字 / 弹窗标题下方） */
    QString statusText() const;
    /** 完整报告（「复制报告」用，纯文本） */
    QString reportText() const;
    /**
     * @brief 弹窗里显示的分类明细
     * @param perCategoryLimit 每类最多列几条（0 = 不限），超出部分写「还有 N 个」
     */
    QStringList detailLines(int perCategoryLimit = 0) const;
};

class SelfIntegrityChecker
{
public:
    /** 程序所在目录（applicationDirPath，拿不到时退回当前目录） */
    static QString applicationRoot();
    /** 本机主程序完整路径 */
    static QString mainProgramPath();
    /** 任意文件的 SHA-256（小写十六进制；打不开或读失败返回空串） */
    static QString calculateSha256(const QString &filePath);
    /** 本机主程序的 SHA-256 */
    static QString mainProgramSha256();

    /**
     * @brief 执行一次校验
     * @param manifest 官方文件清单（为空时只算主程序哈希）
     * @param rootDir 程序目录，空则用 applicationRoot()
     */
    static IntegrityReport check(const TrustFileManifest &manifest,
                                 const QString &rootDir = QString());
};
