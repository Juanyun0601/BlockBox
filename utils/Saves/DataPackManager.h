/**
 * @file   DataPackManager.h
 * @brief  本地数据包（Java 版）扫描、启用状态、排序、预览与兼容性检测
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 数据包（data pack）按世界生效，放在 saves/<世界>/datapacks/ 下，
 * 启用状态与加载顺序保存在该世界 level.dat 的 Data.DataPacks.Enabled /
 * Disabled 字符串列表里：列表越靠后，加载越晚，同名内容会被后面的覆盖。
 *
 * 本模块只做纯数据层（Qt Core），不依赖界面：
 *   - 扫描世界目录下的 .zip / .jar / 文件夹数据包，并合并 level.dat 的启停状态；
 *   - 读取 pack.mcmeta（描述、pack_format / supported_formats / min_format+max_format）；
 *   - 解析数据包内容清单（函数、进度、配方、战利品表、结构、世界生成等）；
 *   - 按世界版本判断兼容性、检测多个数据包之间的同名文件冲突；
 *   - 写回 level.dat（原子写入，自动保留 level.dat_old 备份）；
 *   - 从实例数据包库（实例/datapacks/）或任意路径导入、删除数据包。
 *
 * 界面层见 components/DataPackManagerDialog。
 */
#ifndef DATAPACKMANAGER_H
#define DATAPACKMANAGER_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief 数据包兼容性判定结果
 */
enum class DataPackCompat
{
    Compatible,      ///< 格式与当前世界版本匹配
    VersionMismatch, ///< 格式明显偏新或偏旧，可能加载失败或内容缺失
    Unknown,         ///< 版本/格式无法识别，无法判断
    Invalid          ///< 缺少或无法读取 pack.mcmeta，不是有效数据包
};

/**
 * @brief 单个数据包的信息
 *
 * 既可表示世界 datapacks 目录里的真实文件/文件夹，也可表示 level.dat 中
 * 由原版或模组提供的内置数据包（如 "vanilla"、"mod:xxx"，这类条目不参与文
 * 件管理，仅用于保留加载顺序）。
 */
struct DataPackInfo
{
    // ---------------- 身份 ----------------
    QString id;              ///< level.dat 中的名字，如 "file/xxx.zip"、"vanilla"
    QString fileName;        ///< 显示名：文件名 / 文件夹名 / 原始 id
    QString filePath;        ///< 绝对路径（仅文件型数据包有值）
    bool isFile = false;     ///< 是否世界 datapacks 目录内的文件/文件夹
    bool isBuiltin = false;  ///< 是否原版/模组提供的内置数据包
    bool exists = true;      ///< 文件是否仍存在（level.dat 残留条目时为 false）
    bool enabled = false;    ///< 当前是否启用
    qint64 fileSize = 0;     ///< 文件大小（文件夹包为 0）
    QDateTime lastModified;  ///< 最后修改时间

    // ---------------- pack.mcmeta ----------------
    bool metaOk = false;         ///< pack.mcmeta 存在且是合法 JSON
    QString description;         ///< 数据包描述（纯文本）
    bool hasPackFormat = false;  ///< 是否声明了 pack_format
    int packFormat = 0;          ///< pack_format 主版本
    int packFormatMinor = 0;     ///< pack_format 次版本（1.21.9+ 的 x.y 形式）
    bool hasFormatRange = false; ///< 是否声明了 supported_formats / min+max_format
    int minFormat = 0;           ///< 支持的最低主格式
    int minFormatMinor = 0;      ///< 支持的最低次格式
    int maxFormat = 0;           ///< 支持的最高主格式
    int maxFormatMinor = 0;      ///< 支持的最高次格式

    // ---------------- 内容清单 ----------------
    /// 分类显示名 → 该分类下相对 data/ 的资源路径（最多保留 MAX_PREVIEW_PER_CATEGORY 条用于展示）
    QMap<QString, QStringList> contents;
    int contentFileCount = 0;    ///< 数据文件总数（用于展示与冲突统计）
    bool hasExperimental = false;///< 是否包含动态注册表等实验性内容
    QStringList conflictPaths;   ///< 与其他已启用数据包同名的文件（最多保留若干条）

    // ---------------- 检测结果 ----------------
    DataPackCompat compat = DataPackCompat::Unknown; ///< 兼容性结论
    QString compatDetail;        ///< 兼容性说明（面向用户，含版本区间）
    QString invalidReason;       ///< 无效原因（compat == Invalid 时）
};

/**
 * @brief 一个世界的数据包扫描结果
 */
