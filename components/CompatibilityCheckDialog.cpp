/**
 * @file   CompatibilityCheckDialog.cpp
 * @brief  本地资源兼容性检测结果对话框实现
 * @author BlockBox Team
 * @date   2026-08-30
 */
#include "CompatibilityCheckDialog.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

#include "utils/ThemeManager.h"

CompatibilityCheckDialog::CompatibilityCheckDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_metaLabel(nullptr)
    , m_errorChip(nullptr)
    , m_warningChip(nullptr)
    , m_infoChip(nullptr)
    , m_filterCombo(nullptr)
    , m_scrollArea(nullptr)
    , m_issueContainer(nullptr)
    , m_issueLayout(nullptr)
    , m_runningLabel(nullptr)
    , m_recheckBtn(nullptr)
    , m_watcher(nullptr)
{
    setObjectName(QStringLiteral("compatibilityCheckDialog"));
    setWindowTitle(tr("兼容性检测"));

    initUI();
    initStyle();

    m_watcher = new QFutureWatcher<CompatReport>(this);
    connect(m_watcher, &QFutureWatcher<CompatReport>::finished,
            this, &CompatibilityCheckDialog::onCheckFinished);
}

void CompatibilityCheckDialog::runCheck(QWidget *parent,
                                        const QString &instancePath,
                                        const QString &gameVersion,
                                        const QString &loaderType)
{
    if (instancePath.isEmpty())
        return;
    CompatibilityCheckDialog *dialog = new CompatibilityCheckDialog(parent);
    dialog->m_instancePath = instancePath;
    dialog->m_gameVersion = gameVersion;
    dialog->m_loaderType = loaderType;
    dialog->startCheck();
    dialog->exec();
    dialog->deleteLater();
}

void CompatibilityCheckDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("compatCheckCard"));
    card->setMinimumSize(620, 520);
    card->setMaximumSize(720, 640);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(12);

    // 标题 + 实例信息
    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setObjectName(QStringLiteral("compatTitleLabel"));
    cardLayout->addWidget(titleLabel);

    m_metaLabel = new QLabel(card);
    m_metaLabel->setObjectName(QStringLiteral("compatMetaLabel"));
    m_metaLabel->setWordWrap(true);
    cardLayout->addWidget(m_metaLabel);

    // 汇总 chips + 筛选
    QHBoxLayout *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    m_errorChip = new QLabel(card);
    m_warningChip = new QLabel(card);
    m_infoChip = new QLabel(card);
    m_errorChip->setObjectName(QStringLiteral("compatChip"));
    m_warningChip->setObjectName(QStringLiteral("compatChip"));
    m_infoChip->setObjectName(QStringLiteral("compatChip"));
    summaryRow->addWidget(m_errorChip);
    summaryRow->addWidget(m_warningChip);
    summaryRow->addWidget(m_infoChip);
    summaryRow->addStretch();

    m_filterCombo = new QComboBox(card);
    m_filterCombo->setObjectName(QStringLiteral("compatFilterCombo"));
    m_filterCombo->addItem(tr("全部问题"), -1);
    m_filterCombo->addItem(tr("仅错误"), CompatIssue::Error);
    m_filterCombo->addItem(tr("仅警告"), CompatIssue::Warning);
    m_filterCombo->addItem(tr("仅提示"), CompatIssue::Info);
    summaryRow->addWidget(m_filterCombo);
    cardLayout->addLayout(summaryRow);

    // 问题列表
    m_scrollArea = new QScrollArea(card);
    m_scrollArea->setObjectName(QStringLiteral("compatScroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_issueContainer = new QWidget();
    m_issueContainer->setObjectName(QStringLiteral("compatIssueContainer"));
    m_issueLayout = new QVBoxLayout(m_issueContainer);
    m_issueLayout->setContentsMargins(0, 0, 0, 0);
    m_issueLayout->setSpacing(8);
    m_issueLayout->addStretch();
    m_scrollArea->setWidget(m_issueContainer);
    cardLayout->addWidget(m_scrollArea, 1);

    // 运行中提示
    m_runningLabel = new QLabel(tr("正在扫描模组、资源包与光影包，请稍候…"), card);
    m_runningLabel->setObjectName(QStringLiteral("compatRunningLabel"));
    m_runningLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_runningLabel, 1);

    // 底部按钮
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_recheckBtn = new QPushButton(tr("重新检测"), card);
    m_recheckBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_recheckBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *closeBtn = new QPushButton(tr("关闭"), card);
    closeBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_recheckBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    cardLayout->addLayout(btnRow);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(card, 0, 0, Qt::AlignCenter);

    connect(m_recheckBtn, &QPushButton::clicked, this, &CompatibilityCheckDialog::startCheck);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { populateReport(m_lastReport); });
}

void CompatibilityCheckDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg      = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#333333" : "#e8e8e8";
    const QString subTextColor = isLight ? "#777777" : "#a8a8ad";
    const QString rowBg       = isLight ? "rgba(0, 0, 0, 12)" : "rgba(255, 255, 255, 14)";
    const QString rowBorder   = isLight ? "rgba(0, 0, 0, 25)" : "rgba(255, 255, 255, 25)";
    const QString comboBg     = isLight ? "#f5f5f5" : "#3b3b40";
    const QString comboBorder = isLight ? "#d4d4d4" : "#55555a";

    setStyleSheet(QString(
        "QWidget#compatCheckCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#compatTitleLabel {"
        "    color: %3; background: transparent;"
        "    font-size: 16px; font-weight: bold;"
        "}"
        "QLabel#compatMetaLabel {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QLabel#compatChip {"
        "    color: #ffffff; background-color: %5;"
        "    border-radius: 9px; padding: 2px 10px; font-size: 12px;"
        "}"
        "QComboBox#compatFilterCombo {"
        "    background-color: %6; border: 1px solid %7; border-radius: 8px;"
        "    color: %3; padding: 3px 10px; font-size: 12px;"
        "}"
        "QComboBox#compatFilterCombo QAbstractItemView {"
        "    background-color: %6; color: %3; selection-background-color: %8;"
        "}"
        "QScrollArea#compatScroll {"
        "    background: transparent; border: none;"
        "}"
        "QWidget#compatIssueContainer {"
        "    background: transparent;"
        "}"
        "QLabel#compatRunningLabel {"
        "    color: %4; background: transparent; font-size: 13px;"
        "}"
        "QFrame#issueCard {"
        "    background-color: %9;"
        "    border: 1px solid %10;"
        "    border-radius: 10px;"
        "}"
        "QLabel#issueTitle {"
        "    color: %3; background: transparent; font-size: 13px; font-weight: bold;"
        "}"
        "QLabel#issueMeta {"
        "    color: %4; background: transparent; font-size: 11px;"
        "}"
        "QLabel#issueDetail {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QLabel#issueSuggestion {"
        "    background: transparent; font-size: 12px;"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %6; border: 1px solid %7; border-radius: 10px;"
        "    color: %3; padding: 7px 18px; font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover {"
        "    background-color: %11;"
        "}"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %8; border: none; border-radius: 10px;"
        "    color: #ffffff; padding: 7px 22px; font-size: 13px; font-weight: bold;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover {"
        "    background-color: %11;"
        "}"
        "QScrollArea#compatScroll QScrollBar:vertical {"
        "    background: transparent; width: 8px; margin: 0;"
        "}"
        "QScrollArea#compatScroll QScrollBar::handle:vertical {"
        "    background: %10; border-radius: 4px; min-height: 30px;"
        "}"
        "QScrollArea#compatScroll QScrollBar::add-line:vertical,"
        "QScrollArea#compatScroll QScrollBar::sub-line:vertical { height: 0; }")
                      .arg(cardBg, cardBorder, titleColor, textColor,
                           severityColor(CompatIssue::Error), // 5 chips 统一底色（文字内联覆盖）
                           comboBg, comboBorder,
                           tm->currentThemeColor(),
                           rowBg, rowBorder,
                           tm->getThemeColorHover()));
}

QString CompatibilityCheckDialog::severityColor(int severity) const
{
    const bool isLight = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme);
    switch (severity)
    {
    case CompatIssue::Error:
        return isLight ? "#d64545" : "#ef6b6b";
    case CompatIssue::Warning:
        return isLight ? "#d18f2e" : "#e0a34c";
    default:
        return isLight ? "#3d87c9" : "#5b9dd9";
    }
}

QString CompatibilityCheckDialog::severityName(int severity) const
{
    switch (severity)
    {
    case CompatIssue::Error: return tr("错误");
    case CompatIssue::Warning: return tr("警告");
    default: return tr("提示");
    }
}

void CompatibilityCheckDialog::startCheck()
{
    if (m_instancePath.isEmpty() || m_watcher->isRunning())
        return;

    // 更新实例信息行
    QStringList metaParts;
    if (!m_gameVersion.isEmpty())
        metaParts << (tr("Minecraft %1").arg(m_gameVersion));
    if (!m_loaderType.isEmpty())
        metaParts << m_loaderType;
    else
        metaParts << tr("原版");
    m_metaLabel->setText(metaParts.join(" · "));

    showRunning(true);
    const QString path = m_instancePath;
    const QString ver = m_gameVersion;
    const QString loader = m_loaderType;
    m_watcher->setFuture(QtConcurrent::run([path, ver, loader]() {
        return LocalCompatibilityChecker::checkInstance(path, ver, loader);
    }));
}

void CompatibilityCheckDialog::onCheckFinished()
{
    showRunning(false);
    m_lastReport = m_watcher->result();
    populateReport(m_lastReport);
}

