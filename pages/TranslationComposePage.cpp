/**
 * @file   TranslationComposePage.cpp
 * @brief  创建 / 完善翻译页实现
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "TranslationComposePage.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextList>
#include <QTextListFormat>
#include <QVBoxLayout>

#include "components/AppInputDialog.h"
#include "components/NotificationManager.h"
#include "layouts/FlowLayout.h"
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

/**
 * @brief 把选区扩成整段：格式栏的段落样式 / 列表要作用在整段上
 *
 * 没有选区时就是光标所在的那一段；选区正好停在下一段开头时
 * 不把下一段也扩进来（否则改一个标题会把后面一段一起改掉）。
 */
QTextCursor expandToBlocks(const QTextCursor &source)
{
    QTextCursor cursor = source;
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::BlockUnderCursor);
        return cursor;
    }

    QTextDocument *doc = cursor.document();
    const int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (end > start && end <= doc->characterCount()
        && doc->characterAt(end - 1) == QChar::ParagraphSeparator)
        --end;

    cursor.setPosition(start);
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.setPosition(qMax(start, end), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    return cursor;
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
    m_loginHost->setObjectName(QStringLiteral("translationLoginHost"));
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
    m_mainHost->setObjectName(QStringLiteral("translationMainHost"));
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

    // 详细信息（富文本，预填原文原地翻译，编辑框上方带一排格式按钮）
    {
        auto *block = new QWidget(formCard);
        block->setAttribute(Qt::WA_StyledBackground, false);
        auto *blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(4);
        blockLayout->addWidget(ForumKit::makeMuted(block, tr("详细信息")));
        blockLayout->addWidget(ForumKit::makeMuted(
            block, tr("正文已预填原文，把里面的英文替换成中文即可；图片和链接位置保留不动。"
                      "要强调、分点或加链接，用下面的格式栏点一下就行。")));

        m_detailEdit = new QTextEdit(block);
        m_detailEdit->setObjectName(QStringLiteral("forumReplyEdit"));
        m_detailEdit->setAcceptRichText(true);
        m_detailEdit->setMinimumHeight(300);

        blockLayout->addWidget(createFormatBar(block));
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
    resetDetailEditingState();
}

void TranslationComposePage::prefillFromOriginal()
{
    if (!m_originalSummary.isEmpty())
        m_summaryEdit->setText(m_originalSummary);
    if (!m_originalDetailHtml.isEmpty())
        m_detailEdit->setHtml(m_originalDetailHtml);
    else
        m_detailEdit->clear();
    resetDetailEditingState();
}

void TranslationComposePage::resetDetailEditingState()
{
    // 编辑框的「接下来输入的字用什么格式」会一直留到下一页内容，换资源后
    // 不重置的话，工具栏会莫名亮着加粗，新打的字也跟着变粗
    m_detailEdit->setCurrentCharFormat(QTextCharFormat());
    syncFormatBar();
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

// ──────────────────────────────────────────────────────────── 详细信息格式栏

QWidget *TranslationComposePage::createFormatBar(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("translationFormatBar"));
    bar->setAttribute(Qt::WA_StyledBackground, false);
    auto *flow = new FlowLayout(bar, 0, 2, 2);

    auto addButton = [this, bar, flow](const QString &text, const QString &tip, bool checkable) {
        auto *button = new QPushButton(text, bar);
        button->setObjectName(QStringLiteral("forumMdButton"));
        button->setToolTip(tip);
        button->setCheckable(checkable);
        button->setFixedHeight(26);
        button->setCursor(Qt::PointingHandCursor);
        flow->addWidget(button);
        return button;
    };

    // 撤销 / 重做：跟着编辑框自己的历史走，没得撤销时自动置灰
    m_undoButton = addButton(QStringLiteral("↶"), tr("撤销（Ctrl+Z）"), false);
    m_undoButton->setEnabled(false);
    connect(m_undoButton, &QPushButton::clicked, this, [this]() {
        m_detailEdit->undo();
        m_detailEdit->setFocus();
    });
    m_redoButton = addButton(QStringLiteral("↷"), tr("重做（Ctrl+Y）"), false);
    m_redoButton->setEnabled(false);
    connect(m_redoButton, &QPushButton::clicked, this, [this]() {
        m_detailEdit->redo();
        m_detailEdit->setFocus();
    });
    connect(m_detailEdit, &QTextEdit::undoAvailable, m_undoButton, &QPushButton::setEnabled);
    connect(m_detailEdit, &QTextEdit::redoAvailable, m_redoButton, &QPushButton::setEnabled);

    // 行内样式：按钮按下态跟着光标处的文字格式走
    m_boldButton = addButton(QStringLiteral("B"), tr("加粗（Ctrl+B）"), true);
    QFont boldFont = m_boldButton->font();
    boldFont.setBold(true);
    m_boldButton->setFont(boldFont);
    connect(m_boldButton, &QPushButton::clicked, this,
            [this](bool checked) { toggleInlineStyle(0, checked); });

    m_italicButton = addButton(QStringLiteral("I"), tr("斜体（Ctrl+I）"), true);
    QFont italicFont = m_italicButton->font();
    italicFont.setItalic(true);
    m_italicButton->setFont(italicFont);
    connect(m_italicButton, &QPushButton::clicked, this,
            [this](bool checked) { toggleInlineStyle(1, checked); });

    m_underlineButton = addButton(QStringLiteral("U"), tr("下划线（Ctrl+U）"), true);
    QFont underlineFont = m_underlineButton->font();
    underlineFont.setUnderline(true);
    m_underlineButton->setFont(underlineFont);
    connect(m_underlineButton, &QPushButton::clicked, this,
            [this](bool checked) { toggleInlineStyle(2, checked); });

    m_strikeButton = addButton(QStringLiteral("S"), tr("删除线"), true);
    QFont strikeFont = m_strikeButton->font();
    strikeFont.setStrikeOut(true);
    m_strikeButton->setFont(strikeFont);
    connect(m_strikeButton, &QPushButton::clicked, this,
            [this](bool checked) { toggleInlineStyle(3, checked); });

    // 段落样式：原文里的标题翻译完还能保持层次
    m_styleCombo = new QComboBox(bar);
    m_styleCombo->setObjectName(QStringLiteral("translationStyleCombo"));
    m_styleCombo->setToolTip(tr("段落样式：正文 / 小标题 / 标题"));
    m_styleCombo->addItem(tr("正文"), 0);
    m_styleCombo->addItem(tr("小标题"), 1);
    m_styleCombo->addItem(tr("标题"), 2);
    m_styleCombo->setFixedHeight(26);
    m_styleCombo->setMinimumWidth(88);
    // 外语界面的「小標題 / Подзаголовок」比中文长，宽度跟着内容走，别把字截掉
    m_styleCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_styleCombo, &QComboBox::activated, this, [this](int index) {
        applyTextStyle(m_styleCombo->itemData(index).toInt());
        m_detailEdit->setFocus();
    });
    flow->addWidget(m_styleCombo);

    m_bulletButton = addButton(QStringLiteral("• 列表"), tr("无序列表"), true);
    connect(m_bulletButton, &QPushButton::clicked, this, [this]() { toggleList(false); });
    m_numberButton = addButton(QStringLiteral("1. 编号"), tr("有序列表"), true);
    connect(m_numberButton, &QPushButton::clicked, this, [this]() { toggleList(true); });

    auto *linkButton = addButton(tr("链接"), tr("给选中的文字加链接（Ctrl+K）"), false);
    connect(linkButton, &QPushButton::clicked, this, &TranslationComposePage::insertLink);

    auto *clearButton = addButton(tr("清除格式"), tr("去掉选中文字的加粗、斜体、颜色和缩进"), false);
    connect(clearButton, &QPushButton::clicked, this, &TranslationComposePage::clearFormatting);

    // 快捷键只在编辑框里生效，不抢上面简介框的输入
    auto *boldShortcut = new QShortcut(QKeySequence::Bold, m_detailEdit);
    boldShortcut->setContext(Qt::WidgetShortcut);
    connect(boldShortcut, &QShortcut::activated, this,
            [this]() { toggleInlineStyle(0, !m_boldButton->isChecked()); });
    auto *italicShortcut = new QShortcut(QKeySequence::Italic, m_detailEdit);
    italicShortcut->setContext(Qt::WidgetShortcut);
    connect(italicShortcut, &QShortcut::activated, this,
            [this]() { toggleInlineStyle(1, !m_italicButton->isChecked()); });
    auto *underlineShortcut = new QShortcut(QKeySequence::Underline, m_detailEdit);
    underlineShortcut->setContext(Qt::WidgetShortcut);
    connect(underlineShortcut, &QShortcut::activated, this,
            [this]() { toggleInlineStyle(2, !m_underlineButton->isChecked()); });
    auto *linkShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), m_detailEdit);
    linkShortcut->setContext(Qt::WidgetShortcut);
    connect(linkShortcut, &QShortcut::activated, this, &TranslationComposePage::insertLink);

    connect(m_detailEdit, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &) { syncFormatBar(); });
    connect(m_detailEdit, &QTextEdit::cursorPositionChanged, this,
            &TranslationComposePage::syncFormatBar);

    syncFormatBar();
    return bar;
}

