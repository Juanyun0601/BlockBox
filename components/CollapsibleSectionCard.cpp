/**
 * @file   CollapsibleSectionCard.cpp
 * @brief  可折叠设置分区卡片实现
 * @author BlockBox Team
 * @date   2026-08-18
 */
#include "CollapsibleSectionCard.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

CollapsibleSectionCard::CollapsibleSectionCard(const QString &title, bool collapsed, QWidget *parent)
    : QFrame(parent)
    , m_headerBtn(nullptr)
    , m_content(nullptr)
    , m_contentLayout(nullptr)
{
    setObjectName("settingsCard");

    QVBoxLayout *cardLayout = new QVBoxLayout(this);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    m_headerBtn = new QPushButton(title, this);
    m_headerBtn->setObjectName("settingsSectionHeader");
    m_headerBtn->setCursor(Qt::PointingHandCursor);
    m_headerBtn->setCheckable(true);
    m_headerBtn->setChecked(!collapsed);
    m_headerBtn->setToolTip(tr("点击展开 / 收起此分区"));
    cardLayout->addWidget(m_headerBtn);

    m_content = new QWidget(this);
    m_content->setVisible(!collapsed);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    cardLayout->addWidget(m_content);

    updateArrowIcon(!collapsed);

    connect(m_headerBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_content->setVisible(checked);
        updateArrowIcon(checked);
        emit expandedChanged(checked);
    });

    // 主题文字颜色变更时刷新箭头颜色
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged, this, [this]() {
        updateArrowIcon(m_headerBtn->isChecked());
    });
}

QVBoxLayout *CollapsibleSectionCard::contentLayout() const
{
    return m_contentLayout;
}

void CollapsibleSectionCard::setTitle(const QString &title)
{
    m_headerBtn->setText(title);
}

QString CollapsibleSectionCard::title() const
{
    return m_headerBtn->text();
}

bool CollapsibleSectionCard::isCollapsed() const
{
    return !m_headerBtn->isChecked();
}

void CollapsibleSectionCard::setCollapsed(bool collapsed)
{
    m_headerBtn->setChecked(!collapsed);
}

void CollapsibleSectionCard::updateArrowIcon(bool expanded)
{
    const QColor color(ThemeManager::instance()->currentTextColor());
    m_headerBtn->setIcon(IconHelper::loadColoredIcon(
        expanded ? QStringLiteral(":/Images/Icons/chevron_down.svg")
                 : QStringLiteral(":/Images/Icons/chevron_right.svg"),
        color, 14));
    m_headerBtn->setIconSize(QSize(14, 14));
}
