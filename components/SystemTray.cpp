/**
 * @file   SystemTray.cpp
 * @brief  系统托盘图标与托盘菜单实现
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "components/SystemTray.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#include "utils/AppVersion.h"

namespace {
/// 托盘图标复用软件自己的应用图标（Windows 下 ico 在任务栏与托盘都清晰）
const char kTrayIconPath[] = ":/Images/icon.ico";
}

SystemTray::SystemTray(QObject *parent)
    : QObject(parent)
{
    // 没有托盘服务的环境（手机 / 平板 / 精简 Linux 桌面）什么都不建，
    // 主窗口据此保持「关窗即退出」的老行为
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(this);

    QIcon icon(QString::fromLatin1(kTrayIconPath));
    if (icon.isNull())
        icon = QApplication::windowIcon();
    m_trayIcon->setIcon(icon);

    buildMenu();

    // 左键单击 / 双击托盘图标 = 把窗口叫回来；右键由系统弹上面那份菜单
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    emit showWindowRequested();
                }
            });
    // 点通知本体同样把窗口叫回来（下载完成、提醒这类通知点一下就能看详情）
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this,
            &SystemTray::showWindowRequested);

    retranslateUi();
}

SystemTray::~SystemTray()
{
    if (m_trayIcon)
        m_trayIcon->hide();

    // QMenu 属于控件体系，挂不到本 QObject 当父对象，只能自己回收
    delete m_menu;
}

bool SystemTray::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void SystemTray::show()
{
    if (m_trayIcon)
        m_trayIcon->show();
}

void SystemTray::hide()
{
    if (m_trayIcon)
        m_trayIcon->hide();
}

bool SystemTray::isVisible() const
{
    return m_trayIcon && m_trayIcon->isVisible();
}

void SystemTray::showBalloon(const QString &title, const QString &body, int msecs)
{
    if (!m_trayIcon || !m_trayIcon->isVisible())
        return;

    m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, msecs);
}

void SystemTray::retranslateUi()
{
    if (!m_trayIcon)
        return;

    m_showAction->setText(tr("显示主界面"));
    m_settingsAction->setText(tr("打开设置"));
    m_quitAction->setText(tr("退出方块盒子"));
    m_trayIcon->setToolTip(tr("方块盒子 %1\n点击图标显示主界面").arg(AppVersion::current()));
}

void SystemTray::buildMenu()
{
    m_menu = new QMenu();

    m_showAction = m_menu->addAction(QString());
    connect(m_showAction, &QAction::triggered, this, &SystemTray::showWindowRequested);

    m_settingsAction = m_menu->addAction(QString());
    connect(m_settingsAction, &QAction::triggered, this, &SystemTray::openSettingsRequested);

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitRequested);

    m_trayIcon->setContextMenu(m_menu);
}
