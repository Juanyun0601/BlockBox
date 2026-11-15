/**
 * @file   ForumLinkRowsEditor.cpp
 * @brief  相关链接编辑器实现
 * @author BlockBox Team
 * @date   2026-11-15
 */

#include "ForumLinkRowsEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

/** 每个资源最多绑定的链接条数（服务端同样限制 6 条） */
constexpr int kMaxLinks = 6;

/** 预设链接：名称 + 地址输入框的占位提示 */
QVector<std::pair<QString, QString>> presets()
{
    return {
        { QObject::tr("官网"), QObject::tr("https://你的官网地址") },
        { QObject::tr("GitHub 仓库"), QObject::tr("https://github.com/用户名/仓库") },
        { QObject::tr("反馈问题"), QObject::tr("https://github.com/用户名/仓库/issues") },
    };
}

/** 统一链接地址：没有协议头时按最常见的 https:// 处理 */
QString normalizeUrl(QString url)
{
    url = url.trimmed();
    if (url.isEmpty())
        return url;
    const QString lowered = url.toLower();
    if (!lowered.startsWith(QLatin1String("http://")) && !lowered.startsWith(QLatin1String("https://")))
        url.prepend(QStringLiteral("https://"));
    return url;
}

} // namespace

ForumLinkRowsEditor::ForumLinkRowsEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    buildChips();
    layout->addWidget(m_chipsHost);

    // 链接行都塞进这个容器，加一行 / 删一行只动它
    auto *rowsHost = new QWidget(this);
    rowsHost->setAttribute(Qt::WA_StyledBackground, false);
    m_rowsLayout = new QVBoxLayout(rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(6);
    layout->addWidget(rowsHost);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("forumMuted"));
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);
    refreshHint();
}

void ForumLinkRowsEditor::buildChips()
{
    m_chipsHost = new QWidget(this);
    m_chipsHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *chipsLayout = new QHBoxLayout(m_chipsHost);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(8);

    // 预设芯片：点一下加一行（名称已填好只补地址），再点一下撤掉这一行
    for (const auto &preset : presets())
    {
        auto *chip = new QPushButton(preset.first, m_chipsHost);
        chip->setObjectName(QStringLiteral("forumSortButton"));
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(26);
        chip->setToolTip(tr("点一下添加「%1」链接，再点一下撤销").arg(preset.first));
        connect(chip, &QPushButton::toggled, this, [this, chip, preset](bool checked) {
            if (checked)
                addRow(preset.first, preset.second, QString(), chip);
            else
            {
                // 用户手动取消勾选 = 撤销对应的预设行
                for (const LinkRow &row : m_rows)
                {
                    if (row.presetChip == chip)
                    {
                        removeRowByHost(row.host);
                        return;
                    }
                }
            }
        });
        chipsLayout->addWidget(chip);
        m_presetChips.append(chip);
    }

    auto *customChip = new QPushButton(tr("自定义链接"), m_chipsHost);
    customChip->setObjectName(QStringLiteral("forumSortButton"));
    customChip->setCursor(Qt::PointingHandCursor);
    customChip->setFixedHeight(26);
    customChip->setToolTip(tr("自己起名字，填任意网址"));
    connect(customChip, &QPushButton::clicked, this,
            [this]() { addRow(QString(), tr("https://")); });
    chipsLayout->addWidget(customChip);
    m_customChip = customChip;

    chipsLayout->addStretch(1);
}

