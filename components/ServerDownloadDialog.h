/**
 * @file   ServerDownloadDialog.h
 * @brief  「下载服务端」弹窗：按版本 / 核心把服务端文件下到指定目录
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 资源页「安装新实例」里每个版本卡片的「下载服务端」入口用它：
 *   - 版本取点击时那个版本（不让在弹窗里换，避免选错）；
 *   - 核心可选原版 / Paper / Fabric（直接给服务端 jar），
 *     Forge / NeoForge（给官方安装器，装好才能开服）；
 *   - 保存目录默认 {程序目录}/servers/{版本}-{核心}，可以自己挑；
 *   - 下载进度、失败原因都显示在弹窗里，成功后能直接打开目录 / 复制路径。
 *
 * 真正的下载逻辑复用 LocalServerManager 的「只下载」模式（downloadOnly），
 * 那边已经处理了多源回退、镜像兜底与超时看门狗。
 */

#ifndef SERVERDOWNLOADDIALOG_H
#define SERVERDOWNLOADDIALOG_H

#include "AppDialogBase.h"
#include "utils/server/LocalServerManager.h"

#include <QString>

class QComboBox;
class QFrame;
class QShowEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class ServerDownloadDialog : public AppDialogBase
{
    Q_OBJECT

public:
    /**
     * @brief 打开「下载服务端」弹窗
     * @param parent    宿主页面
     * @param versionId 要下载的 Minecraft 版本（安装页里点的那一项）
     */
    static void openFor(QWidget *parent, const QString &versionId);

protected:
    void showEvent(QShowEvent *event) override;

private:
    explicit ServerDownloadDialog(const QString &versionId, QWidget *parent);

    void buildUi();
    void applyStyle();
    /// 按版本过滤出能用的核心，并尽量保住当前选择
    void rebuildCoreCombo();
    /// 按版本 / 核心给一个默认保存目录
    QString suggestedDir(LocalServerManager::Core core) const;
    /// 目录还是自动生成的那个时，跟着核心一起换
    void refreshDirForCore();
    void pickDir();
    void startDownload();
    void openDownloadDir();
    void copyDownloadPath();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool error = false);
    LocalServerManager::Core currentCore() const;
    LocalServerManager::Config buildConfig() const;

    QFrame *m_card = nullptr;
    QString m_version;
    LocalServerManager *m_manager = nullptr;

    QComboBox *m_coreCombo = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_coreHintLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QString m_lastPath;        ///< 下载好的文件路径（成功后用来打开 / 复制）
    bool m_needsInstall = false; ///< 下的是安装器（Forge / NeoForge）还是服务端 jar
    bool m_busy = false;
    bool m_finished = false;
    QString m_autoDir;         ///< 上次自动生成的目录（用来判断用户有没有手动改过）
};

#endif // SERVERDOWNLOADDIALOG_H
