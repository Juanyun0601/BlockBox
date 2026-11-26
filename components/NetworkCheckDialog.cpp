/**
 * @file   NetworkCheckDialog.cpp
 * @brief  网络连通性自检对话框实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "NetworkCheckDialog.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"
#include "utils/net/NetworkAccess.h"

NetworkCheckDialog::NetworkCheckDialog(QWidget *parent)
    : AppDialogBase(parent)
    , m_metaLabel(nullptr)
    , m_okChip(nullptr)
    , m_failChip(nullptr)
    , m_runningChip(nullptr)
    , m_verdictLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_rowsContainer(nullptr)
    , m_rowsLayout(nullptr)
    , m_recheckBtn(nullptr)
{
    setObjectName(QStringLiteral("networkCheckDialog"));
    setWindowTitle(tr("网络连通性自检"));

    initUI();
    initStyle();
}

void NetworkCheckDialog::runCheck(QWidget *parent)
{
    NetworkCheckDialog *dialog = new NetworkCheckDialog(parent);
    dialog->startCheck();
    dialog->exec();
    dialog->deleteLater();
}

void NetworkCheckDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("networkCheckCard"));
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(640, 520)));
    card->setMaximumSize(760, 640);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 20, 24, 18);
    cardLayout->setSpacing(12);

    // 标题 + 说明
    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setObjectName(QStringLiteral("netCheckTitleLabel"));
    cardLayout->addWidget(titleLabel);

    m_metaLabel = new QLabel(tr("检测启动器依赖的各个网络服务，找出究竟是哪一环不通。"),
                             card);
    m_metaLabel->setObjectName(QStringLiteral("netCheckMetaLabel"));
    m_metaLabel->setWordWrap(true);
    cardLayout->addWidget(m_metaLabel);

    // 汇总 chips
    QHBoxLayout *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    m_okChip = new QLabel(tr("通 0"), card);
    m_okChip->setObjectName(QStringLiteral("netCheckChip"));
    m_failChip = new QLabel(tr("不通 0"), card);
    m_failChip->setObjectName(QStringLiteral("netCheckChip"));
    m_runningChip = new QLabel(tr("检测中 0"), card);
    m_runningChip->setObjectName(QStringLiteral("netCheckChip"));
    summaryRow->addWidget(m_okChip);
    summaryRow->addWidget(m_failChip);
    summaryRow->addWidget(m_runningChip);
    summaryRow->addStretch();
    cardLayout->addLayout(summaryRow);

    // 结果列表
    m_scrollArea = new QScrollArea(card);
    m_scrollArea->setObjectName(QStringLiteral("netCheckScroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_rowsContainer = new QWidget();
    m_rowsContainer->setObjectName(QStringLiteral("netCheckRowsContainer"));
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(8);
    m_scrollArea->setWidget(m_rowsContainer);
    cardLayout->addWidget(m_scrollArea, 1);

    // 检测结论
    m_verdictLabel = new QLabel(card);
    m_verdictLabel->setObjectName(QStringLiteral("netCheckVerdictLabel"));
    m_verdictLabel->setWordWrap(true);
    cardLayout->addWidget(m_verdictLabel);

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

    connect(m_recheckBtn, &QPushButton::clicked, this, &NetworkCheckDialog::startCheck);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void NetworkCheckDialog::initStyle()
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

    const QString okColor    = isLight ? "#3f9c5a" : "#5fbf7f";
    const QString failColor  = isLight ? "#d64545" : "#ef6b6b";
    const QString runColor   = isLight ? "#8a8a8a" : "#8f8f95";

    setStyleSheet(QString(
        "QWidget#networkCheckCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#netCheckTitleLabel {"
        "    color: %3; background: transparent;"
        "    font-size: 16px; font-weight: bold;"
        "}"
        "QLabel#netCheckMetaLabel {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QLabel#netCheckChip {"
        "    color: #ffffff; background-color: %5;"
        "    border-radius: 9px; padding: 2px 10px; font-size: 12px;"
        "}"
        "QScrollArea#netCheckScroll {"
        "    background: transparent; border: none;"
        "}"
        "QWidget#netCheckRowsContainer {"
        "    background: transparent;"
        "}"
        "QFrame#netCheckRow {"
        "    background-color: %6;"
        "    border: 1px solid %7;"
        "    border-radius: 10px;"
        "}"
        "QLabel#netCheckName {"
        "    color: %3; background: transparent; font-size: 13px; font-weight: bold;"
        "}"
        "QLabel#netCheckHint {"
        "    color: %4; background: transparent; font-size: 12px;"
        "}"
        "QLabel#netCheckStatusChip {"
        "    color: #ffffff; border-radius: 8px; padding: 1px 8px; font-size: 11px;"
        "}"
        "QLabel#netCheckVerdictLabel {"
        "    color: %3; background: transparent; font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %8; border: 1px solid %9; border-radius: 10px;"
        "    color: %3; padding: 7px 18px; font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover {"
        "    background-color: %10;"
        "}"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %11; border: none; border-radius: 10px;"
        "    color: #ffffff; padding: 7px 22px; font-size: 13px; font-weight: bold;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover {"
        "    background-color: %10;"
        "}"
        "QScrollArea#netCheckScroll QScrollBar:vertical {"
        "    background: transparent; width: 8px; margin: 0;"
        "}"
        "QScrollArea#netCheckScroll QScrollBar::handle:vertical {"
        "    background: %7; border-radius: 4px; min-height: 30px;"
        "}"
        "QScrollArea#netCheckScroll QScrollBar::add-line:vertical,"
        "QScrollArea#netCheckScroll QScrollBar::sub-line:vertical { height: 0; }")
                      .arg(cardBg, cardBorder, titleColor, subTextColor,
                           runColor, rowBg, rowBorder,
                           comboBg, comboBorder,
                           tm->getThemeColorHover(),
                           tm->currentThemeColor()));
}

void NetworkCheckDialog::startCheck()
{
    // 收集检测项与状态
    m_endpoints.clear();
    m_states.clear();
    m_endpoints
        << Endpoint{tr("基础网络"), QStringLiteral("https://www.baidu.com/"),
                     tr("请检查 Wi-Fi、网线或代理与防火墙设置。")}
        << Endpoint{tr("Mojang 官方源"),
                     QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"),
                     tr("版本列表等官方资源无法获取，可在「设置 → 常规设置」把原版下载源切换为 BMCL 镜像。")}
        << Endpoint{tr("BMCLAPI 镜像"),
                     QStringLiteral("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json"),
                     tr("国内镜像暂不可用，建议先使用官方源或稍后再试。")}
        << Endpoint{tr("MCIM 镜像"),
                     QStringLiteral("https://mod.mcimirror.top/"),
                     tr("可在「设置 → 常规设置」关闭 MCIM 加速后重试。")}
        << Endpoint{tr("Modrinth API"),
                     QStringLiteral("https://api.modrinth.com/v2/"),
                     tr("Modrinth 的模组将无法搜索和下载。")}
        << Endpoint{tr("CurseForge CDN"),
                     QStringLiteral("https://edge.forgecdn.net/"),
                     tr("CurseForge 的模组文件将无法下载。")}
        << Endpoint{tr("微软登录"),
                     QStringLiteral("https://login.microsoftonline.com/"),
                     tr("微软账户将无法登录，离线账户不受影响。")};

    m_states.resize(m_endpoints.size());
    for (CheckState &st : m_states)
        st = CheckState();
    m_replyIndex.clear();
    m_timers.clear();

    m_recheckBtn->setEnabled(false);
    m_verdictLabel->clear();
    buildRows();
    updateSummary();

    // 并发发出全部请求：收到任何 HTTP 回复（含 4xx/5xx）即算"通"，
    // 只有 DNS / 连接 / 超时等网络层错误才判"不通"
    QNetworkAccessManager *nam = Net::networkAccessManager().get();
    for (int i = 0; i < m_endpoints.size(); ++i)
    {
        QNetworkRequest req(QUrl(m_endpoints.at(i).url));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(8000);

        QElapsedTimer timer;
        timer.start();
        m_timers.append(timer);

        QNetworkReply *reply = nam->get(req);
        m_replyIndex.insert(reply, i);
        connect(reply, &QNetworkReply::finished, this, &NetworkCheckDialog::onReplyFinished);
    }
    m_pendingCount = m_replyIndex.size();
}

void NetworkCheckDialog::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    const int index = m_replyIndex.take(reply);
    if (index < 0 || index >= m_states.size())
        return;

    CheckState st;
    st.finished = true;
    st.latencyMs = m_timers.at(index).elapsed();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status.isValid())
    {
        st.ok = true;  // 有 HTTP 回复即连通（4xx/5xx 只代表业务层，不影响连通性）
    }
    else
    {
        st.ok = false;
        st.errorText = chineseError(reply->error());
    }

    m_states[index] = st;
    setCardState(index, st);
    updateSummary();
}

void NetworkCheckDialog::buildRows()
{
    while (m_rowsLayout->count() > 0)
    {
        QLayoutItem *item = m_rowsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < m_endpoints.size(); ++i)
        m_rowsLayout->addWidget(createCheckCard(m_endpoints.at(i), i));
}

QWidget *NetworkCheckDialog::createCheckCard(const Endpoint &endpoint, int index)
{
    Q_UNUSED(index);
    QFrame *frame = new QFrame(m_rowsContainer);
    frame->setObjectName(QStringLiteral("netCheckRow"));
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *mainLayout = new QVBoxLayout(frame);
    mainLayout->setContentsMargins(12, 9, 12, 9);
    mainLayout->setSpacing(4);

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setSpacing(10);

    QLabel *name = new QLabel(endpoint.name, frame);
    name->setObjectName(QStringLiteral("netCheckName"));
    topRow->addWidget(name);
    topRow->addStretch();

    QLabel *chip = new QLabel(tr("检测中…"), frame);
    chip->setObjectName(QStringLiteral("netCheckStatusChip"));
    topRow->addWidget(chip, 0, Qt::AlignTop);

    mainLayout->addLayout(topRow);

    QLabel *hint = new QLabel(endpoint.hint, frame);
    hint->setObjectName(QStringLiteral("netCheckHint"));
    hint->setWordWrap(true);
    hint->hide();
    mainLayout->addWidget(hint);

    frame->setProperty("statusChip", QVariant::fromValue<QLabel *>(chip));
    frame->setProperty("hintLabel", QVariant::fromValue<QLabel *>(hint));
    return frame;
}

void NetworkCheckDialog::setCardState(int index, const CheckState &st)
{
    QWidget *row = m_rowsLayout->itemAt(index) ? m_rowsLayout->itemAt(index)->widget() : nullptr;
    if (!row)
        return;
    QLabel *chip = row->property("statusChip").value<QLabel *>();
    QLabel *hint = row->property("hintLabel").value<QLabel *>();

    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString okColor = isLight ? "#3f9c5a" : "#5fbf7f";
    const QString failColor = isLight ? "#d64545" : "#ef6b6b";

    if (st.ok)
    {
        chip->setText(tr("✓ %1 ms").arg(st.latencyMs));
        chip->setStyleSheet(QString("background-color: %1;").arg(okColor));
        hint->hide();
    }
    else
    {
        chip->setText(tr("✗ 不通"));
        chip->setStyleSheet(QString("background-color: %1;").arg(failColor));
        hint->setText(m_endpoints.at(index).hint
                      + (st.errorText.isEmpty() ? QString()
                                                : QStringLiteral("\n") + st.errorText));
        hint->show();
    }
}

void NetworkCheckDialog::updateSummary()
{
    int okCount = 0;
    int failCount = 0;
    int runningCount = 0;
    for (const CheckState &st : m_states)
    {
        if (!st.finished)
            ++runningCount;
        else if (st.ok)
            ++okCount;
        else
            ++failCount;
    }
    m_okChip->setText(tr("通 %1").arg(okCount));
    m_failChip->setText(tr("不通 %1").arg(failCount));
    m_runningChip->setText(tr("检测中 %1").arg(runningCount));

    m_okChip->setStyleSheet(
        QStringLiteral("background-color: %1;")
            .arg(ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme
                     ? "#3f9c5a" : "#5fbf7f"));
    m_failChip->setStyleSheet(
        QStringLiteral("background-color: %1;")
            .arg(ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme
                     ? "#d64545" : "#ef6b6b"));
    m_runningChip->setStyleSheet(
        QStringLiteral("background-color: %1;")
            .arg(ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme
                     ? "#8a8a8a" : "#8f8f95"));

    if (runningCount > 0)
        return;

    m_recheckBtn->setEnabled(true);
    if (okCount == m_states.size())
    {
        m_verdictLabel->setText(tr("所有服务均连通，网络状态良好。"));
    }
    else if (okCount == 0)
    {
        m_verdictLabel->setText(tr("本机当前无法上网，请先恢复基础网络连接。"));
    }
    else
    {
        QStringList badNames;
        for (int i = 0; i < m_endpoints.size(); ++i)
        {
            if (m_states.at(i).finished && !m_states.at(i).ok)
                badNames << m_endpoints.at(i).name;
        }
        m_verdictLabel->setText(tr("部分服务不通：%1。可参考各项下方的建议处理。")
                                    .arg(badNames.join(tr("、"))));
    }
}

QString NetworkCheckDialog::chineseError(int errorCode) const
{
    switch (errorCode)
    {
    case QNetworkReply::HostNotFoundError:
        return tr("无法解析服务器地址");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return tr("连接超时");
    case QNetworkReply::ConnectionRefusedError:
        return tr("连接被拒绝");
    case QNetworkReply::SslHandshakeFailedError:
        return tr("安全连接失败");
    case QNetworkReply::AuthenticationRequiredError:
        return tr("需要身份验证");
    case QNetworkReply::UnknownNetworkError:
        return tr("网络不可用");
    default:
        break;
    }
    // 兜底：其他网络层错误统一按"连接失败"表述，避免暴露英文原文
    return tr("连接失败");
}
