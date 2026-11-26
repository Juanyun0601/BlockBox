/**
 * @file   ForumTagEditor.cpp
 * @brief  标签编辑器实现：胶囊标签 + 输入框 + 推荐标签
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "ForumTagEditor.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "layouts/FlowLayout.h"

namespace {

/** 最近用过的标签在推荐里显示几个 / 本机记住几个 */
constexpr int kRecentTags = 8;
/** 推荐区一次最多摆几个，避免把发布页撑得太长 */
constexpr int kMaxSuggestions = 8;
/** 临时提示停留多久（毫秒） */
constexpr int kHintHoldMs = 2600;

/** 一次清空 FlowLayout 里的控件：先摘下来再 deleteLater，避免在按钮自己的点击回调里析构它 */
void clearFlow(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

/** 标签分隔符：逗号 / 中文逗号 / 顿号 / 分号 / 空白，粘贴一整串时按它拆开 */
const QRegularExpression &tagSplitter()
{
    static const QRegularExpression re(QStringLiteral("[\\s,，、;；]+"));
    return re;
}

/** 去掉首尾空白与开头的 #，与服务端的 normalizeTag 口径一致 */
QString normalizeTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('#')))
        tag.remove(0, 1);
    return tag.trimmed();
}

/** 按字数截断；正好切在 emoji 的半个代理对上时把尾巴削掉，别留乱码 */
QString trimmedToLength(QString text, int maxLength)
{
    if (maxLength <= 0 || text.size() <= maxLength)
        return text;
    text = text.left(maxLength);
    if (!text.isEmpty() && text.at(text.size() - 1).isHighSurrogate())
        text.chop(1);
    return text;
}

/** 重新套一遍样式表：动态属性（focused / warn / full）变了要 re-polish 才生效 */
void repolish(QWidget *widget)
{
    if (!widget || !widget->style())
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

ForumTagEditor::ForumTagEditor(QWidget *parent) : QWidget(parent)
{
    // 上次发布用过的标签：排在推荐最前面，常发的版本号不用每次都重敲
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    m_recentTags = settings.value(QStringLiteral("forum/recentTags")).toStringList();

    buildUi();
    rebuildChips();
    updateCounter();
    updateInputState();
    rebuildSuggestions();
    refreshHint();
}

void ForumTagEditor::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // ── 标题行：标签 + 已选数量 ──
    auto *headRow = new QHBoxLayout();
    headRow->setContentsMargins(0, 0, 0, 0);
    headRow->setSpacing(8);
    auto *title = new QLabel(tr("标签"), this);
    title->setObjectName(QStringLiteral("forumMuted"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    m_counterLabel = new QLabel(this);
    m_counterLabel->setObjectName(QStringLiteral("forumTagCounter"));
    headRow->addWidget(m_counterLabel);
    layout->addLayout(headRow);

    // ── 输入区：胶囊和输入框共用一圈边框，看起来像一个完整的标签框 ──
    m_field = new QFrame(this);
    m_field->setObjectName(QStringLiteral("forumTagField"));
    m_field->setAttribute(Qt::WA_StyledBackground, true);
    // 点标签框的空白处也把光标放进输入框，不用非得点中那一条细线
    m_field->installEventFilter(this);
    auto *fieldLayout = new QVBoxLayout(m_field);
    fieldLayout->setContentsMargins(10, 7, 10, 7);
    fieldLayout->setSpacing(6);

    m_chipsHost = new QWidget(m_field);
    m_chipsHost->setAttribute(Qt::WA_StyledBackground, false);
    m_chipsLayout = new FlowLayout(m_chipsHost, 0, 6, 6);
    fieldLayout->addWidget(m_chipsHost);

    m_input = new QLineEdit(m_field);
    m_input->setObjectName(QStringLiteral("forumTagFieldInput"));
    m_input->setFrame(false);
    m_input->setMinimumHeight(26);
    m_placeholder = tr("输入标签后按回车或逗号添加，例如：优化");
    m_input->setPlaceholderText(m_placeholder);
    m_input->setToolTip(tr("按回车、逗号或空格确认；粘贴一整串标签会自动拆开"));
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ForumTagEditor::commitPending);
    connect(m_input, &QLineEdit::textChanged, this, &ForumTagEditor::rebuildSuggestions);
    fieldLayout->addWidget(m_input);
    layout->addWidget(m_field);

    // ── 推荐区：最近用过 / 当前类型常用 / 论坛热门，点一下直接加 ──
    m_suggestHost = new QWidget(this);
    m_suggestHost->setAttribute(Qt::WA_StyledBackground, false);
    m_suggestLayout = new FlowLayout(m_suggestHost, 0, 6, 6);
    layout->addWidget(m_suggestHost);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("forumTagHint"));
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    m_hintTimer = new QTimer(this);
    m_hintTimer->setSingleShot(true);
    m_hintTimer->setInterval(kHintHoldMs);
    connect(m_hintTimer, &QTimer::timeout, this, &ForumTagEditor::refreshHint);

    // Tab 切到这个控件时光标直接落在输入框里
    setFocusProxy(m_input);
}

