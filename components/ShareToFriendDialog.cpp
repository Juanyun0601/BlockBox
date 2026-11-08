/**
 * @file   ShareToFriendDialog.cpp
 * @brief  分享给好友弹窗实现
 * @author BlockBox Team
 * @date   2026-11-07
 */
#include "ShareToFriendDialog.h"

#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "shareFriendCard";

} // namespace

bool ShareToFriendDialog::share(QWidget *parent, const QString &title, const QString &url,
                                const QString &summary)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(parent, tr("分享给好友需要先登录论坛账号"));
        return false;
    }

    ShareToFriendDialog dialog(title, url, summary, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    return true;
}

ShareToFriendDialog::ShareToFriendDialog(const QString &title, const QString &url,
                                         const QString &summary, QWidget *parent)
    : AppDialogBase(parent)
    , m_title(title)
    , m_url(url)
    , m_summary(summary)
{
    setObjectName(QStringLiteral("shareToFriendDialog"));
    setWindowTitle(tr("分享给好友"));

    initUI();
    initStyle();

    m_statusLabel->setText(tr("正在获取好友列表…"));
    ForumChatService *service = ForumChatService::instance();
    service->fetchFriends(
        [this](const ForumFriendSnapshot &snapshot) {
            m_friends = snapshot.friends;
            rebuildFriendRows();
            if (m_friends.isEmpty())
                m_statusLabel->setText(tr("还没有好友，去社区认识朋友吧"));
            else
                m_statusLabel->setText(tr("选择一位好友，把这条分享用私信发出去"));
        },
        [this](const QString &message, int) {
            m_statusLabel->setText(tr("好友列表获取失败：%1").arg(message));
        });
}

// ───────────────────────────── UI ─────────────────────────────

void ShareToFriendDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setMinimumSize(460, 560);
    card->setMaximumSize(520, 700);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    // ── 标题栏 ──
    QWidget *header = new QWidget(card);
    QHBoxLayout *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(2, 2, 2, 2);
    headerRow->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("shareHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/share.svg"),
                                                ThemeManager::instance()->currentThemeColor(),
                                                16).pixmap(16, 16));
    headerRow->addWidget(icon);

    QLabel *title = new QLabel(tr("分享给好友"), header);
    title->setObjectName(QStringLiteral("shareTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    // ── 分享内容预览 ──
    QLabel *preview = new QLabel(card);
    preview->setObjectName(QStringLiteral("sharePreview"));
    preview->setWordWrap(true);
    preview->setText(m_summary.isEmpty() ? m_title
                                         : QStringLiteral("%1 · %2").arg(m_summary, m_title));
    cardLayout->addWidget(preview);

    // ── 搜索框 ──
    m_searchEdit = new QLineEdit(card);
    m_searchEdit->setObjectName(QStringLiteral("shareSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索好友"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { rebuildFriendRows(); });
    cardLayout->addWidget(m_searchEdit);

    // ── 好友列表 ──
    m_friendList = new QListWidget(card);
    m_friendList->setObjectName(QStringLiteral("shareFriendList"));
    m_friendList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_friendList->setUniformItemSizes(false);
    connect(m_friendList, &QListWidget::itemSelectionChanged, this, [this]() {
        QList<QListWidgetItem *> selected = m_friendList->selectedItems();
        if (selected.isEmpty())
        {
            m_selected = ForumFriendInfo();
            return;
        }
        const int peerId = selected.first()->data(Qt::UserRole).toInt();
        for (const ForumFriendInfo &friendInfo : m_friends)
        {
            if (friendInfo.id == peerId)
            {
                m_selected = friendInfo;
                break;
            }
        }
    });
    connect(m_friendList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (m_selected.isValid())
            sendToPeer(m_selected);
    });
    cardLayout->addWidget(m_friendList, 1);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("shareStatus"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    // ── 底部按钮 ──
    QWidget *footer = new QWidget(card);
    QHBoxLayout *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(2, 4, 2, 2);
    footerRow->setSpacing(8);
    m_sendBtn = new QPushButton(tr("发送分享"), footer);
    m_sendBtn->setObjectName(QStringLiteral("primaryButton"));
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setEnabled(false);
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
        if (m_selected.isValid())
            sendToPeer(m_selected);
    });
    footerRow->addStretch();
    footerRow->addWidget(m_sendBtn);
    cardLayout->addWidget(footer);

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);

    // 选择变化时刷新发送按钮可用状态
    connect(m_friendList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_sendBtn->setEnabled(m_selected.isValid() && !m_sent);
    });
}

