#include "SkinStationPage.h"

#include "components/NotificationManager.h"
#include "components/Skin3DWidget.h"
#include "utils/AuthManager.h"
#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSharedPointer>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <memory>

namespace
{
struct SkinPalette
{
    QString bg;
    QString card;
    QString card2;
    QString line;
    QString ink;
    QString ink2;
    QString ink3;
    QString brand;
    QString brandHover;
    QString soft;
    QString danger;
    QString dangerSoft;
};

SkinPalette skinPalette(bool dark)
{
    if (dark)
    {
        return {
            QStringLiteral("#0f1512"), QStringLiteral("#161f19"), QStringLiteral("#131b16"),
            QStringLiteral("#26332b"), QStringLiteral("#e7f0e9"), QStringLiteral("#a7baac"),
            QStringLiteral("#7d9184"), QStringLiteral("#66bb6a"), QStringLiteral("#81c784"),
            QStringLiteral("#1b2b21"), QStringLiteral("#ef7c74"), QStringLiteral("#2a1715")};
    }
    return {
        QStringLiteral("#f4f8f3"), QStringLiteral("#ffffff"), QStringLiteral("#fbfdfb"),
        QStringLiteral("#e3ebe2"), QStringLiteral("#17251a"), QStringLiteral("#52665a"),
        QStringLiteral("#87998b"), QStringLiteral("#2e7d32"), QStringLiteral("#1b5e20"),
        QStringLiteral("#e8f5e9"), QStringLiteral("#c62828"), QStringLiteral("#fdecea")};
}

const float kPi = 3.14159265f;

/** 把皮肤图裁成头像（含帽子层），列表里用 */
QIcon headIcon(const QImage &skin, int size)
{
    if (skin.isNull())
        return QIcon();

    const int px = qMax(32, size);
    QImage canvas(px, px, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(0, 0, px, px), skin, QRect(8, 8, 8, 8));
    if (skin.height() >= 64)
        painter.drawImage(QRect(0, 0, px, px), skin, QRect(40, 8, 8, 8));
    painter.end();
    return QIcon(QPixmap::fromImage(canvas));
}

QIcon capeIcon(const QImage &cape, int size)
{
    if (cape.isNull())
        return QIcon();
    const int px = qMax(32, size);
    QImage canvas(px, px, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(0, 0, px, px), cape, QRect(1, 1, 10, 16));
    painter.end();
    return QIcon(QPixmap::fromImage(canvas));
}

/** 大数字缩写：和网页版一样 1.2k / 1.2 万 */
QString compactNumber(qint64 value)
{
    if (value < 10000)
        return QString::number(value);
    if (value < 100000000)
        return QString::number(value / 10000.0, 'f', value >= 100000 ? 0 : 1) + QObject::tr(" 万");
    return QString::number(value / 100000000.0, 'f', 1) + QObject::tr(" 亿");
}

/** 相对时间：刚刚 / n 分钟前 / n 小时前 / n 天前 / 日期 */
QString relativeTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 delta = qMax<qint64>(0, now - seconds);
    if (delta < 60)
        return QObject::tr("刚刚");
    if (delta < 3600)
        return QObject::tr("%1 分钟前").arg(delta / 60);
    if (delta < 86400)
        return QObject::tr("%1 小时前").arg(delta / 3600);
    if (delta < 86400 * 30)
        return QObject::tr("%1 天前").arg(delta / 86400);
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

/** 衣柜 / 皮肤库的卡片：圆角卡面、图标、名字和一行小字 */
class SkinItemDelegate : public QStyledItemDelegate
{
public:
    SkinItemDelegate(bool dark, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_dark(dark)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(124, 162);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const SkinPalette p = skinPalette(m_dark);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (index.flags() == Qt::NoItemFlags && !index.data(Qt::DisplayRole).toString().isEmpty())
        {
            painter->setPen(QColor(p.ink3));
            QFont font = option.font;
            font.setPointSizeF(qMax(9.0, option.font.pointSizeF()));
            painter->setFont(font);
            painter->drawText(option.rect.adjusted(16, 16, -16, -16),
                              Qt::AlignCenter | Qt::TextWordWrap,
                              index.data(Qt::DisplayRole).toString());
            painter->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const QRect card = option.rect.adjusted(4, 4, -4, -4);

        QPen border(selected ? QColor(p.brand) : QColor(p.line));
        border.setWidthF(selected ? 1.6 : 1.0);
        painter->setPen(border);
        painter->setBrush(selected ? QColor(p.soft) : QColor(p.card));
        painter->drawRoundedRect(card, 13, 13);

        if (hover && !selected)
        {
            painter->setPen(QPen(QColor(p.brand), 1.2f));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 12, 12);
        }

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(card.x() + (card.width() - 84) / 2, card.y() + 9, 84, 84);
        icon.paint(painter, iconRect, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);

        QFont nameFont = option.font;
        nameFont.setBold(true);
        nameFont.setPointSizeF(qMax(8.8, option.font.pointSizeF()));
        painter->setFont(nameFont);
        painter->setPen(QColor(p.ink));
        const QRect nameRect(card.x() + 9, iconRect.bottom() + 8, card.width() - 18, 20);
        const QFontMetrics nameMetrics(nameFont);
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                          nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                 Qt::ElideRight, nameRect.width()));

        QFont metaFont = option.font;
        metaFont.setBold(false);
        metaFont.setPointSizeF(qMax(7.6, option.font.pointSizeF() - 1.3));
        painter->setFont(metaFont);
        painter->setPen(QColor(p.ink3));
        const QRect metaRect(card.x() + 7, nameRect.bottom() + 2, card.width() - 14, 18);
        const QFontMetrics metaMetrics(metaFont);
        painter->drawText(metaRect, Qt::AlignHCenter | Qt::AlignVCenter,
                          metaMetrics.elidedText(index.data(Qt::UserRole + 1).toString(),
                                                 Qt::ElideRight, metaRect.width()));
        painter->restore();
    }

private:
    bool m_dark = false;
};
} // namespace

SkinStationPage::SkinStationPage(QWidget *parent) : QWidget(parent)
{
    m_dark = ThemeManager::instance()
             && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    setObjectName(QStringLiteral("skinStationPage"));
    setAcceptDrops(true);

    m_account = ForumAccountService::instance();
    m_net = new QNetworkAccessManager(this);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(320);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        m_libraryQuery = m_search ? m_search->text().trimmed() : QString();
        loadLibrary(1);
    });

    buildUi();
}

void SkinStationPage::openPage()
{
    // 页面常驻：每次进来都按当前登录态重新拉一次，换号 / 登录后回来能看到最新的衣柜
    m_account = ForumAccountService::instance();
    const bool loggedIn = m_account && m_account->hasSession();
    if (m_list)
        m_list->setEnabled(loggedIn);

    if (!loggedIn)
    {
        // 页面常驻：退出登录后再进来，不能还留着上一个账号的衣柜
        m_mySkins.clear();
        m_capes.clear();
        m_presets.clear();
        m_favorites.clear();
        m_history.clear();
        m_library.clear();
        m_related.clear();
        m_playerName.clear();
        m_playerUuid.clear();
        m_currentSkinUrl.clear();
        m_currentCapeUrl.clear();
        m_selectedId = -1;
        m_detailId = -1;
        m_role.clear();
        if (m_siteStatsLabel)
            m_siteStatsLabel->clear();
        if (m_adminBtn)
            m_adminBtn->hide();
        if (m_playerLabel)
            m_playerLabel->setText(tr("未登录"));
        if (m_playerNameLabel)
        {
            m_playerNameLabel->clear();
            m_playerNameLabel->hide();
        }
        if (m_uuidLabel)
            m_uuidLabel->clear();
        if (m_countLabel)
            m_countLabel->clear();
        if (m_preview)
        {
            m_preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
            m_preview->clearCape();
        }
        fillList();
        showStatus(tr("请先在账户管理页登录方块盒子论坛账号，皮肤站会用同一个账号自动登录。"), true);
        return;
    }

    showStatus(tr("正在连接皮肤站…"));
    loadMe();
    loadSiteStats();
    loadFavorites();
    loadHistory();
    if (m_tab == TabLibrary)
        loadLibrary(m_libraryPage);
}

void SkinStationPage::onRefreshClicked()
{
    openPage();
}

SkinStationPage::~SkinStationPage() = default;

