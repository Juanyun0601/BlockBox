/**
 * @file   CompatibilityCheckDialog.cpp
 * @brief  实例体检对话框实现（体检报告 + 兼容矩阵）
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "CompatibilityCheckDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"
#include "utils/compat/CompatMatrixFetcher.h"

namespace
{
/** 矩阵行排序权重：越靠前越需要用户注意 */
int statusPriority(int status)
{
    switch (status)
    {
    case CompatMatrixRow::StatusArchived:       return 0;
    case CompatMatrixRow::StatusOutdated:       return 1;
    case CompatMatrixRow::StatusLoaderMismatch: return 2;
    case CompatMatrixRow::StatusUnknown:        return 3;
    default:                                     return 4;
    }
}
} // namespace

CompatibilityCheckDialog::CompatibilityCheckDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_metaLabel(nullptr)
    , m_reportTabBtn(nullptr)
    , m_matrixTabBtn(nullptr)
    , m_tabStack(nullptr)
    , m_errorChip(nullptr)
    , m_warningChip(nullptr)
    , m_infoChip(nullptr)
    , m_filterCombo(nullptr)
    , m_scrollArea(nullptr)
    , m_issueContainer(nullptr)
    , m_issueLayout(nullptr)
    , m_runningLabel(nullptr)
    , m_matrixSummary(nullptr)
    , m_targetCombo(nullptr)
    , m_onlyUnsupportedBox(nullptr)
    , m_matrixScroll(nullptr)
    , m_matrixContainer(nullptr)
    , m_matrixLayout(nullptr)
    , m_matrixStatus(nullptr)
    , m_recheckBtn(nullptr)
    , m_fetcher(nullptr)
{
    setObjectName(QStringLiteral("compatibilityCheckDialog"));
    setWindowTitle(tr("实例体检"));

    m_fetcher = new CompatMatrixFetcher(this);
    connect(m_fetcher, &CompatMatrixFetcher::localReady,
            this, &CompatibilityCheckDialog::onLocalReady);
    connect(m_fetcher, &CompatMatrixFetcher::matrixReady,
            this, &CompatibilityCheckDialog::onMatrixReady);
    connect(m_fetcher, &CompatMatrixFetcher::failed,
            this, &CompatibilityCheckDialog::onMatrixFailed);
    connect(m_fetcher, &CompatMatrixFetcher::progress,
            this, &CompatibilityCheckDialog::onProgress);

    initUI();
    initStyle();
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
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(700, 560)));
    card->setMaximumSize(820, 680);

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

    // 页签：体检报告 / 兼容矩阵
    QHBoxLayout *tabRow = new QHBoxLayout();
    tabRow->setSpacing(8);
    m_reportTabBtn = new QPushButton(tr("体检报告"), card);
    m_reportTabBtn->setObjectName(QStringLiteral("compatTab"));
    m_reportTabBtn->setCursor(Qt::PointingHandCursor);
    m_reportTabBtn->setCheckable(true);
    m_reportTabBtn->setChecked(true);
    m_matrixTabBtn = new QPushButton(tr("兼容矩阵"), card);
    m_matrixTabBtn->setObjectName(QStringLiteral("compatTab"));
    m_matrixTabBtn->setCursor(Qt::PointingHandCursor);
    m_matrixTabBtn->setCheckable(true);
    tabRow->addWidget(m_reportTabBtn);
    tabRow->addWidget(m_matrixTabBtn);
    tabRow->addStretch();
    cardLayout->addLayout(tabRow);

    m_tabStack = new QStackedWidget(card);
    m_tabStack->setObjectName(QStringLiteral("compatTabStack"));
    cardLayout->addWidget(m_tabStack, 1);

    // ---- 页 1：体检报告 ----
    QWidget *reportPage = new QWidget(m_tabStack);
    QVBoxLayout *reportLayout = new QVBoxLayout(reportPage);
    reportLayout->setContentsMargins(0, 0, 0, 0);
    reportLayout->setSpacing(10);
    buildReportPage(reportLayout);

    // ---- 页 2：兼容矩阵 ----
    QWidget *matrixPage = new QWidget(m_tabStack);
    QVBoxLayout *matrixLayout = new QVBoxLayout(matrixPage);
    matrixLayout->setContentsMargins(0, 0, 0, 0);
    matrixLayout->setSpacing(10);
    buildMatrixPage(matrixLayout);

    m_tabStack->addWidget(reportPage);
    m_tabStack->addWidget(matrixPage);

    // 底部按钮
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_recheckBtn = new QPushButton(tr("重新体检"), card);
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
    connect(m_reportTabBtn, &QPushButton::clicked, this, [this]() { onTabClicked(0); });
    connect(m_matrixTabBtn, &QPushButton::clicked, this, [this]() { onTabClicked(1); });
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { populateReport(m_lastReport); });
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CompatibilityCheckDialog::onTargetVersionChanged);
    connect(m_onlyUnsupportedBox, &QCheckBox::toggled,
            this, [this](bool) { rebuildMatrixRows(); });
}