void ForumTagEditor::setMaxTags(int max)
{
    m_maxTags = qMax(1, max);
    updateCounter();
    updateInputState();
    rebuildSuggestions();
    refreshHint();
}

void ForumTagEditor::setMaxTagLength(int length)
{
    m_maxTagLength = qMax(1, length);
    refreshHint();
}

void ForumTagEditor::setTags(const QStringList &tags)
{
    m_tags.clear();
    for (const QString &raw : tags)
    {
        const QStringList parts = raw.split(tagSplitter(), Qt::SkipEmptyParts);
        for (const QString &part : parts)
        {
            const QString tag = trimmedToLength(normalizeTag(part), m_maxTagLength);
            if (tag.isEmpty() || containsTag(tag) || m_tags.size() >= m_maxTags)
                continue;
            m_tags.append(tag);
        }
    }
    rebuildChips();
    updateCounter();
    updateInputState();
    rebuildSuggestions();
}

void ForumTagEditor::clear()
{
    m_tags.clear();
    {
        const QSignalBlocker blocker(m_input);
        m_input->clear();
    }
    rebuildChips();
    updateCounter();
    updateInputState();
    rebuildSuggestions();
    refreshHint();
}

void ForumTagEditor::setRecommendedTags(const QStringList &tags)
{
    m_recommendedTags = tags;
    rebuildSuggestions();
}

void ForumTagEditor::setPopularTags(const QVector<QPair<QString, int>> &tags)
{
    m_popularTags = tags;
    rebuildSuggestions();
}

