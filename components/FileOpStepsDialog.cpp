/**
 * @file   FileOpStepsDialog.cpp
 * @brief  「文件操作步骤」弹窗实现
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "FileOpStepsDialog.h"

#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

FileOpStepsDialog::FileOpStepsDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_sessionCombo(nullptr)
    , m_filterCombo(nullptr)
    , m_list(nullptr)
    , m_countLabel(nullptr)
    , m_emptyHint(nullptr)
    , m_closeBtn(nullptr)
{
    setWindowTitle(tr("文件操作步骤"));
    initUI();
    reloadFromLogs();
}

void FileOpStepsDialog::initUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("fileOpDialogCard"));
    card->setMinimumSize(640, 480);
    card->setMaximumSize(820, 680);
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleBar = new QWidget(card);
    titleBar->setObjectName(QStringLiteral("fileOpDialogTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);

    auto *titleLabel = new QLabel(tr("文件操作步骤"), card);
    titleLabel->setObjectName(QStringLiteral("fileOpDialogTitle"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    auto *hint = new QLabel(
        tr("根据运行日志整理出的下载、复制、解压等步骤，仅供查看，不会重新执行任何操作。"),
        card);
    hint->setObjectName(QStringLiteral("fileOpHintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    auto *sessionLabel = new QLabel(tr("启动会话"), card);
    sessionLabel->setObjectName(QStringLiteral("fileOpFilterLabel"));
    filterRow->addWidget(sessionLabel);

    m_sessionCombo = new QComboBox(card);
    m_sessionCombo->setObjectName(QStringLiteral("fileOpSessionCombo"));
    m_sessionCombo->setMinimumWidth(160);
    filterRow->addWidget(m_sessionCombo);

    auto *filterLabel = new QLabel(tr("类型"), card);
    filterLabel->setObjectName(QStringLiteral("fileOpFilterLabel"));
    filterRow->addWidget(filterLabel);

    m_filterCombo = new QComboBox(card);
    m_filterCombo->setObjectName(QStringLiteral("fileOpFilterCombo"));
    m_filterCombo->addItems({
        tr("全部"),
        tr("下载"),
        tr("复制"),
        tr("解压"),
        tr("安装"),
        tr("写入"),
        tr("删除"),
        tr("失败"),
    });
    filterRow->addWidget(m_filterCombo);
    filterRow->addStretch();

    m_countLabel = new QLabel(card);
    m_countLabel->setObjectName(QStringLiteral("fileOpCountLabel"));
    filterRow->addWidget(m_countLabel);
    layout->addLayout(filterRow);

    m_list = new QListWidget(card);
    m_list->setObjectName(QStringLiteral("fileOpStepList"));
    m_list->setWordWrap(true);
    m_list->setSpacing(2);
    m_list->setUniformItemSizes(false);
    layout->addWidget(m_list, 1);

    m_emptyHint = new QLabel(
        tr("当前日志里没有找到文件相关的操作记录。先安装实例或下载资源，再回来查看。"),
        card);
    m_emptyHint->setObjectName(QStringLiteral("fileOpEmptyHint"));
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setVisible(false);
    layout->addWidget(m_emptyHint, 1);

    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    m_closeBtn = new QPushButton(tr("关闭"), card);
    m_closeBtn->setObjectName(QStringLiteral("fileOpCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(m_closeBtn);
    layout->addLayout(bottom);

    connect(m_sessionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FileOpStepsDialog::onSessionChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FileOpStepsDialog::onFilterChanged);
}

void FileOpStepsDialog::reloadFromLogs()
{
    m_allSteps = FileOpStepLog::parseLogFiles(FileOpStepLog::defaultLogFiles());

    m_sessionCombo->blockSignals(true);
    m_sessionCombo->clear();
    m_sessionCombo->addItem(tr("全部会话"), -1);

    // 按会话聚合数量，空会话不显示
    QHash<int, int> counts;
    for (const FileOpStep &s : m_allSteps)
        counts[s.sessionIndex] += 1;

    QList<int> keys = counts.keys();
    std::sort(keys.begin(), keys.end());
    for (int key : keys) {
        QString label;
        if (key < 0)
            label = tr("未标记会话");
        else
            label = tr("第 %1 次启动（%2 步）").arg(key + 1).arg(counts.value(key));
        m_sessionCombo->addItem(label, key);
    }
    m_sessionCombo->blockSignals(false);

    // 默认选「全部会话」；若只有一次启动则直接选中那次
    if (keys.size() == 1)
        m_sessionCombo->setCurrentIndex(1);

    refreshList();
}

QList<FileOpStep> FileOpStepsDialog::filteredSteps() const
{
    const bool sessionFilterOn = m_sessionCombo && m_sessionCombo->currentIndex() > 0;
    const int session = sessionFilterOn ? m_sessionCombo->currentData().toInt() : 0;
    const int filter = m_filterCombo ? m_filterCombo->currentIndex() : 0;

    QList<FileOpStep> out;
    for (const FileOpStep &s : m_allSteps) {
        if (sessionFilterOn && s.sessionIndex != session)
            continue;

        switch (filter) {
        case 1: // 下载
            if (s.kind != QStringLiteral("下载")) continue;
            break;
        case 2: // 复制
            if (s.kind != QStringLiteral("复制")) continue;
            break;
        case 3: // 解压
            if (s.kind != QStringLiteral("解压")) continue;
            break;
        case 4: // 安装
            if (s.kind != QStringLiteral("安装")) continue;
            break;
        case 5: // 写入
            if (s.kind != QStringLiteral("写入")) continue;
            break;
        case 6: // 删除
            if (s.kind != QStringLiteral("删除")) continue;
            break;
        case 7: // 失败
            if (s.success) continue;
            break;
        default:
            break;
        }
        out.append(s);
    }
    return out;
}

void FileOpStepsDialog::refreshList()
{
    const QList<FileOpStep> steps = filteredSteps();
    m_list->clear();

    int n = 0;
    for (const FileOpStep &s : steps) {
        ++n;
        const QString timeStr = s.timestamp.isValid()
            ? s.timestamp.toString(QStringLiteral("MM-dd HH:mm:ss"))
            : QStringLiteral("--:--:--");

        QString sessionTag;
        if (s.sessionIndex >= 0)
            sessionTag = tr(" · 第%1次").arg(s.sessionIndex + 1);
        else
            sessionTag = tr(" · 未标记");

        const QString status = s.success ? QString() : tr("  ✕ 失败");
        QString text = QStringLiteral("%1  [%2] %3%4\n%5")
                           .arg(timeStr,
                                s.kind.isEmpty() ? tr("文件") : s.kind,
                                s.title,
                                status,
                                s.detail);
        if (!s.module.isEmpty())
            text += QStringLiteral("\n模块：%1").arg(s.module);

        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, s.success);
        item->setData(Qt::UserRole + 1, sessionTag);
        // 失败步骤用醒目颜色；成功按主题次要文字色（QSS 处理）
        if (!s.success)
            item->setForeground(QColor(QStringLiteral("#F44336")));
        m_list->addItem(item);
    }

    const bool empty = steps.isEmpty();
    m_list->setVisible(!empty);
    m_emptyHint->setVisible(empty);
    m_countLabel->setText(tr("共 %1 步").arg(steps.size()));
}

void FileOpStepsDialog::onSessionChanged(int)
{
    refreshList();
}

void FileOpStepsDialog::onFilterChanged(int)
{
    refreshList();
}
