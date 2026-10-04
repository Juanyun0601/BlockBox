/**
 * @file   ResourceReferenceDialog.h
 * @brief  AI 助手资源引用选择对话框
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 用于在 AI 助手输入框旁点击「引用」按钮后，弹出的资源选择对话框。
 * 四个标签页：
 *  - 实例资源：引用当前实例下的模组、资源包、光影包、投影、当前实例本身
 *  - 本地资源：浏览本地所有实例（及基岩版）的模组、资源包、光影包、
 *              投影、存档、数据包等资源并引用
 *  - 网络资源：搜索 Modrinth 在线模组/资源包/光影包/数据包并引用（不下载）
 *  - 网页：输入 URL 引用网页（可选自动抓取标题）
 * 也可通过文件选择器引用任意文件。选择结果以 ResourceReference 列表返回，
 * 调用方按需将其作为文本描述附加到发送给 AI 的用户消息中。
 */

#pragma once

#include <QList>
#include <QString>

#include "components/AppDialogBase.h"
#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QTabWidget;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QLabel;
class QSplitter;

class ModrinthAPI;

/**
 * @brief 资源引用选择对话框
 *
 * 三标签页结构：
 *  - 实例资源：树形展示当前实例下的模组/资源包/光影包/投影，可勾选引用当前实例
 *  - 本地资源：左侧列出本地全部实例（含基岩版），右侧树形展示选中实例下的
 *              模组/资源包/光影包/投影/存档/数据包，勾选后引用
 *  - 网络资源：搜索框 + 类型下拉，异步查询 Modrinth，结果列表可多选
 *  - 网页：URL 输入框 + 「抓取标题」按钮，可添加多条网页引用
 * 底部「添加任意文件」按钮可将任意本地文件加入引用。
 * 确认后通过 selectedReferences() 取回所有标签页累计的选中项。
 *
 * 注意：ResourceReference 结构体已移至 utils/content/ContentData.h，
 * 便于 AI 服务层（AiService.h）在不依赖 UI 头文件的情况下使用。
 */
class ResourceReferenceDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param instancePath    实例路径（可为 versions/{ver} 目录或 .minecraft 根目录）
     * @param instanceVersion 实例版本（用于「引用当前实例」的附加说明与网络搜索过滤）
     * @param instanceLoader  实例加载器（用于「引用当前实例」的附加说明与网络搜索过滤）
     * @param parent          父窗口
     */
    explicit ResourceReferenceDialog(const QString &instancePath,
                                     const QString &instanceVersion,
                                     const QString &instanceLoader,
                                     QWidget *parent = nullptr);

    ~ResourceReferenceDialog();

    /**
     * @brief 获取用户选中的资源引用列表（合并三个标签页）
     * @return 选中的 ResourceReference 列表（仅在 exec() 返回 Accepted 后调用有意义）
     */
    QList<ResourceReference> selectedReferences() const;

    /**
     * @brief 根据传入的实例路径解析出 .minecraft 根目录
     * @param path 实例路径（versions/{ver} 目录或 .minecraft 根目录）
     * @return 解析后的 .minecraft 根目录（无法识别时原样返回）
     *
     * 识别规则：依次检查 path 与 path/../.. 下是否存在标记目录
     *（schematics/mods/resourcepacks/shaderpacks），命中即视为根目录。
     * 多标记检测避免单一目录缺失（如 vanilla 无 mods）导致误判。
     */
    static QString resolveInstanceRoot(const QString &path);

private slots:
    void onAddAnyFileClicked();        ///< 「添加任意文件」按钮
    void onNetworkSearchClicked();     ///< 网络资源搜索按钮
    void onNetworkSearchCompleted(const ModSearchResult &result);
    void onNetworkSearchFailed(const QString &error);
    void onFetchWebTitleClicked();     ///< 网页「抓取标题」按钮
    void onWebReplyFinished();         ///< 网页标题抓取响应
    void onAddWebPageClicked();        ///< 网页「添加引用」按钮