QString SkinStationPage::skinSiteBaseUrl()
{
    QString url = qEnvironmentVariable("BLOCKBOX_SKIN_SITE_URL").trimmed();
    if (url.isEmpty())
        url = QStringLiteral("https://skin.mcblockbox.top");
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

void SkinStationPage::buildUi()
{
    const SkinPalette p = skinPalette(m_dark);
    QString qss = QStringLiteral(R"QSS(
        QWidget#skinStationPage { background: transparent; }
        /* 同一套配色也用在弹窗上：弹窗是独立顶层窗口，必须有实底，否则会透出后面的内容 */
        QDialog#skinStationPage { background: %CARD%; }
        QWidget#skinStationPage QLabel { color: %INK%; }
        QWidget#skinStationPage QCheckBox { color: %INK2%; }
        QWidget#skinStationPage QPushButton { background: %CARD%; color: %INK2%; border: 1px solid %LINE%; border-radius: 8px; padding: 5px 12px; }
        QWidget#skinStationPage QPushButton:hover { color: %BRAND%; border-color: %BRAND%; }
        QFrame#skinPanel { background: %CARD%; border: 1px solid %LINE%; border-radius: 18px; }
        QFrame#skinPreviewShell { background: %CARD2%; border: 1px solid %LINE%; border-radius: 16px; }
        #skinPreview { background: transparent; border: none; }
        QLabel#skinName { color: %INK%; font-size: 17px; font-weight: 700; }
        QLabel#skinMuted { color: %INK3%; font-size: 11.5px; }
        QLabel#skinUuid { color: %INK2%; font-size: 10.5px; background: %CARD2%; border: 1px solid %LINE%; border-radius: 8px; padding: 4px 9px; }
        QLabel#skinStatus { color: %INK2%; font-size: 12px; padding: 7px 11px; background: %CARD2%; border: 1px solid %LINE%; border-radius: 10px; }
        QLabel#skinDetailTitle { color: %INK%; font-size: 16px; font-weight: 700; }
        QLabel#skinDetailMeta { color: %INK2%; font-size: 12px; }
        QLabel#skinBadge { color: %BRAND%; background: %SOFT%; border-radius: 8px; padding: 3px 8px; font-size: 10.5px; font-weight: 700; }
        QLabel#skinStatChip { color: %INK2%; background: %CARD2%; border: 1px solid %LINE%; border-radius: 9px; padding: 5px 9px; font-size: 11px; }
        QTabBar::tab { background: transparent; color: %INK2%; padding: 7px 13px; font-size: 13px; font-weight: 600; border-radius: 9px; margin-right: 2px; }
        QTabBar::tab:hover { background: %CARD2%; color: %BRAND%; }
        QTabBar::tab:selected { color: %BRAND%; background: %SOFT%; }
        QWidget#skinStationPage QPushButton#skinPrimary { background: %BRAND%; color: #ffffff; border: none; border-radius: 10px; height: 38px; padding: 0 16px; font-weight: 700; font-size: 12.5px; }
        QWidget#skinStationPage QPushButton#skinPrimary:hover { background: %BRAND_HOVER%; }
        QWidget#skinStationPage QPushButton#skinPrimary:disabled { background: %LINE%; color: %INK3%; }
        QWidget#skinStationPage QPushButton#skinGhost { background: %CARD%; color: %INK2%; border: 1px solid %LINE%; border-radius: 10px; height: 36px; padding: 0 13px; font-weight: 600; font-size: 12.5px; }
        QWidget#skinStationPage QPushButton#skinGhost:hover { color: %BRAND%; border-color: %BRAND%; background: %SOFT%; }
        QWidget#skinStationPage QPushButton#skinSmall { background: %CARD%; color: %INK2%; border: 1px solid %LINE%; border-radius: 8px; height: 31px; padding: 0 10px; font-size: 11.5px; font-weight: 600; }
        QWidget#skinStationPage QPushButton#skinSmall:hover { color: %BRAND%; border-color: %BRAND%; background: %SOFT%; }
        QWidget#skinStationPage QPushButton#skinSmall:checked { color: %BRAND%; border-color: %BRAND%; background: %SOFT%; }
        QWidget#skinStationPage QPushButton#skinSmall:disabled { color: %INK3%; }
        QWidget#skinStationPage QPushButton#skinActionPrimary { background: %BRAND%; color: #ffffff; border: none; border-radius: 8px; height: 31px; padding: 0 15px; font-size: 11.5px; font-weight: 700; }
        QWidget#skinStationPage QPushButton#skinActionPrimary:hover { background: %BRAND_HOVER%; }
        QWidget#skinStationPage QPushButton#skinActionPrimary:disabled { background: %LINE%; color: %INK3%; }
        QWidget#skinStationPage QPushButton#skinSmallDanger { background: transparent; color: %DANGER%; border: 1px solid %LINE%; border-radius: 8px; height: 31px; padding: 0 10px; font-size: 11.5px; font-weight: 600; }
        QWidget#skinStationPage QPushButton#skinSmallDanger:hover { background: %DANGER_SOFT%; border-color: %DANGER%; }
        QWidget#skinStationPage QPushButton#skinSmallDanger:disabled { color: %INK3%; border-color: %LINE%; }
        QLineEdit, QComboBox, QPlainTextEdit { background: %CARD2%; border: 1px solid %LINE%; border-radius: 9px; padding: 5px 9px; font-size: 12px; color: %INK%; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: %BRAND%; }
        QComboBox::drop-down { border: none; width: 18px; }
        QComboBox QAbstractItemView { background: %CARD%; color: %INK%; border: 1px solid %LINE%; selection-background-color: %SOFT%; selection-color: %BRAND%; }
        QListWidget#skinList { background: %CARD%; border: 1px solid %LINE%; border-radius: 16px; outline: none; padding: 6px; }
        QScrollBar:vertical { width: 8px; background: transparent; }
        QScrollBar::handle:vertical { background: %LINE%; border-radius: 4px; min-height: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
        QFrame#skinToolbar { background: %CARD2%; border: 1px solid %LINE%; border-radius: 10px; }
        QFrame#skinDetailPanel { background: %CARD2%; border: 1px solid %LINE%; border-radius: 12px; }
    )QSS");
    qss.replace(QStringLiteral("%BG%"), p.bg);
    qss.replace(QStringLiteral("%CARD2%"), p.card2);
    qss.replace(QStringLiteral("%CARD%"), p.card);
    qss.replace(QStringLiteral("%LINE%"), p.line);
    qss.replace(QStringLiteral("%INK3%"), p.ink3);
    qss.replace(QStringLiteral("%INK2%"), p.ink2);
    qss.replace(QStringLiteral("%INK%"), p.ink);
    qss.replace(QStringLiteral("%BRAND_HOVER%"), p.brandHover);
    qss.replace(QStringLiteral("%BRAND%"), p.brand);
    qss.replace(QStringLiteral("%SOFT%"), p.soft);
    qss.replace(QStringLiteral("%DANGER_SOFT%"), p.dangerSoft);
    qss.replace(QStringLiteral("%DANGER%"), p.danger);
    setStyleSheet(qss);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 10, 18, 14);
    root->setSpacing(12);

    // ── 主体 ──
    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    auto *leftCard = new QFrame(this);
    leftCard->setObjectName(QStringLiteral("skinPanel"));
    leftCard->setFixedWidth(312);
    auto *left = new QVBoxLayout(leftCard);
    left->setContentsMargins(14, 14, 14, 14);
    left->setSpacing(8);

    auto *previewShell = new QFrame(leftCard);
    previewShell->setObjectName(QStringLiteral("skinPreviewShell"));
    auto *previewLayout = new QVBoxLayout(previewShell);
    previewLayout->setContentsMargins(5, 5, 5, 5);

    m_preview = new Skin3DWidget(previewShell);
    m_preview->setObjectName(QStringLiteral("skinPreview"));
    m_preview->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_preview->setFixedSize(274, 286);
    m_preview->setAutoRotate(true);
    if (m_dark)
        m_preview->setBackgroundGradient(QColor("#1b2b21"), QColor("#101a14"));
    else
        m_preview->setBackgroundGradient(QColor("#fbfdfb"), QColor("#e8f5e9"));
    m_preview->setBackgroundCornerRadius(12);
    m_preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
    m_preview->setViewAngles(kPi, 1.45f, 3.3f);
    previewLayout->addWidget(m_preview);
    left->addWidget(previewShell);

    m_previewHint = new QLabel(tr("拖动旋转 · 滚轮缩放"), leftCard);
    m_previewHint->setObjectName(QStringLiteral("skinMuted"));
    m_previewHint->setAlignment(Qt::AlignCenter);
    left->addWidget(m_previewHint);

    auto *viewRow = new QHBoxLayout();
    viewRow->setSpacing(4);
    const QList<QPair<QString, float>> views = {
        {tr("正面"), kPi}, {tr("左侧"), kPi / 2.0f}, {tr("背面"), 0.0f}, {tr("右侧"), -kPi / 2.0f}};
    for (const auto &view : views)
    {
        auto *button = new QPushButton(view.first, leftCard);
        button->setObjectName(QStringLiteral("skinSmall"));
        button->setCursor(Qt::PointingHandCursor);
        const float angle = view.second;
        connect(button, &QPushButton::clicked, this, [this, angle]() {
            m_preview->setViewAngles(angle, 1.45f, 3.3f);
            if (m_rotateBtn)
                m_rotateBtn->setChecked(false);
        });
        viewRow->addWidget(button);
    }
    left->addLayout(viewRow);

    auto *viewRow2 = new QHBoxLayout();
    viewRow2->setSpacing(4);
    m_rotateBtn = new QPushButton(tr("自动旋转"), leftCard);
    m_rotateBtn->setObjectName(QStringLiteral("skinSmall"));
    m_rotateBtn->setCheckable(true);
    m_rotateBtn->setChecked(true);
    m_rotateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rotateBtn, &QPushButton::toggled, this, [this](bool on) { m_preview->setAutoRotate(on); });
    viewRow2->addWidget(m_rotateBtn);

    auto *savePreviewBtn = new QPushButton(tr("保存预览图"), leftCard);
    savePreviewBtn->setObjectName(QStringLiteral("skinSmall"));
    savePreviewBtn->setCursor(Qt::PointingHandCursor);
    connect(savePreviewBtn, &QPushButton::clicked, this, &SkinStationPage::savePreview);
    viewRow2->addWidget(savePreviewBtn);
    left->addLayout(viewRow2);

    auto *chipRow = new QHBoxLayout();
    chipRow->addStretch();
    m_modelChip = new QLabel(tr("Classic"), leftCard);
    m_modelChip->setObjectName(QStringLiteral("skinBadge"));
    chipRow->addWidget(m_modelChip);
    chipRow->addStretch();
    left->addLayout(chipRow);

    // 主名字是方块盒子论坛账户名（可以带中文），进游戏用的玩家名单列一行
    m_playerLabel = new QLabel(tr("正在加载…"), leftCard);
    m_playerLabel->setObjectName(QStringLiteral("skinName"));
    m_playerLabel->setAlignment(Qt::AlignCenter);
    m_playerLabel->setWordWrap(true);
    m_playerLabel->setToolTip(tr("方块盒子论坛账户名称"));
    left->addWidget(m_playerLabel);

    m_playerNameLabel = new QLabel(QString(), leftCard);
    m_playerNameLabel->setObjectName(QStringLiteral("skinMuted"));
    m_playerNameLabel->setAlignment(Qt::AlignCenter);
    m_playerNameLabel->setToolTip(tr("进游戏用的玩家名：只能包含字母、数字和下划线"));
    left->addWidget(m_playerNameLabel);

    m_uuidLabel = new QLabel(QString(), leftCard);
    m_uuidLabel->setObjectName(QStringLiteral("skinUuid"));
    m_uuidLabel->setAlignment(Qt::AlignCenter);
    m_uuidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(m_uuidLabel);

    m_countLabel = new QLabel(QString(), leftCard);
    m_countLabel->setObjectName(QStringLiteral("skinMuted"));
    m_countLabel->setAlignment(Qt::AlignCenter);
    left->addWidget(m_countLabel);

    // 全站统计：和网页首页的四张统计卡一个信息
    m_siteStatsLabel = new QLabel(QString(), leftCard);
    m_siteStatsLabel->setObjectName(QStringLiteral("skinMuted"));
    m_siteStatsLabel->setAlignment(Qt::AlignCenter);
    m_siteStatsLabel->setWordWrap(true);
    left->addWidget(m_siteStatsLabel);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);
    auto *uploadBtn = new QPushButton(tr("上传"), leftCard);
    uploadBtn->setObjectName(QStringLiteral("skinPrimary"));
    uploadBtn->setCursor(Qt::PointingHandCursor);
    connect(uploadBtn, &QPushButton::clicked, this, [this]() { openUploadDialog(); });
    buttonRow->addWidget(uploadBtn);

    auto *profileBtn = new QPushButton(tr("编辑资料"), leftCard);
    profileBtn->setObjectName(QStringLiteral("skinGhost"));
    profileBtn->setCursor(Qt::PointingHandCursor);
    connect(profileBtn, &QPushButton::clicked, this, &SkinStationPage::openProfileDialog);
    buttonRow->addWidget(profileBtn);
    left->addLayout(buttonRow);

    // 批量上传 / 我的公开主页：和网页衣柜的两个入口保持一致
    auto *buttonRow2 = new QHBoxLayout();
    buttonRow2->setSpacing(6);
    m_batchBtn = new QPushButton(tr("批量上传"), leftCard);
    m_batchBtn->setObjectName(QStringLiteral("skinGhost"));
    m_batchBtn->setCursor(Qt::PointingHandCursor);
    m_batchBtn->setToolTip(tr("一次选多张 PNG（最多 20 张），按文件名命名并设为私密"));
    buttonRow2->addWidget(m_batchBtn);

    m_myProfileBtn = new QPushButton(tr("我的主页"), leftCard);
    m_myProfileBtn->setObjectName(QStringLiteral("skinGhost"));
    m_myProfileBtn->setCursor(Qt::PointingHandCursor);
    m_myProfileBtn->setToolTip(tr("查看自己的公开主页：别人能看到哪些皮肤和披风"));
    buttonRow2->addWidget(m_myProfileBtn);
    left->addLayout(buttonRow2);
    left->addStretch();

    body->addWidget(leftCard);

    auto *rightCard = new QFrame(this);
    rightCard->setObjectName(QStringLiteral("skinPanel"));
    auto *right = new QVBoxLayout(rightCard);
    right->setContentsMargins(12, 10, 12, 12);
    right->setSpacing(8);

    auto *tabRow = new QHBoxLayout();
    tabRow->setSpacing(4);
    m_tabs = new QTabBar(rightCard);
    m_tabs->setObjectName(QStringLiteral("skinTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->addTab(tr("我的皮肤"));
    m_tabs->addTab(tr("披风"));
    m_tabs->addTab(tr("官方预设"));
    m_tabs->addTab(tr("收藏"));
    m_tabs->addTab(tr("最近使用"));
    m_tabs->addTab(tr("皮肤库"));
    m_tabs->setExpanding(false);
    tabRow->addWidget(m_tabs);
    tabRow->addStretch();

    // 「打开网页版」放在页签行右侧：进网页版看评论 / 点名，页面里不再单占一行
    m_adminBtn = new QPushButton(tr("管理举报"), rightCard);
    m_adminBtn->setObjectName(QStringLiteral("skinSmall"));
    m_adminBtn->setCursor(Qt::PointingHandCursor);
    m_adminBtn->setToolTip(tr("处理皮肤举报：忽略 / 下架 / 删除（仅管理员和版主可见）"));
    m_adminBtn->hide();
    tabRow->addWidget(m_adminBtn);

    auto *openWebBtn = new QPushButton(tr("打开网页版"), rightCard);
    openWebBtn->setObjectName(QStringLiteral("skinSmall"));
    openWebBtn->setCursor(Qt::PointingHandCursor);
    openWebBtn->setToolTip(tr("在浏览器里打开方块皮肤站网页版"));
    connect(openWebBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(skinSiteBaseUrl() + QStringLiteral("/")));
    });
    tabRow->addWidget(openWebBtn);
    right->addLayout(tabRow);

    // 皮肤库工具条（只在「皮肤库」页签显示）
    m_libraryBar = new QWidget(rightCard);
    m_libraryBar->setObjectName(QStringLiteral("skinToolbar"));
    auto *libraryLayout = new QHBoxLayout(m_libraryBar);
    libraryLayout->setContentsMargins(6, 6, 6, 6);
    libraryLayout->setSpacing(6);

    m_kindFilter = new QComboBox(m_libraryBar);
    m_kindFilter->addItem(tr("皮肤"), QStringLiteral("skin"));
    m_kindFilter->addItem(tr("披风"), QStringLiteral("cape"));
    m_kindFilter->setToolTip(tr("皮肤库分「皮肤」和「披风」两类"));
    libraryLayout->addWidget(m_kindFilter);

    m_search = new QLineEdit(m_libraryBar);
    m_search->setPlaceholderText(tr("搜索皮肤名 / 作者…"));
    m_search->setClearButtonEnabled(true);
    libraryLayout->addWidget(m_search, 1);

    m_sort = new QComboBox(m_libraryBar);
    m_sort->addItem(tr("最受欢迎"), QStringLiteral("hot"));
    m_sort->addItem(tr("最新发布"), QStringLiteral("new"));
    m_sort->addItem(tr("下载最多"), QStringLiteral("downloads"));
    libraryLayout->addWidget(m_sort);

    m_modelFilter = new QComboBox(m_libraryBar);
    m_modelFilter->addItem(tr("全部模型"), QString());
    m_modelFilter->addItem(tr("Classic"), QStringLiteral("classic"));
    m_modelFilter->addItem(tr("Slim"), QStringLiteral("slim"));
    libraryLayout->addWidget(m_modelFilter);

    // 和网页首页的「换一批」一样：随机抽一批公开作品，看完再换
    m_shuffleBtn = new QPushButton(tr("换一批"), m_libraryBar);
    m_shuffleBtn->setObjectName(QStringLiteral("skinSmall"));
    m_shuffleBtn->setCursor(Qt::PointingHandCursor);
    m_shuffleBtn->setToolTip(tr("随机换一批公开作品"));
    libraryLayout->addWidget(m_shuffleBtn);
    right->addWidget(m_libraryBar);

    m_list = new QListWidget(rightCard);
    m_list->setObjectName(QStringLiteral("skinList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setItemDelegate(new SkinItemDelegate(m_dark, m_list));
    m_list->setIconSize(QSize(82, 82));
    m_list->setGridSize(QSize(122, 158));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(2);
    m_list->setWordWrap(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    right->addWidget(m_list, 1);

    // 皮肤库翻页
    m_pager = new QWidget(rightCard);
    auto *pagerLayout = new QHBoxLayout(m_pager);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(6);
    pagerLayout->addStretch();
    m_prevPage = new QPushButton(tr("上一页"), m_pager);
    m_prevPage->setObjectName(QStringLiteral("skinSmall"));
    m_prevPage->setCursor(Qt::PointingHandCursor);
    m_pageLabel = new QLabel(QString(), m_pager);
    m_pageLabel->setObjectName(QStringLiteral("skinMuted"));
    m_nextPage = new QPushButton(tr("下一页"), m_pager);
    m_nextPage->setObjectName(QStringLiteral("skinSmall"));
    m_nextPage->setCursor(Qt::PointingHandCursor);
    pagerLayout->addWidget(m_prevPage);
    pagerLayout->addWidget(m_pageLabel);
    pagerLayout->addWidget(m_nextPage);
    right->addWidget(m_pager);

    // 选中作品的操作条
    auto *detailPanel = new QFrame(rightCard);
    detailPanel->setObjectName(QStringLiteral("skinDetailPanel"));
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(10, 8, 10, 8);
    detailLayout->setSpacing(5);

    m_detailTitle = new QLabel(tr("选择一件作品"), detailPanel);
    m_detailTitle->setObjectName(QStringLiteral("skinDetailTitle"));
    detailLayout->addWidget(m_detailTitle);

    m_detailMeta = new QLabel(tr("点击列表里的皮肤，就能在左边转着看。"), detailPanel);
    m_detailMeta->setObjectName(QStringLiteral("skinDetailMeta"));
    detailLayout->addWidget(m_detailMeta);

    auto *actionRow = new QGridLayout();
    actionRow->setSpacing(5);
    m_applyBtn = new QPushButton(tr("穿上"), detailPanel);
    m_applyBtn->setObjectName(QStringLiteral("skinActionPrimary"));
    m_favoriteBtn = new QPushButton(tr("收藏"), detailPanel);
    m_favoriteBtn->setObjectName(QStringLiteral("skinSmall"));
    m_likeBtn = new QPushButton(tr("点赞"), detailPanel);
    m_likeBtn->setObjectName(QStringLiteral("skinSmall"));
    m_publicBtn = new QPushButton(tr("公开"), detailPanel);
    m_publicBtn->setObjectName(QStringLiteral("skinSmall"));
    m_renameBtn = new QPushButton(tr("改名"), detailPanel);
    m_renameBtn->setObjectName(QStringLiteral("skinSmall"));
    m_deleteBtn = new QPushButton(tr("删除"), detailPanel);
    m_deleteBtn->setObjectName(QStringLiteral("skinSmallDanger"));
    m_downloadBtn = new QPushButton(tr("下载 PNG"), detailPanel);
    m_downloadBtn->setObjectName(QStringLiteral("skinSmall"));
    m_shareBtn = new QPushButton(tr("复制链接"), detailPanel);
    m_shareBtn->setObjectName(QStringLiteral("skinSmall"));
    m_commentsBtn = new QPushButton(tr("评论"), detailPanel);
    m_commentsBtn->setObjectName(QStringLiteral("skinSmall"));
    m_reportBtn = new QPushButton(tr("举报"), detailPanel);
    m_reportBtn->setObjectName(QStringLiteral("skinSmallDanger"));
    m_ownerBtn = new QPushButton(tr("作者主页"), detailPanel);
    m_ownerBtn->setObjectName(QStringLiteral("skinSmall"));
    m_relatedBtn = new QPushButton(tr("同作者作品"), detailPanel);
    m_relatedBtn->setObjectName(QStringLiteral("skinSmall"));
    m_detailBtn = new QPushButton(tr("网页详情"), detailPanel);
    m_detailBtn->setObjectName(QStringLiteral("skinSmall"));
    const QVector<QPushButton *> actionButtons = {m_applyBtn, m_favoriteBtn, m_likeBtn, m_publicBtn,
                                                  m_downloadBtn, m_renameBtn, m_deleteBtn, m_commentsBtn,
                                                  m_reportBtn, m_ownerBtn, m_relatedBtn, m_shareBtn, m_detailBtn};
    for (int i = 0; i < actionButtons.size(); ++i)
    {
        QPushButton *button = actionButtons.at(i);
        button->setCursor(Qt::PointingHandCursor);
        actionRow->addWidget(button, i / 5, i % 5);
    }
    detailLayout->addLayout(actionRow);
    right->addWidget(detailPanel);

    m_statusLabel = new QLabel(tr("正在连接皮肤站…"), rightCard);
    m_statusLabel->setObjectName(QStringLiteral("skinStatus"));
    m_statusLabel->setWordWrap(true);
    right->addWidget(m_statusLabel);

    body->addWidget(rightCard, 1);
    root->addLayout(body, 1);

    // ── 信号 ──
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        m_tab = index;
        m_selectedId = -1;
        m_detailId = -1;
        const bool library = index == TabLibrary;
        m_libraryBar->setVisible(library);
        m_pager->setVisible(library);
        if (library && m_library.isEmpty())
            loadLibrary(1);
        else
            fillList();
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const QListWidgetItem *row = m_list->currentItem();
        if (!row)
            return;
        m_selectedId = row->data(Qt::UserRole).toInt();
        const SkinItem item = currentItem();
        if (item.id > 0)
        {
            previewItem(item);
            loadDetail(item.id);
        }
        else if (!item.presetKey.isEmpty())
        {
            previewItem(item);
        }
        updateDetail();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { applySelected(); });

    connect(m_applyBtn, &QPushButton::clicked, this, &SkinStationPage::applySelected);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &SkinStationPage::toggleFavorite);
    connect(m_likeBtn, &QPushButton::clicked, this, &SkinStationPage::toggleLike);
    connect(m_publicBtn, &QPushButton::clicked, this, &SkinStationPage::togglePublic);
    connect(m_renameBtn, &QPushButton::clicked, this, &SkinStationPage::openEditDialog);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SkinStationPage::deleteCurrent);
    connect(m_downloadBtn, &QPushButton::clicked, this, &SkinStationPage::downloadSelected);
    connect(m_shareBtn, &QPushButton::clicked, this, &SkinStationPage::shareSelected);
    connect(m_commentsBtn, &QPushButton::clicked, this, &SkinStationPage::openCommentsDialog);
    connect(m_reportBtn, &QPushButton::clicked, this, &SkinStationPage::reportSelected);
    connect(m_ownerBtn, &QPushButton::clicked, this, [this]() {
        const SkinItem item = currentItem();
        if (!item.ownerName.isEmpty())
            openPlayerDialog(item.ownerName);
    });
    connect(m_relatedBtn, &QPushButton::clicked, this, &SkinStationPage::openRelatedDialog);
    connect(m_detailBtn, &QPushButton::clicked, this, &SkinStationPage::openWebDetail);
    connect(m_batchBtn, &QPushButton::clicked, this, &SkinStationPage::batchUpload);
    connect(m_myProfileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_playerName.isEmpty())
            openPlayerDialog(m_playerName);
    });
    connect(m_adminBtn, &QPushButton::clicked, this, &SkinStationPage::openAdminDialog);

    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        if (m_tab == TabLibrary)
            m_searchTimer->start();
    });
    connect(m_sort, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_librarySort = m_sort->currentData().toString();
        m_libraryRandom = false;
        loadLibrary(1);
    });
    connect(m_kindFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_libraryKind = m_kindFilter->currentData().toString();
        // 和网页一致：披风没有 Classic / Slim 之分，模型筛选直接隐藏
        if (m_modelFilter)
        {
            const bool isSkin = m_libraryKind == QLatin1String("skin");
            m_modelFilter->setVisible(isSkin);
            if (!isSkin)
            {
                m_modelFilter->blockSignals(true);
                m_modelFilter->setCurrentIndex(0);
                m_modelFilter->blockSignals(false);
                m_libraryModel.clear();
            }
        }
        m_libraryRandom = false;
        loadLibrary(1);
    });
    connect(m_modelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_libraryModel = m_modelFilter->currentData().toString();
        m_libraryRandom = false;
        loadLibrary(1);
    });
    connect(m_shuffleBtn, &QPushButton::clicked, this, [this]() {
        m_libraryRandom = true;
        loadLibrary(1, QStringLiteral("random"));
    });
    connect(m_prevPage, &QPushButton::clicked, this, [this]() {
        if (m_libraryPage > 1)
            loadLibrary(m_libraryPage - 1);
    });
    connect(m_nextPage, &QPushButton::clicked, this, [this]() {
        if (m_libraryPage < m_libraryPages)
            loadLibrary(m_libraryPage + 1);
    });

    m_libraryBar->hide();
    m_pager->hide();
    updateDetail();
}

