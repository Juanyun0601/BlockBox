#include "NotificationHistoryDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

NotificationHistoryDialog::NotificationHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("提示信息历史"));
    setFixedSize(600, 450);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    initUI();
    loadHistory();
}

NotificationHistoryDialog::~NotificationHistoryDialog()
{
}

void NotificationHistoryDialog::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    QHBoxLayout *filterLayout = new QHBoxLayout;
    QLabel *filterLabel = new QLabel(tr("筛选:"), this);
    filterLabel->setObjectName("filterLabel");
    m_filterCombo = new QComboBox(this);
    m_filterCombo->setObjectName("filterCombo");
    m_filterCombo->addItem(tr("全部"));
    m_filterCombo->addItem(tr("成功"));
    m_filterCombo->addItem(tr("信息"));
    m_filterCombo->addItem(tr("错误"));
    filterLayout->addWidget(filterLabel);
    filterLayout->addWidget(m_filterCombo);
    filterLayout->addStretch();

    QLabel *countLabel = new QLabel(tr("最多保留最近500条记录"), this);
    countLabel->setStyleSheet("color: #888; font-size: 10px;");
    filterLayout->addWidget(countLabel);

    mainLayout->addLayout(filterLayout);

    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("historyList");
    m_listWidget->setAlternatingRowColors(true);
    m_listWidget->setWordWrap(true);
    m_listWidget->setSpacing(2);
    mainLayout->addWidget(m_listWidget);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_clearButton = new QPushButton(tr("清空历史"), this);
    m_clearButton->setObjectName("bottomActionBtn");
    m_closeButton = new QPushButton(tr("关闭"), this);
    m_closeButton->setObjectName("bottomActionBtn");
    btnLayout->addWidget(m_clearButton);
    btnLayout->addWidget(m_closeButton);
    mainLayout->addLayout(btnLayout);

    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NotificationHistoryDialog::onFilterChanged);
    connect(m_clearButton, &QPushButton::clicked,
            this, &NotificationHistoryDialog::onClearClicked);
    connect(m_closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void NotificationHistoryDialog::loadHistory()
{
    m_allRecords = NotificationManager::history();
    onFilterChanged(m_filterCombo->currentIndex());
}

void NotificationHistoryDialog::onFilterChanged(int index)
{
    m_listWidget->clear();

    for (const NotificationRecord &rec : m_allRecords) {
        if (index == 1 && rec.type != NotificationCard::Success) continue;
        if (index == 2 && rec.type != NotificationCard::Info) continue;
        if (index == 3 && rec.type != NotificationCard::Error) continue;

        QString typeStr;
        QString color;
        switch (rec.type) {
        case NotificationCard::Success:
            typeStr = tr("[成功]");
            color = "#4CAF50";
            break;
        case NotificationCard::Info:
            typeStr = tr("[信息]");
            color = ThemeManager::instance()->currentInfoAccentColor();
            break;
        case NotificationCard::Error:
            typeStr = tr("[错误]");
            color = "#F44336";
            break;
        }

        QString timeStr = rec.timestamp.toString("HH:mm:ss");
        QString display = QString("%1 %2 %3")
            .arg(timeStr)
            .arg(typeStr)
            .arg(rec.text);

        QListWidgetItem *item = new QListWidgetItem(display, m_listWidget);
        item->setForeground(QColor(color));
    }
}

void NotificationHistoryDialog::onClearClicked()
{
    NotificationManager::clearHistory();
    m_allRecords.clear();
    m_listWidget->clear();
}
