/**
 * @file   AiComposeDialog.cpp
 * @brief  手机端 AI 助手输入窗口实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "AiComposeDialog.h"

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

AiComposeDialog::AiComposeDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_titleLabel(nullptr)
    , m_contentLayout(nullptr)
{
    setObjectName(QStringLiteral("aiComposeDialog"));
    setWindowTitle(tr("输入消息"));
    // 页内其它弹窗（模型选择、引用资源等）都挂在主窗口下，
    // 若本窗口设为窗口模态会把它们一并挡住，这里改为非模态：
    // 本窗口本身就铺满主窗口，点到卡片外面即关闭，交互上仍是「弹窗」。
    setWindowModality(Qt::NonModal);
    initUI();
}

void AiComposeDialog::initUI()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("aiComposeCard"));
    // 输入卡片自带一整行模型/工作区/权限选择框与图标按钮，
    // 卡片宽度要留够，过窄会把工具行挤成两半
    card->setMinimumWidth(600);
    card->setMaximumWidth(900);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 14, 20, 16);
    cardLayout->setSpacing(10);

    // 标题行：图标 + 标题 + 关闭按钮
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *iconLabel = new QLabel(card);
    iconLabel->setObjectName(QStringLiteral("aiComposeIcon"));
    iconLabel->setFixedSize(26, 26);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(IconHelper::loadColoredIcon(
                             QStringLiteral(":/Images/Icons/topbar_chat.svg"),
                             ThemeManager::instance()->currentThemeColor(), 16)
                             .pixmap(16, 16));
    headerLayout->addWidget(iconLabel);

    m_titleLabel = new QLabel(tr("和 AI 说点什么"), card);
    m_titleLabel->setObjectName(QStringLiteral("aiComposeTitle"));
    headerLayout->addWidget(m_titleLabel, 1);

    auto *closeBtn = new QToolButton(card);
    closeBtn->setObjectName(QStringLiteral("aiComposeCloseBtn"));
    closeBtn->setAutoRaise(true);
    closeBtn->setIcon(IconHelper::loadColoredIcon(
                          QStringLiteral(":/Images/Icons/close.svg"),
                          ThemeManager::instance()->currentTextColor(), 14));
    closeBtn->setIconSize(QSize(14, 14));
    closeBtn->setFixedSize(26, 26);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(tr("关闭"));
    connect(closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(closeBtn);

    cardLayout->addLayout(headerLayout);

    // 内容区：输入卡片由页面搬进来
    auto *content = new QWidget(card);
    content->setObjectName(QStringLiteral("aiComposeContent"));
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(8);
    cardLayout->addWidget(content);

    rootLayout->addWidget(card, 0, Qt::AlignCenter);
}

QVBoxLayout *AiComposeDialog::contentLayout() const
{
    return m_contentLayout;
}
