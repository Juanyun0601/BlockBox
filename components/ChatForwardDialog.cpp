/**
 * @file   ChatForwardDialog.cpp
 * @brief  转发消息弹窗实现
 * @author BlockBox Team
 * @date   2026-11-14
 */
#include "ChatForwardDialog.h"

#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "chatForwardCard";

/** 列表行里存的目标：f12 / g3，与聊天页的会话 key 同款 */
QString targetKey(bool group, int id)
{
    return (group ? QStringLiteral("g") : QStringLiteral("f")) + QString::number(id);
}

} // namespace

bool ChatForwardDialog::pick(QWidget *parent, const QVector<ForumFriendInfo> &friends,
                             const QVector<ForumGroupInfo> &groups, const QString &preview,
                             ChatForwardTarget &target)
{
    if (friends.isEmpty() && groups.isEmpty())
    {
        NotificationManager::showInfo(parent, tr("还没有可以转发的好友或群聊"));
        return false;
    }
    ChatForwardDialog dialog(friends, groups, preview, parent);
    if (dialog.exec() != QDialog::Accepted || !dialog.m_selected.isValid())
        return false;
    target = dialog.m_selected;
    return true;
}

ChatForwardDialog::ChatForwardDialog(const QVector<ForumFriendInfo> &friends,
                                     const QVector<ForumGroupInfo> &groups,
                                     const QString &preview, QWidget *parent)
    : AppDialogBase(parent)
    , m_friends(friends)
    , m_groups(groups)
    , m_preview(preview)
{
    setObjectName(QStringLiteral("chatForwardDialog"));
    setWindowTitle(tr("转发消息"));

    initUI();
    initStyle();
    rebuildRows();
}

// ───────────────────────────── UI ─────────────────────────────

void ChatForwardDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setMinimumSize(440, 520);
    card->setMaximumSize(520, 660);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    // 标题
    auto *title = new QLabel(tr("转发消息"), card);
    title->setObjectName(QStringLiteral("forwardTitle"));
    cardLayout->addWidget(title);

    // 被转发的消息预览
    auto *preview = new QLabel(card);
    preview->setObjectName(QStringLiteral("forwardPreview"));
    preview->setWordWrap(true);
    preview->setText(m_preview);
    cardLayout->addWidget(preview);

    // 搜索
    m_searchEdit = new QLineEdit(card);
    m_searchEdit->setObjectName(QStringLiteral("forwardSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索好友或群聊"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildRows(); });
    cardLayout->addWidget(m_searchEdit);

    // 会话列表
    m_list = new QListWidget(card);
    m_list->setObjectName(QStringLiteral("forwardTargetList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        m_forwardButton->setEnabled(!selected.isEmpty());
        if (selected.isEmpty())
        {
            m_selected = {};
            return;
        }
        const QString key = selected.first()->data(Qt::UserRole).toString();
        selectTarget(key.startsWith(QLatin1Char('g')), key.mid(1).toInt());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        const QString key = item->data(Qt::UserRole).toString();
        selectTarget(key.startsWith(QLatin1Char('g')), key.mid(1).toInt());
        if (m_selected.isValid())
            accept();
    });
    cardLayout->addWidget(m_list, 1);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("forwardStatus"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    // 底部按钮
    auto *footer = new QWidget(card);
    auto *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(2, 4, 2, 2);
    footerRow->setSpacing(8);
    auto *cancel = new QPushButton(tr("取消"), footer);
    cancel->setObjectName(QStringLiteral("forwardCancelButton"));
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footerRow->addWidget(cancel);
    footerRow->addStretch(1);
    m_forwardButton = new QPushButton(tr("转发"), footer);
    m_forwardButton->setObjectName(QStringLiteral("forwardPrimaryButton"));
    m_forwardButton->setCursor(Qt::PointingHandCursor);
    m_forwardButton->setEnabled(false);
    connect(m_forwardButton, &QPushButton::clicked, this, [this]() { accept(); });
    footerRow->addWidget(m_forwardButton);
    cardLayout->addWidget(footer);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void ChatForwardDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg    = isLight ? "rgba(15,23,42,0.06)" : "rgba(255,255,255,0.06)";
    const QString selBg      = isLight ? "rgba(16,185,129,0.14)" : "rgba(16,185,129,0.20)";
    const QString listBg     = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#forwardTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#forwardPreview {"
        "    color: %6; font-size: 12px; background: %7;"
        "    border: 1px solid %8; border-radius: 10px; padding: 8px 10px;"
        "}"
        "QLineEdit#forwardSearchEdit {"
        "    background: %7; border: 1px solid %8; border-radius: 9px;"
        "    color: %9; padding: 7px 10px; font-size: 12.5px;"
        "}"
        "QListWidget#forwardTargetList {"
        "    background-color: %10; border: 1px solid %8; border-radius: 10px;"
        "    color: %9; font-size: 12.5px; outline: none; padding: 4px;"
        "}"
        "QListWidget#forwardTargetList::item { border-radius: 7px; padding: 6px 8px; min-height: 38px; }"
        "QListWidget#forwardTargetList::item:hover { background-color: %11; }"
        "QListWidget#forwardTargetList::item:selected { background-color: %12; }"
        "QLabel#forwardStatus { color: %13; font-size: 11.5px; background: transparent; }"
        "QPushButton#forwardCancelButton {"
        "    background: transparent; border: 1px solid %8; border-radius: 9px;"
        "    color: %6; padding: 7px 18px; font-size: 12.5px;"
        "}"
        "QPushButton#forwardCancelButton:hover { background: %11; }"
        "QPushButton#forwardPrimaryButton {"
        "    background: %5; border: none; border-radius: 9px;"
        "    color: #ffffff; padding: 7px 22px; font-size: 12.5px; font-weight: 600;"
        "}"
        "QPushButton#forwardPrimaryButton:disabled { background: %11; color: %13; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          subColor, fieldBg, fieldBorder, textColor, listBg, hoverBg, selBg, muted);

    setStyleSheet(style);
}