void ForumLinkRowsEditor::addRow(const QString &presetLabel, const QString &placeholder,
                                 const QString &url, QPushButton *chip)
{
    if (m_rows.size() >= kMaxLinks)
        return;

    LinkRow row;
    row.presetLabel = presetLabel;
    row.presetChip = chip;
    row.host = new QWidget(this);
    row.host->setAttribute(Qt::WA_StyledBackground, false);
    auto *rowLayout = new QHBoxLayout(row.host);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    row.labelEdit = new QLineEdit(row.host);
    row.labelEdit->setObjectName(QStringLiteral("forumField"));
    row.labelEdit->setMaxLength(20);
    row.labelEdit->setFixedWidth(128);
    row.labelEdit->setPlaceholderText(presetLabel.isEmpty() ? tr("链接名称") : presetLabel);
    if (!presetLabel.isEmpty())
        row.labelEdit->setText(presetLabel);
    rowLayout->addWidget(row.labelEdit);

    row.urlEdit = new QLineEdit(row.host);
    row.urlEdit->setObjectName(QStringLiteral("forumField"));
    row.urlEdit->setMaxLength(500);
    row.urlEdit->setPlaceholderText(placeholder);
    if (!url.isEmpty())
        row.urlEdit->setText(url);
    rowLayout->addWidget(row.urlEdit, 1);

    row.removeButton = new QPushButton(tr("移除"), row.host);
    row.removeButton->setObjectName(QStringLiteral("forumGhostButton"));
    row.removeButton->setCursor(Qt::PointingHandCursor);
    row.removeButton->setFixedHeight(26);
    // 按稳定的 host 指针找行，避免往 m_rows 里追加新行时旧指针失效
    connect(row.removeButton, &QPushButton::clicked, this,
            [this, host = row.host]() { removeRowByHost(host); });
    rowLayout->addWidget(row.removeButton);

    m_rows.append(row);
    m_rowsLayout->addWidget(row.host);
    updateChips();
    refreshHint();
    if (!url.isEmpty())
        row.urlEdit->setFocus();
    else
        row.labelEdit->setFocus();
}

void ForumLinkRowsEditor::removeRowByHost(QWidget *host)
{
    if (!host)
        return;
    for (int i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows.at(i).host != host)
            continue;
        // 预设行要同步取消芯片勾选；阻断信号防止 toggled 再走一遍删除
        if (m_rows.at(i).presetChip)
        {
            const QSignalBlocker blocker(m_rows.at(i).presetChip);
            m_rows.at(i).presetChip->setChecked(false);
        }
        m_rowsLayout->removeWidget(host);
        host->deleteLater();
        m_rows.removeAt(i);
        break;
    }
    updateChips();
    refreshHint();
}

void ForumLinkRowsEditor::updateChips()
{
    const bool full = m_rows.size() >= kMaxLinks;
    if (m_customChip)
        m_customChip->setEnabled(!full);
    for (QPushButton *chip : m_presetChips)
    {
        if (chip)
            chip->setEnabled(!full);
    }
}

void ForumLinkRowsEditor::refreshHint()
{
    if (!m_hintLabel)
        return;
    m_hintLabel->setText(m_rows.isEmpty()
                             ? tr("可绑定官网、代码仓库、反馈入口等，展示在帖子详情页（可选，最多 6 条）")
                             : tr("已添加 %1/%2 条，地址留空的行发布时会被忽略")
                                   .arg(m_rows.size())
                                   .arg(kMaxLinks));
}

void ForumLinkRowsEditor::setLinks(const QVector<ForumResourceLink> &links)
{
    // 先清空现有行（预设行会被连带取消勾选，芯片状态保持一致）
    while (!m_rows.isEmpty())
        removeRowByHost(m_rows.first().host);

    const QVector<std::pair<QString, QString>> presetList = presets();
    for (const ForumResourceLink &link : links)
    {
        if (m_rows.size() >= kMaxLinks)
            break;
        // 名称撞上预设就复用对应的芯片（勾选状态保持一致），否则当自定义行铺开
        QString placeholder = tr("https://");
        QPushButton *matchedChip = nullptr;
        for (int i = 0; i < presetList.size(); ++i)
        {
            if (presetList.at(i).first == link.label)
            {
                placeholder = presetList.at(i).second;
                matchedChip = m_presetChips.value(i);
                break;
            }
        }
        addRow(link.label, placeholder, link.url, matchedChip);
    }
}

QVector<ForumResourceLink> ForumLinkRowsEditor::links() const
{
    QVector<ForumResourceLink> result;
    for (const LinkRow &row : m_rows)
    {
        ForumResourceLink link;
        const QString label = row.labelEdit->text().trimmed();
        link.label = label.isEmpty() ? (row.presetLabel.isEmpty() ? tr("链接") : row.presetLabel)
                                     : label.left(20);
        link.url = normalizeUrl(row.urlEdit->text());
        if (link.isValid())
            result.append(link);
        if (result.size() >= kMaxLinks)
            break;
    }
    return result;
}