void TranslationComposePage::toggleInlineStyle(int kind, bool on)
{
    QTextCharFormat format;
    switch (kind) {
    case 0:
        format.setFontWeight(on ? QFont::Bold : QFont::Normal);
        break;
    case 1:
        format.setFontItalic(on);
        break;
    case 2:
        format.setFontUnderline(on);
        break;
    default:
        format.setFontStrikeOut(on);
        break;
    }
    m_detailEdit->mergeCurrentCharFormat(format);
    m_detailEdit->setFocus();
    syncFormatBar();
}

void TranslationComposePage::applyTextStyle(int level)
{
    const qreal body = bodyPointSize();
    qreal size = body;
    if (level == 1)
        size = body * 1.3;
    else if (level == 2)
        size = body * 1.7;

    const QTextCursor original = m_detailEdit->textCursor();
    QTextCursor work = expandToBlocks(original);

    QTextCharFormat format;
    format.setFontPointSize(size);
    format.setFontWeight(level == 0 ? QFont::Normal : QFont::Bold);

    work.mergeCharFormat(format);
    m_detailEdit->setTextCursor(original);
    m_detailEdit->setCurrentCharFormat(format);
    m_detailEdit->setFocus();
    syncFormatBar();
}

void TranslationComposePage::toggleList(bool ordered)
{
    const QTextListFormat::Style wanted =
        ordered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;

    QTextDocument *doc = m_detailEdit->document();
    QTextCursor work = expandToBlocks(m_detailEdit->textCursor());
    const int start = work.selectionStart();
    const int end = work.selectionEnd();

    // 选中的段落是否都已经在目标样式的列表里：是就整段退出，否则套上列表。
    // 这里不能只看光标所在段落——选区落在段首时光标会「站」在下一段上，判断会反。
    bool allInWantedList = true;
    {
        QTextBlock block = doc->findBlock(start);
        const QTextBlock lastBlock = doc->findBlock(qMax(start, end - 1));
        while (block.isValid()) {
            const QTextList *list = block.textList();
            if (!list || list->format().style() != wanted) {
                allInWantedList = false;
                break;
            }
            if (block == lastBlock)
                break;
            block = block.next();
        }
    }

    QTextCursor edit(doc);
    edit.beginEditBlock();
    if (allInWantedList) {
        // 退出列表：段落从列表里摘掉，缩进也一起归零，否则会留一截空白
        QTextBlock block = doc->findBlock(start);
        const QTextBlock lastBlock = doc->findBlock(qMax(start, end - 1));
        while (block.isValid()) {
            if (QTextList *list = block.textList())
                list->remove(block);
            QTextCursor blockCursor(block);
            QTextBlockFormat blockFormat = block.blockFormat();
            blockFormat.setIndent(0);
            blockCursor.setBlockFormat(blockFormat);
            if (block == lastBlock)
                break;
            block = block.next();
        }
    } else {
        QTextListFormat listFormat;
        listFormat.setStyle(wanted);
        listFormat.setIndent(1);
        work.createList(listFormat);
    }
    edit.endEditBlock();

    m_detailEdit->setFocus();
    syncFormatBar();
}

