/**
 * @file   CreateGroupDialog.cpp
 * @brief  发起群聊 / 邀请好友进群弹窗实现
 * @author BlockBox Team
 * @date   2026-11-08
 */
#include "CreateGroupDialog.h"

#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "createGroupCard";
/** 一个群的成员上限（含群主），与服务端一致 */
constexpr int kMaxGroupMembers = 50;
/** 建群时一次最多邀请多少位好友（服务端上限 50 人含群主） */
constexpr int kMaxInvitees = kMaxGroupMembers - 1;

/** 可选的内置群图标：建群弹窗里点一下就能选中 */
const QStringList &groupIconChoices()
{
    static const QStringList choices = {
        QStringLiteral("👥"), QStringLiteral("🎮"), QStringLiteral("🏰"), QStringLiteral("⚔️"),
        QStringLiteral("🔥"), QStringLiteral("⭐"), QStringLiteral("🌸"), QStringLiteral("🍀"),
        QStringLiteral("🐉"), QStringLiteral("🐺"), QStringLiteral("🦊"), QStringLiteral("🐱"),
        QStringLiteral("🍕"), QStringLiteral("🎵"), QStringLiteral("📚"), QStringLiteral("💎"),
        QStringLiteral("🚀"), QStringLiteral("🌙"), QStringLiteral("⚡"), QStringLiteral("🎨"),
        QStringLiteral("🏆"), QStringLiteral("🌈"), QStringLiteral("🍉"), QStringLiteral("🧊"),
    };
    return choices;
}

/** 本地图片裁成圆形预览（与群头像的圆形外观一致） */
QPixmap roundedIconPixmap(const QPixmap &source, int size)
{
    if (source.isNull())
        return QPixmap();
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);
    const QPixmap scaled = source.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    painter.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return out;
}

/**
 * 群图标选择弹窗：24 个内置表情点一下直接选中，也可以选一张本地图片
 * （点「创建群聊」时才上传），或者恢复成「群名首字」。
 * 不声明 Q_OBJECT：只用 lambda，不需要信号槽。
 */
class GroupIconPickerDialog : public AppDialogBase
{
public:
    GroupIconPickerDialog(const QString &currentIcon, QWidget *parent)
        : AppDialogBase(parent)
        , m_icon(currentIcon)
    {
        setObjectName(QStringLiteral("groupIconPickerDialog"));
        setWindowTitle(QObject::tr("选择群图标"));
        initUI();
    }

    /** 选中的表情或已上传的图片地址（空 = 用群名首字） */
    QString icon() const { return m_icon; }
    /** 用户挑的本地图片（非空时由调用方在建群前上传） */
    QString imagePath() const { return m_imagePath; }

private:
    void initUI()
    {
        QWidget *card = new QWidget(this);
        card->setObjectName(QStringLiteral("groupIconPickerCard"));
        card->setMinimumWidth(376);

        QVBoxLayout *layout = new QVBoxLayout(card);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(10);

        QLabel *title = new QLabel(QObject::tr("选择群图标"), card);
        title->setStyleSheet(StyleKit::cardTitle());
        layout->addWidget(title);

        QLabel *hint = new QLabel(QObject::tr("点一个表情，或者传一张自己的图片；不选就按群名首字显示。"), card);
        hint->setStyleSheet(StyleKit::mutedLabel());
        hint->setWordWrap(true);
        layout->addWidget(hint);

        QGridLayout *grid = new QGridLayout();
        grid->setSpacing(6);
        const QStringList choices = groupIconChoices();
        for (int i = 0; i < choices.size(); ++i)
        {
            const QString emoji = choices.at(i);
            auto *button = new QPushButton(emoji, card);
            button->setObjectName(QStringLiteral("groupIconChoice"));
            button->setFixedSize(46, 42);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(QObject::tr("用这个图标"));
            connect(button, &QPushButton::clicked, this, [this, emoji]() {
                m_icon = emoji;
                m_imagePath.clear();
                accept();
            });
            grid->addWidget(button, i / 6, i % 6);
        }
        layout->addLayout(grid);

        QHBoxLayout *actions = new QHBoxLayout();
        actions->setSpacing(8);
        auto *upload = new QPushButton(QObject::tr("上传图片…"), card);
        upload->setObjectName(QStringLiteral("groupIconPick"));
        upload->setCursor(Qt::PointingHandCursor);
        connect(upload, &QPushButton::clicked, this, [this]() { pickLocalImage(); });
        actions->addWidget(upload);

        auto *reset = new QPushButton(QObject::tr("用群名首字"), card);
        reset->setObjectName(QStringLiteral("groupIconPick"));
        reset->setCursor(Qt::PointingHandCursor);
        connect(reset, &QPushButton::clicked, this, [this]() {
            m_icon.clear();
            m_imagePath.clear();
            accept();
        });
        actions->addWidget(reset);
        actions->addStretch(1);

        auto *cancel = new QPushButton(QObject::tr("取消"), card);
        cancel->setObjectName(QStringLiteral("groupIconPick"));
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, [this]() { reject(); });
        actions->addWidget(cancel);
        layout->addLayout(actions);

