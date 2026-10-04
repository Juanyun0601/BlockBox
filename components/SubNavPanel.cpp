/**
 * @file   SubNavPanel.cpp
 * @brief  子导航卡片组面板组件实现
 * @author BlockBox Team
 * @date   2026-06-18
 */
#include "SubNavPanel.h"

#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTimer>

#include "utils/IconHelper.h"
#include "utils/ResourceManager.h"
#include "utils/ThemeManager.h"

SubNavPanel::SubNavPanel(QWidget *parent)
    : QWidget(parent)
    , m_sideBar(nullptr)
    , m_currentParentIndex(-1)
    , m_currentChildIndex(-1)
    , m_iconNormalColor("#666666")
    , m_iconSelectedColor("#10B981")
{
    setObjectName("SubNavPanel");
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // 选中图标初始取当前主题主色
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    if (themeColor.isValid())
        m_iconSelectedColor = themeColor;

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this, [this](const QString &color) {
        QColor c(color);
        if (c.isValid())
            m_iconSelectedColor = c;
        if (isVisible())
            setSelectedChild(m_currentChildIndex);
    });

    // 悬浮气泡外阴影
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 50));
    setGraphicsEffect(shadow);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 面板头部：标题 + 收起按钮（原型 .subnav-header）
    m_header = new QWidget(this);
    m_header->setObjectName("subNavHeader");
    QHBoxLayout *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(16, 12, 12, 12);
    headerLayout->setSpacing(4);

    m_headerTitle = new QLabel(tr("导航"), m_header);
    m_headerTitle->setObjectName("subNavHeaderTitle");

    m_collapseBtn = new QPushButton(m_header);
    m_collapseBtn->setObjectName("subNavCollapseBtn");
    m_collapseBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/chevron_left.svg"));
    m_collapseBtn->setIconSize(QSize(16, 16));
    m_collapseBtn->setFixedSize(24, 24);
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setToolTip(tr("收起面板"));
    connect(m_collapseBtn, &QPushButton::clicked, this, &SubNavPanel::hidePanel);

    headerLayout->addWidget(m_headerTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_collapseBtn);

    m_mainLayout->addWidget(m_header);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_scrollContent = new QWidget(m_scrollArea);
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(8, 12, 12, 16);
    m_contentLayout->setSpacing(2);

    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea);

    m_selectionHighlight = new QWidget(m_scrollContent);
    m_selectionHighlight->setObjectName("subNavHighlight");
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->lower();

    setVisible(false);
}

SubNavPanel::~SubNavPanel()
{
}

void SubNavPanel::setSideBar(SideBar *sideBar)
{
    m_sideBar = sideBar;
}

/** 计算并应用悬浮气泡的几何矩形（贴靠侧边栏右缘） */
void SubNavPanel::updateFloatGeometry()
{
    QWidget *host = parentWidget();
    if (!host)
    {
        setGeometry(kMargin, kMargin, kWidth, 120);
        return;
    }
    const int x = (m_sideBar ? m_sideBar->width() : 0) + kMargin;
    const int y = kMargin;
    const int h = qMax(120, host->height() - 2 * kMargin);
    setGeometry(x, y, kWidth, h);
    raise();
}