SkinStationPage::SkinItem SkinStationPage::itemFromJson(const QJsonObject &object, bool mine)
{
    SkinItem item;
    item.id = object.value(QStringLiteral("id")).toInt();
    item.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("skin"));
    item.name = object.value(QStringLiteral("name")).toString();
    item.model = object.value(QStringLiteral("model")).toString(QStringLiteral("classic"));
    item.url = object.value(QStringLiteral("url")).toString();
    item.likes = object.value(QStringLiteral("likes")).toInt();
    item.downloads = object.value(QStringLiteral("downloads")).toInt();
    item.comments = object.value(QStringLiteral("comments")).toInt();
    item.favoriteCount = object.value(QStringLiteral("favorite_count")).toInt();
    item.isPublic = object.value(QStringLiteral("public")).toBool();
    item.favorited = object.value(QStringLiteral("favorited")).toBool();
    item.liked = object.value(QStringLiteral("liked")).toBool();
    item.mine = mine;
    const qint64 created = qint64(object.value(QStringLiteral("created_at")).toDouble());
    if (created > 0)
        item.createdAt = QDateTime::fromSecsSinceEpoch(created).toString(QStringLiteral("yyyy-MM-dd"));
    const QJsonObject owner = object.value(QStringLiteral("owner")).toObject();
    item.ownerId = owner.value(QStringLiteral("id")).toInt();
    item.ownerName = owner.value(QStringLiteral("player_name")).toString();
    if (item.ownerName.isEmpty())
        item.ownerName = owner.value(QStringLiteral("display_name")).toString();
    if (item.ownerName.isEmpty())
        item.ownerName = owner.value(QStringLiteral("username")).toString();
    return item;
}

QNetworkRequest SkinStationPage::requestFor(const QString &path) const
{
    const QString base = skinSiteBaseUrl();
    const QUrl url(path.startsWith(QStringLiteral("http")) ? path : base + path);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (m_account && !m_account->token().isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_account->token().toUtf8());
    return request;
}

void SkinStationPage::showStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    if (error)
    {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "color:#c62828;background:#fdecea;border:1px solid #f3c5c2;border-radius:9px;padding:6px 10px;"));
    }
    else
    {
        m_statusLabel->setStyleSheet(QString());
        m_statusLabel->setObjectName(QStringLiteral("skinStatus"));
    }
}

void SkinStationPage::handleReply(QNetworkReply *reply, const std::function<void(const QJsonObject &)> &onOk)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, onOk]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        if (status == 401 || status == 403)
        {
            showStatus(tr("登录状态已失效，请回账户管理页重新登录论坛账号。"), true);
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (status <= 0)
        {
            showStatus(tr("连不上皮肤站（%1），检查网络后再试。").arg(skinSiteBaseUrl()), true);
            return;
        }
        if (status >= 400 || parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            QString message = tr("皮肤站返回了错误（%1）").arg(status);
            if (doc.isObject())
                message = doc.object().value(QStringLiteral("error")).toString(message);
            showStatus(message, true);
            return;
        }
        onOk(doc.object());
    });
}

void SkinStationPage::getJson(const QString &path, const std::function<void(const QJsonObject &)> &onOk)
{
    QNetworkReply *reply = m_net->get(requestFor(path));
    handleReply(reply, onOk);
}

void SkinStationPage::postJson(const QString &path, const QJsonObject &body,
                                 const std::function<void(const QJsonObject &)> &onOk)
{
    QNetworkRequest request = requestFor(path);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(reply, onOk);
}

void SkinStationPage::sendJson(const QString &path, const QByteArray &method, const QJsonObject &body,
                                const std::function<void(const QJsonObject &)> &onOk)
{
    QNetworkRequest request = requestFor(path);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net->sendCustomRequest(request, method, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(reply, onOk);
}

void SkinStationPage::postPng(const QString &path, const QByteArray &png,
                                const std::function<void(const QJsonObject &)> &onOk)
{
    QNetworkRequest request = requestFor(path);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    QNetworkReply *reply = m_net->post(request, png);
    handleReply(reply, onOk);
}

void SkinStationPage::fetchImage(const QString &url, const std::function<void(const QImage &)> &onImage)
{
    if (url.isEmpty())
        return;
    if (m_imageCache.contains(url))
    {
        onImage(m_imageCache.value(url));
        return;
    }
    QNetworkRequest request = requestFor(url);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, onImage]() {
        const QByteArray raw = reply->readAll();
        reply->deleteLater();
        QImage image;
        if (image.loadFromData(raw))
        {
            m_imageCache.insert(url, image);
            onImage(image);
        }
    });
}

void SkinStationPage::loadMe()
{
    getJson(QStringLiteral("/api/me"), [this](const QJsonObject &root) {
        if (root.contains(QStringLiteral("me")) && root.value(QStringLiteral("me")).isNull())
        {
            showStatus(tr("请先在论坛登录，再回到皮肤站。"), true);
            return;
        }
        const QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        m_userId = root.value(QStringLiteral("user")).toObject().value(QStringLiteral("id")).toInt();
        if (m_userId <= 0)
            m_userId = profile.value(QStringLiteral("user_id")).toInt();
        m_playerName = profile.value(QStringLiteral("player_name")).toString();
        m_playerUuid = profile.value(QStringLiteral("player_uuid")).toString();
        m_model = profile.value(QStringLiteral("model")).toString(QStringLiteral("classic"));
        m_about = profile.value(QStringLiteral("about")).toString();
        m_currentSkinUrl = profile.value(QStringLiteral("skin")).toObject().value(QStringLiteral("url")).toString();
        m_currentCapeUrl = profile.value(QStringLiteral("cape")).toObject().value(QStringLiteral("url")).toString();

        m_mySkins.clear();
        m_capes.clear();
        m_presets.clear();
        for (const QJsonValue &value : root.value(QStringLiteral("textures")).toArray())
            m_mySkins.append(itemFromJson(value.toObject(), true));
        for (const QJsonValue &value : root.value(QStringLiteral("capes")).toArray())
            m_capes.append(itemFromJson(value.toObject(), true));

        m_role = root.value(QStringLiteral("user")).toObject().value(QStringLiteral("role")).toString();
        if (m_role.isEmpty() && m_account)
            m_role = m_account->user().role;

        // 官方预设：和网页衣柜的「官方预设」页签同一批（/api/me 一次带回）
        int presetIndex = 0;
        for (const QJsonValue &value : root.value(QStringLiteral("presets")).toArray())
        {
            const QJsonObject object = value.toObject();
            SkinItem item;
            item.id = -(++presetIndex); // 负数 id：既不会和真实作品撞号，也方便判断「没选中」
            item.kind = QStringLiteral("skin");
            item.name = object.value(QStringLiteral("name")).toString();
            item.model = object.value(QStringLiteral("model")).toString(QStringLiteral("classic"));
            item.url = object.value(QStringLiteral("url")).toString();
            item.presetKey = object.value(QStringLiteral("key")).toString();
            item.presetDesc = object.value(QStringLiteral("desc")).toString();
            item.mine = true;
            m_presets.append(item);
        }

        // 主名字用方块盒子论坛账户名称（昵称可以带中文），玩家名单独一行标注：
        // 皮肤站的玩家名要能进 Minecraft，只允许字母 / 数字 / 下划线，
        // 论坛昵称里的中文在自动建档案时会被换成下划线（例如「倦雲juanyun」→「__juanyun」）。
        QString forumName;
        if (m_account)
        {
            const ForumUser user = m_account->user();
            forumName = user.displayName.isEmpty() ? user.username : user.displayName;
        }
        if (forumName.isEmpty())
            forumName = m_playerName;

        m_playerLabel->setText(forumName.isEmpty() ? tr("未命名角色") : forumName);
        if (m_playerNameLabel)
        {
            const bool same = !m_playerName.isEmpty() &&
                              forumName.compare(m_playerName, Qt::CaseInsensitive) == 0;
            m_playerNameLabel->setText(same || m_playerName.isEmpty()
                                           ? QString()
                                           : tr("玩家名 %1").arg(m_playerName));
            m_playerNameLabel->setVisible(!same && !m_playerName.isEmpty());
        }
        m_uuidLabel->setText(m_playerUuid);
        m_uuidLabel->setToolTip(tr("UUID 创建后固定不变，改玩家名不会换 UUID"));
        int likeTotal = 0;
        for (const SkinItem &entry : m_mySkins)
            likeTotal += entry.likes;
        for (const SkinItem &entry : m_capes)
            likeTotal += entry.likes;
        m_countLabel->setText(tr("%1 件皮肤 · %2 件披风 · %3 获赞")
                                  .arg(m_mySkins.size())
                                  .arg(m_capes.size())
                                  .arg(compactNumber(likeTotal)));
        if (m_adminBtn)
            m_adminBtn->setVisible(isModerator());
        if (m_modelChip)
            m_modelChip->setText(m_model == QLatin1String("slim") ? QStringLiteral("Slim")
                                                                  : QStringLiteral("Classic"));

        previewCurrentProfile();
        if (m_tab != TabLibrary)
            fillList();
        else if (!m_library.isEmpty())
            fillList();
        showStatus(tr("已连接皮肤站：%1").arg(forumName));
        syncGameAccount();
    });
}

void SkinStationPage::loadSiteStats()
{
    getJson(QStringLiteral("/api/site"), [this](const QJsonObject &root) {
        const QJsonObject stats = root.value(QStringLiteral("stats")).toObject();
        if (stats.isEmpty())
            return;
        if (m_siteStatsLabel)
        {
            m_siteStatsLabel->setText(tr("全站 %1 位玩家 · %2 件皮肤 · %3 次下载")
                                          .arg(compactNumber(stats.value(QStringLiteral("players")).toInt()))
                                          .arg(compactNumber(stats.value(QStringLiteral("skins")).toInt()))
                                          .arg(compactNumber(stats.value(QStringLiteral("downloads")).toInt())));
        }
    });
}

