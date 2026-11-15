#include "ContentCategoryFilterDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ContentCategoryFilterDialog::ContentCategoryFilterDialog(bool modrinthSupported, QWidget *parent)
    : AppDialogBase(parent)
    , m_modrinthSupported(modrinthSupported)
{
    setObjectName(QStringLiteral("contentCategoryFilterDialog"));
    initUI();
}

void ContentCategoryFilterDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("appDialogCard"));
    card->setMinimumSize(660, 500);
    card->setMaximumSize(820, 660);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 20);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(tr("分类筛选"), card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(
        tr("两个来源各自独立筛选，可只选一边；点击已选中的分类可取消。"),
        card);
    hintLabel->setObjectName(QStringLiteral("appDialogPromptLabel"));
    hintLabel->setWordWrap(true);
    cardLayout->addWidget(hintLabel);

    // 左右两栏：Modrinth 分类 / CurseForge 分类
    QHBoxLayout *columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(14);

    if (m_modrinthSupported) {
        m_modrinthColumn = buildColumn(tr("Modrinth 分类"), &m_modrinthList);
        columnsLayout->addWidget(m_modrinthColumn, 1);
    }
    m_curseforgeColumn = buildColumn(tr("CurseForge 分类"), &m_curseforgeList);
    columnsLayout->addWidget(m_curseforgeColumn, 1);

    cardLayout->addLayout(columnsLayout, 1);

    // 底部操作行：重置 | 取消 / 确定
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_resetBtn = new QPushButton(tr("重置"), card);
    m_resetBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_resetBtn);

    btnRow->addStretch();

    m_cancelBtn = new QPushButton(tr("取消"), card);
    m_cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_cancelBtn);

    m_okBtn = new QPushButton(tr("确定"), card);
    m_okBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setDefault(true);
    btnRow->addWidget(m_okBtn);

    cardLayout->addLayout(btnRow);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        if (m_modrinthList) {
            m_modrinthList->setCurrentRow(0);
            m_modrinthList->setProperty("selRow", 0);
        }
        m_curseforgeList->setCurrentRow(0);
        m_curseforgeList->setProperty("selRow", 0);
    });
    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        const QString mrId = m_modrinthSupported ? selectedIdOf(m_modrinthList) : QString();
        const QString cfId = selectedIdOf(m_curseforgeList);
        if (mrId != m_selectedModrinthId || cfId != m_selectedCurseforgeId)
            emit filterApplied(mrId, cfId);
        accept();
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QWidget *ContentCategoryFilterDialog::buildColumn(const QString &title, QListWidget **outList)
{
    QWidget *column = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel *titleLabel = new QLabel(title, column);
    titleLabel->setObjectName(QStringLiteral("categoryFilterColumnTitle"));
    layout->addWidget(titleLabel);

    QListWidget *list = new QListWidget(column);
    list->setObjectName(QStringLiteral("categoryFilterList"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setProperty("selRow", 0);
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem *item) {
        handleItemClicked(list, item);
    });
    layout->addWidget(list, 1);

    *outList = list;
    return column;
}

void ContentCategoryFilterDialog::setSelection(const QString &modrinthId, const QString &curseforgeId)
{
    m_selectedModrinthId = modrinthId;
    m_selectedCurseforgeId = curseforgeId;

    auto applyRow = [](QListWidget *list, const QString &id) {
        if (!list || list->count() == 0)
            return;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString() == id) {
                list->setCurrentRow(i);
                list->setProperty("selRow", i);
                return;
            }
        }
        list->setCurrentRow(0);
        list->setProperty("selRow", 0);
    };
    applyRow(m_modrinthList, modrinthId);
    applyRow(m_curseforgeList, curseforgeId);
}

void ContentCategoryFilterDialog::setModrinthLoading()
{
    if (m_modrinthList)
        setPlaceholder(m_modrinthList, tr("正在加载分类..."));
}

void ContentCategoryFilterDialog::setCurseforgeLoading()
{
    setPlaceholder(m_curseforgeList, tr("正在加载分类..."));
}

void ContentCategoryFilterDialog::setModrinthCategories(const QList<ModCategory> &categories)
{
    if (m_modrinthList)
        populateList(m_modrinthList, categories);
}

void ContentCategoryFilterDialog::setCurseforgeCategories(const QList<ModCategory> &categories)
{
    populateList(m_curseforgeList, categories);
}

void ContentCategoryFilterDialog::setModrinthFailed()
{
    if (m_modrinthList)
        setPlaceholder(m_modrinthList, tr("分类加载失败，请关闭后重试"));
}

void ContentCategoryFilterDialog::setCurseforgeFailed()
{
    setPlaceholder(m_curseforgeList, tr("分类加载失败，请关闭后重试"));
}

void ContentCategoryFilterDialog::populateList(QListWidget *list, const QList<ModCategory> &categories)
{
    if (!list)
        return;
    list->clear();

    QListWidgetItem *allItem = new QListWidgetItem(tr("全部"), list);
    allItem->setData(Qt::UserRole, QString());

    for (const ModCategory &cat : categories) {
        if (cat.id.isEmpty())
            continue;
        QListWidgetItem *item = new QListWidgetItem(cat.name, list);
        item->setData(Qt::UserRole, cat.id);
    }

    // 重建后恢复打开前的选中态
    const QString selId = (list == m_modrinthList) ? m_selectedModrinthId : m_selectedCurseforgeId;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).toString() == selId) {
            list->setCurrentRow(i);
            list->setProperty("selRow", i);
            return;
        }
    }
    list->setCurrentRow(0);
    list->setProperty("selRow", 0);
}

void ContentCategoryFilterDialog::setPlaceholder(QListWidget *list, const QString &text)
{
    if (!list)
        return;
    list->clear();
    QListWidgetItem *item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    list->setProperty("selRow", 0);
}

void ContentCategoryFilterDialog::handleItemClicked(QListWidget *list, QListWidgetItem *item)
{
    if (!item)
        return;
    const int row = list->row(item);
    // 再次点击已选中的分类 = 取消筛选，回到「全部」
    if (row > 0 && row == list->property("selRow").toInt())
        list->setCurrentRow(0);
    list->setProperty("selRow", list->currentRow());
}

QString ContentCategoryFilterDialog::selectedIdOf(QListWidget *list)
{
    if (!list || !list->currentItem())
        return QString();
    return list->currentItem()->data(Qt::UserRole).toString();
}