// ───────────────────────────── 会话列表 ─────────────────────────────

bool ChatForwardDialog::matchesFilter(const QString &name, const QString &subtitle) const
{
    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (keyword.isEmpty())
        return true;
    return name.contains(keyword, Qt::CaseInsensitive) ||
           subtitle.contains(keyword, Qt::CaseInsensitive);
}

void ChatForwardDialog::selectTarget(bool group, int id)
{
    m_selected = {};
    if (group)
    {
        for (const ForumGroupInfo &groupInfo : m_groups)
        {
            if (groupInfo.id == id)
            {
                m_selected = {true, id, groupInfo.name};
                break;
            }
        }
    }
    else
    {
        for (const ForumFriendInfo &friendInfo : m_friends)
        {
            if (friendInfo.id == id)
            {
                m_selected = {false, id, friendInfo.displayName};
                break;
            }
        }
    }
}

void ChatForwardDialog::rebuildRows()
{
    m_list->clear();
    int shown = 0;

    auto addRow = [this, &shown](bool group, int id, const QString &name,
                                 const QString &subtitle, QWidget *avatar) {
        QListWidgetItem *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, targetKey(group, id));
        item->setSizeHint(QSize(0, 52));

        auto *row = new QWidget(m_list);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(avatar);

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(2);
        auto *nameLabel = new ForumKit::ElidedLabel(row);
        nameLabel->setObjectName(QStringLiteral("forwardTargetName"));
        nameLabel->setFullText(name);
        textColumn->addWidget(nameLabel);
        auto *subLabel = new QLabel(subtitle, row);
        subLabel->setObjectName(QStringLiteral("forwardTargetSub"));
        textColumn->addWidget(subLabel);
        rowLayout->addLayout(textColumn, 1);

        m_list->addItem(item);
        m_list->setItemWidget(item, row);
        ++shown;
    };

    // 好友：官方客服放最前面，其余保持页面里的顺序
    auto addFriendRows = [this, &addRow](bool officialOnly) {
        for (const ForumFriendInfo &friendInfo : m_friends)
        {
            if (friendInfo.official != officialOnly)
                continue;
            const QString subtitle =
                friendInfo.official
                    ? tr("官方客服 · 随时提问")
                    : (friendInfo.online ? tr("好友 · 在线") : tr("好友 · 离线"));
            if (!matchesFilter(friendInfo.displayName, subtitle))
                continue;
            auto *avatar = friendInfo.official
                               ? ForumKit::makeOfficialAvatar(m_list, 34)
                               : ForumKit::makeAvatar(m_list, friendInfo.avatar,
                                                      friendInfo.displayName, 34);
            addRow(false, friendInfo.id, friendInfo.displayName, subtitle, avatar);
        }
    };
    addFriendRows(true);
    addFriendRows(false);

    // 群聊：官方群排在最前面
    auto addGroupRows = [this, &addRow](bool officialOnly) {
        for (const ForumGroupInfo &groupInfo : m_groups)
        {
            if (groupInfo.official != officialOnly)
                continue;
            const QString subtitle = groupInfo.official
                                         ? tr("官方群聊 · %1 人").arg(groupInfo.memberCount)
                                         : tr("群聊 · %1 人").arg(groupInfo.memberCount);
            if (!matchesFilter(groupInfo.name, subtitle))
                continue;
            auto *avatar = groupInfo.official
                               ? ForumKit::makeOfficialAvatar(m_list, 34)
                               : ForumKit::makeGroupAvatar(m_list, groupInfo.avatar,
                                                           groupInfo.name, 34);
            addRow(true, groupInfo.id, groupInfo.name, subtitle, avatar);
        }
    };
    addGroupRows(true);
    addGroupRows(false);

    m_forwardButton->setEnabled(false);
    if (shown == 0)
        m_statusLabel->setText(tr("没有找到匹配的会话"));
    else
        m_statusLabel->setText(tr("选择一位好友或一个群聊，把这条消息转发过去"));
}