        QGridLayout *main = new QGridLayout(this);
        main->setContentsMargins(18, 18, 18, 18);
        main->addWidget(card, 0, 0);

        setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QDialog { background-color: transparent; border: none; }"
            "QWidget#groupIconPickerCard {"
            "    background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 16px;"
            "}"
            "QPushButton#groupIconChoice {"
            "    background: transparent; border: 1px solid transparent; border-radius: 10px;"
            "    font-size: 20px; padding: 0;"
            "}"
            "QPushButton#groupIconChoice:hover { background: @BG_HOVER@; border-color: @BORDER@; }"
            "QPushButton#groupIconPick {"
            "    background: transparent; border: 1px solid @BORDER@; border-radius: 8px;"
            "    color: @TEXT_SECONDARY@; padding: 5px 12px; font-size: 12px;"
            "}"
            "QPushButton#groupIconPick:hover { background: @BG_HOVER@; }")));
    }

    void pickLocalImage()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, QObject::tr("选择群图标"), QString(),
            QObject::tr("图片 (*.png *.jpg *.jpeg *.webp *.bmp);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (reader.read().isNull())
        {
            NotificationManager::showInfo(this, QObject::tr("打不开这张图片，换一张 PNG / JPG 试试"));
            return;
        }
        m_imagePath = path;
        m_icon.clear();
        accept();
    }

    QString m_icon;
    QString m_imagePath;
};

} // namespace

bool CreateGroupDialog::create(QWidget *parent,
                               const std::function<void(const ForumGroupInfo &)> &onCreated,
                               int preselectFriendId)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(parent, QObject::tr("发起群聊需要先登录论坛账号"));
        return false;
    }

    CreateGroupDialog dialog(parent);
    dialog.m_preselectId = preselectFriendId;
    if (dialog.exec() != QDialog::Accepted)
        return false;
    // 建群成功才会 accept，把新群交给页面直接打开会话
    if (onCreated && dialog.m_lastCreated.isValid())
        onCreated(dialog.m_lastCreated);
    return true;
}

bool CreateGroupDialog::invite(QWidget *parent, const ForumGroupInfo &group,
                               const QVector<int> &existingMemberIds,
                               const std::function<void()> &onInvited)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(parent, QObject::tr("邀请好友需要先登录论坛账号"));
        return false;
    }

    CreateGroupDialog dialog(parent, group, existingMemberIds);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (onInvited)
        onInvited();
    return true;
}

CreateGroupDialog::CreateGroupDialog(QWidget *parent) : AppDialogBase(parent)
{
    m_capacity = kMaxInvitees;
    setObjectName(QStringLiteral("createGroupDialog"));
    setWindowTitle(tr("发起群聊"));

    initUI();
    initStyle();

    m_statusLabel->setText(tr("正在获取好友列表…"));
    ForumChatService *service = ForumChatService::instance();
    service->fetchFriends(
        [this](const ForumFriendSnapshot &snapshot) {
            m_friends = snapshot.friends;
            applyPreselect();
            rebuildFriendRows();
            suggestGroupName();
            if (m_friends.isEmpty())
                m_statusLabel->setText(tr("还没有好友，先去「添加好友」找一个吧"));
            else
                m_statusLabel->setText(tr("点好友勾进群里，建好后大家都能在群里聊天"));
        },
        [this](const QString &message, int) {
            m_statusLabel->setText(tr("好友列表获取失败：%1").arg(message));
        });
}

