#include "ResourceSyncListWindow.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "AppMessageBox.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace {

/** 类型徽标的展示文字与颜色 */
struct TypeBadgeStyle
{
    QString label;
    QColor color;
};

TypeBadgeStyle typeBadge(ContentType type)
{
    const QString label = ContentTypeConfig::getConfig(type).displayName;
    QColor color;
    switch (type)
    {
    case ContentType::Mod:
        color = QColor("#FF9800");
        break;
    case ContentType::ResourcePack:
        color = QColor("#4CAF50");
        break;
    case ContentType::ShaderPack:
        color = QColor("#2196F3");
        break;
    default:
        color = QColor("#9E9E9E");
        break;
    }
    return { label, color };
}

} // namespace

ResourceSyncListWindow::ResourceSyncListWindow(QWidget *parent)
    : QWidget(parent)
    , m_titleBar(nullptr)
    , m_titleLabel(nullptr)
    , m_countLabel(nullptr)
    , m_hintLabel(nullptr)
    , m_closeBtn(nullptr)
    , m_scrollArea(nullptr)
    , m_listContainer(nullptr)
    , m_listLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_keepAllBtn(nullptr)
    , m_aiBtn(nullptr)
{
    // 独立小窗：无边框 + 半透明背景，圆角卡片由 paintEvent 统一绘制
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(tr("未适配资源清单"));
    resize(460, 540);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);

    auto *contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);
    outerLayout->addLayout(contentLayout);

    // 标题栏：空白处按住可拖动整窗
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("syncListTitleBar");
    m_titleBar->setFixedHeight(34);
    m_titleBar->installEventFilter(this);
    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(14, 4, 8, 4);
    titleLayout->setSpacing(8);

    m_titleLabel = new QLabel(tr("未适配资源清单"), m_titleBar);
    m_titleLabel->setObjectName("syncListTitle");
    m_countLabel = new QLabel(m_titleBar);
    m_countLabel->setObjectName("syncListCount");

    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setObjectName("syncListCloseBtn");
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(tr("关闭清单悬浮窗"));
    m_closeBtn->setText(QStringLiteral("×"));

    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_countLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(m_closeBtn);
    contentLayout->addWidget(m_titleBar);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName("syncListHint");
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setContentsMargins(14, 0, 14, 0);
    contentLayout->addWidget(m_hintLabel);

    // 资源列表（滚动）
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setObjectName("syncListScroll");
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_listContainer = new QWidget(m_scrollArea);
    m_listContainer->setObjectName("syncListContainer");
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(12, 0, 12, 0);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();

    m_emptyLabel = new QLabel(tr("清单已清空"), m_listContainer);
    m_emptyLabel->setObjectName("syncListEmpty");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_listLayout->insertWidget(0, m_emptyLabel);
    m_emptyLabel->hide();

    m_scrollArea->setWidget(m_listContainer);
    contentLayout->addWidget(m_scrollArea, 1);

    // 底部操作：全部保留 / 让 AI 找替代
    auto *footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(14, 4, 14, 8);
    footerLayout->setSpacing(10);
    m_keepAllBtn = new QPushButton(tr("全部保留"), this);
    m_keepAllBtn->setObjectName("cancelButton");
    m_keepAllBtn->setFixedHeight(34);
    m_keepAllBtn->setCursor(Qt::PointingHandCursor);
    m_aiBtn = new QPushButton(tr("让 AI 找替代"), this);
    m_aiBtn->setObjectName("saveButton");
    m_aiBtn->setFixedHeight(34);
    m_aiBtn->setCursor(Qt::PointingHandCursor);

    footerLayout->addWidget(m_keepAllBtn);
    footerLayout->addWidget(m_aiBtn);
    contentLayout->addLayout(footerLayout);

    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(m_keepAllBtn, &QPushButton::clicked, this, &ResourceSyncListWindow::keepAll);
    connect(m_aiBtn, &QPushButton::clicked, this, &ResourceSyncListWindow::requestAiReplacement);

    applyThemeStyles();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ResourceSyncListWindow::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &ResourceSyncListWindow::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &ResourceSyncListWindow::applyThemeStyles);
}

