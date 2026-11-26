/**
 * @file   SystemTray.h
 * @brief  系统托盘图标与托盘菜单声明
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 桌面端常驻的托盘入口：主窗口收进托盘后，用户靠它把窗口叫回来或彻底退出。
 * 手机 / 平板与没有托盘服务的桌面环境（部分精简 Linux 桌面）不支持，
 * 调用方先用 isAvailable() 判断，不支持时保持原来的「关窗即退出」行为。
 */
#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

class SystemTray : public QObject
{
    Q_OBJECT

public:
    explicit SystemTray(QObject *parent = nullptr);
    ~SystemTray() override;

    /** 当前系统是否提供托盘区域（Windows / macOS / 多数 Linux 桌面为 true） */
    static bool isAvailable();

    /** 显示托盘图标（系统不支持时不做任何事） */
    void show();

    /** 移除托盘图标（退出前调用，避免托盘里留下一个点不动的残影） */
    void hide();

    bool isVisible() const;

    /**
     * @brief 弹一条系统通知（主窗口收在托盘里时用：不打开窗口也能看到结果）
     * @param title 通知标题
     * @param body  通知正文
     * @param msecs 停留时长（毫秒）
     */
    void showBalloon(const QString &title, const QString &body, int msecs = 5000);

signals:
    /** 点了托盘图标 / 通知本体：请求把主窗口叫回前台 */
    void showWindowRequested();

    /** 点了托盘菜单「打开设置」 */
    void openSettingsRequested();

    /** 点了托盘菜单「退出方块盒子」：这是明确的退出请求，不能再按「收进托盘」处理 */
    void quitRequested();

public slots:
    /** 语言切换后重刷托盘菜单与提示文字 */
    void retranslateUi();

private:
    /** 建托盘右键菜单（显示主界面 / 打开设置 / 退出方块盒子） */
    void buildMenu();

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;
};

#endif // SYSTEMTRAY_H