struct DataPackWorld
{
    bool ok = false;             ///< level.dat 是否读取成功
    QString error;               ///< ok == false 时的错误说明
    QString worldDir;            ///< 世界文件夹绝对路径
    QString worldVersion;        ///< 世界保存时的 MC 版本（level.dat Version.Name，可能为空）
    QStringList rawEnabled;      ///< level.dat 原始 Enabled 列表
    QStringList rawDisabled;     ///< level.dat 原始 Disabled 列表
    int builtinCount = 0;        ///< 原版/模组提供的内置数据包数量
    /// 文件型数据包列表：启用的按加载顺序排在前面，禁用的排在后面（内部再按名称排序）
    QList<DataPackInfo> packs;
};

/**
 * @brief 本地数据包管理器（纯静态工具类）
 */
class DataPackManager
{
public:
    /// 每个分类最多保留的预览条目数，避免超大整合包把界面撑爆
    static constexpr int kMaxPreviewPerCategory = 60;
    /// 单个数据包最多解析的 zip 条目数，防止异常压缩包拖慢扫描
    static constexpr int kMaxZipEntries = 6000;

    /**
     * @brief 扫描一个世界的数据包
     * @param worldDir    世界文件夹绝对路径（内含 level.dat 与 datapacks/）
     * @param versionHint 实例的 MC 版本（level.dat 没有版本信息时兜底）
     * @return 扫描结果；ok == false 时 error 给出原因，packs 仍会尽量填好文件列表
     */
    static DataPackWorld scanWorld(const QString &worldDir,
                                   const QString &versionHint = QString());

    /**
     * @brief 把界面上的启停状态与顺序写回 level.dat
     *
     * 只重写文件型数据包的条目：原版/模组提供的内置条目原样保留在最前面，
     * 启用文件包按传入顺序追加到 Enabled 末尾，禁用文件包写入 Disabled。
     *
     * @param worldDir 世界文件夹绝对路径
     * @param packs    界面当前的数据包列表（仅 isFile == true 的条目会被写入）
     * @param errorOut 可选，失败时输出原因
     */
    static bool applyStates(const QString &worldDir, const QList<DataPackInfo> &packs,
                            QString *errorOut = nullptr);

    /**
     * @brief 导入一个数据包文件到世界 datapacks/ 目录
     *
     * 支持 .zip / .jar；同名文件会自动加 "(2)" 之类的后缀，不会覆盖已有文件。
     *
     * @param worldDir   世界文件夹绝对路径
     * @param sourcePath 源文件绝对路径
     * @param destNameOut 可选，输出最终使用的文件名（仅文件名，不含路径）
     * @param errorOut   可选，失败时输出原因
     * @return 成功返回 true
     */
    static bool importPack(const QString &worldDir, const QString &sourcePath,
                           QString *destNameOut = nullptr, QString *errorOut = nullptr);

    /**
     * @brief 删除世界 datapacks/ 目录里的一个数据包（文件或文件夹）
     * @param worldDir 世界文件夹绝对路径
     * @param info     要删除的数据包（isFile 且 exists 为 true）
     * @param errorOut 可选，失败时输出原因
     */
    static bool removePack(const QString &worldDir, const DataPackInfo &info,
                           QString *errorOut = nullptr);

    /**
     * @brief 列出实例数据包库（实例/datapacks/）里可导入的数据包
     * @param instancePath 实例路径
     * @return 绝对路径列表（.zip / .jar，忽略已带 .disabled 后缀的文件）
     */
    static QStringList listInstanceLibrary(const QString &instancePath);

    /**
     * @brief 把 MC 版本换算成数据包格式 "主.次"（如 1.21.6 → "80.0"）
     * @return 无法识别时返回空字符串（快照版本、过新版本等）
     */
    static QString packFormatForVersion(const QString &gameVersion);

    /**
     * @brief 把数据包格式换算成兼容的 MC 版本区间说明
     * @param major 主格式
     * @param minor 次格式（可传 0）
     * @return 形如 "1.21–1.21.1" 的区间；未收录时返回空字符串
     */
    static QString versionRangeForFormat(int major, int minor = 0);

    /**
     * @brief 把格式号格式化成便于展示的文字
     * @return 如 "48"、"88.0"；major < 0 时返回空字符串
     */
    static QString formatDisplayName(int major, int minor = 0);

    /**
     * @brief 读取数据包封面 pack.png（zip 包或文件夹包均可）
     * @return png 原始字节；不存在或读取失败时返回空 QByteArray
     */
    static QByteArray readPackIcon(const DataPackInfo &info);

    /// 内容分类的固定展示顺序（与 contents 的键对应）
    static QStringList categoryOrder();

    /**
     * @brief 把数据包内的原始目录名翻译成中文分类名
     * @param typeKey data/<命名空间>/ 下的第一层目录名（新版本单数、旧版本复数都接受）
     */
    static QString categoryDisplayName(const QString &typeKey);

private:
    DataPackManager() = delete;
};

#endif // DATAPACKMANAGER_H