void SkinStationPage::loadDetail(int id)
{
    if (id <= 0)
        return;
    getJson(QStringLiteral("/api/textures/%1").arg(id), [this, id](const QJsonObject &root) {
        const QJsonObject texture = root.value(QStringLiteral("texture")).toObject();
        if (texture.isEmpty())
            return;

        SkinItem updated = itemFromJson(texture, false);
        updated.id = id;
        updated.favoriteCount = root.value(QStringLiteral("favorite_count")).toInt();
        updated.comments = texture.value(QStringLiteral("comments")).toInt();
        updated.liked = texture.value(QStringLiteral("liked")).toBool();
        updated.favorited = texture.value(QStringLiteral("favorited")).toBool();
        updated.isPublic = texture.value(QStringLiteral("public")).toBool();
        const QJsonObject owner = texture.value(QStringLiteral("owner")).toObject();
        if (!owner.isEmpty())
        {
            updated.ownerId = owner.value(QStringLiteral("id")).toInt();
            if (!owner.value(QStringLiteral("player_name")).toString().isEmpty())
                updated.ownerName = owner.value(QStringLiteral("player_name")).toString();
            else if (!owner.value(QStringLiteral("display_name")).toString().isEmpty())
                updated.ownerName = owner.value(QStringLiteral("display_name")).toString();
            else
                updated.ownerName = owner.value(QStringLiteral("username")).toString();
        }
        updated.mine = m_userId > 0 && updated.ownerId == m_userId;

        mergeItemState(updated);
        if (m_selectedId == id)
        {
            m_related.clear();
            for (const QJsonValue &value : root.value(QStringLiteral("related")).toArray())
            {
                SkinItem related = itemFromJson(value.toObject(), false);
                related.mine = m_userId > 0 && related.ownerId == m_userId;
                m_related.append(related);
            }
            m_detailId = id;
            m_detailComments = updated.comments;
            m_detailFavoriteCount = updated.favoriteCount;
            m_detailLiked = updated.liked;
            updateDetail();
            if (!m_related.isEmpty() && m_relatedBtn)
                m_relatedBtn->setToolTip(tr("同作者还有 %1 件公开作品").arg(m_related.size()));
        }
    });
}

void SkinStationPage::mergeItemState(const SkinItem &updated)
{
    for (QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_favorites, &m_history, &m_library})
    {
        for (SkinItem &entry : *vec)
        {
            if (entry.id != updated.id)
                continue;
            entry.name = updated.name.isEmpty() ? entry.name : updated.name;
            entry.model = updated.model.isEmpty() ? entry.model : updated.model;
            entry.url = updated.url.isEmpty() ? entry.url : updated.url;
            entry.comments = updated.comments;
            entry.favoriteCount = updated.favoriteCount;
            entry.liked = updated.liked;
            entry.favorited = updated.favorited;
            entry.isPublic = updated.isPublic;
            entry.mine = updated.mine || entry.mine;
            if (!updated.ownerName.isEmpty())
                entry.ownerName = updated.ownerName;
            if (updated.ownerId > 0)
                entry.ownerId = updated.ownerId;
        }
    }
}

void SkinStationPage::syncGameAccount()
{
    SettingsManager *settings = SettingsManager::instance();
    if (!settings || m_playerName.isEmpty() || m_playerUuid.isEmpty())
        return;

    // 皮肤站在启动器里就是一个内置的第三方认证服务器：这里算出和
    // 「添加角色 → 第三方」一致的根地址，账户列表按它认出皮肤站角色
    const QString skinRoot =
        AuthManager::normalizeAuthServerUrl(skinSiteBaseUrl() + QStringLiteral("/api/yggdrasil/"));
    if (skinRoot.isEmpty())
        return;

    const QList<AccountInfo> accounts = settings->getAccounts();
    QString slotName;
    bool slotDefault = false;
    for (const AccountInfo &account : accounts)
    {
        if (AuthManager::normalizeAuthServerUrl(account.serverUrl) == skinRoot)
        {
            slotName = account.username;
            slotDefault = account.isDefault;
            break;
        }
    }

    // 名字没变：之前已经同步过，不用再动
    if (!slotName.isEmpty() && slotName.compare(m_playerName, Qt::CaseInsensitive) == 0)
        return;

    // 新角色名和其他账户撞名时先不动，避免同步出两个同名账户
    for (const AccountInfo &account : accounts)
    {
        if (account.username.compare(m_playerName, Qt::CaseInsensitive) == 0)
        {
            showStatus(tr("游戏账户里已有叫 %1 的角色，皮肤站角色暂未同步。").arg(m_playerName), true);
            return;
        }
    }

    // 用论坛登录态换一份游戏令牌；旧版服务端没有这个接口时静默跳过
    QNetworkRequest request = requestFor(QStringLiteral("/api/me/game-token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net->post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, skinRoot, slotName, slotDefault]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        if (status != 200)
        {
            // 404：服务端还没部署新接口，安静等待；其余错误给个轻提示
            if (status > 0 && status != 404)
                showStatus(tr("皮肤站角色没能同步到游戏账户（HTTP %1）").arg(status), true);
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        const QString name = profile.value(QStringLiteral("name")).toString();
        const QString uuid = profile.value(QStringLiteral("id")).toString();
        const QString skinUrl =
            profile.value(QStringLiteral("skin")).toObject().value(QStringLiteral("url")).toString();
        const QString accessToken = root.value(QStringLiteral("accessToken")).toString();
        const QString clientToken = root.value(QStringLiteral("clientToken")).toString();
        if (name.isEmpty() || uuid.isEmpty() || accessToken.isEmpty())
        {
            showStatus(tr("皮肤站角色没能同步到游戏账户：返回数据不完整"), true);
            return;
        }

        SettingsManager *settings = SettingsManager::instance();
        if (!settings)
            return;

        // 槽位里是改过名的旧角色（或换过论坛账号）：先把旧的拿掉再存新的
        const bool replacing = !slotName.isEmpty() && slotName.compare(name, Qt::CaseInsensitive) != 0;
        if (replacing)
            settings->removeAccount(slotName);

        // 全新的角色只有在还没有任何游戏账户时才设为默认，不抢当前正在用的角色
        const bool makeDefault = replacing ? slotDefault : settings->getAccounts().isEmpty();
        if (!settings->addThirdPartyAccount(name, tr("方块皮肤站"), skinRoot, accessToken, clientToken,
                                            uuid, skinUrl, makeDefault))
        {
            showStatus(tr("皮肤站角色登录成功，但没能保存到账户列表，请检查启动器所在文件夹的写入权限"), true);
            return;
        }

        if (replacing)
        {
            showStatus(tr("皮肤站角色已更新：%1").arg(name));
        }
        else
        {
            showStatus(tr("已把皮肤站角色 %1 同步到账户管理页").arg(name));
            NotificationManager::showSuccess(this, tr("已把皮肤站角色 %1 同步到游戏账户").arg(name));
        }
    });
}

void SkinStationPage::loadFavorites()
{
    getJson(QStringLiteral("/api/me/favorites"), [this](const QJsonObject &root) {
        m_favorites.clear();
        const int myId = m_userId > 0 ? m_userId : (m_account ? m_account->user().id : 0);
        for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray())
        {
            SkinItem item = itemFromJson(value.toObject(), false);
            item.mine = myId > 0 && item.ownerId == myId;
            m_favorites.append(item);
        }
        if (m_tab == TabFavorites)
            fillList();
    });
}

void SkinStationPage::loadHistory()
{
    getJson(QStringLiteral("/api/me/history"), [this](const QJsonObject &root) {
        m_history.clear();
        const int myId = m_userId > 0 ? m_userId : (m_account ? m_account->user().id : 0);
        for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray())
        {
            SkinItem item = itemFromJson(value.toObject(), false);
            item.mine = myId > 0 && item.ownerId == myId;
            m_history.append(item);
        }
        if (m_tab == TabHistory)
            fillList();
    });
}

void SkinStationPage::loadLibrary(int page, const QString &sortOverride)
{
    m_libraryPage = qMax(1, page);
    const QString sort = sortOverride.isEmpty() ? m_librarySort : sortOverride;
    m_libraryRandom = sort == QLatin1String("random");

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), m_libraryKind);
    query.addQueryItem(QStringLiteral("sort"), sort);
    query.addQueryItem(QStringLiteral("page"), QString::number(m_libraryPage));
    query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("24"));
    if (!m_libraryQuery.isEmpty())
        query.addQueryItem(QStringLiteral("q"), m_libraryQuery);
    if (!m_libraryModel.isEmpty() && m_libraryKind == QLatin1String("skin"))
        query.addQueryItem(QStringLiteral("model"), m_libraryModel);

    showStatus(m_libraryRandom ? tr("正在随机挑一批作品…") : tr("正在加载皮肤库…"));
    getJson(QStringLiteral("/api/library?") + query.toString(QUrl::FullyEncoded), [this](const QJsonObject &root) {
        m_library.clear();
        const int myId = m_userId > 0 ? m_userId : (m_account ? m_account->user().id : 0);
        for (const QJsonValue &value : root.value(QStringLiteral("items")).toArray())
        {
            SkinItem item = itemFromJson(value.toObject(), false);
            item.mine = myId > 0 && item.ownerId == myId;
            m_library.append(item);
        }
        m_libraryPage = qMax(1, root.value(QStringLiteral("page")).toInt());
        m_libraryPages = qMax(1, root.value(QStringLiteral("pages")).toInt());
        if (m_libraryRandom)
        {
            m_pageLabel->setText(tr("随机推荐"));
            m_prevPage->setEnabled(false);
            m_nextPage->setEnabled(false);
        }
        else
        {
            m_pageLabel->setText(tr("第 %1 / %2 页").arg(m_libraryPage).arg(m_libraryPages));
            m_prevPage->setEnabled(m_libraryPage > 1);
            m_nextPage->setEnabled(m_libraryPage < m_libraryPages);
        }
        if (m_tab == TabLibrary)
            fillList();
        showStatus(m_libraryKind == QLatin1String("cape")
                       ? tr("披风库共 %1 件作品").arg(root.value(QStringLiteral("total")).toInt())
                       : tr("皮肤库共 %1 件作品").arg(root.value(QStringLiteral("total")).toInt()));
    });
}

QVector<SkinStationPage::SkinItem> &SkinStationPage::currentVector()
{
    switch (m_tab)
    {
    case TabCapes:
        return m_capes;
    case TabPresets:
        return m_presets;
    case TabFavorites:
        return m_favorites;
    case TabHistory:
        return m_history;
    case TabLibrary:
        return m_library;
    case TabMySkins:
    default:
        return m_mySkins;
    }
}

SkinStationPage::SkinItem SkinStationPage::currentItem() const
{
    const QVector<SkinItem> &list = const_cast<SkinStationPage *>(this)->currentVector();
    for (const SkinItem &item : list)
    {
        if (item.id == m_selectedId)
            return item;
    }
    for (const QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_presets, &m_favorites, &m_history, &m_library})
    {
        for (const SkinItem &item : *vec)
        {
            if (item.id == m_selectedId)
                return item;
        }
    }
    return SkinItem();
}

void SkinStationPage::fillList()
{
    if (!m_list)
        return;
    m_list->blockSignals(true);
    m_list->clear();
    const QVector<SkinItem> &items = currentVector();

    if (items.isEmpty())
    {
        QString emptyText = tr("这里还没有内容");
        if (m_tab == TabCapes)
            emptyText = tr("还没有披风\n点下面的「上传」，选一张 64×32 的 PNG 吧");
        else if (m_tab == TabPresets)
            emptyText = tr("官方预设还没加载出来\n检查网络后点顶栏刷新再试");
        else if (m_tab == TabFavorites)
            emptyText = tr("收藏夹是空的\n在皮肤库或网页详情里点「收藏」，作品就会出现在这里");
        else if (m_tab == TabHistory)
            emptyText = tr("还没有使用记录\n穿过皮肤或披风之后，这里会自动记下来");
        else if (m_tab == TabLibrary)
            emptyText = tr("没有找到作品\n换个关键词或筛选条件再试试");
        else if (m_tab == TabMySkins)
            emptyText = tr("衣柜还是空的\n点下面的「上传」，传一张自己的皮肤吧");
        auto *empty = new QListWidgetItem(emptyText, m_list);
        empty->setFlags(Qt::NoItemFlags);
        empty->setSizeHint(QSize(360, 70));
        m_list->blockSignals(false);
        updateDetail();
        return;
    }

    for (const SkinItem &item : items)
    {
        auto *row = new QListWidgetItem(item.name.isEmpty() ? tr("未命名皮肤") : item.name, m_list);
        QString meta;
        if (!item.presetKey.isEmpty())
            meta = tr("官方预设 · %1").arg(item.model == QLatin1String("slim") ? QStringLiteral("Slim")
                                                                               : QStringLiteral("Classic"));
        else
            meta = item.kind == QLatin1String("cape")
                       ? tr("披风")
                       : (item.model == QLatin1String("slim") ? tr("Slim") : tr("Classic"));
        if (item.likes > 0)
            meta += tr(" · ♥ %1").arg(compactNumber(item.likes));
        if (item.downloads > 0)
            meta += tr(" · ↓ %1").arg(compactNumber(item.downloads));
        row->setData(Qt::UserRole, item.id);
        row->setData(Qt::UserRole + 1, meta);
        row->setSizeHint(QSize(122, 158));
        row->setToolTip(QStringLiteral("%1\n%2\n%3")
                            .arg(item.name,
                                 item.presetKey.isEmpty()
                                     ? (item.kind == QLatin1String("cape") ? tr("披风") : tr("皮肤"))
                                     : tr("官方预设"),
                                 item.presetKey.isEmpty()
                                     ? (item.isPublic ? tr("已公开") : tr("仅自己可见"))
                                     : item.presetDesc));

        const int id = item.id;
        const bool cape = item.kind == QLatin1String("cape");
        fetchImage(item.url, [this, id, cape](const QImage &image) {
            for (int i = 0; i < m_list->count(); ++i)
            {
                QListWidgetItem *target = m_list->item(i);
                if (target && target->data(Qt::UserRole).toInt() == id)
                {
                    target->setIcon(cape ? capeIcon(image, 82) : headIcon(image, 82));
                    break;
                }
            }
        });
    }
    m_list->blockSignals(false);

    int rowToSelect = 0;
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toInt() == m_selectedId)
        {
            rowToSelect = i;
            break;
        }
    }
    m_list->setCurrentRow(rowToSelect);
    updateDetail();
    const SkinItem selected = currentItem();
    if (selected.id > 0)
    {
        previewItem(selected);
        loadDetail(selected.id);
    }
    else if (!selected.presetKey.isEmpty())
    {
        previewItem(selected);
    }
    m_list->blockSignals(false);
}

