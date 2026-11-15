/**
 * @file   ChatBookmarkDialog.cpp
 * @brief  聊天收藏列表实现
 * @author BlockBox Team
 * @date   2026-11-14
 */
#include "ChatBookmarkDialog.h"

#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/ChatBookmarkStore.h"
#include "utils/DownloadUtils.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "chatBookmarkCard";

/** 收藏行右侧的「打开 / 删除」按钮 */
QPushButton *makeRowButton(QWidget *parent, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("bookmarkRowButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(24);
    return button;
}

} // namespace

void ChatBookmarkDialog::show(QWidget *parent, std::function<void(const QString &)> onOpen)
{
    ChatBookmarkDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!dialog.m_openKey.isEmpty() && onOpen)
        onOpen(dialog.m_openKey);
}

ChatBookmarkDialog::ChatBookmarkDialog(QWidget *parent) : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("chatBookmarkDialog"));
    setWindowTitle(tr("聊天收藏"));

    initUI();
    initStyle();

    connect(ChatBookmarkStore::instance(), &ChatBookmarkStore::changed, this,
            &ChatBookmarkDialog::rebuild);
    rebuild();
}

// ───────────────────────────── UI ─────────────────────────────

void ChatBookmarkDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setMinimumSize(500, 480);
    card->setMaximumSize(620, 700);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    auto *title = new QLabel(tr("聊天收藏"), card);
    title->setObjectName(QStringLiteral("bookmarkTitle"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    m_countLabel = new QLabel(card);
    m_countLabel->setObjectName(QStringLiteral("bookmarkCount"));
    titleRow->addWidget(m_countLabel);
    cardLayout->addLayout(titleRow);

    auto *hint = new QLabel(tr("收藏只保存在这台设备上；对方撤回消息后，收藏里的内容仍然保留。"), card);
    hint->setObjectName(QStringLiteral("bookmarkHint"));
    hint->setWordWrap(true);
    cardLayout->addWidget(hint);

    m_scroll = new QScrollArea(card);
    m_scroll->setObjectName(QStringLiteral("bookmarkScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *container = new QWidget(m_scroll);
    container->setObjectName(QStringLiteral("bookmarkContainer"));
    m_listLayout = new QVBoxLayout(container);
    m_listLayout->setContentsMargins(2, 2, 2, 2);
    m_listLayout->setSpacing(6);
    m_scroll->setWidget(container);
    cardLayout->addWidget(m_scroll, 1);

    m_emptyLabel = new QLabel(card);
    m_emptyLabel->setObjectName(QStringLiteral("bookmarkEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setText(tr("还没有收藏过消息。\n在聊天里右键一条消息，选「收藏消息」就能存到这儿。"));
    m_emptyLabel->hide();
    cardLayout->addWidget(m_emptyLabel);

    auto *footer = new QWidget(card);
    auto *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(2, 4, 2, 2);
    footerRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), footer);
    closeButton->setObjectName(QStringLiteral("bookmarkCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footerRow->addWidget(closeButton);
    cardLayout->addWidget(footer);

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void ChatBookmarkDialog::initStyle()
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
    const QString rowBg      = isLight ? "#fafafa" : "#38383d";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#bookmarkTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#bookmarkCount { color: %7; font-size: 11.5px; background: transparent; }"
        "QLabel#bookmarkHint { color: %13; font-size: 11.5px; background: transparent; }"
        "QScrollArea#bookmarkScroll { background: transparent; border: none; }"
        "QWidget#bookmarkContainer { background: transparent; }"
        "QFrame#bookmarkRow {"
        "    background-color: %10; border: 1px solid %8; border-radius: 10px;"
        "}"
        "QLabel#bookmarkConversation { color: %6; font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QLabel#bookmarkPreview { color: %9; font-size: 12px; background: transparent; }"
        "QLabel#bookmarkTime { color: %13; font-size: 11px; background: transparent; }"
        "QPushButton#bookmarkRowButton {"
        "    background: transparent; border: 1px solid %8; border-radius: 7px;"
        "    color: %9; padding: 0 10px; font-size: 11.5px;"
        "}"
        "QPushButton#bookmarkRowButton:hover { background: %12; color: %4; }"
        "QLabel#bookmarkEmpty { color: %13; font-size: 12.5px; background: transparent; }"
        "QPushButton#bookmarkCloseButton {"
        "    background: %5; border: none; border-radius: 9px;"
        "    color: #ffffff; padding: 7px 22px; font-size: 12.5px; font-weight: 600;"
        "}"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          textColor, subColor, fieldBorder, muted, rowBg, hoverBg, hoverBg, muted);

    setStyleSheet(style);
}

// ───────────────────────────── 列表 ─────────────────────────────

void ChatBookmarkDialog::rebuild()
{
    ForumKit::clearLayout(m_listLayout);

    const QVector<ChatBookmark> bookmarks = ChatBookmarkStore::instance()->bookmarks();
    m_countLabel->setText(bookmarks.isEmpty() ? QString() : tr("共 %1 条").arg(bookmarks.size()));
    m_emptyLabel->setVisible(bookmarks.isEmpty());
    m_scroll->setVisible(!bookmarks.isEmpty());

    for (const ChatBookmark &bookmark : bookmarks)
    {
        auto *row = new QFrame(m_scroll->widget());
        row->setObjectName(QStringLiteral("bookmarkRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 9, 10, 9);
        rowLayout->setSpacing(10);

        // 类型小徽标
        QString kindText = tr("文字");
        if (bookmark.isImage())
            kindText = tr("图片");
        else if (bookmark.isFile())
            kindText = tr("文件");
        auto *kind = new QLabel(kindText, row);
        kind->setObjectName(QStringLiteral("bookmarkKind"));
        kind->setAlignment(Qt::AlignCenter);
        kind->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QLabel#bookmarkKind {"
            "    background-color: @PRIMARY_RGBA_20@;"
            "    color: @PRIMARY@;"
            "    border-radius: 7px;"
            "    font-size: 10.5px;"
            "    font-weight: 700;"
            "    padding: 2px 7px;"
            "}")));
        rowLayout->addWidget(kind, 0, Qt::AlignTop);

        // 中间：会话名 · 发言人 / 内容预览 / 时间
        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);
        auto *conversation = new ForumKit::ElidedLabel(row);
        conversation->setObjectName(QStringLiteral("bookmarkConversation"));
        const QString sender = bookmark.senderName.isEmpty() ? tr("消息") : bookmark.senderName;
        conversation->setFullText(tr("%1 · %2").arg(bookmark.conversationName, sender));
        textColumn->addWidget(conversation);

        QString preview = bookmark.body;
        if (preview.isEmpty())
        {
            if (bookmark.isImage())
                preview = tr("[图片]");
            else if (bookmark.isFile())
                preview = tr("[文件] %1").arg(bookmark.fileName);
            else
                preview = tr("（没有文字内容）");
        }
        else if (bookmark.isFile())
        {
            preview = tr("[文件] %1 · %2").arg(bookmark.fileName, preview);
        }
        if (bookmark.fileSize > 0 && bookmark.isFile())
            preview += tr("（%1）").arg(DownloadUtils::formatSize(bookmark.fileSize));
        auto *previewLabel = new QLabel(preview.simplified(), row);
        previewLabel->setObjectName(QStringLiteral("bookmarkPreview"));
        previewLabel->setWordWrap(true);
        textColumn->addWidget(previewLabel);

        auto *time = new QLabel(ForumKit::relativeTime(bookmark.createdAt), row);
        time->setObjectName(QStringLiteral("bookmarkTime"));
        textColumn->addWidget(time);
        rowLayout->addLayout(textColumn, 1);

        // 右侧：打开 / 删除
        auto *actions = new QVBoxLayout();
        actions->setSpacing(4);
        auto *open = makeRowButton(row, tr("打开"));
        const QString conversationKey = bookmark.conversationKey;
        connect(open, &QPushButton::clicked, this, [this, conversationKey]() {
            m_openKey = conversationKey;
            accept();
        });
        actions->addWidget(open);
        auto *remove = makeRowButton(row, tr("删除"));
        const QString bookmarkId = bookmark.id;
        connect(remove, &QPushButton::clicked, this, [this, bookmarkId]() {
            // 删除后 changed 信号会触发 rebuild
            ChatBookmarkStore::instance()->remove(bookmarkId);
        });
        actions->addWidget(remove);
        actions->addStretch(1);
        rowLayout->addLayout(actions);

        m_listLayout->addWidget(row);
    }
    m_listLayout->addStretch(1);
}