void TranslationComposePage::insertLink()
{
    QTextCursor cursor = m_detailEdit->textCursor();
    const QTextCharFormat current = m_detailEdit->currentCharFormat();
    const QString existing = current.isAnchor() ? current.anchorHref() : QString();

    bool ok = false;
    QString url = AppInputDialog::getText(
        this, tr("插入链接"), tr("链接地址，例如 https://example.com"),
        QLineEdit::Normal, existing, &ok);
    if (!ok)
        return;
    url = url.trimmed();
    if (url.isEmpty())
        return;
    if (!url.contains(QStringLiteral("://")) && !url.startsWith(QStringLiteral("mailto:")))
        url.prepend(QStringLiteral("https://"));

    QTextCharFormat linkFormat;
    linkFormat.setAnchor(true);
    linkFormat.setAnchorHref(url);

    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(linkFormat);
    } else {
        // 没选文字时直接把网址插成链接，省得先打一遍再回头加
        cursor.insertText(url, linkFormat);
        m_detailEdit->setTextCursor(cursor);
    }
    m_detailEdit->setFocus();
    syncFormatBar();
}

void TranslationComposePage::clearFormatting()
{
    QTextDocument *doc = m_detailEdit->document();
    const QTextCursor work = expandToBlocks(m_detailEdit->textCursor());
    const int start = work.selectionStart();
    const int end = work.selectionEnd();

    QTextCharFormat plain;
    plain.setFontWeight(QFont::Normal);
    plain.setFontItalic(false);
    plain.setFontUnderline(false);
    plain.setFontStrikeOut(false);
    plain.setFontPointSize(bodyPointSize());
    plain.setAnchor(false);

    QTextCursor edit(doc);
    edit.beginEditBlock();

    QTextBlock block = doc->findBlock(start);
    const QTextBlock lastBlock = doc->findBlock(qMax(start, end - 1));
    while (block.isValid()) {
        // 段落：退出列表、去掉缩进与居中
        if (QTextList *list = block.textList())
            list->remove(block);
        {
            QTextCursor blockCursor(block);
            QTextBlockFormat blockFormat = block.blockFormat();
            blockFormat.setIndent(0);
            blockFormat.setAlignment(Qt::AlignLeft);
            blockCursor.setBlockFormat(blockFormat);
        }

        // 文字：按片段复位格式。图片那一段整段跳过，别把预填的图片清掉
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.charFormat().isImageFormat())
                continue;
            QTextCursor fragmentCursor(doc);
            fragmentCursor.setPosition(fragment.position());
            fragmentCursor.setPosition(fragment.position() + fragment.length(),
                                      QTextCursor::KeepAnchor);
            fragmentCursor.setCharFormat(plain);
        }

        if (block == lastBlock)
            break;
        block = block.next();
    }
    edit.endEditBlock();

    // 没有选区时才顺手把「接下来输入的字」也复位；有选区时不能调
    // setCurrentCharFormat —— 它会再作用一遍选区，把图片这类对象字符一起清掉
    if (!m_detailEdit->textCursor().hasSelection())
        m_detailEdit->setCurrentCharFormat(plain);
    m_detailEdit->setFocus();
    syncFormatBar();
}

