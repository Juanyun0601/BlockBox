/**
 * @file   CreateGroupDialog.cpp
 * @brief  发起群聊弹窗实现
 * @author BlockBox Team
 * @date   2026-11-08
 */
#include "CreateGroupDialog.h"

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

const char kCardName[] = "createGroupCard";
/** 一次最多邀请多少位好友（服务端上限 50 人含群主） */
constexpr int kMaxInvitees = 49;

} // namespace

bool CreateGroupDialog::create(QWidget *parent,
                               const std::function<void(const ForumGroupInfo &)> &onCreated)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(parent, QObject::tr("发起群聊需要先登录论坛账号"));
        return false;
    }

    CreateGroupDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    // 建群成功才会 accept，把新群交给页面直接打开会话
    if (onCreated && dialog.m_lastCreated.isValid())
        onCreated(dialog.m_lastCreated);
    return true;
}

CreateGroupDialog::CreateGroupDialog(QWidget *parent) : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("createGroupDialog"));
    setWindowTitle(tr("发起群聊"));

    initUI();
    initStyle();

    m_statusLabel->setText(tr("正在获取好友列表…"));
    ForumChatService *service = ForumChatService::instance();
    service->fetchFriends(
        [this](const ForumFriendSnapshot &snapshot) {
            m_friends = snapshot.friends;
            rebuildFriendRows();
            if (m_friends.isEmpty())
                m_statusLabel->setText(tr("还没有好友，先去「添加好友」找一个吧"));
            else
                m_statusLabel->setText(tr("点好友勾进群里，建好后大家都能在群里聊天"));
        },
        [this](const QString &message, int) {
            m_statusLabel->setText(tr("好友列表获取失败：%1").arg(message));
        });
}

// ───────────────────────────── UI ─────────────────────────────

void CreateGroupDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setMinimumSize(460, 580);
    card->setMaximumSize(520, 720);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    // ── 标题栏 ──
    QWidget *header = new QWidget(card);
    QHBoxLayout *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(2, 2, 2, 2);
    headerRow->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("groupHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setText(QStringLiteral("👥"));
    headerRow->addWidget(icon);

    QLabel *title = new QLabel(tr("发起群聊"), header);
    title->setObjectName(QStringLiteral("groupTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    // ── 群名称 ──
    m_nameEdit = new QLineEdit(card);
    m_nameEdit->setObjectName(QStringLiteral("groupNameEdit"));
    m_nameEdit->setPlaceholderText(tr("群名称（最多 20 字）"));
    m_nameEdit->setMaxLength(20);
    m_nameEdit->setClearButtonEnabled(true);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { refreshConfirmState(); });
    cardLayout->addWidget(m_nameEdit);

    // ── 搜索框 ──
    m_searchEdit = new QLineEdit(card);
    m_searchEdit->setObjectName(QStringLiteral("groupSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索好友"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildFriendRows(); });
    cardLayout->addWidget(m_searchEdit);

    // ── 好友列表：点行勾选 / 取消 ──
    m_friendList = new QListWidget(card);
    m_friendList->setObjectName(QStringLiteral("groupFriendList"));
    m_friendList->setSelectionMode(QAbstractItemView::NoSelection);
    m_friendList->setUniformItemSizes(false);
    connect(m_friendList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int friendId = item->data(Qt::UserRole).toInt();
        if (friendId <= 0 || m_creating)
            return;
        if (m_selectedIds.contains(friendId))
            m_selectedIds.remove(friendId);
        else if (m_selectedIds.size() < kMaxInvitees)
            m_selectedIds.insert(friendId);
        else
            NotificationManager::showInfo(this, tr("一个群最多 %1 位成员（含你）").arg(kMaxInvitees + 1));
        rebuildFriendRows();
        refreshConfirmState();
    });
    cardLayout->addWidget(m_friendList, 1);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("groupStatus"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    // ── 底部按钮 ──
    QWidget *footer = new QWidget(card);
    QHBoxLayout *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(2, 4, 2, 2);
    footerRow->setSpacing(8);
    m_countLabel = new QLabel(footer);
    m_countLabel->setObjectName(QStringLiteral("groupCount"));
    footerRow->addWidget(m_countLabel);
    footerRow->addStretch();
    m_confirmBtn = new QPushButton(tr("创建群聊"), footer);
    m_confirmBtn->setObjectName(QStringLiteral("primaryButton"));
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setEnabled(false);
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() { submit(); });
    footerRow->addWidget(m_confirmBtn);
    cardLayout->addWidget(footer);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);

    refreshConfirmState();
}

