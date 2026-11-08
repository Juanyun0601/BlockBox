/**
 * @file   ForumReportPage.cpp
 * @brief  举报楼层页实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "ForumReportPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumContentService.h"
#include "utils/StyleKit.h"

namespace {

constexpr int kDetailMaxLength = 200;   // 与服务端举报理由上限一致

/** 举报分类：slug 与服务端 /api/reports 的白名单一致，新增时两边同步 */
struct ReportCategory
{
    const char *slug;
    const char *label;
};

const ReportCategory kCategories[] = {
    {"spam", QT_TRANSLATE_NOOP("ForumReportPage", "垃圾广告 / 导流")},
    {"insult", QT_TRANSLATE_NOOP("ForumReportPage", "人身攻击 / 辱骂")},
    {"porn", QT_TRANSLATE_NOOP("ForumReportPage", "色情低俗")},
    {"illegal", QT_TRANSLATE_NOOP("ForumReportPage", "违法违规")},
    {"infringe", QT_TRANSLATE_NOOP("ForumReportPage", "侵权 / 盗搬")},
    {"privacy", QT_TRANSLATE_NOOP("ForumReportPage", "泄露隐私")},
    {"flooding", QT_TRANSLATE_NOOP("ForumReportPage", "引战 / 刷屏")},
    {"other", QT_TRANSLATE_NOOP("ForumReportPage", "其他")},
};

} // namespace

ForumReportPage::ForumReportPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &ForumReportPage::updateState);
}

void ForumReportPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(12);

    // ── 未登录态 ──
    m_loginHost = new QWidget(content);
    m_loginHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *loginLayout = new QVBoxLayout(m_loginHost);
    loginLayout->setContentsMargins(0, 60, 0, 60);
    loginLayout->setSpacing(12);
    auto *loginHint = new QLabel(tr("登录论坛账号后才能提交举报"), m_loginHost);
    loginHint->setObjectName(QStringLiteral("forumStatus"));
    loginHint->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(loginHint);
    auto *loginButton = new QPushButton(tr("去登录"), m_loginHost);
    loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setFixedHeight(34);
    connect(loginButton, &QPushButton::clicked, this, &ForumReportPage::loginRequested);
    loginLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginHost);

    // ── 登录后的举报表单 ──
    m_mainHost = new QWidget(content);
    m_mainHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *mainLayout = new QVBoxLayout(m_mainHost);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);

    // 举报对象卡
    auto *targetCard = ForumKit::makeCard(m_mainHost);
    auto *targetLayout = new QVBoxLayout(targetCard);
    targetLayout->setContentsMargins(20, 16, 20, 16);
    targetLayout->setSpacing(6);
    targetLayout->addWidget(ForumKit::makeSectionTitle(targetCard, tr("举报对象")));
    m_targetLabel = ForumKit::makeMuted(targetCard);
    targetLayout->addWidget(m_targetLabel);
    m_excerptLabel = ForumKit::makeMuted(targetCard);
    m_excerptLabel->setWordWrap(true);
    targetLayout->addWidget(m_excerptLabel);
    mainLayout->addWidget(targetCard);

    // 举报分类卡
    auto *formCard = ForumKit::makeCard(m_mainHost);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(20, 16, 20, 16);
    formLayout->setSpacing(10);

    formLayout->addWidget(ForumKit::makeSectionTitle(formCard, tr("举报分类 *")));
    formLayout->addWidget(ForumKit::makeMuted(formCard,
                                              tr("选一个最贴切的分类，管理员会优先按分类处理。")));
    for (const ReportCategory &category : kCategories)
    {
        auto *option = new QRadioButton(tr(category.label), formCard);
        option->setCursor(Qt::PointingHandCursor);
        formLayout->addWidget(option);
        m_categoryButtons.append(option);
    }

    formLayout->addWidget(ForumKit::makeMuted(formCard, tr("补充说明")));
    formLayout->addWidget(ForumKit::makeMuted(
        formCard, tr("选「其他」时必填；写清楚当时的情况，方便管理员核实。")));
    m_detailEdit = new QPlainTextEdit(formCard);
    m_detailEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_detailEdit->setPlaceholderText(tr("补充细节（可选）…"));
    m_detailEdit->setFixedHeight(90);
    formLayout->addWidget(m_detailEdit);

    mainLayout->addWidget(formCard);

    // 提交行
    auto *submitCard = ForumKit::makeCard(m_mainHost);
    auto *submitLayout = new QHBoxLayout(submitCard);
    submitLayout->setContentsMargins(20, 12, 20, 12);
    submitLayout->setSpacing(10);
    m_statusLabel = new QLabel(submitCard);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    submitLayout->addWidget(m_statusLabel, 1);
    m_submitButton = new QPushButton(tr("提交举报"), submitCard);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(34);
    m_submitButton->setMinimumWidth(110);
    connect(m_submitButton, &QPushButton::clicked, this, &ForumReportPage::submit);
    submitLayout->addWidget(m_submitButton);
    mainLayout->addWidget(submitCard);
    mainLayout->addStretch(1);

    layout->addWidget(m_loginHost);
    layout->addWidget(m_mainHost);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    updateState();
}

void ForumReportPage::open(int postId, int floor, const QString &authorName, const QString &excerpt)
{
    m_postId = postId;
    m_targetLabel->setText(tr("第 %1 楼 · %2").arg(floor).arg(
        authorName.isEmpty() ? tr("未知用户") : authorName));
    m_excerptLabel->setText(excerpt.isEmpty() ? tr("（该楼层没有正文内容）") : excerpt);

    // 清掉上一次的填写残留，避免串到别的楼层
    for (QRadioButton *option : m_categoryButtons)
        option->setChecked(false);
    m_detailEdit->clear();
    setStatus(QString());

    updateState();
}

void ForumReportPage::updateState()
{
    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();
    m_mainHost->setVisible(loggedIn);
    m_loginHost->setVisible(!loggedIn);
}

int ForumReportPage::checkedCategory() const
{
    for (int i = 0; i < m_categoryButtons.size(); ++i)
    {
        if (m_categoryButtons.at(i)->isChecked())
            return i;
    }
    return -1;
}

void ForumReportPage::submit()
{
    const int checked = checkedCategory();
    if (checked < 0)
    {
        NotificationManager::showInfo(this, tr("先选一个举报分类"));
        return;
    }

    const QString detail = m_detailEdit->toPlainText().trimmed();
    if (qstrcmp(kCategories[checked].slug, "other") == 0 && detail.isEmpty())
    {
        NotificationManager::showInfo(this, tr("选「其他」时要写清楚举报原因"));
        m_detailEdit->setFocus();
        return;
    }
    if (detail.size() > kDetailMaxLength)
    {
        NotificationManager::showInfo(this, tr("补充说明最多 %1 个字").arg(kDetailMaxLength));
        return;
    }

    m_submitButton->setEnabled(false);
    setStatus(tr("正在提交…"));

    ForumContentService::instance()->reportPost(
        m_postId, QString::fromLatin1(kCategories[checked].slug), detail,
        [this]() {
            m_submitButton->setEnabled(true);
            NotificationManager::showSuccess(this, tr("已提交举报，管理员会尽快处理"));
            emit submitted();
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            setStatus(message, true);
        });
}

void ForumReportPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