void CompatibilityCheckDialog::buildReportPage(QVBoxLayout *parentLayout)
{
    // 汇总 chips + 筛选
    QHBoxLayout *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    m_errorChip = new QLabel(this);
    m_warningChip = new QLabel(this);
    m_infoChip = new QLabel(this);
    m_errorChip->setObjectName(QStringLiteral("compatChip"));
    m_warningChip->setObjectName(QStringLiteral("compatChip"));
    m_infoChip->setObjectName(QStringLiteral("compatChip"));
    summaryRow->addWidget(m_errorChip);
    summaryRow->addWidget(m_warningChip);
    summaryRow->addWidget(m_infoChip);
    summaryRow->addStretch();

    m_filterCombo = new QComboBox();
    m_filterCombo->setObjectName(QStringLiteral("compatFilterCombo"));
    m_filterCombo->addItem(tr("全部问题"), -1);
    m_filterCombo->addItem(tr("仅错误"), CompatIssue::Error);
    m_filterCombo->addItem(tr("仅警告"), CompatIssue::Warning);
    m_filterCombo->addItem(tr("仅提示"), CompatIssue::Info);
    summaryRow->addWidget(m_filterCombo);
    parentLayout->addLayout(summaryRow);

    // 问题列表
    m_scrollArea = new QScrollArea();
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
    parentLayout->addWidget(m_scrollArea, 1);

    // 运行中提示
    m_runningLabel = new QLabel(tr("正在扫描模组、资源包与光影包，请稍候…"));
    m_runningLabel->setObjectName(QStringLiteral("compatRunningLabel"));
    m_runningLabel->setAlignment(Qt::AlignCenter);
    parentLayout->addWidget(m_runningLabel, 1);
}