void CreateGroupDialog::initStyle()
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
    const QString listBg     = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#groupTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#groupHeaderIcon { background: %5; border-radius: 9px; font-size: 16px; }"
        "QLineEdit#groupNameEdit, QLineEdit#groupSearchEdit {"
        "    background: %6; border: 1px solid %7; border-radius: 9px;"
        "    color: %8; padding: 7px 10px; font-size: 12.5px;"
        "}"
        "QListWidget#groupFriendList {"
        "    background-color: %9; border: 1px solid %7; border-radius: 10px;"
        "    color: %8; font-size: 12.5px; outline: none; padding: 4px;"
        "}"
        "QListWidget#groupFriendList::item {"
        "    border-radius: 7px; padding: 8px 10px; min-height: 30px;"
        "}"
        "QListWidget#groupFriendList::item:hover { background-color: %10; }"
        "QLabel#groupStatus { color: %11; font-size: 11.5px; background: transparent; }"
        "QLabel#groupCount { color: %12; font-size: 11.5px; background: transparent; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          fieldBg, fieldBorder, textColor, listBg, hoverBg, muted, subColor);

    setStyleSheet(style);
}

// ───────────────────────────── 好友列表 ─────────────────────────────

void CreateGroupDialog::rebuildFriendRows()
{
    m_friendList->clear();

    for (const ForumFriendInfo &friendInfo : m_friends)
    {
        if (!matchesFilter(friendInfo))
            continue;

        const bool picked = m_selectedIds.contains(friendInfo.id);
        QListWidgetItem *item = new QListWidgetItem(m_friendList);
        item->setData(Qt::UserRole, friendInfo.id);
        item->setSizeHint(QSize(0, 46));

        auto *row = new QWidget(m_friendList);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(ForumKit::makeAvatar(row, friendInfo.avatar,
                                                  friendInfo.displayName, 34));

        QVBoxLayout *textColumn = new QVBoxLayout();
        textColumn->setSpacing(2);
        auto *name = new ForumKit::ElidedLabel(row);
        name->setObjectName(QStringLiteral("groupFriendName"));
        name->setFullText(friendInfo.displayName.isEmpty() ? friendInfo.username
                                                           : friendInfo.displayName);
        textColumn->addWidget(name);
        auto *account = new QLabel(row);
        account->setObjectName(QStringLiteral("groupFriendAccount"));
        account->setText(QStringLiteral("@%1").arg(friendInfo.username));
        textColumn->addWidget(account);
        rowLayout->addLayout(textColumn, 1);

        // 勾选标记：选中行右侧亮一个 ✓
        auto *mark = new QLabel(row);
        mark->setObjectName(QStringLiteral("groupFriendPick"));
        mark->setText(picked ? QStringLiteral("✓") : QStringLiteral(""));
        mark->setStyleSheet(StyleKit::resolve(picked
            ? QStringLiteral("color: @SUCCESS@; font-size: 15px; font-weight: 700;")
            : QStringLiteral("color: transparent; font-size: 15px;")));
        rowLayout->addWidget(mark);
        if (picked)
        {
            // 选中行铺一层淡色底，一眼看清勾了谁（与下面 initStyle 的 pickedBg 同款）
            const bool isLight = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme);
            row->setObjectName(QStringLiteral("groupPickedRow"));
            row->setStyleSheet(QStringLiteral(
                "QWidget#groupPickedRow { background: %1; border-radius: 7px; }")
                                   .arg(isLight ? "rgba(16,185,129,0.12)" : "rgba(16,185,129,0.18)"));
        }

        m_friendList->addItem(item);
        m_friendList->setItemWidget(item, row);
    }

    if (m_friendList->count() == 0 && !m_friends.isEmpty())
        m_statusLabel->setText(tr("没有匹配「%1」的好友").arg(m_searchEdit->text()));
}

bool CreateGroupDialog::matchesFilter(const ForumFriendInfo &friendInfo) const
{
    const QString keyword = m_searchEdit->text().trimmed();
    if (keyword.isEmpty())
        return true;
    return friendInfo.displayName.contains(keyword, Qt::CaseInsensitive) ||
           friendInfo.username.contains(keyword, Qt::CaseInsensitive);
}

void CreateGroupDialog::refreshConfirmState()
{
    const bool nameOk = !m_nameEdit->text().trimmed().isEmpty();
    const bool pickedOk = !m_selectedIds.isEmpty();
    m_countLabel->setText(m_selectedIds.isEmpty()
                              ? tr("还没挑人")
                              : tr("已选 %1 / %2 人").arg(m_selectedIds.size()).arg(kMaxInvitees));
    m_confirmBtn->setEnabled(nameOk && pickedOk && !m_creating);
}

// ───────────────────────────── 建群 ─────────────────────────────

void CreateGroupDialog::submit()
{
    if (m_creating)
        return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || m_selectedIds.isEmpty())
        return;

    m_creating = true;
    m_confirmBtn->setEnabled(false);
    m_statusLabel->setText(tr("正在创建群聊…"));

    QVector<int> memberIds(m_selectedIds.cbegin(), m_selectedIds.cend());
    ForumChatService::instance()->createGroup(
        name, memberIds,
        [this](const ForumGroupInfo &group) {
            m_lastCreated = group;
            NotificationManager::showSuccess(this, tr("群聊「%1」创建成功").arg(group.name));
            accept();
        },
        [this](const QString &message, int) {
            m_creating = false;
            refreshConfirmState();
            m_statusLabel->setText(tr("创建失败：%1").arg(message));
        });
}