void SkinStationPage::selectFirst()
{
    if (m_list && m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void SkinStationPage::previewCurrentProfile()
{
    if (!m_preview)
        return;
    m_preview->setModelType(m_model == QLatin1String("slim") ? SkinModelType::Slim : SkinModelType::Default);
    if (!m_currentCapeUrl.isEmpty())
        fetchImage(m_currentCapeUrl, [this](const QImage &image) { m_preview->setCape(image); });
    else
        m_preview->clearCape();

    if (!m_currentSkinUrl.isEmpty())
        fetchImage(m_currentSkinUrl, [this](const QImage &image) { m_preview->setSkin(image); });
    else
        m_preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
}

void SkinStationPage::previewItem(const SkinItem &item)
{
    if (!m_preview)
        return;
    if (item.kind == QLatin1String("cape"))
    {
        fetchImage(item.url, [this](const QImage &image) { m_preview->setCape(image); });
        return;
    }

    m_preview->setModelType(item.model == QLatin1String("slim") ? SkinModelType::Slim : SkinModelType::Default);
    fetchImage(item.url, [this](const QImage &image) { m_preview->setSkin(image); });
    if (!m_currentCapeUrl.isEmpty())
        fetchImage(m_currentCapeUrl, [this](const QImage &image) { m_preview->setCape(image); });
    else
        m_preview->clearCape();
}

void SkinStationPage::updateDetail()
{
    const SkinItem item = currentItem();
    const bool preset = !item.presetKey.isEmpty();
    const bool has = item.id > 0 || preset;
    const bool detailReady = item.id > 0 && m_detailId == item.id;

    // 操作按钮始终占位（禁用的点亮不了），布局不会因为选中作品不同而跳来跳去
    const QVector<QPushButton *> allButtons = {m_applyBtn,    m_favoriteBtn, m_likeBtn,  m_publicBtn,
                                               m_renameBtn,   m_deleteBtn,   m_downloadBtn, m_shareBtn,
                                               m_commentsBtn, m_reportBtn,   m_ownerBtn, m_relatedBtn,
                                               m_detailBtn};
    for (QPushButton *button : allButtons)
        button->setVisible(true);
    if (!has)
    {
        for (QPushButton *button : allButtons)
            button->setEnabled(false);
        m_detailTitle->setText(tr("选择一件作品"));
        m_detailMeta->setText(tr("点击列表里的皮肤，就能在左边转着看。"));
        return;
    }

    m_detailTitle->setText(item.name.isEmpty() ? tr("未命名皮肤") : item.name);

    if (preset)
    {
        QStringList bits;
        bits << tr("官方预设")
             << (item.model == QLatin1String("slim") ? QStringLiteral("Slim") : QStringLiteral("Classic"));
        if (!item.presetDesc.isEmpty())
            bits << item.presetDesc;
        bits << tr("点「穿上」会复制进衣柜并立即穿到角色上");
        m_detailMeta->setText(bits.join(QStringLiteral(" · ")));

        m_applyBtn->setEnabled(true);
        for (QPushButton *button : allButtons)
        {
            if (button != m_applyBtn)
                button->setEnabled(false);
        }
        return;
    }

    QStringList bits;
    bits << (item.kind == QLatin1String("cape") ? tr("披风")
                                                : (item.model == QLatin1String("slim") ? tr("Slim")
                                                                                       : tr("Classic")));
    if (!item.ownerName.isEmpty())
        bits << (item.mine ? tr("我的作品") : tr("@%1").arg(item.ownerName));
    if (item.mine)
        bits << (item.isPublic ? tr("已公开") : tr("仅自己可见"));
    bits << tr("♥ %1").arg(compactNumber(item.likes));
    if (detailReady)
        bits << tr("收藏 %1").arg(compactNumber(m_detailFavoriteCount));
    bits << tr("下载 %1").arg(compactNumber(item.downloads));
    bits << tr("评论 %1").arg(compactNumber(detailReady ? m_detailComments : item.comments));
    if (!item.createdAt.isEmpty())
        bits << item.createdAt;
    m_detailMeta->setText(bits.join(QStringLiteral(" · ")));

    m_applyBtn->setEnabled(true);
    m_downloadBtn->setEnabled(true);
    m_shareBtn->setEnabled(true);
    m_detailBtn->setEnabled(true);
    m_commentsBtn->setEnabled(true);
    m_ownerBtn->setEnabled(!item.ownerName.isEmpty());
    m_ownerBtn->setText(item.mine ? tr("我的主页") : tr("作者主页"));

    if (item.mine)
    {
        m_publicBtn->setEnabled(true);
        m_renameBtn->setEnabled(true);
        m_deleteBtn->setEnabled(true);
        m_favoriteBtn->setEnabled(false);
        m_likeBtn->setEnabled(false);
        m_reportBtn->setEnabled(false);
        m_publicBtn->setText(item.isPublic ? tr("设为私密") : tr("公开分享"));
        m_favoriteBtn->setText(tr("收藏"));
        m_likeBtn->setText(tr("点赞"));
    }
    else
    {
        m_publicBtn->setEnabled(false);
        m_renameBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        m_favoriteBtn->setEnabled(true);
        m_likeBtn->setEnabled(true);
        m_favoriteBtn->setText(item.favorited ? tr("已收藏") : tr("收藏"));
        m_likeBtn->setText(detailReady && m_detailLiked ? tr("已点赞") : tr("点赞"));
        m_reportBtn->setEnabled(item.isPublic);
    }

    m_relatedBtn->setEnabled(detailReady && !m_related.isEmpty());
    m_relatedBtn->setText(detailReady && !m_related.isEmpty()
                              ? tr("同作者作品（%1）").arg(m_related.size())
                              : tr("同作者作品"));
    m_relatedBtn->setToolTip(tr("看看这位作者公开的其他作品"));
}

void SkinStationPage::applySelected()
{
    const SkinItem item = currentItem();
    if (item.id <= 0 && item.presetKey.isEmpty())
    {
        showStatus(tr("先在列表里选一件皮肤或披风。"), true);
        return;
    }
    showStatus(tr("正在穿上「%1」…").arg(item.name));
    applyItem(item);
}

void SkinStationPage::applyItem(const SkinItem &item, const std::function<void(bool)> &done)
{
    const QString path = !item.presetKey.isEmpty()
                             ? QStringLiteral("/api/presets/%1/use")
                                   .arg(QString::fromUtf8(QUrl::toPercentEncoding(item.presetKey)))
                             : QStringLiteral("/api/textures/%1/apply").arg(item.id);
    auto finish = [this, item, done](bool ok) {
        if (done)
            done(ok);
        if (!ok)
            return;
        previewCurrentProfile();
        loadHistory();
        if (!item.presetKey.isEmpty())
            loadMe(); // 预设会复制一件新作品进衣柜，重新拉一次
        showStatus(item.kind == QLatin1String("cape") ? tr("披风已更新，下次进游戏生效。")
                                                      : tr("皮肤已更新，下次进游戏生效。"));
    };

    postJson(path, QJsonObject(), [this, item, finish](const QJsonObject &root) {
        const QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        if (!profile.isEmpty())
        {
            const QString skinUrl =
                profile.value(QStringLiteral("skin")).toObject().value(QStringLiteral("url")).toString();
            const QString capeUrl =
                profile.value(QStringLiteral("cape")).toObject().value(QStringLiteral("url")).toString();
            if (!skinUrl.isEmpty())
                m_currentSkinUrl = skinUrl;
            if (!capeUrl.isEmpty() || item.kind == QLatin1String("cape"))
                m_currentCapeUrl = capeUrl;
            const QString model = profile.value(QStringLiteral("model")).toString();
            if (!model.isEmpty())
                m_model = model;
        }
        else if (item.kind != QLatin1String("cape") && !item.model.isEmpty())
        {
            m_model = item.model;
        }
        if (m_modelChip)
            m_modelChip->setText(m_model == QLatin1String("slim") ? QStringLiteral("Slim")
                                                                  : QStringLiteral("Classic"));
        finish(true);
    });
}

void SkinStationPage::toggleFavorite()
{
    const SkinItem item = currentItem();
    if (item.id <= 0)
        return;
    postJson(QStringLiteral("/api/textures/%1/favorite").arg(item.id), QJsonObject(), [this, item](const QJsonObject &root) {
        const bool favorited = root.value(QStringLiteral("favorited")).toBool();
        for (QVector<SkinItem> *vec : {&m_library, &m_capes, &m_mySkins, &m_history})
        {
            for (SkinItem &entry : *vec)
                if (entry.id == item.id)
                    entry.favorited = favorited;
        }
        if (m_selectedId == item.id)
            m_detailFavoriteCount = qMax(0, m_detailFavoriteCount + (favorited ? 1 : -1));
        showStatus(favorited ? tr("已加入收藏") : tr("已取消收藏"));
        loadFavorites();
        updateDetail();
    });
}

void SkinStationPage::toggleLike()
{
    const SkinItem item = currentItem();
    if (item.id <= 0)
        return;
    postJson(QStringLiteral("/api/textures/%1/like").arg(item.id), QJsonObject(), [this, item](const QJsonObject &root) {
        const int likes = root.value(QStringLiteral("likes")).toInt();
        const bool liked = root.value(QStringLiteral("liked")).toBool();
        for (QVector<SkinItem> *vec : {&m_library, &m_capes, &m_favorites, &m_history})
        {
            for (SkinItem &entry : *vec)
            {
                if (entry.id == item.id)
                {
                    entry.likes = likes;
                    entry.liked = liked;
                }
            }
        }
        if (m_selectedId == item.id)
            m_detailLiked = liked;
        updateDetail();
        fillList();
        showStatus(liked ? tr("已点赞") : tr("已取消点赞"));
    });
}

void SkinStationPage::togglePublic()
{
    const SkinItem item = currentItem();
    if (item.id <= 0 || !item.mine)
        return;
    const bool next = !item.isPublic;
    QJsonObject body;
    body.insert(QStringLiteral("public"), next);
    sendJson(QStringLiteral("/api/textures/%1").arg(item.id), "PATCH", body, [this, item, next](const QJsonObject &) {
        for (QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_library})
        {
            for (SkinItem &entry : *vec)
                if (entry.id == item.id)
                    entry.isPublic = next;
        }
        fillList();
        updateDetail();
        showStatus(next ? tr("已公开分享到皮肤库") : tr("已设为仅自己可见"));
    });
}

void SkinStationPage::downloadSelected()
{
    const SkinItem item = currentItem();
    if (item.id <= 0)
        return;
    const QString name = item.name.isEmpty() ? QStringLiteral("skin") : item.name;
    const QString path = QFileDialog::getSaveFileName(this, tr("保存皮肤 PNG"),
                                                      QDir::homePath() + QLatin1Char('/') + name + QStringLiteral(".png"),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    showStatus(tr("正在下载「%1」…").arg(name));
    // ?download=1 才会在皮肤站上加一次下载计数（和网页版的下载按钮一致）
    const QString url = item.url + QStringLiteral("?download=1");
    QNetworkRequest request = requestFor(url);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, item]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status >= 400 || data.isEmpty())
        {
            showStatus(tr("下载失败，稍后再试。"), true);
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            showStatus(tr("保存失败：没有写入权限或磁盘满了。"), true);
            return;
        }
        for (QVector<SkinItem> *vec : {&m_library, &m_favorites, &m_history, &m_mySkins, &m_capes})
        {
            for (SkinItem &entry : *vec)
                if (entry.id == item.id)
                    ++entry.downloads;
        }
        updateDetail();
        showStatus(tr("已保存到 %1").arg(path));
    });
}

void SkinStationPage::reportSelected()
{
    const SkinItem item = currentItem();
    if (item.id <= 0 || item.mine)
        return;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("举报这件作品"));
    dialog.setMinimumWidth(420);
    auto *form = new QFormLayout(&dialog);
    auto *reason = new QComboBox(&dialog);
    reason->addItem(tr("色情低俗"), QStringLiteral("sexual"));
    reason->addItem(tr("暴力血腥"), QStringLiteral("violence"));
    reason->addItem(tr("辱骂 / 引战"), QStringLiteral("hate"));
    reason->addItem(tr("垃圾广告"), QStringLiteral("ad"));
    reason->addItem(tr("盗用他人作品"), QStringLiteral("copyright"));
    reason->addItem(tr("其他"), QStringLiteral("other"));
    auto *detail = new QLineEdit(&dialog);
    detail->setPlaceholderText(tr("补充说明（可选，最多 200 字）"));
    form->addRow(tr("举报原因"), reason);
    form->addRow(tr("说明"), detail);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("提交举报"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("reason"), reason->currentData().toString());
    body.insert(QStringLiteral("detail"), detail->text().trimmed().left(200));
    postJson(QStringLiteral("/api/textures/%1/report").arg(item.id), body, [this](const QJsonObject &) {
        showStatus(tr("举报已提交，管理员会尽快处理。"));
    });
}

void SkinStationPage::shareSelected()
{
    const SkinItem item = currentItem();
    if (item.id <= 0)
        return;
    const QString url = QStringLiteral("%1/skin.html?id=%2").arg(skinSiteBaseUrl()).arg(item.id);
    if (QGuiApplication::clipboard())
    {
        QGuiApplication::clipboard()->setText(url);
        showStatus(tr("链接已复制：%1").arg(url));
    }
}

void SkinStationPage::openWebDetail()
{
    const SkinItem item = currentItem();
    const QString url = item.id > 0
                            ? QStringLiteral("%1/skin.html?id=%2").arg(skinSiteBaseUrl()).arg(item.id)
                            : QStringLiteral("%1/").arg(skinSiteBaseUrl());
    QDesktopServices::openUrl(QUrl(url));
}

void SkinStationPage::savePreview()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存预览图"),
                                                      QDir::homePath() + QStringLiteral("/skin-preview.png"),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    const QPixmap pixmap = m_preview->grab();
    if (pixmap.isNull() || !pixmap.save(path))
    {
        showStatus(tr("预览图保存失败。"), true);
        return;
    }
    showStatus(tr("预览图已保存到 %1").arg(path));
}

bool SkinStationPage::isModerator() const
{
    return m_role == QLatin1String("admin") || m_role == QLatin1String("moderator");
}

void SkinStationPage::openEditDialog()
{
    const SkinItem item = currentItem();
    if (item.id <= 0 || !item.mine)
        return;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("编辑作品"));
    dialog.setMinimumWidth(430);
    auto *form = new QFormLayout(&dialog);

    auto *name = new QLineEdit(item.name, &dialog);
    name->setMaxLength(40);
    name->setPlaceholderText(tr("给这件作品起个名字"));
    auto *publicBox = new QCheckBox(tr("公开分享到皮肤库"), &dialog);
    publicBox->setChecked(item.isPublic);

    auto *hint = new QLabel(tr("改名只改显示名称，不影响已经穿上的皮肤；设为公开后其他玩家能在皮肤库看到它。"),
                            &dialog);
    hint->setObjectName(QStringLiteral("skinMuted"));
    hint->setWordWrap(true);

    form->addRow(tr("名称"), name);
    form->addRow(QString(), publicBox);
    form->addRow(QString(), hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    name->setFocus();
    name->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString newName = name->text().trimmed().left(40);
    const bool newPublic = publicBox->isChecked();
    QJsonObject body;
    body.insert(QStringLiteral("name"), newName);
    body.insert(QStringLiteral("public"), newPublic);
    sendJson(QStringLiteral("/api/textures/%1").arg(item.id), "PATCH", body,
             [this, item, newName, newPublic](const QJsonObject &) {
                 for (QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_library})
                 {
                     for (SkinItem &entry : *vec)
                     {
                         if (entry.id != item.id)
                             continue;
                         entry.name = newName;
                         entry.isPublic = newPublic;
                     }
                 }
                 fillList();
                 updateDetail();
                 showStatus(tr("作品资料已保存"));
             });
}