void CompatibilityCheckDialog::buildMatrixPage(QVBoxLayout *parentLayout)
{
    m_matrixSummary = new QLabel();
    m_matrixSummary->setObjectName(QStringLiteral("matrixSummary"));
    m_matrixSummary->setWordWrap(true);
    parentLayout->addWidget(m_matrixSummary);

    QHBoxLayout *targetRow = new QHBoxLayout();
    targetRow->setSpacing(8);
    QLabel *targetLabel = new QLabel(tr("回到这个版本："));
    targetLabel->setObjectName(QStringLiteral("matrixMeta"));
    targetRow->addWidget(targetLabel);

    m_targetCombo = new QComboBox();
    m_targetCombo->setObjectName(QStringLiteral("compatFilterCombo"));
    m_targetCombo->setMinimumWidth(180);
    targetRow->addWidget(m_targetCombo);

    m_onlyUnsupportedBox = new QCheckBox(tr("只看不支持的模组"));
    m_onlyUnsupportedBox->setObjectName(QStringLiteral("matrixCheck"));
    targetRow->addWidget(m_onlyUnsupportedBox);
    targetRow->addStretch();
    parentLayout->addLayout(targetRow);

    m_matrixScroll = new QScrollArea();
    m_matrixScroll->setObjectName(QStringLiteral("compatScroll"));
    m_matrixScroll->setWidgetResizable(true);
    m_matrixScroll->setFrameShape(QFrame::NoFrame);
    m_matrixContainer = new QWidget();
    m_matrixContainer->setObjectName(QStringLiteral("matrixContainer"));
    m_matrixLayout = new QVBoxLayout(m_matrixContainer);
    m_matrixLayout->setContentsMargins(0, 0, 0, 0);
    m_matrixLayout->setSpacing(8);
    m_matrixLayout->addStretch();
    m_matrixScroll->setWidget(m_matrixContainer);
    parentLayout->addWidget(m_matrixScroll, 1);

    m_matrixStatus = new QLabel(tr("正在向 Modrinth 核对模组…"));
    m_matrixStatus->setObjectName(QStringLiteral("matrixStatus"));
    m_matrixStatus->setWordWrap(true);
    parentLayout->addWidget(m_matrixStatus);

    // 未体检前先隐藏目标下拉，避免出现空下拉框
    m_targetCombo->hide();
    m_onlyUnsupportedBox->hide();
}

void CompatibilityCheckDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg       = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder   = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor   = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor    = isLight ? "#333333" : "#e8e8e8";
    const QString subTextColor = isLight ? "#777777" : "#a8a8ad";
    const QString rowBg        = isLight ? "rgba(0, 0, 0, 12)" : "rgba(255, 255, 255, 14)";
    const QString rowBorder    = isLight ? "rgba(0, 0, 0, 25)" : "rgba(255, 255, 255, 25)";
    const QString comboBg      = isLight ? "#f5f5f5" : "#3b3b40";
    const QString comboBorder  = isLight ? "#d4d4d4" : "#55555a";

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
        "QPushButton#compatTab {"
        "    background-color: transparent; border: 1px solid %7; border-radius: 10px;"
        "    color: %4; padding: 5px 16px; font-size: 13px;"
        "}"
        "QPushButton#compatTab:checked {"
        "    background-color: %8; border-color: %8; color: #ffffff; font-weight: bold;"
        "}"
        "QPushButton#compatTab:hover {"
        "    border-color: %8;"
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
        "QWidget#compatIssueContainer, QWidget#matrixContainer {"
        "    background: transparent;"
        "}"
        "QLabel#compatRunningLabel, QLabel#matrixStatus {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QLabel#matrixSummary {"
        "    color: %3; background: transparent; font-size: 13px;"
        "}"
        "QLabel#matrixMeta {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QCheckBox#matrixCheck {"
        "    color: %4; background: transparent; font-size: 12px; spacing: 6px;"
        "}"
        "QFrame#matrixRow, QFrame#issueCard {"
        "    background-color: %9;"
        "    border: 1px solid %10;"
        "    border-radius: 10px;"
        "}"
        "QLabel#matrixName {"
        "    color: %3; background: transparent; font-size: 13px; font-weight: bold;"
        "}"
        "QLabel#matrixRowMeta {"
        "    color: %4; background: transparent; font-size: 11px;"
        "}"
        "QLabel#matrixChip {"
        "    color: #ffffff; border-radius: 8px; padding: 1px 8px; font-size: 11px;"
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
        "QLabel#compatEmptyLabel {"
        "    color: %4; background: transparent; font-size: 13px;"
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
                      .arg(cardBg, cardBorder, titleColor, subTextColor,
                           severityColor(CompatIssue::Error),
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

QString CompatibilityCheckDialog::statusColor(int status) const
{
    const bool isLight = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme);
    switch (status)
    {
    case CompatMatrixRow::StatusOk:
        return isLight ? "#3f9c5a" : "#5fbf7f";
    case CompatMatrixRow::StatusOutdated:
        return isLight ? "#d18f2e" : "#e0a34c";
    case CompatMatrixRow::StatusArchived:
        return isLight ? "#d64545" : "#ef6b6b";
    case CompatMatrixRow::StatusLoaderMismatch:
        return isLight ? "#8a6bbf" : "#a98cd8";
    default:
        return isLight ? "#8a8a8a" : "#8f8f95";
    }
}

