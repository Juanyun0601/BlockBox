/**
 * @file   TranslationComposePage.cpp
 * @brief  创建 / 完善翻译页实现
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "TranslationComposePage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {
constexpr int kSummaryMinLength = 2;
constexpr int kSummaryMaxLength = 500;
constexpr int kDetailMinLength = 5;

/** 提交人看到的上次提交状态文案（status 为空表示还没有提交过） */
QString statusLabelFor(const QString &status)
{
    if (status == QStringLiteral("pending"))
        return QObject::tr("上次提交：待审核，通过后会对所有人显示");
    if (status == QStringLiteral("rejected"))
        return QObject::tr("上次提交：未通过审核，修改后可再次提交");
    if (status == QStringLiteral("approved"))
        return QObject::tr("上次提交：已通过审核，欢迎继续完善");
    return QString();
}
} // namespace

TranslationComposePage::TranslationComposePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &TranslationComposePage::updateState);
}

void TranslationComposePage::buildUi()
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
    auto *loginHint = new QLabel(tr("登录论坛账号后就可以为这个资源创建翻译了"), m_loginHost);
    loginHint->setObjectName(QStringLiteral("forumStatus"));
    loginHint->setAlignment(Qt::AlignCenter);
    loginLayout->addWidget(loginHint);
    auto *loginButton = new QPushButton(tr("去登录"), m_loginHost);
    loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setFixedHeight(34);
    connect(loginButton, &QPushButton::clicked, this, &TranslationComposePage::loginRequested);
    loginLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_loginHost);

    // ── 登录后的翻译表单 ──
    m_mainHost = new QWidget(content);
    m_mainHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *mainLayout = new QVBoxLayout(m_mainHost);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);

    auto *formCard = ForumKit::makeCard(m_mainHost);
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(20, 16, 20, 16);
    formLayout->setSpacing(10);

    // 资源信息：名称 + 来源角标
    {
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(10);
        m_projectTitleLabel = ForumKit::makeSectionTitle(formCard, QString());
        titleRow->addWidget(m_projectTitleLabel);
        m_projectSourceLabel = ForumKit::makeMuted(formCard, QString());
        titleRow->addWidget(m_projectSourceLabel);
        titleRow->addStretch(1);
        formLayout->addLayout(titleRow);
    }

    m_existingStatusLabel = ForumKit::makeMuted(formCard, QString());
    m_existingStatusLabel->setWordWrap(true);
    formLayout->addWidget(m_existingStatusLabel);

    // 目标语言（本版只有简体中文，服务端支持多语言后在这里扩充选项）
    auto *langRow = new QHBoxLayout();
    langRow->setSpacing(10);
    langRow->addWidget(ForumKit::makeMuted(formCard, tr("翻译为")));
    m_langCombo = new QComboBox(formCard);
    m_langCombo->setObjectName(QStringLiteral("forumField"));
    m_langCombo->addItem(tr("简体中文"));
    m_langCombo->setFixedWidth(160);
    langRow->addWidget(m_langCombo);
    langRow->addStretch(1);
    formLayout->addLayout(langRow);

    // 简介（单行）
    {
        auto *block = new QWidget(formCard);
        block->setAttribute(Qt::WA_StyledBackground, false);
        auto *blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(4);
        blockLayout->addWidget(ForumKit::makeMuted(block, tr("简介")));
        m_summaryEdit = new QLineEdit(block);
        m_summaryEdit->setObjectName(QStringLiteral("forumField"));
        m_summaryEdit->setPlaceholderText(tr("把上面的英文简介翻译成中文"));
        m_summaryEdit->setMaxLength(kSummaryMaxLength);
        blockLayout->addWidget(m_summaryEdit);
        formLayout->addWidget(block);
    }

    // 详细信息（富文本，预填原文原地翻译）
    {
        auto *block = new QWidget(formCard);
        block->setAttribute(Qt::WA_StyledBackground, false);
        auto *blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(4);
        blockLayout->addWidget(ForumKit::makeMuted(block, tr("详细信息")));
        blockLayout->addWidget(ForumKit::makeMuted(
            block, tr("正文已预填原文，把里面的英文替换成中文即可；图片和链接位置保留不动")));
        m_detailEdit = new QTextEdit(block);
        m_detailEdit->setObjectName(QStringLiteral("forumReplyEdit"));
        m_detailEdit->setAcceptRichText(true);
        m_detailEdit->setMinimumHeight(260);
        blockLayout->addWidget(m_detailEdit);
        formLayout->addWidget(block);
    }

    formLayout->addWidget(ForumKit::makeMuted(
        formCard, tr("提交后会先进入审核，通过后其他用户打开这个详情页就能看到你的翻译。")));

    mainLayout->addWidget(formCard);

    // 提交行
    auto *submitCard = ForumKit::makeCard(m_mainHost);
    auto *submitLayout = new QHBoxLayout(submitCard);
    submitLayout->setContentsMargins(20, 12, 20, 12);
    submitLayout->setSpacing(10);
    m_statusLabel = new QLabel(submitCard);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    submitLayout->addWidget(m_statusLabel, 1);
    m_submitButton = new QPushButton(tr("提交翻译"), submitCard);
    m_submitButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_submitButton->setCursor(Qt::PointingHandCursor);
    m_submitButton->setFixedHeight(34);
    m_submitButton->setMinimumWidth(110);
    connect(m_submitButton, &QPushButton::clicked, this, &TranslationComposePage::submit);
    submitLayout->addWidget(m_submitButton);
    mainLayout->addWidget(submitCard);
    mainLayout->addStretch(1);

    layout->addWidget(m_loginHost);
    layout->addWidget(m_mainHost);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void TranslationComposePage::open(const ModInfo &info, const QString &originalSummary,
                                  const QString &originalDetailHtml)
{
    m_modInfo = info;
    m_originalSummary = originalSummary;
    m_originalDetailHtml = originalDetailHtml;
    m_targetLang = ContentTranslationService::defaultTargetLang();
    m_existing = ContentTranslation{};
    m_loadingExisting = false;

    m_projectTitleLabel->setText(info.name);
    m_projectSourceLabel->setText(info.source == QStringLiteral("modrinth") ? tr("Modrinth")
                                                                            : tr("CurseForge"));

    prefillFromOriginal();
    m_existingStatusLabel->setText(QString());
    m_statusLabel->setText(QString());
    m_submitButton->setEnabled(true);

    updateState();
}