void SubNavPanel::showForParent(int parentIndex, bool selectFirst)
{
    if (!m_sideBar)
    {
        return;
    }

    const QVector<NavItem> &parents = m_sideBar->parentItems();
    if (parentIndex < 0 || parentIndex >= parents.size())
    {
        return;
    }

    const NavItem &parent = parents[parentIndex];
    if (parent.children.isEmpty())
    {
        hidePanel();
        return;
    }

    // 更新面板标题为父导航项名称
    if (m_headerTitle)
        m_headerTitle->setText(parent.text);

    // Clear previous content
    clearContent();

    m_currentParentIndex = parentIndex;
    int childGlobalIndex = 0;

    for (const NavItem &section : parent.children)
    {
        if (section.isSectionLabel)
        {
            // Section label
            QLabel *label = new QLabel(section.text, m_scrollContent);
            label->setObjectName("subNavSectionLabel");
            label->setContentsMargins(0, 8, 0, 2);
            m_sectionLabels.append(label);
            m_contentLayout->addWidget(label);

            for (const NavItem &child : section.children)
            {
                QPushButton *button = new QPushButton(child.text, m_scrollContent);
                button->setObjectName("subNavButton");
                button->setFixedHeight(40);
                button->setCheckable(true);
                button->setProperty("parentIndex", parentIndex);
                button->setProperty("childIndex", childGlobalIndex);
                button->setProperty("iconPath", child.iconPath);
                button->setProperty("iconIsSvg", child.iconIsSvg);
                button->setIconSize(QSize(18, 18));

                if (!child.iconPath.isEmpty())
                {
                    button->setIcon(loadIcon(child.iconPath, child.iconIsSvg, m_iconNormalColor));
                }

                connect(button, &QPushButton::clicked, this, [this, button]()
                {
                    int pIdx = button->property("parentIndex").toInt();
                    int cIdx = button->property("childIndex").toInt();

                    setSelectedChild(cIdx);
                    emit childItemClicked(pIdx, cIdx);
                });

                m_buttons.append(button);
                m_contentLayout->addWidget(button);
                childGlobalIndex++;
            }
        }
        else
        {
            // Direct child (no section grouping, e.g., settings)
            QPushButton *button = new QPushButton(section.text, m_scrollContent);
            button->setObjectName("subNavButton");
            button->setFixedHeight(40);
            button->setCheckable(true);
            button->setProperty("parentIndex", parentIndex);
            button->setProperty("childIndex", childGlobalIndex);
            button->setProperty("iconPath", section.iconPath);
            button->setProperty("iconIsSvg", section.iconIsSvg);
            button->setIconSize(QSize(18, 18));

            if (!section.iconPath.isEmpty())
            {
                button->setIcon(loadIcon(section.iconPath, section.iconIsSvg, m_iconNormalColor));
            }

            connect(button, &QPushButton::clicked, this, [this, button]()
            {
                int pIdx = button->property("parentIndex").toInt();
                int cIdx = button->property("childIndex").toInt();

                setSelectedChild(cIdx);
                emit childItemClicked(pIdx, cIdx);
            });

            m_buttons.append(button);
            m_contentLayout->addWidget(button);
            childGlobalIndex++;
        }
    }

    // Add spacer at bottom
    m_contentLayout->addStretch();

    // Reset scroll position to top
    m_scrollArea->verticalScrollBar()->setValue(0);

    // 重新计算悬浮几何（贴靠侧边栏），再显示
    updateFloatGeometry();
    setVisible(true);
    emit panelVisibilityChanged(true);

    // Select first child by default (if requested).
    // 延迟到下一次事件循环，确保按钮 geometry 已就绪以便高亮定位
    m_currentChildIndex = selectFirst ? 0 : -1;
    if (selectFirst && !m_buttons.isEmpty())
    {
        QTimer::singleShot(0, this, [this]()
        {
            if (!m_buttons.isEmpty())
                setSelectedChild(0);
        });
    }

    // Slide-in animation: 气泡从侧边栏边缘向右展开
    const QRect endRect = this->geometry();
    const QRect startRect = QRect(endRect.x(), endRect.y(), 0, endRect.height());
    auto *animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(160);
    animation->setStartValue(startRect);
    animation->setEndValue(endRect);
    animation->setEasingCurve(QEasingCurve::OutCubic);

    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void SubNavPanel::hidePanel()
{
    m_currentParentIndex = -1;

    // Slide-out animation: 气泡向侧边栏边缘收拢
    const QRect startRect = this->geometry();
    const QRect endRect = QRect(startRect.x(), startRect.y(), 0, startRect.height());
    auto *animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(140);
    animation->setStartValue(startRect);
    animation->setEndValue(endRect);
    animation->setEasingCurve(QEasingCurve::InCubic);
    connect(animation, &QPropertyAnimation::finished, this, [this]()
    {
        clearContent();
        setVisible(false);
        emit panelVisibilityChanged(false);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

QPushButton *SubNavPanel::findButtonByChildIndex(int childIndex) const
{
    for (QPushButton *btn : m_buttons)
    {
        if (btn->property("childIndex").toInt() == childIndex)
            return btn;
    }
    return nullptr;
}

void SubNavPanel::setSelectedChild(int childIndex)
{
    // 清除所有按钮的选中状态与图标颜色
    for (QPushButton *b : m_buttons)
    {
        b->setChecked(false);
        QString iconPath = b->property("iconPath").toString();
        bool isSvg = b->property("iconIsSvg").toBool();
        if (!iconPath.isEmpty())
        {
            b->setIcon(loadIcon(iconPath, isSvg, m_iconNormalColor));
        }
    }

    QPushButton *target = findButtonByChildIndex(childIndex);
    if (target)
    {
        target->setChecked(true);
        QString iconPath = target->property("iconPath").toString();
        bool isSvg = target->property("iconIsSvg").toBool();
        if (!iconPath.isEmpty())
        {
            target->setIcon(loadIcon(iconPath, isSvg, m_iconSelectedColor));
        }
        slideHighlightTo(childIndex);
    }
    else
    {
        m_selectionHighlight->setVisible(false);
        m_currentChildIndex = -1;
    }
}

void SubNavPanel::slideHighlightTo(int childIndex)
{
    QPushButton *target = findButtonByChildIndex(childIndex);
    if (!target) return;

    QRect endRect = target->geometry();
    // 按钮刚创建时 geometry 可能尚未就绪，延迟到下一次事件循环重试，
    // 确保高亮定位准确（调用方无需关心时序）。
    // 增加重试上限，避免 geometry 永不有效（如面板被隐藏/零尺寸）时无限递归。
    static constexpr int kMaxRetry = 20;
    if (!endRect.isValid() || endRect.width() <= 0)
    {
        if (m_highlightRetryCount < kMaxRetry)
        {
            ++m_highlightRetryCount;
            QTimer::singleShot(0, this, [this, childIndex]() { slideHighlightTo(childIndex); });
        }
        m_currentChildIndex = childIndex;
        return;
    }
    m_highlightRetryCount = 0;

    m_selectionHighlight->setVisible(true);

    auto *anim = new QPropertyAnimation(m_selectionHighlight, "geometry");
    anim->setDuration(250);
    anim->setStartValue(m_selectionHighlight->geometry());
    anim->setEndValue(endRect);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    m_currentChildIndex = childIndex;
}

void SubNavPanel::clearContent()
{
    m_selectionHighlight->setVisible(false);
    m_currentChildIndex = -1;

    QLayoutItem *item;
    while ((item = m_contentLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            delete item->widget();
        }
        delete item;
    }
    m_buttons.clear();
    m_sectionLabels.clear();
}

QIcon SubNavPanel::loadColoredIcon(const QString &path, const QColor &color) const
{
    QIcon baseIcon = IconHelper::loadColoredIcon(path, color, 18);
    if (baseIcon.isNull()) return QIcon();

    return baseIcon;
}

QIcon SubNavPanel::loadIcon(const QString &path, bool isSvg, const QColor &color) const
{
    if (!isSvg)
    {
        // 普通图片（如插件 PNG 图标）：按原图加载，不染色
        QPixmap pix(path);
        if (pix.isNull())
            return QIcon();
        return QIcon(pix.scaled(18, 18, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return loadColoredIcon(path, color);
}