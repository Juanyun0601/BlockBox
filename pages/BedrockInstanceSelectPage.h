/**
 * @file   BedrockInstanceSelectPage.h
 * @brief  基岩版实例选择页 — 布局与样式对齐 Java 版实例选择页
 * @author BlockBox Team
 *
 * 复用 InstanceSelectPage 的界面结构：
 *   左侧悬浮导航气泡 + 右侧工具条（视图切换/操作按钮）+ 列表/瀑布流双视图。
 * 点击启动后由 MainWindow 调用 BedrockInstanceManager::activateInstance
 * 建立数据联接，再调用 BedrockLauncher 启动 UWP 基岩版。
 */

#pragma once

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "../components/ContentViewSwitch.h"
#include "../layouts/MasonryLayout.h"
#include "utils/bedrock/BedrockInstanceManager.h"

class BedrockInstanceSelectPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockInstanceSelectPage(QWidget *parent = nullptr);

    void refresh();

signals:
    void backRequested();
    void launchRequested(const QString &instanceId);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onViewModeChanged(ContentViewSwitch::ViewMode mode);
    void onCreateInstanceClicked();
    void onImportInstanceClicked();
    void onOpenDataDirClicked();
    void onAsyncSizesCalculated();
    void onManagerChanged();

private:
    void initUI();
    void initLeftSidebar();
    void initRightMainBar();
    void rebuildCards();
    void rebuildMasonry();
    void clearMasonry();
    void addListCard(const BedrockInstance &inst, QListWidget *listWidget);
    void addMasonryCard(const BedrockInstance &inst);
    QWidget *buildMasonryCard(const BedrockInstance &inst, QWidget *parent);
    void launchInstance(const QString &id);
    void renameInstance(const QString &id);
    void deleteInstance(const QString &id);
    void openInstanceFolder(const QString &id);
    void updateLeftBubbleGeometry();
    QIcon loadColoredIcon(const QString &path, const QColor &color) const;
    void slideHighlightTo(QPushButton *target);
    static QMap<QString, qint64> calculateSizesStatic(const QStringList &dirs);
    void startAsyncSizeCalculation();

    // 布局（与 InstanceSelectPage 一致）
    QHBoxLayout *m_mainLayout;
    QVBoxLayout *m_leftLayout;
    QWidget *m_leftWidget;
    QWidget *m_folderHighlight;

    QVBoxLayout *m_rightLayout;
    QHBoxLayout *m_topLayout;
    ContentViewSwitch *m_viewSwitch;
    int m_viewMode;   // 0=列表式, 1=瀑布流

    QScrollArea *m_instanceScrollArea;
    QWidget *m_instanceContainer;
    QListWidget *m_instanceListWidget;
    QWidget *m_masonryContainer;
    MasonryLayout *m_masonryLayout;
    QStackedWidget *m_viewStack;   // 0=列表 1=瀑布流 2=空状态
    QWidget *m_emptyState;

    // 数据
    QMap<QString, QList<QLabel *>> m_sizeLabels; // 实例ID -> 大小标签（列表/瀑布流各一份）
    QFutureWatcher<QMap<QString, qint64>> m_sizeWatcher;
    bool m_shown = false;
    bool m_versionResolved = false;
    QString m_installedVersion;

    // 悬浮气泡参数
    static constexpr int kBubbleWidth = 200;
    static constexpr int kBubbleMargin = 10;
};