ResourceSyncListWindow::~ResourceSyncListWindow()
{
}

void ResourceSyncListWindow::setItems(const QList<ResourceSyncItem> &items,
                                      const QString &newVersion)
{
    m_items = items;
    m_newVersion = newVersion;
    m_hintLabel->setText(tr("实例已更新到 %1，以下资源没有适配新版本的版本，可以删除、保留，或让 AI 帮忙找替代。")
                             .arg(newVersion));
    updateCountLabel();
    rebuildList();
}

void ResourceSyncListWindow::rebuildList()
{
    // 清空旧行
    while (m_listLayout->count() > 1)
    {
        QLayoutItem *item = m_listLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (m_items.isEmpty())
    {
        m_emptyLabel->show();
        return;
    }
    m_emptyLabel->hide();

    for (const ResourceSyncItem &item : std::as_const(m_items))
    {
        auto *row = new QWidget(m_listContainer);
        row->setObjectName("syncRowCard");
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(10);

        // 类型徽标
        const TypeBadgeStyle badge = typeBadge(item.type);
        auto *typeLabel = new QLabel(badge.label, row);
        typeLabel->setObjectName("syncRowType");
        typeLabel->setStyleSheet(QStringLiteral(
            "QLabel#syncRowType {"
            " background: rgba(%1, %2, %3, 0.16);"
            " color: rgb(%1, %2, %3);"
            " border-radius: 6px;"
            " padding: 3px 8px;"
            " font-size: 11px;"
            " font-weight: 600; }")
            .arg(badge.color.red()).arg(badge.color.green()).arg(badge.color.blue()));

        // 名称 + 待处理原因
        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);
        auto *nameLabel = new QLabel(item.displayName, row);
        nameLabel->setObjectName("syncRowName");
        nameLabel->setToolTip(item.filePath);
        auto *noteLabel = new QLabel(item.note, row);
        noteLabel->setObjectName("syncRowNote");
        noteLabel->setWordWrap(true);
        infoLayout->addWidget(nameLabel);
        infoLayout->addWidget(noteLabel);

        auto *deleteBtn = new QPushButton(tr("删除"), row);
        deleteBtn->setObjectName("cancelButton");
        deleteBtn->setFixedWidth(56);
        deleteBtn->setFixedHeight(28);
        deleteBtn->setCursor(Qt::PointingHandCursor);
        connect(deleteBtn, &QPushButton::clicked, this, [this, item]()
        {
            if (AppMessageBox::question(this, tr("删除资源"),
                    tr("确定要删除「%1」吗？此操作不可撤销。").arg(item.displayName),
                    AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No) != AppMessageBox::Yes)
                return;
            QFile::remove(item.filePath);
            removeItem(item.filePath);
        });

        auto *keepBtn = new QPushButton(tr("保留"), row);
        keepBtn->setObjectName("actionButton");
        keepBtn->setFixedWidth(56);
        keepBtn->setFixedHeight(28);
        keepBtn->setCursor(Qt::PointingHandCursor);
        connect(keepBtn, &QPushButton::clicked, this, [this, item]()
        {
            removeItem(item.filePath);
        });

        rowLayout->addWidget(typeLabel);
        rowLayout->addLayout(infoLayout, 1);
        rowLayout->addWidget(deleteBtn);
        rowLayout->addWidget(keepBtn);

        m_listLayout->insertWidget(m_listLayout->count() - 1, row);
    }
}

void ResourceSyncListWindow::removeItem(const QString &filePath)
{
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (QFileInfo(m_items.at(i).filePath).absoluteFilePath()
            == QFileInfo(filePath).absoluteFilePath())
        {
            m_items.removeAt(i);
            break;
        }
    }
    updateCountLabel();
    rebuildList();

    // 清单清空后自动关闭（删除 / 保留都已处理完，没有需要展示的内容了）
    if (m_items.isEmpty())
        close();
}

