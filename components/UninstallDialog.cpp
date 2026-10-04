/**
 * @file   UninstallDialog.cpp
 * @brief  卸载方块盒子确认对话框实现
 * @author BlockBox Team
 * @date   2026-09-12
 */
#include "UninstallDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "CustomCheckBox.h"
#include "utils/StyleKit.h"

UninstallDialog::UninstallDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_gameDataItemLabel(nullptr)
    , m_keepDataCheck(nullptr)
    , m_confirmed(false)
{
    setObjectName(QStringLiteral("uninstallDialog"));
    initUi();
}

void UninstallDialog::initUi()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumWidth(440);
    card->setMaximumWidth(540);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(10);

    QLabel *titleLabel = new QLabel(tr("卸载方块盒子"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    titleLabel->setWordWrap(true);

    QLabel *promptLabel = new QLabel(
        tr("此操作将关闭方块盒子并从电脑中移除以下内容，删除后无法恢复："), card);
    promptLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    promptLabel->setWordWrap(true);

    QLabel *launcherItemLabel = new QLabel(
        tr("· 方块盒子程序文件与全部启动器数据（配置、账户、缓存、注册表设置项）"), card);
    launcherItemLabel->setStyleSheet(StyleKit::bodyLabel());
    launcherItemLabel->setWordWrap(true);

    QLabel *quickLaunchItemLabel = new QLabel(
        tr("· 桌面上的 .blockbox 快捷启动文件"), card);
    quickLaunchItemLabel->setStyleSheet(StyleKit::bodyLabel());
    quickLaunchItemLabel->setWordWrap(true);

    m_gameDataItemLabel = new QLabel(
        tr("· 游戏实例与存档数据（.minecraft、BedrockData）"), card);
    m_gameDataItemLabel->setStyleSheet(StyleKit::bodyLabel());
    m_gameDataItemLabel->setWordWrap(true);

    QLabel *hostsItemLabel = new QLabel(
        tr("· 对系统 hosts 文件的修改（可能弹出管理员授权窗口）"), card);
    hostsItemLabel->setStyleSheet(StyleKit::bodyLabel());
    hostsItemLabel->setWordWrap(true);

    QHBoxLayout *keepDataRow = new QHBoxLayout();
    keepDataRow->setContentsMargins(0, 4, 0, 0);
    m_keepDataCheck = new CustomCheckBox(tr("保留游戏实例与存档"), card);
    connect(m_keepDataCheck, &CustomCheckBox::toggled, this, [this](bool) {
        m_gameDataItemLabel->setVisible(!m_keepDataCheck->isChecked());
    });

    QLabel *noteLabel = new QLabel(
        tr("确认卸载后方块盒子将立即退出，剩余清理在后台完成；如有正在运行的游戏，请先手动退出。"),
        card);
    noteLabel->setStyleSheet(StyleKit::mutedLabel());
    noteLabel->setWordWrap(true);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    QPushButton *cancelBtn = new QPushButton(tr("取消"), card);
    cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &UninstallDialog::reject);

    QPushButton *confirmBtn = new QPushButton(tr("确认卸载"), card);
    confirmBtn->setStyleSheet(StyleKit::dangerButton());
    connect(confirmBtn, &QPushButton::clicked, this, [this]() {
        m_confirmed = true;
        accept();
    });

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(confirmBtn);

    cardLayout->addWidget(titleLabel);
    cardLayout->addWidget(promptLabel);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(launcherItemLabel);
    cardLayout->addWidget(quickLaunchItemLabel);
    cardLayout->addWidget(m_gameDataItemLabel);
    cardLayout->addWidget(hostsItemLabel);
    cardLayout->addLayout(keepDataRow);
    cardLayout->addSpacing(2);
    cardLayout->addWidget(noteLabel);
    cardLayout->addSpacing(8);
    cardLayout->addLayout(btnRow);

    // 整体居中
    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(card, 0, 0, Qt::AlignCenter);
}

bool UninstallDialog::deleteGameData() const
{
    return !m_keepDataCheck->isChecked();
}
