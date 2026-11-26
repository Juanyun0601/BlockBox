/**
 * @file   ResourceFilterDialog.cpp
 * @brief  在线资源列表页的「单列筛选」弹窗实现
 * @author BlockBox Team
 * @date   2026-11-20
 */

#include "ResourceFilterDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ResourceFilterDialog::ResourceFilterDialog(const QString &title, const QString &hint,
                                           const QString &columnTitle, QWidget *parent)
    : AppDialogBase(parent)
    , m_title(title)
    , m_hint(hint)
    , m_columnTitle(columnTitle)
{
    initUI();
}

void ResourceFilterDialog::initUI()
{
    // 与在线资源「分类筛选」弹窗同款居中卡片与按钮
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(400, 300);
    card->setMaximumSize(520, 620);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(12);

    auto *titleLabel = new QLabel(m_title, card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    auto *hintLabel = new QLabel(m_hint, card);
    hintLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    hintLabel->setWordWrap(true);
    cardLayout->addWidget(hintLabel);

    m_columnTitleLabel = new QLabel(m_columnTitle, card);
    m_columnTitleLabel->setObjectName(QStringLiteral("categoryFilterColumnTitle"));
    cardLayout->addWidget(m_columnTitleLabel);

    m_list = new QListWidget(card);
    m_list->setObjectName(QStringLiteral("categoryFilterList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setProperty("selRow", 0);
    cardLayout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        const int row = m_list->row(item);
        // 再次点击已选中的项 = 取消筛选，回到「全部」
        if (row > 0 && row == m_list->property("selRow").toInt())
            m_list->setCurrentRow(0);
        m_list->setProperty("selRow", m_list->currentRow());
    });

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_resetBtn = new QPushButton(tr("重置"), card);
    m_resetBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_resetBtn);
    btnRow->addStretch();

    auto *cancelBtn = new QPushButton(tr("取消"), card);
    cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(cancelBtn);

    auto *okBtn = new QPushButton(tr("确定"), card);
    okBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);
    btnRow->addWidget(okBtn);

    cardLayout->addLayout(btnRow);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        if (m_list->count() > 0)
            m_list->setCurrentRow(0);
        m_list->setProperty("selRow", 0);
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        const QString id = selectedId();
        if (id != m_selectedId)
            emit filterApplied(id);
        accept();
    });
}

void ResourceFilterDialog::setOptions(const QVector<QPair<QString, QString>> &options)
{
    m_options = options;
    populateList();
}

void ResourceFilterDialog::populateList()
{
    m_list->clear();
    for (const auto &option : m_options)
    {
        auto *item = new QListWidgetItem(option.second, m_list);
        item->setData(Qt::UserRole, option.first);
    }

    // 列表高度按选项数收紧：选项不多时整列一眼看全，多了才出现滚动条
    const int visibleRows = qBound(1, static_cast<int>(m_options.size()), 7);
    m_list->setFixedHeight(8 + visibleRows * 46);

    // 恢复打开前的选中态；找不到就退回第一项（「全部」）
    int targetRow = 0;
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toString() == m_selectedId)
        {
            targetRow = i;
            break;
        }
    }
    if (m_list->count() > 0)
    {
        m_list->setCurrentRow(targetRow);
        m_list->setProperty("selRow", targetRow);
    }
}

void ResourceFilterDialog::setSelectedId(const QString &id)
{
    m_selectedId = id;
    populateList();
}

QString ResourceFilterDialog::selectedId() const
{
    if (!m_list || !m_list->currentItem())
        return QString();
    return m_list->currentItem()->data(Qt::UserRole).toString();
}