void ResourceSyncListWindow::keepAll()
{
    // 保留 = 什么都不动，直接关掉清单
    m_items.clear();
    close();
}

void ResourceSyncListWindow::requestAiReplacement()
{
    if (m_items.isEmpty())
        return;
    emit findReplacementRequested(m_items);
}

void ResourceSyncListWindow::updateCountLabel()
{
    m_countLabel->setText(m_items.isEmpty() ? QString()
                                            : tr("%1 个资源").arg(m_items.size()));
    m_aiBtn->setEnabled(!m_items.isEmpty());
    m_keepAllBtn->setEnabled(!m_items.isEmpty());
}

void ResourceSyncListWindow::applyThemeStyles()
{
    // 行卡片等样式挂在容器上统一解析，行重建时无需逐个刷样式
    m_listContainer->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "#syncListContainer { background: transparent; }"
        "#syncRowCard { background: @BG_CARD@; border: 1px solid @BORDER@;"
        "  border-radius: 10px; }"
        "#syncRowCard QLabel { background: transparent; }"
        "#syncRowName { color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 600; }"
        "#syncRowNote { color: @TEXT_SECONDARY@; font-size: 11px; }"
        "#syncListEmpty { color: @TEXT_SECONDARY@; padding: 24px; }")));

    m_titleBar->setStyleSheet(QStringLiteral(
        "#syncListTitleBar { background: transparent; }"));

    const QString textColor = ThemeManager::instance()->currentTextColor();
    m_titleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 14px; font-weight: 600; background: transparent;")
            .arg(textColor));
    m_countLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; background: transparent;")
            .arg(ThemeManager::instance()->currentThemeColor()));
    m_hintLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "#syncListHint { color: @TEXT_SECONDARY@; font-size: 12px; background: transparent; }")));
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton#syncListCloseBtn { border: none; border-radius: 13px;"
        "  background: transparent; color: %1; font-size: 16px; }"
        "QPushButton#syncListCloseBtn:hover { background: rgba(128, 128, 128, 0.2); }")
        .arg(textColor));

    update();
}

void ResourceSyncListWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 阴影边距内画圆角卡片本体，外圈由内到外叠一圈半透明描边做柔和投影
    const QRectF card(kShadowMargin, kShadowMargin, width() - 2 * kShadowMargin,
                      height() - 2 * kShadowMargin);
    for (int i = 1; i <= kShadowMargin; ++i)
    {
        const int alpha = qMax(2, 24 - i * 2 - (i > kShadowMargin / 2 ? 2 : 0));
        painter.setPen(QPen(QColor(0, 0, 0, alpha), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(card.adjusted(-i, -i + 2, i, i + 2),
                                kCornerRadius + i, kCornerRadius + i);
    }

    QPainterPath path;
    path.addRoundedRect(card, kCornerRadius, kCornerRadius);
    painter.fillPath(path, QColor(StyleKit::resolve(QStringLiteral("@BG_BASE@"))));
    painter.setPen(QPen(QColor(ThemeManager::instance()->currentBorderColor()), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card, kCornerRadius, kCornerRadius);
}

void ResourceSyncListWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    // 第一次弹出时居中到主窗口（悬浮窗挂在主窗口下）
    if (!m_centeredOnce)
    {
        m_centeredOnce = true;
        const QWidget *host = window()->parentWidget()
            ? window()->parentWidget()->window() : nullptr;
        if (host && host->isVisible())
        {
            const QRect hostRect = host->geometry();
            move(hostRect.center().x() - width() / 2,
                 hostRect.center().y() - height() / 2);
        }
    }
}

bool ResourceSyncListWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 标题栏空白处按住拖动整个窗口
    if (watched == m_titleBar)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_dragging = true;
                m_dragOrigin = mouseEvent->globalPosition().toPoint()
                    - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton))
            {
                move(mouseEvent->globalPosition().toPoint() - m_dragOrigin);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
        {
            m_dragging = false;
            return true;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
