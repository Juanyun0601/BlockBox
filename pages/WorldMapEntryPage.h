/**
 * @file   WorldMapEntryPage.h
 * @brief  存档地图入口页：模块没装时先下载，装好后显示地图页
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图页（实例管理 > 存档 > 地图）不再随安装包分发。本页负责：
 *   - 桌面端：先展示「首次使用需要下载地图模块」的等待页，下载/校验/解压
 *     完成后把真正的地图页嵌进来；之后打开不再联网。
 *   - 移动端 / Windows ARM64（系统不允许加载下载的动态库）：继续使用内置
 *     的地图页，行为与以前完全一致。
 *
 * 外层（InstanceManagePage）只跟本页打交道，因此两种模式的调用方式一致：
 * setCurrentSave / setInstancePath / reset / refresh。
 */
#ifndef WORLDMAPENTRYPAGE_H
#define WORLDMAPENTRYPAGE_H

#include <QWidget>

class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

#if defined(BLOCKBOX_WORLDMAP_REMOTE_MODULE)
class WorldMapEntryPage : public QWidget
{
    Q_OBJECT

public:
    explicit WorldMapEntryPage(QWidget *parent = nullptr);
    ~WorldMapEntryPage() override;

    /** 打开指定存档的地图（路径指向存档目录） */
    void setCurrentSave(const QString &saveFolderPath, const QString &saveName);
    /** 当前实例目录（读客户端 JAR 的群系名用） */
    void setInstancePath(const QString &instancePath);
    /** 清空当前存档 */
    void reset();
    /** 顶栏刷新：重新渲染当前维度 */
    void refresh();

private:
    void buildUi();
    void applyTheme();
    void ensureModulePage();
    void applyPendingSave();
    void updateStateText();
    void updateProgress(qint64 received, qint64 total);
    void showFailure(const QString &message);
    void callPageRefresh();
    void callPageReset();
    void callPageInstancePath();
    void callPageCurrentSave();

    QStackedWidget *m_stack = nullptr;
    QWidget *m_loadingPage = nullptr;
    QWidget *m_moduleHost = nullptr;
    QFrame *m_card = nullptr;
    QVBoxLayout *m_moduleLayout = nullptr;
    QWidget *m_modulePage = nullptr;

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_retryBtn = nullptr;

    QString m_instancePath;
    QString m_pendingSavePath;
    QString m_pendingSaveName;
    bool m_hasPendingSave = false;
};
#else
#include "pages/WorldMapPage.h"

class WorldMapEntryPage : public QWidget
{
    Q_OBJECT

public:
    explicit WorldMapEntryPage(QWidget *parent = nullptr);

    void setCurrentSave(const QString &saveFolderPath, const QString &saveName);
    void setInstancePath(const QString &instancePath);
    void reset();
    void refresh();

private:
    WorldMapPage *ensurePage();

    WorldMapPage *m_page = nullptr;
    QString m_instancePath;
    QString m_pendingSavePath;
    QString m_pendingSaveName;
    bool m_hasPendingSave = false;
};
#endif // BLOCKBOX_WORLDMAP_REMOTE_MODULE

#endif // WORLDMAPENTRYPAGE_H