void ForumTagEditor::rememberTags()
{
    if (m_tags.isEmpty())
        return;

    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    QStringList merged = m_tags;
    for (const QString &tag : settings.value(QStringLiteral("forum/recentTags")).toStringList())
    {
        bool exists = false;
        for (const QString &kept : merged)
        {
            if (kept.compare(tag, Qt::CaseInsensitive) == 0)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
            merged.append(tag);
    }
    while (merged.size() > kRecentTags)
        merged.removeLast();
    settings.setValue(QStringLiteral("forum/recentTags"), merged);
    m_recentTags = merged;
    rebuildSuggestions();
}

void ForumTagEditor::commitPending()
{
    const QString text = m_input->text();
    if (text.trimmed().isEmpty())
        return;

    {
        // 清空输入框时别再触发一次推荐刷新，下面 addParsedTags 末尾会统一刷
        const QSignalBlocker blocker(m_input);
        m_input->clear();
    }
    addParsedTags(text);
}

void ForumTagEditor::addParsedTags(const QString &text)
{
    bool changed = false;
    bool truncated = false;
    bool full = false;
    QString duplicateTag;

    const QStringList parts = text.split(tagSplitter(), Qt::SkipEmptyParts);
    for (const QString &part : parts)
    {
        QString tag = normalizeTag(part);
        if (tag.isEmpty())
            continue;
        if (tag.size() > m_maxTagLength)
        {
            tag = trimmedToLength(tag, m_maxTagLength);
            truncated = true;
        }
        if (containsTag(tag))
        {
            duplicateTag = tag;
            continue;
        }
        if (m_tags.size() >= m_maxTags)
        {
            full = true;
            break;
        }
        m_tags.append(tag);
        changed = true;
    }

    if (full)
        showTransientHint(tr("最多 %1 个标签，先点掉一个再加吧").arg(m_maxTags));
    else if (truncated)
        showTransientHint(tr("每个标签最多 %1 个字，超出的部分已经剪掉").arg(m_maxTagLength));
    else if (!changed && !duplicateTag.isEmpty())
        showTransientHint(tr("「%1」已经加过了").arg(duplicateTag));

    if (changed)
    {
        rebuildChips();
        updateCounter();
        updateInputState();
        emit tagsChanged();
    }
    rebuildSuggestions();
}

void ForumTagEditor::removeTag(int index)
{
    if (index < 0 || index >= m_tags.size())
        return;
    m_tags.removeAt(index);
    rebuildChips();
    updateCounter();
    updateInputState();
    rebuildSuggestions();
    emit tagsChanged();
}

void ForumTagEditor::rebuildChips()
{
    clearFlow(m_chipsLayout);
    for (int index = 0; index < m_tags.size(); ++index)
    {
        auto *chip = new QPushButton(QStringLiteral("# %1  ×").arg(m_tags.at(index)), m_chipsHost);
        chip->setObjectName(QStringLiteral("forumTagEditorChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(24);
        chip->setToolTip(tr("点击移除「%1」").arg(m_tags.at(index)));
        connect(chip, &QPushButton::clicked, this, [this, index]() { removeTag(index); });
        m_chipsLayout->addWidget(chip);
    }
    m_chipsHost->setVisible(!m_tags.isEmpty());
    m_chipsHost->updateGeometry();
}

void ForumTagEditor::rebuildSuggestions()
{
    clearFlow(m_suggestLayout);

    // 标签满了就不再摆推荐，避免点了没反应
    if (m_tags.size() >= m_maxTags)
    {
        m_suggestHost->setVisible(false);
        return;
    }

    const QString filter = filterText();
    int shown = 0;

    // 正在输入的那段文字：第一个推荐就是把它的内容加成标签
    if (!filter.isEmpty() && !containsTag(filter))
    {
        auto *addChip = new QPushButton(
            tr("＋ 添加「%1」").arg(trimmedToLength(filter, m_maxTagLength)), m_suggestHost);
        addChip->setObjectName(QStringLiteral("forumTagSuggest"));
        addChip->setCursor(Qt::PointingHandCursor);
        addChip->setFixedHeight(24);
        addChip->setToolTip(tr("把输入框里的文字加成标签"));
        connect(addChip, &QPushButton::clicked, this, [this]() {
            commitPending();
            m_input->setFocus();
        });
        m_suggestLayout->addWidget(addChip);
        ++shown;
    }

    // 先筛出真正要摆出来的候选，没有候选就不显示光秃秃的「推荐」两个字
    QVector<QPair<QString, int>> visible;
    for (const auto &candidate : suggestionCandidates())
    {
        if (shown + visible.size() >= kMaxSuggestions || containsTag(candidate.first))
            continue;
        if (!filter.isEmpty()
            && !candidate.first.contains(filter, Qt::CaseInsensitive))
            continue;
        visible.append(candidate);
    }

    if (!visible.isEmpty())
    {
        auto *label = new QLabel(tr("推荐"), m_suggestHost);
        label->setObjectName(QStringLiteral("forumMuted"));
        label->setFixedHeight(24);
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_suggestLayout->addWidget(label);
    }

    for (const auto &candidate : visible)
    {
        auto *chip = new QPushButton(QStringLiteral("＋ %1").arg(candidate.first), m_suggestHost);
        chip->setObjectName(QStringLiteral("forumTagSuggest"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(24);
        chip->setToolTip(candidate.second > 0
                             ? tr("论坛已有 %1 个主题用了这个标签").arg(candidate.second)
                             : tr("点击添加「%1」").arg(candidate.first));
        const QString tag = candidate.first;
        connect(chip, &QPushButton::clicked, this, [this, tag]() {
            {
                const QSignalBlocker blocker(m_input);
                m_input->clear();
            }
            addParsedTags(tag);
            m_input->setFocus();
        });
        m_suggestLayout->addWidget(chip);
        ++shown;
    }

    m_suggestHost->setVisible(shown > 0);
    m_suggestHost->updateGeometry();
}

QVector<QPair<QString, int>> ForumTagEditor::suggestionCandidates() const
{
    QVector<QPair<QString, int>> candidates;
    const auto appendUnique = [this, &candidates](const QString &tag, int count) {
        const QString normalized = trimmedToLength(normalizeTag(tag), m_maxTagLength);
        if (normalized.isEmpty())
            return;
        for (const auto &item : candidates)
        {
            if (item.first.compare(normalized, Qt::CaseInsensitive) == 0)
                return;
        }
        candidates.append(qMakePair(normalized, count));
    };

    // 最近用过的排最前，其次是当前发布类型的常用标签，最后是论坛热门
    for (const QString &tag : m_recentTags)
        appendUnique(tag, 0);
    for (const QString &tag : m_recommendedTags)
        appendUnique(tag, 0);
    for (const auto &item : m_popularTags)
        appendUnique(item.first, item.second);
    return candidates;
}

QString ForumTagEditor::filterText() const
{
    const QString separators = QStringLiteral(",，、;；");
    const QString text = m_input->text();
    int cut = -1;
    for (int index = 0; index < text.size(); ++index)
    {
        const QChar ch = text.at(index);
        if (ch.isSpace() || separators.contains(ch))
            cut = index;
    }
    return text.mid(cut + 1).trimmed();
}

bool ForumTagEditor::containsTag(const QString &tag) const
{
    for (const QString &existing : m_tags)
    {
        if (existing.compare(tag, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void ForumTagEditor::updateCounter()
{
    m_counterLabel->setText(QStringLiteral("%1 / %2").arg(m_tags.size()).arg(m_maxTags));
    m_counterLabel->setProperty("full", m_tags.size() >= m_maxTags);
    repolish(m_counterLabel);
}

void ForumTagEditor::updateInputState()
{
    const bool full = m_tags.size() >= m_maxTags;
    m_input->setReadOnly(full);
    m_input->setPlaceholderText(full ? tr("已有 %1 个标签，先点掉一个再加").arg(m_maxTags)
                                     : m_placeholder);
}

void ForumTagEditor::refreshHint()
{
    if (m_hintTimer)
        m_hintTimer->stop();
    setHintWarning(false);
    m_hintLabel->setText(tr("按回车或逗号确认；每个最多 %1 个字、最多 %2 个，点标签可移除。"
                            "标签会显示在帖子上，点它能看到同类主题。")
                             .arg(m_maxTagLength)
                             .arg(m_maxTags));
}

void ForumTagEditor::showTransientHint(const QString &text)
{
    setHintWarning(true);
    m_hintLabel->setText(text);
    m_hintTimer->start();
}

void ForumTagEditor::setHintWarning(bool warn)
{
    if (m_hintLabel->property("warn").toBool() == warn)
        return;
    m_hintLabel->setProperty("warn", warn);
    repolish(m_hintLabel);
}

void ForumTagEditor::applyFrameFocus(bool focused)
{
    if (!m_field)
        return;
    m_field->setProperty("focused", focused);
    repolish(m_field);
}

bool ForumTagEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::MouseButtonPress)
    {
        m_input->setFocus();
        return true;
    }
    if (watched != m_input)
        return QWidget::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::InputMethod:
    {
        // 记下输入法是否正在选词：选词时空格 / 回车属于输入法，不能拿来提交标签
        auto *imeEvent = static_cast<QInputMethodEvent *>(event);
        m_composing = !imeEvent->preeditString().isEmpty();
        break;
    }
    case QEvent::KeyPress:
    {
        if (m_composing)
            break;
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const int key = keyEvent->key();
        const QString typed = keyEvent->text();

        // 回车 / 逗号 / 顿号 / 分号：把当前输入收成标签。
        // 空格不做拦截——中文输入法还在选词时，空格可能是选字键。
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Comma
            || typed == QStringLiteral("，") || typed == QStringLiteral("、")
            || typed == QStringLiteral("；"))
        {
            commitPending();
            return true;
        }
        // 输入框空着时按退格：撤掉最后一个标签
        if (key == Qt::Key_Backspace && m_input->text().isEmpty() && !m_tags.isEmpty())
        {
            removeTag(m_tags.size() - 1);
            return true;
        }
        if (key == Qt::Key_Escape && !m_input->text().isEmpty())
        {
            m_input->clear();
            return true;
        }
        break;
    }
    case QEvent::FocusIn:
        applyFrameFocus(true);
        break;
    case QEvent::FocusOut:
        m_composing = false;
        applyFrameFocus(false);
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