CreateGroupDialog::CreateGroupDialog(QWidget *parent, const ForumGroupInfo &group,
                                     const QVector<int> &existingMemberIds)
    : AppDialogBase(parent)
    , m_inviteMode(true)
    , m_group(group)
    , m_existingIds(existingMemberIds)
    , m_capacity(qBound(0, kMaxGroupMembers - qMax(0, group.memberCount), kMaxInvitees))
{
    setObjectName(QStringLiteral("createGroupDialog"));
    setWindowTitle(tr("邀请好友进群"));

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
                m_statusLabel->setText(tr("点好友勾进群里，点「邀请进群」他们就来了"));
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

    QLabel *title = new QLabel(m_inviteMode ? tr("邀请好友进群") : tr("发起群聊"), header);
    title->setObjectName(QStringLiteral("groupTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    // ── 群名称（邀请模式不用起名，藏掉） ──
    m_nameEdit = new QLineEdit(card);
    m_nameEdit->setObjectName(QStringLiteral("groupNameEdit"));
    m_nameEdit->setPlaceholderText(tr("群名称（最多 20 字）"));
    m_nameEdit->setMaxLength(20);
    m_nameEdit->setClearButtonEnabled(true);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() {
        refreshConfirmState();
        refreshIconPreview();
    });
    // textEdited 只在用户亲手输入时触发：自动起名用的 setText 不会点亮这个标记
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this]() { m_nameTouched = true; });
    if (m_inviteMode)
        m_nameEdit->hide();
    cardLayout->addWidget(m_nameEdit);

    // ── 群图标：预览 + 说明 + 「选择图标」（邀请模式没有） ──
    QWidget *iconRow = new QWidget(card);
    QHBoxLayout *iconRowLayout = new QHBoxLayout(iconRow);
    iconRowLayout->setContentsMargins(2, 0, 2, 0);
    iconRowLayout->setSpacing(10);

    m_iconSlot = new ForumKit::ClickableCard(iconRow);
    m_iconSlot->setObjectName(QStringLiteral("groupIconSlot"));
    m_iconSlot->setFixedSize(46, 46);
    m_iconSlot->setToolTip(tr("点这里换群图标"));
    auto *slotLayout = new QVBoxLayout(m_iconSlot);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    slotLayout->setAlignment(Qt::AlignCenter);
    m_iconSlot->onClick = [this]() { openIconPicker(); };
    iconRowLayout->addWidget(m_iconSlot);

    QVBoxLayout *iconText = new QVBoxLayout();
    iconText->setSpacing(1);
    auto *iconTitle = new QLabel(tr("群图标"), iconRow);
    iconTitle->setObjectName(QStringLiteral("groupIconTitle"));
    iconText->addWidget(iconTitle);
    m_iconHint = new QLabel(iconRow);
    m_iconHint->setObjectName(QStringLiteral("groupIconHint"));
    m_iconHint->setWordWrap(true);
    iconText->addWidget(m_iconHint);
    iconRowLayout->addLayout(iconText, 1);

    auto *pickIconBtn = new QPushButton(tr("选择图标"), iconRow);
    pickIconBtn->setObjectName(QStringLiteral("groupIconPick"));
    pickIconBtn->setCursor(Qt::PointingHandCursor);
    connect(pickIconBtn, &QPushButton::clicked, this, [this]() { openIconPicker(); });
    iconRowLayout->addWidget(pickIconBtn);

    iconRow->setVisible(!m_inviteMode);
    cardLayout->addWidget(iconRow);
    refreshIconPreview();

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
        else if (m_selectedIds.size() < m_capacity)
            m_selectedIds.insert(friendId);
        else
            NotificationManager::showInfo(this, tr("一个群最多 %1 位成员（含你）").arg(m_capacity + 1));
        rebuildFriendRows();
        refreshConfirmState();
        suggestGroupName();
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
    m_confirmBtn = new QPushButton(m_inviteMode ? tr("邀请进群") : tr("创建群聊"), footer);
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
        "QLabel#groupIconTitle { color: %4; font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QLabel#groupIconHint { color: %11; font-size: 11.5px; background: transparent; }"
        "QPushButton#groupIconPick {"
        "    background: transparent; border: 1px solid %7; border-radius: 8px;"
        "    color: %12; padding: 5px 12px; font-size: 12px;"
        "}"
        "QPushButton#groupIconPick:hover { background: %10; }"
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
        // 邀请模式：已经在群里的人不再出现
        if (m_inviteMode && m_existingIds.contains(friendInfo.id))
            continue;
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
    {
        if (m_inviteMode && m_searchEdit->text().trimmed().isEmpty())
            m_statusLabel->setText(tr("你的好友都已经在群里了"));
        else
            m_statusLabel->setText(tr("没有匹配「%1」的好友").arg(m_searchEdit->text()));
    }
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
    const bool nameOk = m_inviteMode || !m_nameEdit->text().trimmed().isEmpty();
    const bool pickedOk = !m_selectedIds.isEmpty();
    if (m_inviteMode)
    {
        m_countLabel->setText(m_selectedIds.isEmpty()
                                  ? tr("还能邀请 %1 位好友").arg(m_capacity)
                                  : tr("已选 %1 人，还能邀 %2 位")
                                        .arg(m_selectedIds.size())
                                        .arg(m_capacity - m_selectedIds.size()));
    }
    else
    {
        m_countLabel->setText(m_selectedIds.isEmpty()
                                  ? tr("还没挑人")
                                  : tr("已选 %1 / %2 人").arg(m_selectedIds.size()).arg(m_capacity));
    }
    m_confirmBtn->setEnabled(nameOk && pickedOk && !m_creating);
}

