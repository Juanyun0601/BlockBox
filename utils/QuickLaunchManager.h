/**
 * @file   QuickLaunchManager.h
 * @brief  桌面快捷启动（.blockbox 文件）管理器
 * @author BlockBox Team
 * @date   2026-09-06
 */
#ifndef QUICKLAUNCHMANAGER_H
#define QUICKLAUNCHMANAGER_H

#include <QPixmap>
#include <QString>
#include <QtGlobal>

/**
 * @brief 桌面快捷启动管理器
 *
 * 在桌面生成 "<名称>.blockbox" 快捷启动文件，并通过 HKCU 注册表注册
 * ".blockbox" 扩展名与方块盒子的关联（无需管理员权限）。双击该文件即可
 * 打开方块盒子，并按文件里记录的目标直接启动：实例、某个存档，或某台服务器。
 *
 * 文件结构（图标与数据合一，从前往后）：
 *   [多尺寸 ICO 图标数据][载荷标记\n][JSON 载荷]
 * 资源管理器通过 ProgID 的 DefaultIcon = %1 直接把文件自身的 ICO 部分
 * 渲染为文件图标，因此桌面文件显示的就是这个快捷方式对应的图标：存档用
 * 存档封面、服务器用服务器图标、实例用实例设置里配置的图标；
 * ICO 目录之外的载荷字节会被图标解析器忽略，两种数据互不干扰。
 */
class QuickLaunchManager
{
public:
    /** 快捷启动目标类型 */
    enum class Kind
    {
        Instance,   ///< 直接启动整个实例
        Save,       ///< 启动实例并直接进入指定存档
        Server      ///< 启动实例并自动连接指定服务器
    };

    /** 解析出来的快捷启动目标 */
    struct Target
    {
        Kind kind = Kind::Instance;
        /** 目标实例目录（三种类型都必填） */
        QString instancePath;
        /** 实例显示名（写入载荷，仅用于展示） */
        QString instanceName;
        /** 桌面文件显示名（为空时按类型自动生成） */
        QString title;
        /** 存档文件夹名（Kind::Save 必填） */
        QString saveName;
        /** 服务器地址（Kind::Server 必填） */
        QString serverAddress;
        /** 服务器端口（Kind::Server 使用，默认 25565） */
        quint16 serverPort = 25565;
        /** 自定义图标（服务器卡片的图标等；为空时依次退回存档封面 / 实例图标） */
        QPixmap icon;
    };

    /** 在桌面创建（或刷新）指定实例的 .blockbox 快捷启动文件 */
    static bool createDesktopShortcut(const QString &instancePath, QString *errorMessage = nullptr);
    /** 在桌面创建「启动并直接进入存档」的 .blockbox 快捷启动文件 */
    static bool createSaveShortcut(const QString &instancePath, const QString &saveName,
                                   QString *errorMessage = nullptr);
    /** 在桌面创建「启动并自动连接服务器」的 .blockbox 快捷启动文件
     *  @param icon 服务器卡片上显示的图标（离线时为空，退回实例图标） */
    static bool createServerShortcut(const QString &instancePath, const QString &serverName,
                                     const QString &address, quint16 port,
                                     const QPixmap &icon = QPixmap(),
                                     QString *errorMessage = nullptr);
    /** 按目标结构创建快捷启动文件（上面三个入口的公共实现） */
    static bool createShortcut(const Target &target, QString *errorMessage = nullptr);
    /** 解析 .blockbox 快捷启动文件，取出完整目标（实例 / 存档 / 服务器） */
    static bool parseQuickLaunchFile(const QString &filePath, Target *target,
                                     QString *errorMessage = nullptr);
    /** 兼容旧调用：只取文件里记录的实例路径 */
    static bool parseQuickLaunchFile(const QString &filePath, QString *instancePath,
                                     QString *errorMessage = nullptr);
    /** 实例名称/图标变更后刷新已存在的快捷启动文件（文件被用户删除/移动则不再重建） */
    static void refreshDesktopShortcut(const QString &instancePath);
    /** 注册 .blockbox 扩展名与方块盒子的文件关联（HKCU，无需管理员权限） */
    static bool registerFileAssociation(QString *errorMessage = nullptr);
};

#endif // QUICKLAUNCHMANAGER_H