void SkinStationPage::deleteCurrent()
{
    const SkinItem item = currentItem();
    if (item.id <= 0 || !item.mine)
        return;

    const bool confirmed = QMessageBox::question(
                               this, tr("删除这件作品？"),
                               tr("删除后它会从你的衣柜里移除；如果正穿着它，角色会回到默认皮肤。\n"
                                  "这个操作不能撤销，确定删除「%1」吗？")
                                   .arg(item.name.isEmpty() ? tr("未命名作品") : item.name),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    if (!confirmed)
        return;

    showStatus(tr("正在删除「%1」…").arg(item.name));
    sendJson(QStringLiteral("/api/textures/%1").arg(item.id), "DELETE", QJsonObject(),
             [this, item](const QJsonObject &) {
                 for (QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_favorites, &m_history, &m_library})
                 {
                     for (int i = vec->size() - 1; i >= 0; --i)
                         if (vec->at(i).id == item.id)
                             vec->remove(i);
                 }
                 if (m_selectedId == item.id)
                 {
                     m_selectedId = -1;
                     m_detailId = -1;
                 }
                 fillList();
                 updateDetail();
                 showStatus(tr("已删除「%1」").arg(item.name));
                 loadFavorites();
                 loadHistory();
                 loadMe();
             });
}

void SkinStationPage::openCommentsDialog()
{
    const SkinItem item = currentItem();
    if (item.id <= 0)
        return;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("评论 · %1").arg(item.name.isEmpty() ? tr("未命名作品") : item.name));
    dialog.resize(640, 560);

    auto *root = new QVBoxLayout(&dialog);
    auto *header = new QLabel(tr("评论"), &dialog);
    header->setObjectName(QStringLiteral("skinDetailTitle"));
    root->addWidget(header);

    auto *countLabel = new QLabel(tr("正在加载评论…"), &dialog);
    countLabel->setObjectName(QStringLiteral("skinMuted"));
    root->addWidget(countLabel);

    auto *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("skinList"));
    list->setWordWrap(true);
    root->addWidget(list, 1);

    auto *composerRow = new QHBoxLayout();
    auto *input = new QPlainTextEdit(&dialog);
    input->setPlaceholderText(tr("说点什么…（最多 300 字）"));
    input->setMaximumHeight(84);
    composerRow->addWidget(input, 1);
    auto *sendBtn = new QPushButton(tr("发表评论"), &dialog);
    sendBtn->setObjectName(QStringLiteral("skinActionPrimary"));
    sendBtn->setCursor(Qt::PointingHandCursor);
    composerRow->addWidget(sendBtn);
    root->addLayout(composerRow);

    auto *counter = new QLabel(tr("0 / 300"), &dialog);
    counter->setObjectName(QStringLiteral("skinMuted"));
    auto *footer = new QHBoxLayout();
    footer->addWidget(counter);
    footer->addStretch();
    auto *deleteBtn = new QPushButton(tr("删除所选评论"), &dialog);
    deleteBtn->setObjectName(QStringLiteral("skinSmallDanger"));
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setEnabled(false);
    footer->addWidget(deleteBtn);
    auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
    closeBtn->setObjectName(QStringLiteral("skinSmall"));
    footer->addWidget(closeBtn);
    root->addLayout(footer);

    QPointer<QDialog> guard(&dialog);

    auto setComments = [this, item, list, countLabel, deleteBtn](const QJsonObject &rootObject) {
        list->clear();
        const QJsonArray array = rootObject.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : array)
        {
            const QJsonObject object = value.toObject();
            CommentItem comment;
            comment.id = object.value(QStringLiteral("id")).toInt();
            const QJsonObject user = object.value(QStringLiteral("user")).toObject();
            comment.userId = user.value(QStringLiteral("id")).toInt();
            comment.author = user.value(QStringLiteral("player_name")).toString();
            if (comment.author.isEmpty())
                comment.author = user.value(QStringLiteral("display_name")).toString();
            if (comment.author.isEmpty())
                comment.author = user.value(QStringLiteral("username")).toString();
            comment.content = object.value(QStringLiteral("content")).toString();
            const qint64 created = qint64(object.value(QStringLiteral("created_at")).toDouble());
            comment.createdAt = relativeTime(created);
            comment.canDelete = (m_userId > 0 && comment.userId == m_userId) || isModerator();

            auto *row = new QListWidgetItem(
                QStringLiteral("%1 · %2\n%3").arg(comment.author, comment.createdAt, comment.content), list);
            row->setData(Qt::UserRole, comment.id);
            row->setData(Qt::UserRole + 1, comment.canDelete);
            // 评论可能很长，按字数估行数，别把内容裁掉
            const int lines = qBound(1, (comment.content.size() + 37) / 38, 8);
            row->setSizeHint(QSize(0, 42 + lines * 18));
        }
        if (array.isEmpty())
        {
            auto *empty = new QListWidgetItem(tr("还没有评论，来说说这件皮肤怎么样吧。"), list);
            empty->setFlags(Qt::NoItemFlags);
            empty->setSizeHint(QSize(0, 60));
        }
        countLabel->setText(tr("%1 条评论").arg(rootObject.value(QStringLiteral("count")).toInt()));
        deleteBtn->setEnabled(false);
        if (m_detailId == item.id)
            m_detailComments = rootObject.value(QStringLiteral("count")).toInt();
        for (QVector<SkinItem> *vec : {&m_mySkins, &m_capes, &m_favorites, &m_history, &m_library})
        {
            for (SkinItem &entry : *vec)
                if (entry.id == item.id)
                    entry.comments = rootObject.value(QStringLiteral("count")).toInt();
        }
        updateDetail();
    };

    auto loadComments = [this, item, guard, setComments]() {
        if (!guard)
            return;
        getJson(QStringLiteral("/api/textures/%1/comments").arg(item.id),
                [guard, setComments](const QJsonObject &rootObject) {
                    if (guard)
                        setComments(rootObject);
                });
    };

    connect(list, &QListWidget::currentItemChanged, &dialog, [list, deleteBtn](QListWidgetItem *current, QListWidgetItem *) {
        deleteBtn->setEnabled(current && current->data(Qt::UserRole + 1).toBool());
    });
    connect(input, &QPlainTextEdit::textChanged, &dialog, [input, counter]() {
        const int length = input->toPlainText().length();
        counter->setText(tr("%1 / 300").arg(length));
        counter->setStyleSheet(length > 300 ? QStringLiteral("color:#c62828;") : QString());
    });
    connect(sendBtn, &QPushButton::clicked, &dialog, [this, item, input, sendBtn, loadComments, counter, guard]() {
        const QString content = input->toPlainText().trimmed();
        if (content.isEmpty())
        {
            showStatus(tr("先写点内容再发吧"), true);
            return;
        }
        if (content.length() > 300)
        {
            showStatus(tr("评论最多 300 个字"), true);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("content"), content);
        sendBtn->setEnabled(false);
        postJson(QStringLiteral("/api/textures/%1/comments").arg(item.id), body,
                 [this, input, sendBtn, loadComments, counter, guard](const QJsonObject &) {
                     if (!guard)
                         return;
                     input->clear();
                     counter->setText(tr("0 / 300"));
                     sendBtn->setEnabled(true);
                     showStatus(tr("评论已发布"));
                     loadComments();
                 });
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [this, list, deleteBtn, loadComments, guard]() {
        QListWidgetItem *current = list->currentItem();
        if (!current || !current->data(Qt::UserRole + 1).toBool())
            return;
        const int commentId = current->data(Qt::UserRole).toInt();
        deleteBtn->setEnabled(false);
        sendJson(QStringLiteral("/api/comments/%1").arg(commentId), "DELETE", QJsonObject(),
                 [this, loadComments, guard](const QJsonObject &) {
                     if (!guard)
                         return;
                     showStatus(tr("评论已删除"));
                     loadComments();
                 });
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    loadComments();
    dialog.exec();
}

void SkinStationPage::openRelatedDialog()
{
    if (m_related.isEmpty())
    {
        showStatus(tr("这位作者暂时没有其他公开作品。"));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("同作者的其他作品"));
    dialog.resize(620, 520);
    auto *root = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr("来自 @%1 的公开作品，双击可以穿上。")
                                .arg(m_related.first().ownerName.isEmpty() ? tr("这位作者")
                                                                           : m_related.first().ownerName),
                            &dialog);
    hint->setObjectName(QStringLiteral("skinMuted"));
    root->addWidget(hint);

    auto *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("skinList"));
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(82, 82));
    list->setGridSize(QSize(140, 158));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(list, 1);

    QPointer<QDialog> dialogGuard(&dialog);
    const QVector<SkinItem> items = m_related;
    for (const SkinItem &item : items)
    {
        auto *row = new QListWidgetItem(item.name.isEmpty() ? tr("未命名作品") : item.name, list);
        row->setData(Qt::UserRole, item.id);
        row->setToolTip(item.name);
        const int id = item.id;
        const bool cape = item.kind == QLatin1String("cape");
        fetchImage(item.url, [dialogGuard, list, id, cape](const QImage &image) {
            if (!dialogGuard)
                return;
            for (int i = 0; i < list->count(); ++i)
            {
                QListWidgetItem *target = list->item(i);
                if (target && target->data(Qt::UserRole).toInt() == id)
                {
                    target->setIcon(cape ? capeIcon(image, 82) : headIcon(image, 82));
                    break;
                }
            }
        });
    }

    auto *status = new QLabel(QString(), &dialog);
    status->setObjectName(QStringLiteral("skinMuted"));
    auto *row = new QHBoxLayout();
    auto *applyBtn = new QPushButton(tr("穿上所选"), &dialog);
    applyBtn->setObjectName(QStringLiteral("skinActionPrimary"));
    auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
    closeBtn->setObjectName(QStringLiteral("skinSmall"));
    row->addWidget(status, 1);
    row->addWidget(applyBtn);
    row->addWidget(closeBtn);
    root->addLayout(row);

    QPointer<QLabel> statusGuard(status);
    auto applyPicked = [this, list, items, statusGuard]() {
        const int index = list->currentRow();
        if (index < 0 || index >= items.size())
            return;
        const SkinItem item = items.at(index);
        if (statusGuard)
            statusGuard->setText(tr("正在穿上「%1」…").arg(item.name));
        applyItem(item, [statusGuard](bool ok) {
            if (ok && statusGuard)
                statusGuard->setText(tr("已穿上，进游戏就能看到。"));
        });
    };
    connect(applyBtn, &QPushButton::clicked, &dialog, applyPicked);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [applyPicked](QListWidgetItem *) { applyPicked(); });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void SkinStationPage::openPlayerDialog(const QString &playerName)
{
    if (playerName.isEmpty())
        return;
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(playerName));
    getJson(QStringLiteral("/api/users/%1").arg(encoded), [this, playerName](const QJsonObject &root) {
        const QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        if (profile.isEmpty())
        {
            showStatus(tr("还没有这位玩家的皮肤站角色。"), true);
            return;
        }

        const QString name = profile.value(QStringLiteral("player_name")).toString(playerName);
        const QJsonObject owner = profile.value(QStringLiteral("user")).toObject();
        const QString displayName = owner.value(QStringLiteral("display_name")).toString();
        const QString username = owner.value(QStringLiteral("username")).toString();
        const QString model = profile.value(QStringLiteral("model")).toString(QStringLiteral("classic"));
        QString about = profile.value(QStringLiteral("about")).toString();
        const QJsonObject counts = profile.value(QStringLiteral("count")).toObject();

        QVector<SkinItem> skins;
        QVector<SkinItem> capes;
        for (const QJsonValue &value : profile.value(QStringLiteral("textures")).toArray())
            skins.append(itemFromJson(value.toObject(), false));
        for (const QJsonValue &value : profile.value(QStringLiteral("capes")).toArray())
            capes.append(itemFromJson(value.toObject(), false));
        int likes = 0;
        for (const SkinItem &entry : skins)
            likes += entry.likes;
        for (const SkinItem &entry : capes)
            likes += entry.likes;

        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("skinStationPage"));
        dialog.setStyleSheet(styleSheet());
        dialog.setWindowTitle(tr("玩家主页 · %1").arg(name));
        dialog.resize(800, 660);
        QPointer<QDialog> guard(&dialog);

        auto *rootLayout = new QVBoxLayout(&dialog);

        auto *headRow = new QHBoxLayout();
        auto *viewer = new Skin3DWidget(&dialog);
        viewer->setFixedSize(216, 244);
        viewer->setAutoRotate(true);
        viewer->setBackgroundCornerRadius(12);
        if (m_dark)
            viewer->setBackgroundGradient(QColor("#1b2b21"), QColor("#101a14"));
        else
            viewer->setBackgroundGradient(QColor("#fbfdfb"), QColor("#e8f5e9"));
        viewer->setModelType(model == QLatin1String("slim") ? SkinModelType::Slim : SkinModelType::Default);
        viewer->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
        headRow->addWidget(viewer);

        auto *infoCol = new QVBoxLayout();
        auto *nameLabel = new QLabel(name, &dialog);
        nameLabel->setObjectName(QStringLiteral("skinName"));
        infoCol->addWidget(nameLabel);
        auto *subLabel = new QLabel(
            QStringLiteral("%1@%2 · %3")
                .arg(displayName.isEmpty() ? QString() : displayName + QStringLiteral(" "))
                .arg(username.isEmpty() ? tr("方块盒子玩家") : username)
                .arg(model == QLatin1String("slim") ? QStringLiteral("Slim") : QStringLiteral("Classic")),
            &dialog);
        subLabel->setObjectName(QStringLiteral("skinMuted"));
        infoCol->addWidget(subLabel);
        auto *aboutLabel = new QLabel(about.isEmpty() ? tr("这位玩家还没有写简介。") : about, &dialog);
        aboutLabel->setWordWrap(true);
        aboutLabel->setObjectName(QStringLiteral("skinDetailMeta"));
        infoCol->addWidget(aboutLabel);
        auto *facts = new QLabel(
            tr("%1 件皮肤 · %2 件披风 · %3 获赞 · 加入于 %4")
                .arg(counts.value(QStringLiteral("skins")).toInt())
                .arg(counts.value(QStringLiteral("capes")).toInt())
                .arg(compactNumber(likes))
                .arg(relativeTime(qint64(profile.value(QStringLiteral("created_at")).toDouble()))),
            &dialog);
        facts->setObjectName(QStringLiteral("skinMuted"));
        facts->setWordWrap(true);
        infoCol->addWidget(facts);

        auto *links = new QHBoxLayout();
        auto *copyServerBtn = new QPushButton(tr("复制认证地址"), &dialog);
        copyServerBtn->setObjectName(QStringLiteral("skinSmall"));
        auto *shareBtn = new QPushButton(tr("分享主页"), &dialog);
        shareBtn->setObjectName(QStringLiteral("skinSmall"));
        auto *webBtn = new QPushButton(tr("打开网页主页"), &dialog);
        webBtn->setObjectName(QStringLiteral("skinSmall"));
        links->addWidget(copyServerBtn);
        links->addWidget(shareBtn);
        links->addWidget(webBtn);
        links->addStretch();
        infoCol->addLayout(links);
        infoCol->addStretch();
        headRow->addLayout(infoCol, 1);
        rootLayout->addLayout(headRow);

        auto *listsRow = new QHBoxLayout();
        auto *skinCol = new QVBoxLayout();
        auto *skinTitle = new QLabel(tr("公开的皮肤（%1）").arg(skins.size()), &dialog);
        skinTitle->setObjectName(QStringLiteral("skinDetailTitle"));
        skinCol->addWidget(skinTitle);
        auto *skinList = new QListWidget(&dialog);
        skinList->setObjectName(QStringLiteral("skinList"));
        skinList->setViewMode(QListView::IconMode);
        skinList->setIconSize(QSize(82, 82));
        skinList->setGridSize(QSize(140, 158));
        skinList->setResizeMode(QListView::Adjust);
        skinList->setMovement(QListView::Static);
        skinCol->addWidget(skinList, 1);
        auto *skinApply = new QPushButton(tr("穿上所选皮肤"), &dialog);
        skinApply->setObjectName(QStringLiteral("skinSmall"));
        skinCol->addWidget(skinApply);
        listsRow->addLayout(skinCol, 1);

        auto *capeCol = new QVBoxLayout();
        auto *capeTitle = new QLabel(tr("公开的披风（%1）").arg(capes.size()), &dialog);
        capeTitle->setObjectName(QStringLiteral("skinDetailTitle"));
        capeCol->addWidget(capeTitle);
        auto *capeList = new QListWidget(&dialog);
        capeList->setObjectName(QStringLiteral("skinList"));
        capeList->setViewMode(QListView::IconMode);
        capeList->setIconSize(QSize(82, 82));
        capeList->setGridSize(QSize(140, 158));
        capeList->setResizeMode(QListView::Adjust);
        capeList->setMovement(QListView::Static);
        capeCol->addWidget(capeList, 1);
        auto *capeApply = new QPushButton(tr("换上所选披风"), &dialog);
        capeApply->setObjectName(QStringLiteral("skinSmall"));
        capeCol->addWidget(capeApply);
        listsRow->addLayout(capeCol, 1);
        if (capes.isEmpty())
        {
            capeTitle->hide();
            capeList->hide();
            capeApply->hide();
        }
        rootLayout->addLayout(listsRow, 1);

        auto *dialogStatus = new QLabel(QString(), &dialog);
        dialogStatus->setObjectName(QStringLiteral("skinMuted"));
        auto *bottom = new QHBoxLayout();
        auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
        closeBtn->setObjectName(QStringLiteral("skinSmall"));
        bottom->addWidget(dialogStatus, 1);
        bottom->addWidget(closeBtn);
        rootLayout->addLayout(bottom);

        // 3D 预览：当前穿着的皮肤和披风
        const QJsonObject skinObject = profile.value(QStringLiteral("skin")).toObject();
        const QJsonObject capeObject = profile.value(QStringLiteral("cape")).toObject();
        const QString skinUrl = skinObject.value(QStringLiteral("url")).toString();
        const QString capeUrl = capeObject.value(QStringLiteral("url")).toString();
        if (!skinUrl.isEmpty())
        {
            fetchImage(skinUrl, [guard, viewer](const QImage &image) {
                if (guard)
                    viewer->setSkin(image);
            });
        }
        if (!capeUrl.isEmpty())
        {
            fetchImage(capeUrl, [guard, viewer](const QImage &image) {
                if (guard)
                    viewer->setCape(image);
            });
        }

        auto fillList = [this, guard](QListWidget *list, const QVector<SkinItem> &items) {
            for (const SkinItem &item : items)
            {
                auto *row = new QListWidgetItem(item.name.isEmpty() ? tr("未命名作品") : item.name, list);
                row->setToolTip(item.name);
                const int id = item.id;
                const bool cape = item.kind == QLatin1String("cape");
                fetchImage(item.url, [guard, list, id, cape](const QImage &image) {
                    if (!guard)
                        return;
                    for (int i = 0; i < list->count(); ++i)
                    {
                        QListWidgetItem *target = list->item(i);
                        if (target && target->data(Qt::UserRole).toInt() == id)
                        {
                            target->setIcon(cape ? capeIcon(image, 82) : headIcon(image, 82));
                            break;
                        }
                    }
                });
                row->setData(Qt::UserRole, item.id);
            }
            if (items.isEmpty())
            {
                auto *empty = new QListWidgetItem(tr("还没有公开作品"), list);
                empty->setFlags(Qt::NoItemFlags);
                empty->setSizeHint(QSize(200, 60));
            }
        };
        fillList(skinList, skins);
        fillList(capeList, capes);

        QPointer<QLabel> statusGuard(dialogStatus);
        auto applyPicked = [this, statusGuard](QListWidget *list, const QVector<SkinItem> &items) {
            const int index = list->currentRow();
            if (index < 0 || index >= items.size())
                return;
            const SkinItem item = items.at(index);
            if (statusGuard)
                statusGuard->setText(tr("正在穿上「%1」…").arg(item.name));
            applyItem(item, [statusGuard](bool ok) {
                if (ok && statusGuard)
                    statusGuard->setText(tr("已穿上，进游戏就能看到。"));
            });
        };
        connect(skinApply, &QPushButton::clicked, &dialog, [applyPicked, skinList, skins]() {
            applyPicked(skinList, skins);
        });
        connect(capeApply, &QPushButton::clicked, &dialog, [applyPicked, capeList, capes]() {
            applyPicked(capeList, capes);
        });
        connect(skinList, &QListWidget::itemDoubleClicked, &dialog, [applyPicked, skinList, skins](QListWidgetItem *) {
            applyPicked(skinList, skins);
        });
        connect(capeList, &QListWidget::itemDoubleClicked, &dialog, [applyPicked, capeList, capes](QListWidgetItem *) {
            applyPicked(capeList, capes);
        });

        const QString serverAddress = skinSiteBaseUrl() + QStringLiteral("/api/yggdrasil");
        connect(copyServerBtn, &QPushButton::clicked, &dialog, [this, serverAddress]() {
            if (QGuiApplication::clipboard())
            {
                QGuiApplication::clipboard()->setText(serverAddress);
                showStatus(tr("认证地址已复制：%1").arg(serverAddress));
            }
        });
        connect(shareBtn, &QPushButton::clicked, &dialog, [this, name]() {
            const QString url =
                QStringLiteral("%1/profile.html?name=%2").arg(skinSiteBaseUrl(), QString::fromUtf8(QUrl::toPercentEncoding(name)));
            if (QGuiApplication::clipboard())
            {
                QGuiApplication::clipboard()->setText(url);
                showStatus(tr("主页链接已复制：%1").arg(url));
            }
        });
        connect(webBtn, &QPushButton::clicked, &dialog, [name]() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("%1/profile.html?name=%2")
                                               .arg(skinSiteBaseUrl(),
                                                    QString::fromUtf8(QUrl::toPercentEncoding(name)))));
        });
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        dialog.exec();
    });
}