void CompatibilityCheckDialog::startCheck()
{
    if (m_instancePath.isEmpty())
        return;

    // 实例信息行
    QStringList metaParts;
    if (!m_gameVersion.isEmpty())
        metaParts << (tr("Minecraft %1").arg(m_gameVersion));
    else
        metaParts << tr("版本未知");
    if (!m_loaderType.isEmpty())
        metaParts << m_loaderType;
    else
        metaParts << tr("原版");
    m_metaLabel->setText(metaParts.join(" · "));

    m_localDone = false;
    m_lastReport = CompatReport();
    m_matrixReport = CompatMatrixReport();
    m_matrixSummary->setText(tr("正在体检…"));
    m_matrixStatus->setText(tr("正在扫描模组…"));
    m_targetCombo->clear();
    m_targetCombo->hide();
    m_onlyUnsupportedBox->hide();
    clearLayout(m_matrixLayout, 1);

    showRunning(true);
    m_fetcher->start(m_instancePath, m_gameVersion, m_loaderType);
}

void CompatibilityCheckDialog::showRunning(bool running)
{
    m_runningLabel->setVisible(running && !m_localDone);
    m_scrollArea->setVisible(m_localDone);
    m_recheckBtn->setEnabled(!running);
    if (running)
    {
        m_errorChip->setText(tr("检测中…"));
        m_warningChip->hide();
        m_infoChip->hide();
    }
}

void CompatibilityCheckDialog::onProgress(const QString &text)
{
    m_matrixStatus->setText(text);
    if (!m_localDone)
        m_runningLabel->setText(text);
}

void CompatibilityCheckDialog::onLocalReady(const CompatReport &report,
                                            const QList<ModInfo> &mods)
{
    Q_UNUSED(mods);
    m_localDone = true;
    m_lastReport = report;
    showRunning(false);
    populateReport(report);
    m_matrixStatus->setText(tr("正在向 Modrinth 核对模组…"));
}

void CompatibilityCheckDialog::onMatrixReady(const CompatMatrixReport &report)
{
    m_matrixReport = report;
    m_recheckBtn->setEnabled(true);
    m_runningLabel->hide();
    m_scrollArea->setVisible(true);
    populateMatrix();
}

void CompatibilityCheckDialog::onMatrixFailed(const QString &error)
{
    m_recheckBtn->setEnabled(true);
    m_runningLabel->hide();
    m_scrollArea->setVisible(true);
    m_matrixStatus->setText(tr("体检失败：%1").arg(error));
    m_matrixSummary->setText(tr("本地检查也没能完成，请稍后重试。"));
}

void CompatibilityCheckDialog::onTabClicked(int index)
{
    m_reportTabBtn->setChecked(index == 0);
    m_matrixTabBtn->setChecked(index == 1);
    m_tabStack->setCurrentIndex(index);
}

void CompatibilityCheckDialog::onTargetVersionChanged(int index)
{
    Q_UNUSED(index);
    rebuildMatrixRows();
}

