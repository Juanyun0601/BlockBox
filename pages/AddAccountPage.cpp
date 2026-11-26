/**
 * @file   AddAccountPage.cpp
 * @brief  「添加角色」页实现
 * @author BlockBox Team
 * @date   2026-11-15
 */
#include "AddAccountPage.h"
#include "MicrosoftLoginDialog.h"

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "utils/AuthManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"

namespace {

/**
 * 更新认证服务器状态徽标（未检测 / 检测中 / 在线 / 无法连接）。
 *
 * 文案每次都改，但样式只在状态真的变了才重新 polish——否则每检测一次都要重算
 * 整条样式规则，切换登录方式时能看出卡顿。
 */
void setAuthServerPill(QLabel *pill, const char *state, const QString &text,
                       const QString &tooltip = QString())
{
    if (!pill)
        return;

    pill->setText(text);
    pill->setToolTip(tooltip);

    if (pill->property("pillState").toString() == QLatin1String(state))
        return;

    pill->setProperty("pillState", QLatin1String(state));
    if (QStyle *style = pill->style())
    {
        style->unpolish(pill);
        style->polish(pill);
    }
    pill->update();
}

/** 表单字段：标签在上、控件在下（对照 HTML 原型 .form-field） */
QWidget *buildLoginField(const QString &labelText, QWidget *editor, QWidget *parent)
{
    auto *field = new QWidget(parent);
    auto *fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(6);

    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("loginFieldLabel"));
    fieldLayout->addWidget(label);
    fieldLayout->addWidget(editor);
    return field;
}

/** 表单提示条：说明该登录方式的能力与限制（对照 HTML 原型 .form-hint） */
QLabel *buildLoginHint(const QString &text, QWidget *parent)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("loginHint"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return hint;
}

} // namespace

AddAccountPage::AddAccountPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("addAccountPage"));
    buildUi();
}

void AddAccountPage::open()
{
    resetForm();
    updateColumnWidth();
    updateColumnHeight();
}

void AddAccountPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateColumnWidth();
    updateColumnHeight();
}

void AddAccountPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateColumnWidth();
    updateColumnHeight();
}

void AddAccountPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 页面可以滚：手机竖屏 / 窄窗口下卡片内容比可视区高时也能看全
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    outer->addWidget(m_scroll);

    m_page = new QWidget(m_scroll);
    m_page->setObjectName(QStringLiteral("accountLoginPage"));
    m_page->setAttribute(Qt::WA_StyledBackground, false);
    auto *loginLayout = new QVBoxLayout(m_page);
    loginLayout->setContentsMargins(24, 24, 24, 24);
    loginLayout->setSpacing(0);

    // 单列卡片：标题 + 分段登录方式 + 表单（对照 HTML 原型 .account-add）
    m_column = new QWidget(m_page);
    m_column->setObjectName(QStringLiteral("loginColumn"));
    auto *columnLayout = new QVBoxLayout(m_column);
    columnLayout->setContentsMargins(24, 22, 24, 24);
    columnLayout->setSpacing(0);

    auto *titleLabel = new QLabel(tr("添加角色"), m_column);
    titleLabel->setObjectName(QStringLiteral("loginTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    columnLayout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(tr("选择一种登录方式，把新账户加入方块盒子"), m_column);
    subtitleLabel->setObjectName(QStringLiteral("loginSubtitle"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    columnLayout->addWidget(subtitleLabel);
    columnLayout->addSpacing(16);

    // 分段控件：三个登录方式等宽铺满（QTabWidget 自带 TabBar 高度不随容器拉伸，隐藏后自绘）
    auto *segBar = new QWidget(m_column);
    segBar->setObjectName(QStringLiteral("loginSegBar"));
    auto *segLayout = new QHBoxLayout(segBar);
    segLayout->setContentsMargins(4, 4, 4, 4);
    segLayout->setSpacing(4);

    const QStringList segLabels = {tr("离线"), tr("微软正版"), tr("第三方")};
    QList<QPushButton *> segButtons;
    for (int i = 0; i < segLabels.size(); ++i)
    {
        auto *segBtn = new QPushButton(segLabels.at(i), segBar);
        segBtn->setObjectName(QStringLiteral("loginSegBtn"));
        segBtn->setCheckable(true);
        segBtn->setAutoExclusive(true);
        segBtn->setCursor(Qt::PointingHandCursor);
        segBtn->setChecked(i == 0);
        segLayout->addWidget(segBtn, 1);
        segButtons.append(segBtn);
    }
    columnLayout->addWidget(segBar);
    columnLayout->addSpacing(12);

    m_tabs = new QTabWidget(m_column);
    m_tabs->setObjectName(QStringLiteral("loginMethodTabs"));
    m_tabs->tabBar()->hide();   // 用上面的分段控件切换登录方式
    columnLayout->addWidget(m_tabs);

    // 卡片在页面里水平居中、竖直居中（窗口矮时收成贴顶可滚动）
    auto *centerRow = new QHBoxLayout();
    centerRow->setContentsMargins(0, 0, 0, 0);
    centerRow->setSpacing(0);
    centerRow->addStretch(1);
    centerRow->addWidget(m_column, 0, Qt::AlignTop);
    centerRow->addStretch(1);

    loginLayout->addStretch(1);
    loginLayout->addLayout(centerRow);
    loginLayout->addStretch(1);
    m_scroll->setWidget(m_page);

    // ── 微软正版 ──
    auto *microsoftTab = new QWidget(m_tabs);
    auto *microsoftLayout = new QVBoxLayout(microsoftTab);
    microsoftLayout->setContentsMargins(0, 0, 0, 0);
    microsoftLayout->setSpacing(12);

    auto *heroTile = new QLabel(microsoftTab);
    heroTile->setObjectName(QStringLiteral("loginHeroTile"));
    heroTile->setFixedSize(64, 64);
    heroTile->setAlignment(Qt::AlignCenter);
    heroTile->setPixmap(
        IconHelper::loadColoredIcon(":/Images/Icons/grid.svg", QColor("#00A4EF"), 28).pixmap(28, 28));
    auto *heroIconRow = new QHBoxLayout();
    heroIconRow->addStretch();
    heroIconRow->addWidget(heroTile);
    heroIconRow->addStretch();
    microsoftLayout->addLayout(heroIconRow);

    auto *heroTitle = new QLabel(tr("微软正版登录"), microsoftTab);
    heroTitle->setObjectName(QStringLiteral("loginHeroTitle"));
    heroTitle->setAlignment(Qt::AlignCenter);
    microsoftLayout->addWidget(heroTitle);

    auto *heroDesc = new QLabel(
        tr("跳转浏览器完成授权，登录后自动同步正版皮肤、成就与联机身份。"), microsoftTab);
    heroDesc->setObjectName(QStringLiteral("loginHeroDesc"));
    heroDesc->setAlignment(Qt::AlignCenter);
    heroDesc->setWordWrap(true);
    microsoftLayout->addWidget(heroDesc);

    auto *chipRow = new QHBoxLayout();
    chipRow->setSpacing(8);
    chipRow->addStretch();
    const QStringList featureChips = {tr("正版联机"), tr("皮肤同步"), tr("成就同步")};
    for (const QString &chipText : featureChips)
    {
        auto *chip = new QLabel(chipText, microsoftTab);
        chip->setObjectName(QStringLiteral("loginChip"));
        chip->setAlignment(Qt::AlignCenter);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch();
    microsoftLayout->addLayout(chipRow);
    microsoftLayout->addSpacing(4);

    m_microsoftProgress = new QProgressBar(microsoftTab);
    m_microsoftProgress->setObjectName(QStringLiteral("microsoftProgressBar"));
    m_microsoftProgress->setMinimum(0);
    m_microsoftProgress->setMaximum(100);
    m_microsoftProgress->setValue(0);
    m_microsoftProgress->setVisible(false);
    microsoftLayout->addWidget(m_microsoftProgress);

    m_microsoftStatus = new QLabel(tr(""), microsoftTab);
    m_microsoftStatus->setObjectName(QStringLiteral("microsoftStatusLabel"));
    m_microsoftStatus->setAlignment(Qt::AlignCenter);
    m_microsoftStatus->setWordWrap(true);
    microsoftLayout->addWidget(m_microsoftStatus);

    microsoftLayout->addStretch();
    const QString microsoftBtnLabel = tr("使用 Microsoft 账户登录");
    m_microsoftLoginBtn = new QPushButton(microsoftBtnLabel, microsoftTab);
    m_microsoftLoginBtn->setObjectName(QStringLiteral("loginPrimaryBtn"));
    m_microsoftLoginBtn->setMinimumHeight(44);
    m_microsoftLoginBtn->setCursor(Qt::PointingHandCursor);
    microsoftLayout->addWidget(m_microsoftLoginBtn);

    connect(m_microsoftLoginBtn, &QPushButton::clicked, this, [=]() {
        // 登录 API 还没批下来：先统一提示，不进浏览器授权流程
        if (MicrosoftLoginDialog::isLoginUnavailable())
        {
            m_microsoftStatus->setText(MicrosoftLoginDialog::loginUnavailableMessage());
            MicrosoftLoginDialog::showUnavailableNotice(this);
            return;
        }

        m_microsoftLoginBtn->setEnabled(false);
        m_microsoftLoginBtn->setText(tr("登录中..."));
        m_microsoftProgress->setVisible(true);
        m_microsoftProgress->setValue(0);
        m_microsoftStatus->setText(tr("正在启动登录流程..."));

        auto *loginDialog = new MicrosoftLoginDialog(this);
        connect(loginDialog, &MicrosoftLoginDialog::loginSucceeded,
                this, [=](const QString &username, const QString &accessToken,
                          const QString &refreshToken) {
                    m_microsoftLoginBtn->setEnabled(true);
                    m_microsoftLoginBtn->setText(microsoftBtnLabel);
                    m_microsoftProgress->setVisible(false);
                    m_microsoftStatus->setText(tr("登录成功！"));

                    bool saved = false;
                    if (SettingsManager::instance())
                    {
                        SettingsManager::instance()->addMicrosoftAccount(
                            username, accessToken, refreshToken, QString(), QString(), true);
                        saved = SettingsManager::instance()->accountExists(username);
                    }

                    if (!saved)
                    {
                        // 授权成功了但账户没存进磁盘（例如启动器装在只读目录），必须如实告知
                        AppMessageBox::information(
                            window(), tr("保存失败"),
                            tr("Microsoft 账户已登录成功，但账户信息没能保存到磁盘，请检查启动器所在文件夹的写入权限。"));
                        loginDialog->deleteLater();
                        return;
                    }

                    NotificationManager::showSuccess(window(),
                                                     tr("Microsoft账户登录成功：%1").arg(username));
                    loginDialog->deleteLater();
                    emit accountAdded(username);
                });

        connect(loginDialog, &MicrosoftLoginDialog::loginFailed, this, [=]() {
            m_microsoftLoginBtn->setEnabled(true);
            m_microsoftLoginBtn->setText(microsoftBtnLabel);
            m_microsoftProgress->setVisible(false);
            m_microsoftStatus->setText(tr("登录失败"));
            loginDialog->deleteLater();
        });

        connect(loginDialog, &MicrosoftLoginDialog::loginCanceled, this, [=]() {
            m_microsoftLoginBtn->setEnabled(true);
            m_microsoftLoginBtn->setText(microsoftBtnLabel);
            m_microsoftProgress->setVisible(false);
            m_microsoftStatus->setText(tr("登录已取消"));
            loginDialog->deleteLater();
        });

        loginDialog->startLogin();
        loginDialog->exec();
    });

    // ── 离线 ──
    auto *offlineTab = new QWidget(m_tabs);
    auto *offlineLayout = new QVBoxLayout(offlineTab);
    offlineLayout->setContentsMargins(0, 0, 0, 0);
    offlineLayout->setSpacing(14);

    m_offlineNameEdit = new QLineEdit(offlineTab);
    m_offlineNameEdit->setObjectName(QStringLiteral("loginInput"));
    m_offlineNameEdit->setPlaceholderText(tr("输入游戏内显示的名称，例如 Steve"));
    m_offlineNameEdit->setClearButtonEnabled(true);
    m_offlineNameEdit->setMaxLength(16);
    offlineLayout->addWidget(buildLoginField(tr("玩家名"), m_offlineNameEdit, offlineTab));

    offlineLayout->addWidget(buildLoginHint(
        tr("离线账户无需联网验证，适合单机与局域网游戏；无法使用正版服务器、皮肤同步与成就。"),
        offlineTab));

    offlineLayout->addStretch();
    auto *offlineLoginBtn = new QPushButton(tr("创建离线账户"), offlineTab);
    offlineLoginBtn->setObjectName(QStringLiteral("loginPrimaryBtn"));
    offlineLoginBtn->setMinimumHeight(44);
    offlineLoginBtn->setCursor(Qt::PointingHandCursor);
    offlineLayout->addWidget(offlineLoginBtn);

    connect(offlineLoginBtn, &QPushButton::clicked, this, [this]() {
        const QString username = m_offlineNameEdit->text().trimmed();
        if (username.isEmpty())
        {
            NotificationManager::showError(window(), tr("用户名不能为空"));
            return;
        }

        if (SettingsManager::instance() && SettingsManager::instance()->accountExists(username))
        {
            NotificationManager::showError(window(), tr("该用户名已存在"));
            return;
        }

        bool created = false;
        if (SettingsManager::instance())
            created = SettingsManager::instance()->addAccount(username, tr("离线"), QString(), true);

        if (!created)
        {
            // 写入失败（例如启动器装在 Program Files 但没有写入权限）时如实提示
            NotificationManager::showError(
                window(),
                tr("离线账户创建失败：账户信息没能保存到磁盘，请检查启动器所在文件夹的写入权限"));
            return;
        }

        NotificationManager::showSuccess(window(), tr("离线账户创建成功"));
        emit accountAdded(username);
    });

    // ── 第三方（authlib-injector） ──
    auto *thirdPartyTab = new QWidget(m_tabs);
    auto *thirdPartyLayout = new QVBoxLayout(thirdPartyTab);
    thirdPartyLayout->setContentsMargins(0, 0, 0, 0);
    thirdPartyLayout->setSpacing(14);

    m_serverCombo = new QComboBox(thirdPartyTab);
    m_serverCombo->setObjectName(QStringLiteral("loginCombo"));
    m_serverCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    QList<AuthServerInfo> servers;
    if (SettingsManager::instance())
        servers = SettingsManager::instance()->getAuthServers();
    for (const AuthServerInfo &server : servers)
        m_serverCombo->addItem(server.name, server.url);
    m_serverCombo->addItem(tr("添加自定义服务器"), QStringLiteral("custom"));

    m_serverPill = new QLabel(thirdPartyTab);
    m_serverPill->setObjectName(QStringLiteral("loginStatusPill"));
    m_serverPill->setAlignment(Qt::AlignCenter);
    setAuthServerPill(m_serverPill, "unknown", tr("未检测"));

    auto *serverFieldEditor = new QWidget(thirdPartyTab);
    auto *serverFieldRow = new QHBoxLayout(serverFieldEditor);
    serverFieldRow->setContentsMargins(0, 0, 0, 0);
    serverFieldRow->setSpacing(8);
    serverFieldRow->addWidget(m_serverCombo, 1);
    serverFieldRow->addWidget(m_serverPill, 0, Qt::AlignVCenter);
    thirdPartyLayout->addWidget(buildLoginField(tr("认证服务器"), serverFieldEditor, thirdPartyTab));

    m_thirdPartyUserEdit = new QLineEdit(thirdPartyTab);
    m_thirdPartyUserEdit->setObjectName(QStringLiteral("loginInput"));
    m_thirdPartyUserEdit->setPlaceholderText(tr("认证服务器上的账户名或邮箱"));
    m_thirdPartyUserEdit->setClearButtonEnabled(true);
    thirdPartyLayout->addWidget(
        buildLoginField(tr("用户名"), m_thirdPartyUserEdit, thirdPartyTab));

    m_thirdPartyPasswordEdit = new QLineEdit(thirdPartyTab);
    m_thirdPartyPasswordEdit->setObjectName(QStringLiteral("loginInput"));
    m_thirdPartyPasswordEdit->setEchoMode(QLineEdit::Password);
    m_thirdPartyPasswordEdit->setPlaceholderText(tr("输入密码"));
    thirdPartyLayout->addWidget(
        buildLoginField(tr("密码"), m_thirdPartyPasswordEdit, thirdPartyTab));

    thirdPartyLayout->addWidget(buildLoginHint(
        tr("支持方块皮肤站、LittleSkin 等 Yggdrasil 兼容认证服务器；账号密码仅发送至所选服务器。"
           "登录后启动游戏会自动注入外置登录组件，游戏内可正常识别身份和皮肤。"),
        thirdPartyTab));

    thirdPartyLayout->addStretch();
    m_thirdPartyLoginBtn = new QPushButton(tr("登录"), thirdPartyTab);
    m_thirdPartyLoginBtn->setObjectName(QStringLiteral("loginPrimaryBtn"));
    m_thirdPartyLoginBtn->setMinimumHeight(44);
    m_thirdPartyLoginBtn->setCursor(Qt::PointingHandCursor);
    thirdPartyLayout->addWidget(m_thirdPartyLoginBtn);

    // 服务器连通性检测：连不上就如实显示「无法连接」，不再写死「在线」
    const auto checkCurrentServer = [this]() {
        const QString serverUrl = m_serverCombo->currentData().toString();
        if (serverUrl.isEmpty() || serverUrl == QLatin1String("custom"))
        {
            setAuthServerPill(m_serverPill, "unknown", tr("未检测"));
            return;
        }
        setAuthServerPill(m_serverPill, "busy", tr("检测中..."));
        if (AuthManager::instance())
            AuthManager::instance()->checkAuthServer(serverUrl);
    };

    // 添加自定义认证服务器：地址先规范化（补协议 / 补 /api/yggdrasil/ / 去掉误粘贴的端点地址）
    const auto addCustomServer = [this, checkCurrentServer]() {
        QDialog customServerDialog(window());
        customServerDialog.setWindowTitle(tr("添加自定义服务器"));
        customServerDialog.resize(420, 240);

        auto *dialogLayout = new QVBoxLayout(&customServerDialog);

        dialogLayout->addWidget(new QLabel(tr("服务器名称："), &customServerDialog));
        auto *nameInput = new QLineEdit(&customServerDialog);
        nameInput->setPlaceholderText(tr("例如 我的皮肤站"));
        dialogLayout->addWidget(nameInput);

        dialogLayout->addWidget(new QLabel(tr("服务器地址："), &customServerDialog));
        auto *urlInput = new QLineEdit(&customServerDialog);
        urlInput->setPlaceholderText(QStringLiteral("例如：https://littleskin.cn/api/yggdrasil/"));
        dialogLayout->addWidget(urlInput);

        auto *urlHint = new QLabel(
            tr("填 Yggdrasil API 根地址（authlib-injector 里那个地址）；只填域名会自动补成 /api/yggdrasil/。"),
            &customServerDialog);
        urlHint->setWordWrap(true);
        dialogLayout->addWidget(urlHint);

        auto *buttonLayout = new QHBoxLayout();
        auto *cancelBtn = new QPushButton(tr("取消"), &customServerDialog);
        cancelBtn->setObjectName(QStringLiteral("cancelButton"));
        auto *okBtn = new QPushButton(tr("确定"), &customServerDialog);
        okBtn->setObjectName(QStringLiteral("okBtn"));
        buttonLayout->addStretch();
        buttonLayout->addWidget(cancelBtn);
        buttonLayout->addWidget(okBtn);
        dialogLayout->addLayout(buttonLayout);

        connect(cancelBtn, &QPushButton::clicked, &customServerDialog, &QDialog::reject);
        connect(okBtn, &QPushButton::clicked, &customServerDialog, &QDialog::accept);

        if (customServerDialog.exec() != QDialog::Accepted)
            return;

        const QString normalizedUrl = AuthManager::normalizeAuthServerUrl(urlInput->text());
        if (normalizedUrl.isEmpty())
        {
            NotificationManager::showError(
                window(), tr("服务器地址无效，请填写形如 https://littleskin.cn/api/yggdrasil/ 的地址"));
            return;
        }

        QString serverName = nameInput->text().trimmed();
        if (serverName.isEmpty())
            serverName = QUrl(normalizedUrl).host();

        if (SettingsManager::instance())
            SettingsManager::instance()->addAuthServer(normalizedUrl, serverName);

        // addAuthServer 对重复地址会直接忽略，这里按地址查一遍，避免下拉框里加出两条一样的
        int targetIndex = -1;
        for (int i = 0; i < m_serverCombo->count(); ++i)
        {
            const QString itemUrl =
                AuthManager::normalizeAuthServerUrl(m_serverCombo->itemData(i).toString());
            if (!itemUrl.isEmpty() && itemUrl == normalizedUrl)
            {
                targetIndex = i;
                break;
            }
        }
        if (targetIndex < 0)
        {
            targetIndex = m_serverCombo->count() - 1;   // 插在"添加自定义服务器"之前
            m_serverCombo->insertItem(targetIndex, serverName, normalizedUrl);
        }
        if (m_serverCombo->currentIndex() == targetIndex)
            checkCurrentServer();
        else
            m_serverCombo->setCurrentIndex(targetIndex);
    };

    connect(m_serverCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, addCustomServer, checkCurrentServer](int index) {
                if (m_serverCombo->currentData().toString() != QLatin1String("custom"))
                {
                    // 记住最近一次选中的真实服务器，弹窗取消后好退回这里
                    m_serverCombo->setProperty("lastRealIndex", index);
                    checkCurrentServer();
                    return;
                }

                addCustomServer();

                // 弹窗被取消：当前项还停在"添加自定义服务器"上，退回上一个真实服务器
                if (m_serverCombo->currentData().toString() == QLatin1String("custom"))
                {
                    const int previous = m_serverCombo->property("lastRealIndex").toInt();
                    if (previous >= 0 && previous < m_serverCombo->count()
                        && m_serverCombo->itemData(previous).toString() != QLatin1String("custom"))
                    {
                        m_serverCombo->setCurrentIndex(previous);
                    }
                }
            });

    // 切到第三方页时顺手测一次，进页面就能看到服务器通不通
    connect(m_tabs, &QTabWidget::currentChanged, this, [checkCurrentServer](int index) {
        if (index == 2)
            checkCurrentServer();
    });

    // 检测结果回填到徽标（只认当前选中服务器，换过服务器后旧结果直接丢弃）
    if (AuthManager::instance())
    {
        connect(AuthManager::instance(), &AuthManager::authServerStatusChecked, this,
                [this](const QString &serverUrl, bool online, const QString &detail) {
                    if (m_serverCombo->currentData().toString() != serverUrl)
                        return;
                    if (online)
                        setAuthServerPill(m_serverPill, "ok", tr("在线"), detail);
                    else
                        setAuthServerPill(m_serverPill, "error", tr("无法连接"), detail);
                });
    }

    // 连接认证成功信号（username 为服务器返回的正式角色名）
    if (AuthManager::instance())
    {
        connect(AuthManager::instance(), &AuthManager::authenticationSucceeded, this,
                [this](const QString &username, const QString &serverUrl, const QString &uuid,
                       const QString &accessToken, const QString &clientToken, const QString &skinUrl) {
                    m_thirdPartyLoginBtn->setText(tr("登录"));
                    m_thirdPartyLoginBtn->setEnabled(true);

                    // 账户类型记成服务器名：列表里一眼就能看出是哪个皮肤站的号
                    QString serverName = QUrl(serverUrl).host();
                    for (int i = 0; i < m_serverCombo->count(); ++i)
                    {
                        const QString itemUrl =
                            AuthManager::normalizeAuthServerUrl(m_serverCombo->itemData(i).toString());
                        if (!itemUrl.isEmpty() && itemUrl == serverUrl)
                        {
                            serverName = m_serverCombo->itemText(i);
                            break;
                        }
                    }

                    // 角色名不分大小写，与写入时的判重保持一致
                    if (SettingsManager::instance())
                    {
                        const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
                        for (const AccountInfo &account : accounts)
                        {
                            if (account.username.compare(username, Qt::CaseInsensitive) == 0)
                            {
                                NotificationManager::showError(
                                    window(),
                                    tr("账户列表里已经有叫 %1 的账户了，请先删掉再添加").arg(username));
                                return;
                            }
                        }
                    }

                    bool created = false;
                    if (SettingsManager::instance())
                    {
                        created = SettingsManager::instance()->addThirdPartyAccount(
                            username, serverName, serverUrl, accessToken, clientToken, uuid, skinUrl,
                            true);
                    }

                    if (!created)
                    {
                        NotificationManager::showError(
                            window(),
                            tr("第三方账户登录成功，但账户信息没能保存到磁盘，请检查启动器所在文件夹的写入权限"));
                        return;
                    }

                    m_thirdPartyUserEdit->clear();
                    m_thirdPartyPasswordEdit->clear();

                    NotificationManager::showSuccess(window(), tr("第三方账户登录成功：%1").arg(username));
                    emit accountAdded(username);
                });
    }

    if (AuthManager::instance())
    {
        connect(AuthManager::instance(), &AuthManager::authenticationFailed, this,
                [this](const QString &errorMessage) {
                    m_thirdPartyLoginBtn->setText(tr("登录"));
                    m_thirdPartyLoginBtn->setEnabled(true);
                    NotificationManager::showError(window(), errorMessage);
                });
    }

    connect(m_thirdPartyLoginBtn, &QPushButton::clicked, this, [this]() {
        const QString username = m_thirdPartyUserEdit->text().trimmed();
        const QString password = m_thirdPartyPasswordEdit->text();
        const QString serverUrl = m_serverCombo->currentData().toString();

        if (username.isEmpty() || password.isEmpty())
        {
            NotificationManager::showError(window(), tr("用户名和密码不能为空"));
            return;
        }
        if (serverUrl.isEmpty() || serverUrl == QLatin1String("custom"))
        {
            NotificationManager::showError(window(), tr("请先选择或添加一个认证服务器"));
            return;
        }

        m_thirdPartyLoginBtn->setText(tr("登录中..."));
        m_thirdPartyLoginBtn->setEnabled(false);
        m_serverPill->setToolTip(tr("正在向 %1 验证账号").arg(QUrl(serverUrl).host()));

        if (AuthManager::instance())
            AuthManager::instance()->authenticate(username, password, serverUrl);
    });

    // 顺序与 HTML 原型一致：离线 / 微软正版 / 第三方
    m_tabs->addTab(offlineTab, tr("离线"));
    m_tabs->addTab(microsoftTab, tr("微软正版"));
    m_tabs->addTab(thirdPartyTab, tr("第三方"));

    // 分段控件 <-> 登录方式页 双向同步
    for (int i = 0; i < segButtons.size(); ++i)
    {
        QPushButton *segBtn = segButtons.at(i);
        connect(segBtn, &QPushButton::clicked, this, [this, i]() { m_tabs->setCurrentIndex(i); });
    }
    connect(m_tabs, &QTabWidget::currentChanged, this, [segButtons](int index) {
        for (int i = 0; i < segButtons.size(); ++i)
            segButtons.at(i)->setChecked(i == index);
    });
    // 换登录方式后按新表单重算卡片高度
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { updateColumnHeight(); });

    updateColumnWidth();
    updateColumnHeight();
}

void AddAccountPage::resetForm()
{
    if (m_tabs)
        m_tabs->setCurrentIndex(0);

    if (m_offlineNameEdit)
        m_offlineNameEdit->clear();
    if (m_thirdPartyUserEdit)
        m_thirdPartyUserEdit->clear();
    if (m_thirdPartyPasswordEdit)
        m_thirdPartyPasswordEdit->clear();

    if (m_microsoftProgress)
    {
        m_microsoftProgress->setVisible(false);
        m_microsoftProgress->setValue(0);
    }
    if (m_microsoftStatus)
        m_microsoftStatus->clear();
    if (m_microsoftLoginBtn)
    {
        m_microsoftLoginBtn->setEnabled(true);
        m_microsoftLoginBtn->setText(tr("使用 Microsoft 账户登录"));
    }
    if (m_thirdPartyLoginBtn)
    {
        m_thirdPartyLoginBtn->setEnabled(true);
        m_thirdPartyLoginBtn->setText(tr("登录"));
    }

    // 服务器连通性重新检测：上次停在哪台服务器就测哪台
    if (m_serverPill)
        setAuthServerPill(m_serverPill, "unknown", tr("未检测"));
    if (m_serverCombo && m_serverCombo->count() > 0
        && m_serverCombo->currentData().toString() == QLatin1String("custom"))
    {
        m_serverCombo->setCurrentIndex(0);
    }
}

void AddAccountPage::updateColumnWidth()
{
    if (!m_column || !m_page || !m_page->layout())
        return;

    const QMargins cm = m_page->layout()->contentsMargins();
    int available = width() - cm.left() - cm.right();
    if (available <= 0 && m_scroll)
        available = m_scroll->viewport()->width() - cm.left() - cm.right();

    // 宽屏固定 560；窄屏（手机）让卡片跟着收缩，避免横向被裁掉
    const int target = qBound(260, available, 560);
    if (m_column->width() != target)
        m_column->setFixedWidth(target);
}

void AddAccountPage::updateColumnHeight()
{
    if (!m_column || !m_tabs)
        return;

    QLayout *columnLayout = m_column->layout();
    if (!columnLayout)
        return;

    // 表单区宽度 = 卡片宽 - 左右内边距
    const QMargins cm = columnLayout->contentsMargins();
    int paneWidth = m_column->width() - cm.left() - cm.right();
    if (paneWidth <= 0)
        paneWidth = qMax(200, 560 - cm.left() - cm.right());

    QWidget *page = m_tabs->currentWidget();
    int pageHeight = (page && page->layout()) ? page->layout()->heightForWidth(paneWidth) : 0;
    if (pageHeight <= 0 && page)
        pageHeight = page->sizeHint().height();
    if (pageHeight <= 0)
        pageHeight = 200;
    m_tabs->setFixedHeight(pageHeight);

    // 逐项累加：除表单区外各项高度稳定，避开 QWidget 的 sizeHint 缓存
    int totalHeight = cm.top() + cm.bottom();
    for (int i = 0; i < columnLayout->count(); ++i)
    {
        QLayoutItem *item = columnLayout->itemAt(i);
        if (!item)
            continue;
        const bool isPane = item->widget() && item->widget() == m_tabs;
        totalHeight += isPane ? pageHeight : item->sizeHint().height();
    }
    totalHeight += (columnLayout->count() - 1) * qMax(0, columnLayout->spacing());

    if (m_column->height() != totalHeight)
        m_column->setFixedHeight(totalHeight);
}