void CreateGroupDialog::applyPreselect()
{
    if (m_preselectId <= 0 || m_selectedIds.contains(m_preselectId))
        return;
    if (m_selectedIds.size() < m_capacity)
        m_selectedIds.insert(m_preselectId);
    refreshConfirmState();
}

void CreateGroupDialog::suggestGroupName()
{
    if (m_inviteMode || m_nameTouched || !m_nameEdit->text().trimmed().isEmpty())
        return;
    QStringList names;
    for (const ForumFriendInfo &friendInfo : m_friends)
    {
        if (!m_selectedIds.contains(friendInfo.id))
            continue;
        names << (friendInfo.displayName.isEmpty() ? friendInfo.username : friendInfo.displayName);
        if (names.size() == 3)
            break;
    }
    if (names.isEmpty())
        return;
    QString name = names.join(QStringLiteral("、"));
    if (m_selectedIds.size() > 3)
        name += tr("等%1人的群聊").arg(m_selectedIds.size());
    else
        name += tr("的群聊");
    m_nameEdit->setText(name);
}

// ───────────────────────────── 群图标 ─────────────────────────────

void CreateGroupDialog::openIconPicker()
{
    if (m_creating)
        return;
    GroupIconPickerDialog picker(m_icon, this);
    if (picker.exec() != QDialog::Accepted)
        return;

    if (!picker.imagePath().isEmpty())
    {
        // 本地图片先留在 m_iconPath，点「创建群聊」时才上传
        m_iconPath = picker.imagePath();
        m_icon.clear();
    }
    else
    {
        m_icon = picker.icon();
        m_iconPath.clear();
    }
    refreshIconPreview();
}

void CreateGroupDialog::refreshIconPreview()
{
    if (!m_iconSlot || !m_iconHint || !m_iconSlot->layout())
        return;
    ForumKit::clearLayout(m_iconSlot->layout());

    if (!m_iconPath.isEmpty())
    {
        auto *preview = new QLabel(m_iconSlot);
        preview->setFixedSize(46, 46);
        preview->setAlignment(Qt::AlignCenter);
        preview->setPixmap(roundedIconPixmap(QPixmap(m_iconPath), 46));
        m_iconSlot->layout()->addWidget(preview);
        m_iconHint->setText(tr("已选一张本地图片，建群时自动上传"));
        return;
    }

    m_iconSlot->layout()->addWidget(
        ForumKit::makeGroupAvatar(m_iconSlot, m_icon, m_nameEdit->text().trimmed(), 46));
    if (m_icon.isEmpty())
        m_iconHint->setText(tr("默认用群名首字，点这里换个图标"));
    else if (m_icon.startsWith(QLatin1Char('/')) || m_icon.startsWith(QStringLiteral("http")))
        m_iconHint->setText(tr("已选一张自定义图片"));
    else
        m_iconHint->setText(tr("已选图标 %1").arg(m_icon));
}

// ───────────────────────────── 建群 / 邀请 ─────────────────────────────

void CreateGroupDialog::submit()
{
    if (m_creating)
        return;

    // ── 邀请模式：把勾中的人拉进已有群 ──
    if (m_inviteMode)
    {
        if (m_selectedIds.isEmpty())
            return;
        m_creating = true;
        m_confirmBtn->setEnabled(false);
        m_statusLabel->setText(tr("正在邀请…"));

        QVector<int> memberIds(m_selectedIds.cbegin(), m_selectedIds.cend());
        ForumChatService::instance()->inviteGroupMembers(
            m_group.id, memberIds,
            [this](int added, int) {
                NotificationManager::showSuccess(this, tr("已邀请 %1 位好友进群").arg(added));
                accept();
            },
            [this](const QString &message, int) {
                m_creating = false;
                refreshConfirmState();
                m_statusLabel->setText(tr("邀请失败：%1").arg(message));
            });
        return;
    }

    // ── 建群模式 ──
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || m_selectedIds.isEmpty())
        return;

    m_creating = true;
    m_confirmBtn->setEnabled(false);

    QVector<int> memberIds(m_selectedIds.cbegin(), m_selectedIds.cend());
    auto createWithIcon = [this, name, memberIds](const QString &icon) {
        ForumChatService::instance()->createGroup(
            name, icon, memberIds,
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
    };

    // 选了本地图片：先传图床拿站内地址，传完再带着图标建群
    if (!m_iconPath.isEmpty())
    {
        m_statusLabel->setText(tr("正在上传群图标…"));
        ForumChatService::instance()->uploadGroupIcon(
            m_iconPath, createWithIcon,
            [this](const QString &message, int) {
                m_creating = false;
                refreshConfirmState();
                m_statusLabel->setText(tr("群图标上传失败：%1").arg(message));
            });
        return;
    }

    m_statusLabel->setText(tr("正在创建群聊…"));
    createWithIcon(m_icon);
}
