/**
 * @file   SettingsManager.h
 * @brief  设置管理器类定义
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QSettings>
#include <QString>

#include "utils/MemoryAllocator.h"
#include "utils/translation/TranslationService.h"

struct AccountInfo {
    QString username;
    QString type;
    QString serverUrl; // 第三方服务器URL，离线账户为空
    bool isDefault;
    
    // Microsoft / 第三方账户相关信息
    QString accessToken; // Microsoft访问令牌 / 第三方认证访问令牌
    QString refreshToken; // Microsoft刷新令牌
    QString clientToken; // 第三方认证客户端令牌（刷新令牌时要用同一个）
    QString uuid; // 用户UUID
    QString skinUrl; // 皮肤URL
    
    // 构造函数
    AccountInfo() : isDefault(false) {}
};

struct AuthServerInfo {
    QString url;
    QString name;
};

struct InstanceFolderInfo {
    QString name;
    QString path;
    bool isDefault = false;
};

enum class ForgeDownloadSource {
    Official = 0,
    BMCL = 1
};

enum class FabricDownloadSource {
    Official = 0,
    BMCL = 1
};

enum class OptiFineDownloadSource {
    Official = 0,
    BMCL = 1
};

enum class NeoForgeDownloadSource {
    Official = 0,
    BMCL = 1
};

enum class DownloadSource {
    Official,
    BMCL,
    Auto
};

/** 存档地图结构标记图标风格：文字旁小图标（默认） / 结构概览缩略图 */
enum class MapStructureIconStyle {
    Icon = 0,     ///< 生成结构表中文字旁的 16px 小图标（EnvSprite）
    Overview = 1  ///< 结构页面主图（Wiki pageimages 缩略图）
};

class SettingsManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int DEFAULT_MAX_MEMORY_MB = 4096;
    static constexpr int DEFAULT_MIN_MEMORY_MB = 1024;
    static constexpr int MIN_MEMORY_MB = 512;
    static constexpr int MAX_MEMORY_MB = 16384;
    static constexpr int DEFAULT_AUTH_PORT = 8080;
    static constexpr int MEMORY_SLIDER_STEP = 512;

    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager();

    // 获取单例实例
    static SettingsManager *instance();

    // 导航按钮显示设置
    bool isHomeButtonVisible() const;
    void setHomeButtonVisible(bool visible);

    bool isResourcesButtonVisible() const;
    void setResourcesButtonVisible(bool visible);

    bool isSettingsButtonVisible() const;
    void setSettingsButtonVisible(bool visible);

    // Account management methods
    QList<AccountInfo> getAccounts() const;
    /**
     * @brief 添加账户
     * @return 是否真的写入成功（同名已存在或磁盘写入失败时返回 false，调用方需据此提示用户）
     */
    bool addAccount(const QString &username, const QString &type, const QString &serverUrl, bool isDefault = false);
    /**
     * @brief 添加第三方（Yggdrasil）账户，连同登录拿到的令牌、角色 UUID、皮肤地址一起存
     * @return 是否真的写入成功（同名已存在或磁盘写入失败时返回 false）
     */
    bool addThirdPartyAccount(const QString &username, const QString &type, const QString &serverUrl,
                              const QString &accessToken, const QString &clientToken,
                              const QString &uuid, const QString &skinUrl, bool isDefault = false);
    void addMicrosoftAccount(const QString &username, const QString &accessToken, const QString &refreshToken, const QString &uuid, const QString &skinUrl, bool isDefault = false);
    void removeAccount(const QString &username);
    void setDefaultAccount(const QString &username);
    AccountInfo getDefaultAccount() const;
    bool accountExists(const QString &username) const;
    
    // Auth server methods
    QList<AuthServerInfo> getAuthServers() const;
    void addAuthServer(const QString &url, const QString &name);
    void removeAuthServer(const QString &url);
    
    // Java path methods
    QString getJavaPath() const;
    void setJavaPath(const QString &path);

    // Java installations list methods
    QList<QPair<QString, QString>> getJavaInstallations() const;
    void setJavaInstallations(const QList<QPair<QString, QString>>& installations);

    // Java auto-select by game version
    bool getAutoSelectJava() const;
    void setAutoSelectJava(bool enabled);
    
    // Instance folder methods
    QList<InstanceFolderInfo> getInstanceFolders() const;
    void addInstanceFolder(const QString &name, const QString &path, bool isDefault = false);
    void removeInstanceFolder(const QString &path);
    void setDefaultInstanceFolder(const QString &path);
    InstanceFolderInfo getDefaultInstanceFolder() const;
    QString getDefaultInstancePath() const;
    /** 未配置实例文件夹时的兜底默认路径：移动端用应用数据目录（程序目录只读），
     *  桌面端保持跟随启动器所在目录 */
    static QString defaultInstanceFolderPath();

    // Instance category methods (自定义实例分类)
    QStringList getInstanceCategories() const;
    void addInstanceCategory(const QString &name);
    void removeInstanceCategory(const QString &name);
    void renameInstanceCategory(const QString &oldName, const QString &newName);
    QString getInstanceCategory(const QString &instancePath) const;
    void setInstanceCategory(const QString &instancePath, const QString &category);
    
    // Property methods
    void setProperty(const QString &key, const QVariant &value);
    /** 读取通用属性，与 setProperty 同源（均落在 QSettings）。
     *  此重载刻意遮蔽 QObject::property(const char*)，否则字符串字面量调用会
     *  命中 Qt 动态属性而非 QSettings，永远读不到 setProperty 写入的值。 */
    QVariant property(const char *key, const QVariant &defaultValue = QVariant()) const;
    QVariant getProperty(const QString &key, const QVariant &defaultValue = QVariant()) const;
    /** 移除指定属性（不存在时无操作） */
    void removeProperty(const QString &key);
    
    // Version isolation methods
    bool isVersionIsolationEnabled() const;
    void setVersionIsolationEnabled(bool enabled);
    
    // Forge download source methods
    ForgeDownloadSource getForgeDownloadSource() const;
    void setForgeDownloadSource(ForgeDownloadSource source);
    
    // Fabric download source methods
    FabricDownloadSource getFabricDownloadSource() const;
    void setFabricDownloadSource(FabricDownloadSource source);
    
    // OptiFine download source methods
    OptiFineDownloadSource getOptiFineDownloadSource() const;
    void setOptiFineDownloadSource(OptiFineDownloadSource source);
    
    // NeoForge download source methods
    NeoForgeDownloadSource getNeoForgeDownloadSource() const;
    void setNeoForgeDownloadSource(NeoForgeDownloadSource source);
    
    // Download source methods
    DownloadSource getDownloadSource() const;
    void setDownloadSource(DownloadSource source);

    // 文件选择器类型（系统自带 / 软件自带）
    // 电脑端默认系统自带，手机端（Android / HarmonyOS）默认并固定软件自带

    // 存档地图结构标记图标风格（文字旁小图标 / 结构概览）
    MapStructureIconStyle getMapStructureIconStyle() const;
    void setMapStructureIconStyle(MapStructureIconStyle style);
    
    void initDefaultAuthServers();

    // Download thread count methods
    int getDownloadThreadCount() const;
    void setDownloadThreadCount(int count);

    // Memory allocation methods
    bool isAutoMemoryEnabled() const;
    void setAutoMemoryEnabled(bool enabled);
    int getMaxMemory() const;
    void setMaxMemory(int maxMemoryMb);
    int getMinMemory() const;
    void setMinMemory(int minMemoryMb);
    MemoryAllocationMode getMemoryAllocationMode() const;
    void setMemoryAllocationMode(MemoryAllocationMode mode);
    
    void initDefaultInstanceFolders();

    // GitHub acceleration properties (delegated to GithubAccelerator)
    bool isGithubAccelerationEnabled() const;
    void setGithubAccelerationEnabled(bool enabled);
    int getGithubAccelerationProxyIndex() const;
    void setGithubAccelerationProxyIndex(int index);
    QString getGithubAccelerationCustomUrl() const;
    void setGithubAccelerationCustomUrl(const QString &url);

    // Command history methods (中文输入, 英文输出)
    /**
     * @brief 获取指令历史记录
     * @return 历史记录列表，每项是 (中文输入, 英文输出) 对，最新在前
     */
    QList<QPair<QString, QString>> commandHistory() const;

    /**
     * @brief 保存指令历史记录
     * @param history 历史记录列表，每项是 (中文输入, 英文输出) 对
     */
    void saveCommandHistory(const QList<QPair<QString, QString>> &history);

    // Command pack list methods (自定义指令包)
    /**
     * @brief 获取已保存的指令包列表（每项是 (filePath, enabled) 对）
     * @return 指令包路径与启用状态列表
     */
    QList<QPair<QString, bool>> commandPackList() const;

    /**
     * @brief 保存指令包列表
     * @param packs 指令包路径与启用状态列表
     */
    void saveCommandPackList(const QList<QPair<QString, bool>> &packs);

    // 开机自启动（委托平台层操作注册表/LaunchAgents/autostart 目录）
    /**
     * @brief 查询是否已开启开机自启动
     * @return 当前系统层面自启动状态
     */
    bool isAutoStartEnabled() const;

    /**
     * @brief 开启或关闭开机自启动
     * @param enabled 是否开启
     */
    void setAutoStartEnabled(bool enabled);

    // Translation settings methods (翻译设置)
    /**
     * @brief 获取翻译配置
     * @return 翻译配置结构体
     */
    TranslationConfig getTranslationConfig() const;

    /**
     * @brief 保存翻译配置
     * @param config 翻译配置结构体
     */
    void setTranslationConfig(const TranslationConfig &config);

    // 实验性功能（默认全部关闭）
    /**
     * @brief 是否开启「基岩版」实验性功能
     * @return 开启时顶栏显示 Java 版 / 基岩版 切换入口
     */
    bool isExperimentalBedrockEnabled() const;

    /**
     * @brief 开启或关闭「基岩版」实验性功能
     * @param enabled 是否开启
     */
    void setExperimentalBedrockEnabled(bool enabled);

    /**
     * @brief 是否开启「存档地图立体视图」实验性功能
     * @return 开启时存档地图页工具栏显示「3D」开关
     */
    bool isExperimentalMap3dEnabled() const;

    /**
     * @brief 开启或关闭「存档地图立体视图」实验性功能
     * @param enabled 是否开启
     */
    void setExperimentalMap3dEnabled(bool enabled);

    /**
     * @brief 方块盒子论坛的访问地址（默认官方站点，可改成本地服务方便测试）
     * @return 规范化后的地址，例如 https://forum.mcblockbox.top
     */
    QString forumBaseUrl() const;

    /**
     * @brief 设置论坛地址；传空字符串表示恢复默认
     * @param url 论坛根地址，可省略协议与结尾斜杠
     */
    void setForumBaseUrl(const QString &url);

    /** 官方论坛默认地址 */
    static QString defaultForumBaseUrl();
    /** 补全协议、去掉结尾斜杠、空值回落默认 */
    static QString normalizeForumUrl(const QString &url);

    /**
     * @brief 是否已同意《方块盒子社区规范》（首次发布前弹窗确认，同意一次长期有效）
     */
    bool isCommunityAgreementAccepted() const;
    void setCommunityAgreementAccepted(bool accepted);

signals:
    void downloadSourceChanged(DownloadSource source);
    void accountsChanged();
    /** 实验性功能「基岩版」开关状态变化 */
    void experimentalBedrockChanged(bool enabled);
    /** 实验性功能「存档地图立体视图」开关状态变化 */
    void experimentalMap3dChanged(bool enabled);
    /** 论坛地址被修改 */
    void forumBaseUrlChanged(const QString &url);

private:
    bool saveAccountsToSettings(const QList<AccountInfo>& accounts);
    QString getAccountsFilePath() const;
    QList<AccountInfo> loadAccountsFromFile() const;
    bool saveAccountsToFile(const QList<AccountInfo>& accounts) const;

    QSettings *m_settings;        // 系统设置（注册表/AppData），存UI偏好等
    QSettings *m_configSettings;  // BlockBox/config.ini，仅存Java安装缓存
    static SettingsManager *m_instance;
    static QMutex m_instanceMutex;
};