void ShareToFriendDialog::initStyle()
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
        "QLabel#shareTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#shareHeaderIcon { background: %5; border-radius: 9px; }"
        "QLabel#sharePreview {"
        "    color: %6; font-size: 12px; background: %7;"
        "    border: 1px solid %8; border-radius: 10px; padding: 8px 10px;"
        "}"
        "QLineEdit#shareSearchEdit {"
        "    background: %7; border: 1px solid %8; border-radius: 9px;"
        "    color: %9; padding: 7px 10px; font-size: 12.5px;"
        "}"
        "QListWidget#shareFriendList {"
        "    background-color: %10; border: 1px solid %8; border-radius: 10px;"
        "    color: %9; font-size: 12.5px; outline: none; padding: 4px;"
        "}"
        "QListWidget#shareFriendList::item {"
        "    border-radius: 7px; padding: 8px 10px; min-height: 30px;"
        "}"
        "QListWidget#shareFriendList::item:hover { background-color: %11; }"
        "QListWidget#shareFriendList::item:selected {"
        "    background-color: %12; color: %4;"
        "}"
        "QLabel#shareStatus { color: %13; font-size: 11.5px; background: transparent; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          subColor, fieldBg, fieldBorder, textColor, listBg, hoverBg, selBg, muted);

    setStyleSheet(style);
}

// ───────────────────────────── 好友列表 ─────────────────────────────

void ShareToFriendDialog::rebuildFriendRows()
{
    const qint64 selectedId = m_selected.id;
    m_friendList->clear();

    for (const ForumFriendInfo &friendInfo : m_friends)
    {
        if (!matchesFilter(friendInfo))
            continue;

        // 用小控件行展示头像 + 昵称 + 在线状态
        const QString onlineMark = friendInfo.online ? QStringLiteral("● 在线")
                                                     : QStringLiteral("○ 离线");

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
        name->setObjectName(QStringLiteral("shareFriendName"));
        name->setFullText(friendInfo.displayName.isEmpty() ? friendInfo.username
                                                           : friendInfo.displayName);
        textColumn->addWidget(name);
        auto *account = new QLabel(row);
        account->setObjectName(QStringLiteral("shareFriendAccount"));
        account->setText(QStringLiteral("@%1").arg(friendInfo.username));
        textColumn->addWidget(account);
        rowLayout->addLayout(textColumn, 1);

        auto *online = new QLabel(row);
        online->setObjectName(QStringLiteral("shareFriendOnline"));
        online->setText(onlineMark);
        online->setStyleSheet(StyleKit::resolve(
            friendInfo.online ? QStringLiteral("color: @SUCCESS@; font-size: 11px;")
                              : QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        rowLayout->addWidget(online);

        // QListWidget 行内容：把整行控件塞进 item
        m_friendList->addItem(item);
        m_friendList->setItemWidget(item, row);

        if (friendInfo.id == selectedId)
            item->setSelected(true);
    }

    if (m_friendList->count() == 0 && !m_friends.isEmpty())
        m_statusLabel->setText(tr("没有匹配「%1」的好友").arg(m_searchEdit->text()));
}

bool ShareToFriendDialog::matchesFilter(const ForumFriendInfo &friendInfo) const
{
    const QString keyword = m_searchEdit->text().trimmed();
    if (keyword.isEmpty())
        return true;
    return friendInfo.displayName.contains(keyword, Qt::CaseInsensitive) ||
           friendInfo.username.contains(keyword, Qt::CaseInsensitive);
}

// ───────────────────────────── 发送 ─────────────────────────────

void ShareToFriendDialog::sendToPeer(const ForumFriendInfo &peer)
{
    if (m_sent)
        return;

    QString body = QStringLiteral("【分享】%1").arg(m_title);
    if (!m_url.isEmpty())
        body += QStringLiteral("\n%1").arg(m_url);

    m_sendBtn->setEnabled(false);
    m_statusLabel->setText(tr("正在发送给 %1 …").arg(peer.displayName.isEmpty()
                                                        ? peer.username
                                                        : peer.displayName));

    ForumChatService::instance()->sendMessage(
        peer.id, body, 0,
        [this, peer](const ForumChatMessage &) {
            m_sent = true;
            const QString peerName = peer.displayName.isEmpty() ? peer.username
                                                                : peer.displayName;
            NotificationManager::showSuccess(this, tr("已分享给 %1").arg(peerName));
            accept();
        },
        [this](const QString &message, int) {
            m_sendBtn->setEnabled(m_selected.isValid());
            m_statusLabel->setText(tr("发送失败：%1").arg(message));
        });
}
