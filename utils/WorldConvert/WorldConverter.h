/**
 * @file   WorldConverter.h
 * @brief  Java 版 ⇄ 基岩版 存档互转核心（内置 Chunker CLI 引擎，无需插件）
 * @author BlockBox Team
 * @date   2026-10-17
 *
 * 转换引擎为 HiveGamesOSS/Chunker（MIT 协议）的命令行版本 chunker-cli，
 * 随启动器分发于程序目录 libs/ 下，运行时通过 Java 17+ 调用。
 */
#ifndef WORLDCONVERTER_H
#define WORLDCONVERTER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace WorldConverter {

/** 转换方向 */
enum class Direction
{
    JavaToBedrock,
    BedrockToJava
};

/** 存档版本类型（用于自动识别输入） */
enum class WorldEdition
{
    Unknown,
    Java,
    Bedrock
};

/** 转换质量与输出选项 */
struct Options
{
    bool discardEmpty = true;    ///< 丢弃空区块（避免输出出现空洞）
    bool enableCompact = true;   ///< LevelDB 压缩（转基岩版时更健康）
    bool keepContent = true;     ///< 保留地图/物品/方块连接/战利品表
    bool preventYBlend = false;  ///< 生物群系 Y 混合修正（Java 1.18+）
    bool packageOutput = true;   ///< 转换完成后打包为 .mcworld / .zip
};

/** 引擎版本（chunker-cli 文件名的一部分） */
QString engineVersion();

/** 支持的 Java 版本列表（用于目标版本下拉） */
QStringList javaVersions();

/** 支持的基岩版版本列表 */
QStringList bedrockVersions();

/** Java 版本号 → Chunker 格式 ID（1.21.4 → JAVA_1_21_4） */
QString formatIdForJava(const QString &version);

/** 基岩版版本号 → Chunker 格式 ID（1.21.40 → BEDROCK_R21_40） */
QString formatIdForBedrock(const QString &version);

/** 目标版本 ID 转显示名（JAVA_1_21_4 → Java版 1.21.4） */
QString formatIdToLabel(const QString &formatId);

/**
 * @brief 查找转换引擎 chunker-cli jar
 * @return jar 绝对路径；找不到返回空串
 *
 * 搜索顺序：数据目录（BlockBox/world_convert/，下载缓存）→
 * 程序目录/libs → 程序目录 → 程序目录/../libs → 工作目录/libs → 工作目录
 */
QString findEngineJar();

/** 引擎下载后的存放目录（软件同级 BlockBox/world_convert/，不存在时创建） */
QString engineDir();

/** 预期引擎完整路径（无论文件是否已存在） */
QString engineJarPath();

/** 引擎官方下载地址（直链 + 镜像兜底） */
QStringList engineDownloadUrls();

/** 引擎文件 SHA256（下载后校验，全小写十六进制） */
QString engineSha256();

/**
 * @brief 查找可用的 Java 17+ 运行时
 * @param versionText 输出参数：java -version 首行文本，可为 nullptr
 * @return java 可执行文件绝对路径；找不到返回空串
 *
 * 依次尝试：启动器设置的 Java → 常见安装目录 → JAVA_HOME → PATH
 */
QString findJavaRuntime(QString *versionText = nullptr);

/** 根据存档目录内容判断版本类型（db/=基岩，region|playerdata|data=Java） */
WorldEdition detectWorldEdition(const QString &worldDir);

/** 确保输出目录可用（不存在或为空文件夹）；不可用时返回错误说明，可用返回空串 */
QString checkOutputDir(const QString &outputDir);

/**
 * @brief 将目录打包为 .mcworld / .zip（level.dat 位于包根）
 * @return 生成的压缩包路径；失败返回空串
 */
QString packageWorldDir(const QString &dir, Direction direction, QString *errorOut = nullptr);

/**
 * @brief 一次存档转换任务（异步，事件驱动）
 *
 * start() 后通过 progress / statusChanged / logLine 上报进度，
 * 结束时发出 finished；cancel() 可中止转换进程。
 */
class WorldConvertTask : public QObject
{
    Q_OBJECT

public:
    struct Request
    {
        QString javaExe;       ///< java 可执行文件
        QString jarPath;       ///< chunker-cli jar 路径
        QString inputWorldDir; ///< 输入世界目录（含 level.dat）
        QString outputDir;     ///< 输出目录（不存在或为空）
        QString formatId;      ///< 目标格式 ID（JAVA_x / BEDROCK_Rx）
        Direction direction = Direction::JavaToBedrock;
        Options options;
    };

    explicit WorldConvertTask(QObject *parent = nullptr);
    ~WorldConvertTask() override;

    /** 启动转换（立即返回，结果经信号回调） */
    void start(const Request &request);

    /** 中止转换（杀死子进程） */
    void cancel();

    /** 是否正在运行 */
    bool isRunning() const;

signals:
    /** 百分比进度 0-100 */
    void progress(int percent);
    /** 状态文本变化 */
    void statusChanged(const QString &text);
    /** 引擎日志行 */
    void logLine(const QString &line);
    /**
     * 转换结束
     * @param ok          是否成功
     * @param exitCode   引擎退出码（12 = 内存不足）
     * @param outputPath 成功时的输出目录路径
     * @param message    结束说明（成功/失败原因）
     */
    void finished(bool ok, int exitCode, const QString &outputPath, const QString &message);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void handleLine(const QString &line);
    void finishInternal(bool ok, int exitCode, const QString &message);
    QString writeSettingsFile();

    QProcess *m_process = nullptr;
    Request m_request;
    QString m_settingsFile;
    QString m_tempDir;
    bool m_doneMarkerSeen = false; ///< 是否看到 "Conversion complete"
    bool m_finishing = false;      ///< 防止 finished 重复发出
};

/**
 * @brief 转换引擎在线下载器（首次使用时从官方 release 下载，不随启动器打包）
 *
 * 依次尝试直链与镜像，下载完成后校验 SHA256 并移动到引擎目录。
 */
class WorldEngineDownloader : public QObject
{
    Q_OBJECT

public:
    explicit WorldEngineDownloader(QObject *parent = nullptr);
    ~WorldEngineDownloader() override;

    /** 开始下载（已在目标位置且校验通过则直接成功返回） */
    void start();
    /** 取消下载 */
    void cancel();
    bool isRunning() const;

signals:
    /** 下载进度（received/total 字节，total 可能为 0 表示未知） */
    void progress(qint64 received, qint64 total);
    /** 下载完成：ok 成功；message 说明；jarPath 成功时的引擎路径 */
    void finished(bool ok, const QString &message, const QString &jarPath);

private slots:
    void onReplyFinished();

private:
    void tryNextUrl();
    void finishWith(bool ok, const QString &message, const QString &jarPath = QString());

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QStringList m_urls;
    int m_urlIndex = -1;
    QString m_tempFile;
    bool m_finishing = false;
    bool m_cancelled = false;
};

} // namespace WorldConverter

#endif // WORLDCONVERTER_H