private:
    void initUI();
    void initInstanceTab();            ///< 构建实例资源标签页
    void initLocalTab();               ///< 构建本地资源标签页
    void initNetworkTab();             ///< 构建网络资源标签页
    void initWebTab();                 ///< 构建网页标签页
    void loadInstanceResources();      ///< 扫描实例目录填充树
    void addFileItem(QTreeWidgetItem *parent, const QString &category,
                     const QString &dirPath, const QStringList &nameFilters);
    void addCustomFileItem(const QString &filePath);  ///< 添加任意文件到实例树「文件」分组
    QString formatSize(qint64 bytes) const;           ///< 格式化文件大小

    /**
     * @brief 扫描本地全部实例（Java 版）并填充本地资源标签页的实例列表
     *
     * 依次遍历各实例目录下 versions/*，解析版本 JSON 中的
     * inheritsFrom / clientVersion / id 获取版本，并据 JSON 键名推断加载器。
     * 基岩版无需实例，直接以「基岩版本地资源」伪条目置于列表顶部。
     */
    void loadLocalInstances();
    /**
     * @brief 根据选中的本地实例（或基岩版资源根）填充资源类别树
     * @param instancePath 实例路径（Java 版为 versions/{ver}）或基岩版 com.mojang 目录
     * @param bedrock      是否基岩版（资源子目录与类别不同）
     *
     * 每个资源类别作为树顶层节点，文件（或存档目录）为可勾选叶子。
     */
    void populateLocalTree(const QString &instancePath, bool bedrock);
    /**
     * @brief 向本地资源树添加某一类别的文件条目（支持目录类）
     * @param parent     类别顶层节点
     * @param category   引用类别（如「本地模组」）
     * @param dirPath    资源子目录路径
     * @param nameFilters 文件名过滤器（目录类传空）
     * @param dirsOnly   是否仅列出子目录（存档/世界类资源）
     */
    void addLocalFileItem(QTreeWidgetItem *parent, const QString &category,
                          const QString &dirPath, const QStringList &nameFilters,
                          bool dirsOnly);

    /**
     * @brief 解析资源子目录的实际路径，兼容版本隔离与非隔离两种布局
     * @param instancePath 实例路径（versions/{ver} 目录或 .minecraft 根目录）
     * @param subDir 子目录名（如 mods / resourcepacks / shaderpacks / schematics）
     * @return 实际存在的目录绝对路径；都不存在时返回隔离路径
     */
    static QString resolveResourceDirFor(const QString &instancePath, const QString &subDir);

    /**
     * @brief 定位基岩版 com.mojang 本地资源根目录
     * @return com.mojang 目录绝对路径（未安装基岩版时返回空字符串）
     */
    static QString bedrockComMojangDir();

    /**
     * @brief 解析资源子目录的实际路径，兼容版本隔离与非隔离两种布局
     * @param subDir 子目录名（如 mods / resourcepacks / shaderpacks / schematics）
     * @return 实际存在的目录绝对路径；都不存在时返回空字符串
     *
     * 查找顺序：
     *  1. m_instancePath/{subDir}   —— 版本隔离布局（gameDir = versions/{ver}）
     *  2. m_instancePath/../{subDir} —— 非隔离布局（gameDir = .minecraft）
     * 这样无论版本隔离是否开启，都能正确定位模组/资源包/光影包/投影目录。
     */
    QString resolveResourceDir(const QString &subDir) const;

    /// 将网络资源类型枚举转换为 Modrinth project_type
    QString networkTypeToProjectType(int typeIndex) const;
    /// 将网络资源类型枚举转换为引用类别显示名
    QString networkTypeToCategory(int typeIndex) const;

    QString m_instancePath;        ///< 原始实例路径
    QString m_instanceRoot;        ///< 解析后的 .minecraft 根目录
    QString m_instanceVersion;     ///< 实例版本
    QString m_instanceLoader;      ///< 实例加载器

    // 通用
    QTabWidget *m_tabs;
    QPushButton *m_addFileBtn;     ///< 「添加任意文件」按钮（底部通用）

    // 实例资源标签页
    QTreeWidget *m_instanceTree;
    QTreeWidgetItem *m_instanceItem;       ///< 「当前实例」可勾选项
    QTreeWidgetItem *m_fileCategoryItem;   ///< 「文件」分组（任意文件挂在下面）

    // 本地资源标签页（浏览本地全部实例的资源）
    struct LocalInst
    {
        QString name;      ///< 实例名（versions 目录名）
        QString path;      ///< 实例路径
        QString version;   ///< 游戏版本
        QString loader;    ///< 加载器（Fabric/Forge/NeoForge/Quilt）
    };
    QList<LocalInst> m_localInstances;     ///< 本地 Java 版实例列表
    QListWidget *m_localInstanceList;      ///< 左侧实例列表
    QTreeWidget *m_localTree;              ///< 右侧资源类别树

    // 网络资源标签页
    QLineEdit *m_netSearchEdit;
    QComboBox *m_netTypeCombo;
    QPushButton *m_netSearchBtn;
    QListWidget *m_netResultList;
    QLabel *m_netStatusLabel;
    ModrinthAPI *m_modrinthApi;            ///< Modrinth 搜索 API（异步）
    int m_netPendingCount;                 ///< 待返回的搜索请求数（防重复刷新）

    // 网页标签页
    QLineEdit *m_webUrlEdit;
    QLineEdit *m_webTitleEdit;
    QPushButton *m_webFetchBtn;
    QPushButton *m_webAddBtn;
    QListWidget *m_webList;                ///< 已添加的网页引用列表
    QNetworkAccessManager *m_webNetwork;   ///< 网页标题抓取网络管理器
    QNetworkReply *m_webReply;             ///< 当前网页抓取回复
};