void CompatibilityCheckDialog::showRunning(bool running)
{
    m_runningLabel->setVisible(running);
    m_scrollArea->setVisible(!running);
    m_recheckBtn->setEnabled(!running);
    if (running)
    {
        m_errorChip->setText(tr("检测中…"));
        m_warningChip->hide();
        m_infoChip->hide();
    }
}

void CompatibilityCheckDialog::populateReport(const CompatReport &report)
{
    m_issues = report.issues;

    // 汇总 chips
    m_warningChip->show();
    m_infoChip->show();
    m_errorChip->setText(tr("%1 个错误").arg(report.errorCount));
    m_warningChip->setText(tr("%1 个警告").arg(report.warningCount));
    m_infoChip->setText(tr("%1 个提示").arg(report.infoCount));
    m_errorChip->setStyleSheet(QString("background-color: %1;")
                                   .arg(severityColor(CompatIssue::Error)));
    m_warningChip->setStyleSheet(QString("background-color: %1;")
                                     .arg(severityColor(CompatIssue::Warning)));
    m_infoChip->setStyleSheet(QString("background-color: %1;")
                                  .arg(severityColor(CompatIssue::Info)));

    // 筛选
    const int severityFilter = m_filterCombo->currentData().toInt();

    // 清空旧卡片
    while (m_issueLayout->count() > 1)
    {
        QLayoutItem *item = m_issueLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    // 按严重级别排序（错误在前）
    QList<CompatIssue> sorted = m_issues;
    std::sort(sorted.begin(), sorted.end(),
              [](const CompatIssue &a, const CompatIssue &b) { return a.severity < b.severity; });

    int shown = 0;
    for (const CompatIssue &issue : sorted)
    {
        if (severityFilter >= 0 && issue.severity != severityFilter)
            continue;
        m_issueLayout->insertWidget(m_issueLayout->count() - 1, createIssueCard(issue));
        ++shown;
    }

    // 检查范围说明（无问题或需要上下文时展示）
    QString scopeText = tr("已检查 %1 个模组、%2 个资源包、%3 个光影包")
                            .arg(report.modCount)
                            .arg(report.resourcePackCount)
                            .arg(report.shaderPackCount);
    if (shown == 0)
    {
        QLabel *emptyLabel = new QLabel(
            m_issues.isEmpty()
                ? QString("%1\n%2").arg(tr("未发现兼容性问题"), scopeText)
                : tr("当前筛选下没有问题"),
            m_issueContainer);
        emptyLabel->setObjectName(QStringLiteral("compatEmptyLabel"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_issueLayout->insertWidget(0, emptyLabel, 1, Qt::AlignCenter);
    }
    else
    {
        QLabel *scopeLabel = new QLabel(scopeText, m_issueContainer);
        scopeLabel->setObjectName(QStringLiteral("issueMeta"));
        scopeLabel->setAlignment(Qt::AlignCenter);
        m_issueLayout->insertWidget(m_issueLayout->count() - 1, scopeLabel);
    }
}

QWidget *CompatibilityCheckDialog::createIssueCard(const CompatIssue &issue)
{
    QFrame *card = new QFrame(m_issueContainer);
    card->setObjectName(QStringLiteral("issueCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(12, 10, 12, 10);
    rowLayout->setSpacing(10);

    // 严重级别圆点
    QLabel *dot = new QLabel(card);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QString("background-color: %1; border-radius: 5px;")
                           .arg(severityColor(issue.severity)));
    rowLayout->addWidget(dot, 0, Qt::AlignTop);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(3);

    QLabel *title = new QLabel(issue.title, card);
    title->setObjectName(QStringLiteral("issueTitle"));
    title->setWordWrap(true);
    textLayout->addWidget(title);

    // 类别 + 涉及文件
    QStringList metaParts;
    if (issue.category == "mod")
        metaParts << tr("模组");
    else if (issue.category == "resourcepack")
        metaParts << tr("资源包");
    else if (issue.category == "shaderpack")
        metaParts << tr("光影包");
    if (!issue.fileName.isEmpty())
        metaParts << issue.fileName;
    if (!metaParts.isEmpty())
    {
        QLabel *meta = new QLabel(metaParts.join(" · "), card);
        meta->setObjectName(QStringLiteral("issueMeta"));
        meta->setWordWrap(true);
        textLayout->addWidget(meta);
    }

    if (!issue.detail.isEmpty())
    {
        QLabel *detail = new QLabel(issue.detail, card);
        detail->setObjectName(QStringLiteral("issueDetail"));
        detail->setWordWrap(true);
        textLayout->addWidget(detail);
    }

    if (!issue.suggestion.isEmpty())
    {
        QLabel *suggestion = new QLabel(tr("建议：%1").arg(issue.suggestion), card);
        suggestion->setObjectName(QStringLiteral("issueSuggestion"));
        suggestion->setStyleSheet(QString("color: %1;").arg(severityColor(issue.severity)));
        suggestion->setWordWrap(true);
        textLayout->addWidget(suggestion);
    }

    rowLayout->addLayout(textLayout, 1);
    return card;
}