void SkinStationPage::openAdminDialog()
{
    if (!isModerator())
    {
        showStatus(tr("只有管理员和版主才能处理举报。"), true);
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("处理皮肤举报"));
    dialog.resize(760, 600);

    auto *root = new QVBoxLayout(&dialog);
    auto *topRow = new QHBoxLayout();
    auto *statusCombo = new QComboBox(&dialog);
    statusCombo->addItem(tr("待处理"), QStringLiteral("open"));
    statusCombo->addItem(tr("已处理"), QStringLiteral("handled"));
    statusCombo->addItem(tr("全部"), QStringLiteral("all"));
    topRow->addWidget(new QLabel(tr("举报状态"), &dialog));
    topRow->addWidget(statusCombo);
    auto *statsLabel = new QLabel(QString(), &dialog);
    statsLabel->setObjectName(QStringLiteral("skinMuted"));
    topRow->addSpacing(12);
    topRow->addWidget(statsLabel, 1);
    auto *reloadBtn = new QPushButton(tr("刷新"), &dialog);
    reloadBtn->setObjectName(QStringLiteral("skinSmall"));
    topRow->addWidget(reloadBtn);
    root->addLayout(topRow);

    auto *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("skinList"));
    list->setWordWrap(true);
    root->addWidget(list, 1);

    auto *hint = new QLabel(
        tr("选一条举报：忽略 = 保留作品；下架 = 从皮肤库隐藏（作者仍能看到）；删除 = 彻底删除作品。"), &dialog);
    hint->setObjectName(QStringLiteral("skinMuted"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *buttonsRow = new QHBoxLayout();
    auto *viewBtn = new QPushButton(tr("查看作品"), &dialog);
    viewBtn->setObjectName(QStringLiteral("skinSmall"));
    auto *ignoreBtn = new QPushButton(tr("忽略"), &dialog);
    ignoreBtn->setObjectName(QStringLiteral("skinSmall"));
    auto *hideBtn = new QPushButton(tr("下架作品"), &dialog);
    hideBtn->setObjectName(QStringLiteral("skinSmall"));
    auto *deleteBtn = new QPushButton(tr("删除作品"), &dialog);
    deleteBtn->setObjectName(QStringLiteral("skinSmallDanger"));
    auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
    closeBtn->setObjectName(QStringLiteral("skinSmall"));
    buttonsRow->addStretch();
    buttonsRow->addWidget(viewBtn);
    buttonsRow->addWidget(ignoreBtn);
    buttonsRow->addWidget(hideBtn);
    buttonsRow->addWidget(deleteBtn);
    buttonsRow->addWidget(closeBtn);
    root->addLayout(buttonsRow);

    // 没选中举报时按钮不可点，避免点了没反应
    auto updateActions = [list, viewBtn, ignoreBtn, hideBtn, deleteBtn]() {
        QListWidgetItem *current = list->currentItem();
        const bool has = current && current->data(Qt::UserRole).toInt() > 0;
        viewBtn->setEnabled(has && current->data(Qt::UserRole + 3).toInt() > 0);
        ignoreBtn->setEnabled(has);
        hideBtn->setEnabled(has && current->data(Qt::UserRole + 1).toBool());
        deleteBtn->setEnabled(has);
    };
    connect(list, &QListWidget::currentItemChanged, &dialog,
            [updateActions](QListWidgetItem *, QListWidgetItem *) { updateActions(); });
    updateActions();

    auto reasonLabel = [](const QString &reason) {
        if (reason == QLatin1String("sexual"))
            return QObject::tr("色情低俗");
        if (reason == QLatin1String("violence"))
            return QObject::tr("暴力血腥");
        if (reason == QLatin1String("hate"))
            return QObject::tr("辱骂 / 引战");
        if (reason == QLatin1String("ad"))
            return QObject::tr("垃圾广告");
        if (reason == QLatin1String("copyright"))
            return QObject::tr("盗用他人作品");
        return QObject::tr("其他");
    };

    auto reportIds = QSharedPointer<QVector<int>>::create();
    QPointer<QDialog> adminGuard(&dialog);
    auto loadReports = std::make_shared<std::function<void()>>();
    *loadReports = [this, statusCombo, list, statsLabel, reportIds, reasonLabel, adminGuard]() {
        if (!adminGuard)
            return;
        list->clear();
        reportIds->clear();
        const QString status = statusCombo->currentData().toString();
        getJson(QStringLiteral("/api/admin/reports?status=%1").arg(status), [list, statsLabel, reportIds, reasonLabel, adminGuard](const QJsonObject &rootObject) {
            if (!adminGuard)
                return;
            const QJsonObject counts = rootObject.value(QStringLiteral("counts")).toObject();
            statsLabel->setText(QObject::tr("待处理 %1 · 公开皮肤 %2 · 评论 %3")
                                    .arg(counts.value(QStringLiteral("open_reports")).toInt())
                                    .arg(counts.value(QStringLiteral("public_skins")).toInt())
                                    .arg(counts.value(QStringLiteral("comments")).toInt()));
            for (const QJsonValue &value : rootObject.value(QStringLiteral("items")).toArray())
            {
                const QJsonObject report = value.toObject();
                const QJsonObject texture = report.value(QStringLiteral("texture")).toObject();
                const QJsonObject reporter = report.value(QStringLiteral("reporter")).toObject();
                QString reporterName = reporter.value(QStringLiteral("display_name")).toString();
                if (reporterName.isEmpty())
                    reporterName = reporter.value(QStringLiteral("username")).toString();
                QString ownerName;
                const QJsonObject owner = texture.value(QStringLiteral("owner")).toObject();
                ownerName = owner.value(QStringLiteral("player_name")).toString();
                if (ownerName.isEmpty())
                    ownerName = owner.value(QStringLiteral("display_name")).toString();
                const QString detail = report.value(QStringLiteral("detail")).toString();
                const QString text = QObject::tr("#%1 · %2 · %3\n%4\n举报人 @%5 · 作者 @%6 · %7")
                                          .arg(report.value(QStringLiteral("id")).toInt())
                                          .arg(reasonLabel(report.value(QStringLiteral("reason")).toString()))
                                          .arg(report.value(QStringLiteral("status")).toString()
                                                   == QLatin1String("open")
                                               ? QObject::tr("待处理")
                                               : QObject::tr("已处理"))
                                          .arg(texture.isEmpty()
                                                   ? QObject::tr("作品已被删除")
                                                   : QObject::tr("作品「%1」%2")
                                                         .arg(texture.value(QStringLiteral("name")).toString(),
                                                              texture.value(QStringLiteral("public")).toBool()
                                                                  ? QObject::tr("· 公开中")
                                                                  : QObject::tr("· 已下架")))
                                          .arg(reporterName)
                                          .arg(ownerName)
                                          .arg(detail.isEmpty() ? QObject::tr("没有补充说明")
                                                                : QObject::tr("说明：%1").arg(detail));
                auto *row = new QListWidgetItem(text, list);
                row->setData(Qt::UserRole, report.value(QStringLiteral("id")).toInt());
                row->setData(Qt::UserRole + 1, texture.value(QStringLiteral("public")).toBool());
                row->setData(Qt::UserRole + 2, texture.isEmpty());
                row->setData(Qt::UserRole + 3, texture.value(QStringLiteral("id")).toInt());
                row->setSizeHint(QSize(0, 80));
                reportIds->append(report.value(QStringLiteral("id")).toInt());
            }
            if (reportIds->isEmpty())
            {
                auto *empty = new QListWidgetItem(QObject::tr("没有举报记录"), list);
                empty->setFlags(Qt::NoItemFlags);
            }
        });
    };
    loadReports->operator()();

    auto handleReport = [this, list, loadReports, adminGuard](const QString &action) {
        if (!adminGuard)
            return;
        QListWidgetItem *current = list->currentItem();
        if (!current || current->data(Qt::UserRole).toInt() <= 0)
            return;
        const int reportId = current->data(Qt::UserRole).toInt();
        QJsonObject body;
        body.insert(QStringLiteral("action"), action);
        postJson(QStringLiteral("/api/admin/reports/%1").arg(reportId), body,
                 [this, loadReports, action, adminGuard](const QJsonObject &) {
                     if (!adminGuard)
                         return;
                     showStatus(action == QLatin1String("delete")   ? tr("举报已处理：作品已删除")
                                : action == QLatin1String("hide") ? tr("举报已处理：作品已下架")
                                                                  : tr("举报已处理：已忽略"));
                     (*loadReports)();
                 });
    };

    connect(reloadBtn, &QPushButton::clicked, &dialog, [loadReports]() { (*loadReports)(); });
    connect(viewBtn, &QPushButton::clicked, &dialog, [list]() {
        QListWidgetItem *current = list->currentItem();
        const int textureId = current ? current->data(Qt::UserRole + 3).toInt() : 0;
        if (textureId > 0)
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("%1/skin.html?id=%2").arg(SkinStationPage::skinSiteBaseUrl()).arg(textureId)));
    });
    connect(statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [loadReports]() { (*loadReports)(); });
    connect(ignoreBtn, &QPushButton::clicked, &dialog, [handleReport]() { handleReport(QStringLiteral("ignore")); });
    connect(hideBtn, &QPushButton::clicked, &dialog, [handleReport]() { handleReport(QStringLiteral("hide")); });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem *current = list->currentItem();
        if (!current || current->data(Qt::UserRole).toInt() <= 0)
            return;
        if (QMessageBox::question(&dialog, tr("删除这件作品？"),
                                  tr("删除后作品、评论、收藏和举报都会一起清掉，不能撤销。确定吗？"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        handleReport(QStringLiteral("delete"));
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void SkinStationPage::batchUpload()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("批量上传皮肤 / 披风"), QDir::homePath(), tr("PNG 图片 (*.png)"));
    if (paths.isEmpty())
        return;
    uploadBatchFiles(paths);
}

void SkinStationPage::uploadBatchFiles(const QStringList &paths)
{
    struct Pending
    {
        QString path;
        QString kind;
    };
    QVector<Pending> valid;
    for (const QString &path : paths)
    {
        if (valid.size() >= 20)
            break;
        if (!QFileInfo(path).isFile() || QFileInfo(path).size() > 1024 * 1024)
            continue;
        QImage image(path);
        if (image.isNull())
            continue;
        const int w = image.width();
        const int h = image.height();
        QString kind;
        if (w == 22 && h == 17)
            kind = QStringLiteral("cape");
        else if (w == 64 && (h == 64 || h == 32))
            kind = QStringLiteral("skin");
        else
            continue;
        valid.append({path, kind});
    }
    if (valid.isEmpty())
    {
        showStatus(tr("没有可上传的图片：需要 64×64 / 64×32 皮肤或 64×32 / 64×64 / 22×17 披风。"), true);
        return;
    }

    auto okCount = std::make_shared<int>(0);
    auto failCount = std::make_shared<int>(0);
    m_batchStep = [this, valid, okCount, failCount](int index) {
        if (index >= valid.size())
        {
            showStatus(tr("批量上传完成：成功 %1 张%2")
                           .arg(*okCount)
                           .arg(*failCount > 0 ? tr("，失败 %1 张").arg(*failCount) : QString()));
            m_batchStep = nullptr;
            loadMe();
            return;
        }
        const Pending item = valid.at(index);
        QFile file(item.path);
        if (!file.open(QIODevice::ReadOnly))
        {
            ++*failCount;
            if (m_batchStep)
                m_batchStep(index + 1);
            return;
        }
        const QByteArray png = file.readAll();
        file.close();

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("kind"), item.kind);
        query.addQueryItem(QStringLiteral("model"), QStringLiteral("classic"));
        query.addQueryItem(QStringLiteral("name"), QFileInfo(item.path).completeBaseName().left(40));
        query.addQueryItem(QStringLiteral("public"), QStringLiteral("0"));
        QNetworkRequest request = requestFor(QStringLiteral("/api/textures?%1").arg(query.toString(QUrl::FullyEncoded)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
        QNetworkReply *reply = m_net->post(request, png);
        connect(reply, &QNetworkReply::finished, this, [this, reply, okCount, failCount, index]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            if (status >= 200 && status < 300)
                ++*okCount;
            else
                ++*failCount;
            if (m_batchStep)
                m_batchStep(index + 1);
        });
    };
    m_batchStep(0);
}

void SkinStationPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void SkinStationPage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            paths.append(url.toLocalFile());
    }
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    if (paths.size() > 1)
        uploadBatchFiles(paths);
    else
        openUploadDialog(paths.first());
}

