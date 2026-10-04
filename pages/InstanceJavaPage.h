/**
 * @file   InstanceJavaPage.h
 * @brief  实例助手 - Java 管理页面，管理本机已安装的 Java 运行时
 * @author BlockBox Team
 * @date   2026-07-21
 *
 * 用于 InstanceAssistantWindow 的「Java管理」标签页。窗口宽度较窄（450px），
 * 因此采用紧凑的单列垂直布局，参考 HMCL/PCL-CE 的工具栏 + 列表风格：
 *  - 工具栏：全盘扫描 / 手动添加 / 快速检测 / 下载Java
 *  - 列表：已发现的 Java 安装项卡片（标题/路径/版本/打开文件夹/移除）
 *  - 全局设置：默认 Java 选择 + 按游戏版本自动选择开关
 *
 * 复用 JavaScanWorker、GameLauncher::JavaInfo、SettingsManager
 * 的全部现有逻辑，与主程序 SettingsPage::initJavaManagerSettings 共享同一份
 * Java 缓存（getJavaInstallations / setJavaInstallations）。
 */
#ifndef INSTANCEJAVAPAGE_H
#define INSTANCEJAVAPAGE_H

#include <QString>
#include <QWidget>

#include "components/OutlinedLabel.h"

class CustomCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

class InstanceJavaPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceJavaPage(QWidget *parent = nullptr);
    ~InstanceJavaPage() = default;

    /** 手动刷新 Java 列表（外部下载完成后可调用） */
    void refreshJavaList();
    /** 手动刷新全局 Java 下拉框 */
    void refreshGlobalJavaCombo();

signals:
    /** 用户点击「下载Java」按钮，请求切换到下载 Java 页 */
    void downloadJavaRequested();

    /** 全局 Java 设置发生变化，外部可据此刷新启动器缓存 */
    void javaSettingsChanged();

private slots:
    void onScanClicked();
    void onAddClicked();
    void onDetectClicked();
    void onDownloadClicked();
    void onRemoveJava(const QString &javaPath);
    void onOpenJavaFolder(const QString &javaPath);
    void onGlobalJavaChanged(int index);
    void onAutoSelectToggled(bool checked);

private:
    void initUI();
    QWidget *createJavaItemWidget(const QString &path, const QString &version);

    // ---- UI 控件 ----
    OutlinedLabel *m_titleLabel;
    QLabel *m_subtitleLabel;
    QPushButton *m_scanBtn;
    QPushButton *m_addBtn;
    QPushButton *m_detectBtn;
    QPushButton *m_downloadBtn;
    QLabel *m_listTitleLabel;
    QListWidget *m_listWidget;
    QLabel *m_emptyLabel;
    QComboBox *m_globalJavaCombo;
    CustomCheckBox *m_autoSelectCheck;
};

#endif // INSTANCEJAVAPAGE_H