void CompatibilityCheckDialog::populateReport(const CompatReport &report)
{
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

    const int severityFilter = m_filterCombo->currentData().toInt();

    clearLayout(m_issueLayout, 1);

    QList<CompatIssue> sorted = report.issues;
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

    QString scopeText = tr("已检查 %1 个模组、%2 个资源包、%3 个光影包")
                            .arg(report.modCount)
                            .arg(report.resourcePackCount)
                            .arg(report.shaderPackCount);
    if (shown == 0)
    {
        QLabel *emptyLabel = new QLabel(
            report.issues.isEmpty()
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

void CompatibilityCheckDialog::populateMatrix()
{
    const CompatMatrixReport &r = m_matrixReport;

    // ---- 摘要 ----
    QString summary;
    if (!r.networkOk)
    {
        summary = tr("未能从 Modrinth 取到数据（%1），这次只完成了本地检查。"
                     "连上网络后点「重新体检」即可看到兼容矩阵。")
                      .arg(r.note.isEmpty() ? tr("数据源不可用") : r.note);
    }
    else if (r.modCount == 0)
    {
        summary = tr("这个实例的 mods 目录里没有模组。");
    }
    else
    {
        QStringList parts;
        const int active = r.activeCount > 0 ? r.activeCount : r.matchedCount;
        const bool hasCurrent = !r.gameVersion.isEmpty();

        if (r.stableComplete)
        {
            if (!hasCurrent)
            {
                parts << tr("实例版本未知，按模组支持情况看，这个包最稳的版本是 %1。")
                             .arg(r.stableVersion);
            }
            else
            {
                const int cmp = LocalCompatibilityChecker::compareVersions(r.stableVersion,
                                                                           r.gameVersion);
                if (cmp > 0)
                {
                    parts << tr("建议升级到 %1：所有已核对的模组都有对应版本。")
                                 .arg(r.stableVersion);
                }
                else if (cmp == 0)
                {
                    parts << tr("建议停在当前 %1：所有已核对的模组都支持这个版本。")
                                 .arg(r.gameVersion);
                }
                else
                {
                    parts << tr("当前 %1 只有 %2/%3 个模组有对应版本，建议回到 %4——"
                                "所有已核对的模组都支持它。")
                                 .arg(r.gameVersion)
                                 .arg(r.currentSupported)
                                 .arg(active)
                                 .arg(r.stableVersion);
                }
            }
        }
        else if (hasCurrent)
        {
            parts << tr("没有哪个版本能让所有模组都可用；当前 %1 有 %2/%3 个模组可用，"
                        "升级前建议先补齐下表标红的模组。")
                         .arg(r.gameVersion)
                         .arg(r.currentSupported)
                         .arg(active);
        }
        else
        {
            parts << tr("实例版本未知，无法给出升级建议。");
        }

        QStringList statParts;
        statParts << tr("已核对 %1/%2").arg(r.matchedCount).arg(r.modCount);
        if (r.archivedCount > 0)
            statParts << tr("已停更 %1").arg(r.archivedCount);
        if (r.outdatedCount > 0)
            statParts << tr("仅支持旧版本 %1").arg(r.outdatedCount);
        if (r.unknownCount > 0)
            statParts << tr("未收录 %1").arg(r.unknownCount);
        parts << statParts.join(" · ");
        summary = parts.join("\n");
    }
    m_matrixSummary->setText(summary);

    // ---- 目标版本下拉 ----
    m_targetCombo->blockSignals(true);
    m_targetCombo->clear();
    if (!r.gameVersion.isEmpty())
    {
        m_targetCombo->addItem(tr("当前 %1（%2/%3 可用）")
                                   .arg(r.gameVersion)
                                   .arg(r.currentSupported)
                                   .arg(r.activeCount > 0 ? r.activeCount : r.matchedCount),
                               r.gameVersion);
    }
    for (int i = r.targets.size() - 1; i >= 0; --i)
    {
        const CompatMatrixTarget &t = r.targets.at(i);
        m_targetCombo->addItem(tr("%1（%2/%3 可用）")
                                   .arg(t.gameVersion)
                                   .arg(t.supported)
                                   .arg(t.total),
                               t.gameVersion);
    }
    // 推荐版本低于当前（或与当前相同但下拉里没有）时补一项，方便直接对比
    if (r.stableComplete && r.modCount > 0 && r.activeCount > 0
        && m_targetCombo->findData(r.stableVersion) < 0)
    {
        m_targetCombo->addItem(tr("%1（%2/%3 可用）")
                                   .arg(r.stableVersion)
                                   .arg(r.activeCount)
                                   .arg(r.activeCount),
                               r.stableVersion);
    }

    // 默认停在建议版本（没有建议则停在当前版本）
    int selected = 0;
    if (r.stableComplete)
    {
        const int idx = m_targetCombo->findData(r.stableVersion);
        if (idx >= 0)
            selected = idx;
    }
    if (m_targetCombo->count() > 0)
        m_targetCombo->setCurrentIndex(selected);
    m_targetCombo->blockSignals(false);
    m_targetCombo->setVisible(r.networkOk && r.modCount > 0);
    m_onlyUnsupportedBox->setVisible(r.networkOk && r.modCount > 0);
    m_onlyUnsupportedBox->setChecked(false);

    m_matrixStatus->setText(r.note.isEmpty() ? tr("说明：按 Modrinth 资源页声明的支持版本统计。")
                                             : r.note);

    rebuildMatrixRows();
}

void CompatibilityCheckDialog::rebuildMatrixRows()
{
    clearLayout(m_matrixLayout, 1);

    if (!m_matrixReport.networkOk && m_matrixReport.rows.isEmpty())
    {
        QLabel *hint = new QLabel(tr("没有可展示的模组数据。"), m_matrixContainer);
        hint->setObjectName(QStringLiteral("compatEmptyLabel"));
        hint->setAlignment(Qt::AlignCenter);
        m_matrixLayout->insertWidget(0, hint, 1, Qt::AlignCenter);
        return;
    }

    const QString target = m_targetCombo->count() > 0
                               ? m_targetCombo->currentData().toString()
                               : m_matrixReport.gameVersion;
    const bool onlyUnsupported = m_onlyUnsupportedBox->isChecked();

    QList<CompatMatrixRow> rows = m_matrixReport.rows;
    std::sort(rows.begin(), rows.end(),
              [](const CompatMatrixRow &a, const CompatMatrixRow &b) {
                  const int pa = statusPriority(a.status);
                  const int pb = statusPriority(b.status);
                  if (pa != pb)
                      return pa < pb;
                  return a.displayName.localeAwareCompare(b.displayName) < 0;
              });

    int shown = 0;
    for (const CompatMatrixRow &row : rows)
    {
        const bool supportTarget = row.matched && row.loaderSupported
                                   && CompatMatrix::supportsVersion(row, target);
        if (onlyUnsupported && supportTarget)
            continue;
        m_matrixLayout->insertWidget(m_matrixLayout->count() - 1,
                                     createMatrixRow(row, target));
        ++shown;
    }

    if (shown == 0)
    {
        QLabel *emptyLabel = new QLabel(
            onlyUnsupported ? tr("所有模组都支持这个版本") : tr("没有模组"),
            m_matrixContainer);
        emptyLabel->setObjectName(QStringLiteral("compatEmptyLabel"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_matrixLayout->insertWidget(m_matrixLayout->count() - 1, emptyLabel, 1, Qt::AlignCenter);
    }
}

QWidget *CompatibilityCheckDialog::createMatrixRow(const CompatMatrixRow &row,
                                                   const QString &targetVersion)
{
    QFrame *frame = new QFrame(m_matrixContainer);
    frame->setObjectName(QStringLiteral("matrixRow"));
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *rowLayout = new QHBoxLayout(frame);
    rowLayout->setContentsMargins(12, 9, 12, 9);
    rowLayout->setSpacing(10);

    QLabel *dot = new QLabel(frame);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QString("background-color: %1; border-radius: 5px;")
                           .arg(statusColor(row.status)));
    rowLayout->addWidget(dot, 0, Qt::AlignTop);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(3);

    QLabel *name = new QLabel(row.displayName, frame);
    name->setObjectName(QStringLiteral("matrixName"));
    name->setWordWrap(true);
    if (!row.projectUrl.isEmpty())
    {
        // 有资源页的模组直接点名字跳浏览器，方便用户自己核对作者说明
        const QString linkColor = ThemeManager::instance()->currentThemeColor();
        name->setTextFormat(Qt::RichText);
        name->setText(QString("<a style=\"color:%1; text-decoration:none;\" href=\"%2\">%3</a>")
                          .arg(linkColor,
                               row.projectUrl.toHtmlEscaped(),
                               row.displayName.toHtmlEscaped()));
        name->setOpenExternalLinks(true);
        name->setToolTip(row.projectUrl);
    }
    textLayout->addWidget(name);

    // 悬停看完整支持版本列表（最多列 12 个，避免提示框过长）
    if (!row.supportedGameVersions.isEmpty())
    {
        QStringList shown = row.supportedGameVersions;
        std::sort(shown.begin(), shown.end(),
                  [](const QString &a, const QString &b) {
                      return LocalCompatibilityChecker::compareVersions(a, b) < 0;
                  });
        const int limit = 12;
        QString tip = tr("作者声明的支持版本：") + shown.mid(0, limit).join("、");
        if (shown.size() > limit)
            tip += QStringLiteral(" …");
        frame->setToolTip(tip);
    }

    QStringList metaParts;
    if (!row.localVersion.isEmpty())
        metaParts << tr("本地 %1").arg(row.localVersion);
    if (!row.highestSupportedVersion.isEmpty() && row.matched)
        metaParts << tr("作者支持到 %1").arg(row.highestSupportedVersion);
    if (!row.enabled)
        metaParts << tr("已禁用");
    if (!metaParts.isEmpty())
    {
        QLabel *meta = new QLabel(metaParts.join(" · "), frame);
        meta->setObjectName(QStringLiteral("matrixRowMeta"));
        meta->setWordWrap(true);
        textLayout->addWidget(meta);
    }
    rowLayout->addLayout(textLayout, 1);

    // 该模组在「目标版本」上的可用性：未知的不下结论
    if (!targetVersion.isEmpty())
    {
        bool supportTarget = false;
        QString chipText;
        QString chipColor;
        if (!row.matched)
        {
            chipText = tr("版本未知");
            chipColor = statusColor(CompatMatrixRow::StatusUnknown);
        }
        else if (!row.enabled)
        {
            chipText = tr("已禁用");
            chipColor = statusColor(CompatMatrixRow::StatusUnknown);
        }
        else
        {
            supportTarget = row.loaderSupported
                            && CompatMatrix::supportsVersion(row, targetVersion);
            chipText = supportTarget ? tr("%1 可用").arg(targetVersion)
                                     : tr("%1 无").arg(targetVersion);
            chipColor = supportTarget ? statusColor(CompatMatrixRow::StatusOk)
                                      : statusColor(CompatMatrixRow::StatusOutdated);
        }
        QLabel *targetChip = new QLabel(chipText, frame);
        targetChip->setObjectName(QStringLiteral("matrixChip"));
        targetChip->setStyleSheet(QString("background-color: %1;").arg(chipColor));
        rowLayout->addWidget(targetChip, 0, Qt::AlignTop);
    }

    // 状态胶囊
    QLabel *statusChip = new QLabel(CompatMatrix::statusText(row.status), frame);
    statusChip->setObjectName(QStringLiteral("matrixChip"));
    statusChip->setStyleSheet(QString("background-color: %1;").arg(statusColor(row.status)));
    rowLayout->addWidget(statusChip, 0, Qt::AlignTop);

    return frame;
}

QWidget *CompatibilityCheckDialog::createIssueCard(const CompatIssue &issue)
{
    QFrame *card = new QFrame(m_issueContainer);
    card->setObjectName(QStringLiteral("issueCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(12, 10, 12, 10);
    rowLayout->setSpacing(10);

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

void CompatibilityCheckDialog::clearLayout(QLayout *layout, int keepTail)
{
    if (!layout)
        return;
    while (layout->count() > keepTail)
    {
        QLayoutItem *item = layout->takeAt(0);
        if (!item)
            break;
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}