void TranslationComposePage::syncFormatBar()
{
    if (!m_detailEdit || !m_boldButton || !m_styleCombo)
        return;

    const QTextCharFormat format = m_detailEdit->currentCharFormat();
    m_boldButton->setChecked(format.fontWeight() >= QFont::DemiBold);
    m_italicButton->setChecked(format.fontItalic());
    m_underlineButton->setChecked(format.fontUnderline());
    m_strikeButton->setChecked(format.fontStrikeOut());

    const QTextList *list = m_detailEdit->textCursor().block().textList();
    const int listStyle = list ? list->format().style() : QTextListFormat::ListStyleUndefined;
    m_bulletButton->setChecked(listStyle == QTextListFormat::ListDisc
                               || listStyle == QTextListFormat::ListCircle
                               || listStyle == QTextListFormat::ListSquare);
    m_numberButton->setChecked(listStyle == QTextListFormat::ListDecimal
                               || listStyle == QTextListFormat::ListLowerAlpha
                               || listStyle == QTextListFormat::ListUpperAlpha
                               || listStyle == QTextListFormat::ListLowerRoman
                               || listStyle == QTextListFormat::ListUpperRoman);

    int level = 0;
    const qreal size = format.fontPointSize();
    const qreal body = bodyPointSize();
    if (size > 0 && body > 0) {
        if (size >= body * 1.5)
            level = 2;
        else if (size >= body * 1.15)
            level = 1;
    }
    m_styleCombo->setCurrentIndex(level);
}

qreal TranslationComposePage::bodyPointSize() const
{
    const QFont defaultFont = m_detailEdit->document()->defaultFont();
    if (defaultFont.pointSizeF() > 0)
        return defaultFont.pointSizeF();
    if (defaultFont.pixelSize() > 0)
        return defaultFont.pixelSize() * 72.0 / 96.0;
    return 10.0;
}