void SkinStationPage::openUploadDialog(const QString &initialPath)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("上传到皮肤站"));
    dialog.setMinimumWidth(640);
    auto *rootLayout = new QHBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(14);

    // 左边 3D 实时预览：选好 PNG 就能看到穿上的样子（和网页上传弹窗一致）
    auto *previewCol = new QVBoxLayout();
    previewCol->setSpacing(6);
    auto *preview = new Skin3DWidget(&dialog);
    preview->setFixedSize(232, 262);
    preview->setAutoRotate(true);
    preview->setBackgroundCornerRadius(12);
    if (m_dark)
        preview->setBackgroundGradient(QColor("#1b2b21"), QColor("#101a14"));
    else
        preview->setBackgroundGradient(QColor("#fbfdfb"), QColor("#e8f5e9"));
    preview->setViewAngles(kPi, 1.45f, 3.3f);
    preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
    previewCol->addWidget(preview);
    auto *previewHint = new QLabel(tr("选好 PNG 后这里会实时预览"), &dialog);
    previewHint->setObjectName(QStringLiteral("skinMuted"));
    previewHint->setAlignment(Qt::AlignCenter);
    previewCol->addWidget(previewHint);
    previewCol->addStretch();
    rootLayout->addLayout(previewCol);

    auto *formHost = new QWidget(&dialog);
    auto *form = new QFormLayout(formHost);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);

    auto *kind = new QComboBox(formHost);
    kind->addItem(tr("皮肤"), QStringLiteral("skin"));
    kind->addItem(tr("披风"), QStringLiteral("cape"));

    auto *model = new QComboBox(formHost);
    model->addItem(tr("Classic（4 像素手臂）"), QStringLiteral("classic"));
    model->addItem(tr("Slim（3 像素手臂）"), QStringLiteral("slim"));

    auto *name = new QLineEdit(formHost);
    name->setMaxLength(40);
    name->setPlaceholderText(tr("给这件作品起个名字"));

    auto *publicBox = new QCheckBox(tr("公开分享到皮肤库"), formHost);
    auto *applyNow = new QCheckBox(tr("上传后立即穿上"), formHost);
    applyNow->setChecked(true);

    auto *fileEdit = new QLineEdit(formHost);
    fileEdit->setReadOnly(true);
    fileEdit->setPlaceholderText(tr("还没有选择 PNG"));
    auto *browse = new QPushButton(tr("选择 PNG…"), formHost);
    auto *fileRow = new QWidget(formHost);
    auto *fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->addWidget(fileEdit, 1);
    fileLayout->addWidget(browse);

    auto *sizeHint = new QLabel(tr("皮肤 64×64 / 64×32 · 披风 64×32 / 64×64 / 22×17 · 单张不超过 1 MB"),
                               formHost);
    sizeHint->setWordWrap(true);
    sizeHint->setObjectName(QStringLiteral("skinMuted"));

    form->addRow(tr("用途"), kind);
    form->addRow(tr("模型"), model);
    form->addRow(tr("名称"), name);
    form->addRow(tr("文件"), fileRow);
    form->addRow(QString(), sizeHint);
    form->addRow(QString(), publicBox);
    form->addRow(QString(), applyNow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, formHost);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("上传"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(buttons);
    rootLayout->addWidget(formHost, 1);

    QString filePath;
    QImage fileImage;
    int imageWidth = 0;
    int imageHeight = 0;

    auto refreshPreview = [&]() {
        const bool cape = kind->currentData().toString() == QLatin1String("cape");
        // 64×32 的旧版皮肤没有 Slim 版本，模型选项直接禁用（和网页一致）
        model->setEnabled(!cape && imageHeight != 32);
        if (fileImage.isNull())
        {
            preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
            preview->clearCape();
            return;
        }
        if (cape)
        {
            preview->setModelType(SkinModelType::Default);
            preview->setSkin(QImage(QStringLiteral(":/Images/Skins/Steve.png")));
            preview->setCape(fileImage);
        }
        else
        {
            preview->setModelType(model->currentData().toString() == QLatin1String("slim")
                                      ? SkinModelType::Slim
                                      : SkinModelType::Default);
            preview->clearCape();
            preview->setSkin(fileImage);
        }
    };

    auto selectFile = [&](const QString &path) -> bool {
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).size() > 1024 * 1024)
        {
            QMessageBox::warning(&dialog, tr("图片太大了"), tr("单张 PNG 不能超过 1 MB。"));
            return false;
        }
        QImage image(path);
        if (image.isNull())
        {
            QMessageBox::warning(&dialog, tr("读不了图片"), tr("请选择一张有效的 PNG。"));
            return false;
        }
        const int w = image.width();
        const int h = image.height();
        const bool skinSize = (w == 64 && (h == 64 || h == 32));
        const bool capeSize = (w == 64 && (h == 32 || h == 64)) || (w == 22 && h == 17);
        if (!skinSize && !capeSize)
        {
            QMessageBox::warning(&dialog, tr("尺寸不支持"),
                                 tr("皮肤需要 64×64 / 64×32，披风需要 64×32 / 64×64 / 22×17。"));
            return false;
        }
        filePath = path;
        fileImage = image;
        imageWidth = w;
        imageHeight = h;
        fileEdit->setText(path);
        if (name->text().trimmed().isEmpty())
            name->setText(QFileInfo(path).completeBaseName().left(40));
        if (w == 22 && h == 17)
            kind->setCurrentIndex(kind->findData(QStringLiteral("cape")));
        if (h == 32)
            model->setCurrentIndex(model->findData(QStringLiteral("classic")));
        sizeHint->setText(tr("%1×%2 像素 · 皮肤 64×64 / 64×32 · 披风 64×32 / 64×64 / 22×17 · 单张不超过 1 MB")
                              .arg(w)
                              .arg(h));
        refreshPreview();
        return true;
    };

    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(&dialog, tr("选择皮肤 PNG"), QString(),
                                                          tr("PNG 图片 (*.png)"));
        selectFile(path);
    });
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&]() {
        refreshPreview();
    });
    connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&]() {
        refreshPreview();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (filePath.isEmpty())
        {
            QMessageBox::warning(&dialog, tr("还没选文件"), tr("先选择一张 PNG 再上传。"));
            return;
        }
        if (kind->currentData().toString() == QLatin1String("cape") &&
            !((imageWidth == 64 && (imageHeight == 32 || imageHeight == 64)) ||
              (imageWidth == 22 && imageHeight == 17)))
        {
            QMessageBox::warning(&dialog, tr("尺寸不支持"), tr("披风需要 64×32、64×64 或 22×17 像素。"));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 拖进来的单张 PNG 直接带进弹窗
    if (!initialPath.isEmpty())
        selectFile(initialPath);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        showStatus(tr("读不了这个文件：%1").arg(filePath), true);
        return;
    }
    const QByteArray png = file.readAll();
    file.close();

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), kind->currentData().toString());
    query.addQueryItem(QStringLiteral("model"), kind->currentData().toString() == QLatin1String("cape")
                                                    ? QStringLiteral("classic")
                                                    : model->currentData().toString());
    query.addQueryItem(QStringLiteral("name"), name->text().trimmed().left(40));
    query.addQueryItem(QStringLiteral("public"), publicBox->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));

    const bool applyAfter = applyNow->isChecked();
    showStatus(tr("正在上传…"));
    postPng(QStringLiteral("/api/textures?%1").arg(query.toString(QUrl::FullyEncoded)), png,
            [this, applyAfter](const QJsonObject &root) {
                const int id = root.value(QStringLiteral("texture")).toObject().value(QStringLiteral("id")).toInt();
                if (applyAfter && id > 0)
                {
                    postJson(QStringLiteral("/api/textures/%1/apply").arg(id), QJsonObject(),
                             [this](const QJsonObject &) {
                                 showStatus(tr("上传成功，已经穿上。"));
                                 loadMe();
                             });
                    return;
                }
                showStatus(tr("上传成功，正在刷新衣柜…"));
                loadMe();
            });
}

void SkinStationPage::openProfileDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("skinStationPage"));
    dialog.setStyleSheet(styleSheet());
    dialog.setWindowTitle(tr("编辑皮肤站资料"));
    dialog.setMinimumWidth(440);
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(m_playerName, &dialog);
    name->setMaxLength(16);
    auto *model = new QComboBox(&dialog);
    model->addItem(tr("Classic（4 像素手臂）"), QStringLiteral("classic"));
    model->addItem(tr("Slim（3 像素手臂）"), QStringLiteral("slim"));
    model->setCurrentIndex(m_model == QLatin1String("slim") ? 1 : 0);
    auto *about = new QPlainTextEdit(&dialog);
    about->setMaximumHeight(80);
    about->setPlainText(m_about);
    form->addRow(tr("玩家名"), name);
    // 玩家名要进 Minecraft，不能用中文；论坛昵称里的中文在自动建档案时会变成下划线
    auto *nameHint = new QLabel(tr("进游戏用的名字：只能是字母、数字、下划线（3–16 位，不能用中文）"), &dialog);
    nameHint->setWordWrap(true);
    nameHint->setStyleSheet(QStringLiteral("color:%1; font-size:11.5px;")
                                .arg(m_dark ? QStringLiteral("#7d9184") : QStringLiteral("#87998b")));
    form->addRow(QString(), nameHint);
    form->addRow(tr("默认模型"), model);
    form->addRow(tr("简介"), about);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("player_name"), name->text().trimmed());
    body.insert(QStringLiteral("model"), model->currentData().toString());
    body.insert(QStringLiteral("about"), about->toPlainText().trimmed().left(200));
    postJson(QStringLiteral("/api/profile"), body, [this](const QJsonObject &root) {
        const QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        m_playerName = profile.value(QStringLiteral("player_name")).toString(m_playerName);
        m_model = profile.value(QStringLiteral("model")).toString(m_model);
        m_about = profile.value(QStringLiteral("about")).toString(m_about);
        previewCurrentProfile();
        showStatus(tr("资料已保存"));
        // 玩家名可能变了：重新拉一次档案，把左侧「论坛账户名 / 玩家名」两行一起刷新
        loadMe();
    });
}