void TranslationComposePage::updateState()
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    m_mainHost->setVisible(loggedIn);
    m_loginHost->setVisible(!loggedIn);
    if (loggedIn && m_modInfo.isValid() && !m_loadingExisting) {
        m_loadingExisting = true;
        loadExisting();
    }
}

void TranslationComposePage::loadExisting()
{
    auto *service = ContentTranslationService::instance();
    // 自己的提交优先：预填它、显示审核状态；自己没有再看公开翻译
    service->fetchMyTranslation(
        m_modInfo.source, m_modInfo.id, m_targetLang,
        [this](const ContentTranslation &mine) {
            if (mine.isValid()) {
                m_existing = mine;
                prefillFrom(mine);
                m_existingStatusLabel->setText(statusLabelFor(mine.status));
                m_loadingExisting = false;
                return;
            }
            ContentTranslationService::instance()->fetchTranslation(
                m_modInfo.source, m_modInfo.id, m_targetLang,
                [this](const ContentTranslation &publicOne) {
                    m_loadingExisting = false;
                    if (!publicOne.isValid())
                        return;
                    m_existing = publicOne;
                    prefillFrom(publicOne);
                },
                [this](const QString &, int) { m_loadingExisting = false; });
        },
        [this](const QString &, int) {
            m_loadingExisting = false;
        });
}

void TranslationComposePage::prefillFrom(const ContentTranslation &translation)
{
    if (!translation.summary.isEmpty())
        m_summaryEdit->setText(translation.summary);
    if (!translation.detailHtml.isEmpty())
        m_detailEdit->setHtml(translation.detailHtml);
}

void TranslationComposePage::prefillFromOriginal()
{
    if (!m_originalSummary.isEmpty())
        m_summaryEdit->setText(m_originalSummary);
    if (!m_originalDetailHtml.isEmpty())
        m_detailEdit->setHtml(m_originalDetailHtml);
    else
        m_detailEdit->clear();
}

void TranslationComposePage::submit()
{
    ContentTranslation draft;
    draft.source = m_modInfo.source;
    draft.projectId = m_modInfo.id;
    draft.lang = m_targetLang;
    draft.summary = m_summaryEdit->text().trimmed();

    if (draft.summary.size() < kSummaryMinLength) {
        NotificationManager::showInfo(this, tr("简介至少要 %1 个字").arg(kSummaryMinLength));
        m_summaryEdit->setFocus();
        return;
    }

    // 详情区是否真的被翻译过：拿纯文本跟原文比，没动过就不提交详情翻译，
    // 免得把英文原文当「译文」传上去。富文本转出的 HTML 带排版差异，不能直接比字符串。
    const QString detailHtml = m_detailEdit->toHtml().trimmed();
    const QString detailPlain = m_detailEdit->toPlainText().trimmed();
    QString originalPlain;
    if (!m_originalDetailHtml.isEmpty()) {
        QTextDocument doc;
        doc.setHtml(m_originalDetailHtml);
        originalPlain = doc.toPlainText().trimmed();
    }
    const bool detailChanged = !originalPlain.isEmpty() && !detailPlain.isEmpty()
                               && detailPlain != originalPlain;
    if (detailChanged && detailHtml.size() < kDetailMinLength) {
        NotificationManager::showInfo(this, tr("详细信息不能是空的"));
        m_detailEdit->setFocus();
        return;
    }
    if (detailChanged)
        draft.detailHtml = detailHtml;

    m_submitButton->setEnabled(false);
    setStatus(tr("正在提交…"));

    ContentTranslationService::instance()->submitTranslation(
        draft, m_originalSummary, m_originalDetailHtml, m_modInfo.name,
        [this]() {
            m_submitButton->setEnabled(true);
            setStatus(QString());
            m_existingStatusLabel->setText(statusLabelFor(QStringLiteral("pending")));
            NotificationManager::showInfo(this, tr("翻译已提交，进入审核后会对所有人显示。感谢参与！"));
            emit submitted();
        },
        [this](const QString &message, int) {
            m_submitButton->setEnabled(true);
            setStatus(message, true);
        });
}

void TranslationComposePage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
