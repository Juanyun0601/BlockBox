/**
 * @file   CommunitySkinDetailPage.cpp
 * @brief  皮肤库作品详情页实现：3D 预览 + 作品信息 + 穿上 / 离线皮肤 / 导出
 * @author BlockBox Team
 * @date   2026-11-25
 */

#include "CommunitySkinDetailPage.h"

#include <QBoxLayout>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "components/Skin3DWidget.h"
#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/SkinDownloader.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace {

/** 正面视角（与编辑页 / 首页 3D 预览同一套约定：theta=0 是正面） */
constexpr float kFrontAngle = 0.0f;
constexpr float kViewPhi = 0.5f;
constexpr float kViewRadius = 3.3f;

/** 窄屏阈值：小于这个宽度改成上下堆叠（手机 / 小窗口） */
constexpr int kStackBelow = 720;

/** 宽屏时左侧 3D 预览卡的宽度 */
constexpr int kPreviewCardWidth = 352;

/** 账户是不是离线账户（第三方账户有 serverUrl，离线账户没有） */
bool isOfflineAccount(const AccountInfo &account)
{
    if (account.serverUrl.trimmed().isEmpty())
        return true;
    return account.type.contains(QObject::tr("离线"));
}

/** 页面样式：两张卡片 + 标题 / 说明 / 芯片 / 小按钮，颜色全部走主题令牌 */
QString pageStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QFrame#skinDetailCard {"
        "  background-color: @BG_CARD@;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 14px;"
        "}"
        "QLabel#skinDetailName {"
        "  background: transparent; border: none;"
        "  color: @TEXT_PRIMARY@; font-size: 20px; font-weight: 700;"
        "}"
        "QLabel#skinDetailAuthor {"
        "  background: transparent; border: none;"
        "  color: @TEXT_SECONDARY@; font-size: 13px;"
        "}"
        "QLabel#skinDetailMuted {"
        "  background: transparent; border: none;"
        "  color: @TEXT_TERTIARY@; font-size: 11px;"
        "}"
        "QLabel#skinDetailStats {"
        "  background: transparent; border: none;"
        "  color: @TEXT_SECONDARY@; font-size: 12px;"
        "}"
        "QLabel#skinDetailChip {"
        "  background-color: @PRIMARY_BG@; color: @TEXT_ACCENT@;"
        "  border: none; border-radius: 9px; padding: 2px 9px;"
        "  font-size: 11px; font-weight: 600;"
        "}"
        "QLabel#skinDetailChip[kind=\"private\"] {"
        "  background-color: @WARNING_BG@; color: @WARNING_TEXT@;"
        "}"
        "QPushButton#skinDetailSmall {"
        "  background: transparent; border: 1px solid @BORDER_LIGHT@;"
        "  border-radius: 8px; color: @TEXT_SECONDARY@;"
        "  font-size: 11px; padding: 4px 10px;"
        "}"
        "QPushButton#skinDetailSmall:hover {"
        "  color: @TEXT_PRIMARY@; border-color: @PRIMARY@; background: @BG_HOVER@;"
        "}"
        "QPushButton#skinDetailSmall:checked {"
        "  color: @TEXT_ON_PRIMARY@; background-color: @PRIMARY@; border-color: @PRIMARY@;"
        "}"
        "QLabel#skinDetailStatus {"
        "  background: transparent; border: none;"
        "  color: @TEXT_TERTIARY@; font-size: 12px;"
        "}"
        "QLabel#skinDetailStatus[error=\"true\"] { color: @DANGER_TEXT@; }"));
}

} // namespace

CommunitySkinDetailPage::CommunitySkinDetailPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    // 主题 / 主题色变了：卡片与小按钮的配色跟着换，3D 预览底色也重铺一次
    if (ThemeManager::instance())
    {
        connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](ThemeManager::ThemeType) { applyStyle(); });
        connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
                [this](const QString &) { applyStyle(); });
    }
}

void CommunitySkinDetailPage::buildUi()
{
    m_net = new QNetworkAccessManager(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(m_scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *pageLayout = new QVBoxLayout(content);
    pageLayout->setContentsMargins(20, 10, 20, 14);
    pageLayout->setSpacing(12);

    // ── 两栏：左 3D 预览 / 右 作品信息与操作（窄屏自动改成上下） ──
    m_bodyLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(16);

    // 左：3D 预览卡
    auto *previewCard = new QFrame(content);
    previewCard->setObjectName(QStringLiteral("skinDetailCard"));
    m_previewCard = previewCard;
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(14, 14, 14, 12);
    previewLayout->setSpacing(8);

    m_preview3D = new Skin3DWidget(previewCard);
    m_preview3D->setFixedHeight(380);
    m_preview3D->setMinimumWidth(200);
    m_preview3D->setMaximumWidth(320);
    m_preview3D->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_preview3D->setAutoRotate(true);
    m_preview3D->setSkin(SkinDownloader::defaultSkin());
    m_preview3D->setViewAngles(kFrontAngle, kViewPhi, kViewRadius);
    m_preview3D->setBackgroundCornerRadius(12);
    previewLayout->addWidget(m_preview3D, 0, Qt::AlignHCenter);

    auto *hint = new QLabel(tr("拖动旋转 · 滚轮缩放"), previewCard);
    hint->setObjectName(QStringLiteral("skinDetailMuted"));
    hint->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(hint);

    // 四个固定视角 + 自动旋转（theta=0 为正面，±90° 为左右侧，180° 为背面）
    auto *viewRow = new QHBoxLayout();
    viewRow->setSpacing(6);
    viewRow->addStretch(1);
    constexpr float kHalfTurn = 1.5707964f;
    const QList<QPair<QString, float>> views = {
        {tr("正面"), kFrontAngle},
        {tr("左侧"), kHalfTurn},
        {tr("背面"), kHalfTurn * 2.0f},
        {tr("右侧"), -kHalfTurn}};
    for (const auto &view : views)
    {
        auto *button = new QPushButton(view.first, previewCard);
        button->setObjectName(QStringLiteral("skinDetailSmall"));
        button->setCursor(Qt::PointingHandCursor);
        const float angle = view.second;
        connect(button, &QPushButton::clicked, this, [this, angle]() {
            if (!m_preview3D)
                return;
            m_preview3D->setViewAngles(angle, kViewPhi, kViewRadius);
            if (m_rotateButton)
                m_rotateButton->setChecked(false);
        });
        viewRow->addWidget(button);
    }
    viewRow->addStretch(1);
    previewLayout->addLayout(viewRow);

    auto *rotateRow = new QHBoxLayout();
    rotateRow->addStretch(1);
    m_rotateButton = new QPushButton(tr("自动旋转"), previewCard);
    m_rotateButton->setObjectName(QStringLiteral("skinDetailSmall"));
    m_rotateButton->setCheckable(true);
    m_rotateButton->setChecked(true);
    m_rotateButton->setCursor(Qt::PointingHandCursor);
    connect(m_rotateButton, &QPushButton::toggled, this, [this](bool on) {
        if (m_preview3D)
            m_preview3D->setAutoRotate(on);
    });
    rotateRow->addWidget(m_rotateButton);
    rotateRow->addStretch(1);
    previewLayout->addLayout(rotateRow);

    m_bodyLayout->addWidget(previewCard, 0);

    // 右：作品信息 + 动作
    auto *infoCard = new QFrame(content);
    infoCard->setObjectName(QStringLiteral("skinDetailCard"));
    m_infoCard = infoCard;
    auto *infoLayout = new QVBoxLayout(infoCard);
    infoLayout->setContentsMargins(18, 16, 18, 16);
    infoLayout->setSpacing(8);

    m_nameLabel = new QLabel(infoCard);
    m_nameLabel->setObjectName(QStringLiteral("skinDetailName"));
    m_nameLabel->setWordWrap(true);
    m_nameLabel->setTextFormat(Qt::PlainText);
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_nameLabel);

    m_authorLabel = new QLabel(infoCard);
    m_authorLabel->setObjectName(QStringLiteral("skinDetailAuthor"));
    m_authorLabel->setWordWrap(true);
    m_authorLabel->setTextFormat(Qt::PlainText);
    m_authorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_authorLabel);

    auto *chipRow = new QHBoxLayout();
    chipRow->setSpacing(6);
    m_modelChip = new QLabel(infoCard);
    m_modelChip->setObjectName(QStringLiteral("skinDetailChip"));
    chipRow->addWidget(m_modelChip);
    m_stateChip = new QLabel(infoCard);
    m_stateChip->setObjectName(QStringLiteral("skinDetailChip"));
    m_stateChip->hide();
    chipRow->addWidget(m_stateChip);
    chipRow->addStretch(1);
    infoLayout->addLayout(chipRow);

    m_statsLabel = new QLabel(infoCard);
    m_statsLabel->setObjectName(QStringLiteral("skinDetailStats"));
    m_statsLabel->setWordWrap(true);
    infoLayout->addWidget(m_statsLabel);

    m_timeLabel = new QLabel(infoCard);
    m_timeLabel->setObjectName(QStringLiteral("skinDetailMuted"));
    infoLayout->addWidget(m_timeLabel);

    infoLayout->addStretch(1);

    m_applyStationBtn = new QPushButton(tr("穿上到皮肤站"), infoCard);
    m_applyStationBtn->setFixedHeight(38);
    m_applyStationBtn->setCursor(Qt::PointingHandCursor);
    connect(m_applyStationBtn, &QPushButton::clicked, this,
            &CommunitySkinDetailPage::applyToSkinStation);
    infoLayout->addWidget(m_applyStationBtn);

    m_applyOfflineBtn = new QPushButton(tr("应用为离线皮肤"), infoCard);
    m_applyOfflineBtn->setFixedHeight(38);
    m_applyOfflineBtn->setCursor(Qt::PointingHandCursor);
    connect(m_applyOfflineBtn, &QPushButton::clicked, this,
            &CommunitySkinDetailPage::applyToOfflineAccount);
    infoLayout->addWidget(m_applyOfflineBtn);

    m_downloadBtn = new QPushButton(tr("下载 PNG"), infoCard);
    m_downloadBtn->setFixedHeight(38);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, &CommunitySkinDetailPage::downloadPng);
    infoLayout->addWidget(m_downloadBtn);

    m_statusLabel = new QLabel(infoCard);
    m_statusLabel->setObjectName(QStringLiteral("skinDetailStatus"));
    m_statusLabel->setWordWrap(true);
    infoLayout->addWidget(m_statusLabel);

    m_bodyLayout->addWidget(infoCard, 1);

    pageLayout->addLayout(m_bodyLayout, 0);
    pageLayout->addStretch(1);

    m_scroll->setWidget(content);
    root->addWidget(m_scroll);

    applyStyle();
    applyLayoutMode();
}

void CommunitySkinDetailPage::applyStyle()
{
    setStyleSheet(pageStyle());

    if (m_applyStationBtn)
        m_applyStationBtn->setStyleSheet(StyleKit::primaryButton());
    if (m_applyOfflineBtn)
        m_applyOfflineBtn->setStyleSheet(StyleKit::outlineButton());
    if (m_downloadBtn)
        m_downloadBtn->setStyleSheet(StyleKit::outlineButton());

    // 3D 预览底色：深色主题深绿渐变，浅色主题浅绿渐变，圆角与卡片对齐
    if (m_preview3D)
    {
        const bool dark = ThemeManager::instance()
                          && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
        if (dark)
            m_preview3D->setBackgroundGradient(QColor(QStringLiteral("#1b2b21")),
                                               QColor(QStringLiteral("#101a14")));
        else
            m_preview3D->setBackgroundGradient(QColor(QStringLiteral("#fbfdfb")),
                                               QColor(QStringLiteral("#e8f5e9")));
        m_preview3D->setBackgroundCornerRadius(12);
    }
}

void CommunitySkinDetailPage::applyLayoutMode()
{
    if (!m_bodyLayout || !m_previewCard)
        return;

    const bool stacked = width() > 0 && width() < kStackBelow;
    const int mode = stacked ? 1 : 0;
    if (mode == m_layoutMode)
        return;
    m_layoutMode = mode;

    m_bodyLayout->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_bodyLayout->setSpacing(stacked ? 12 : 16);

    if (stacked)
    {
        m_previewCard->setMinimumWidth(0);
        m_previewCard->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    else
    {
        m_previewCard->setFixedWidth(kPreviewCardWidth);
    }
    m_bodyLayout->invalidate();
}

void CommunitySkinDetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyLayoutMode();
}

// ── 打开作品：先铺列表里已有的信息，再拉详情补全 ─────────────────────────

void CommunitySkinDetailPage::openItem(const SkinLibraryItem &item)
{
    m_item = item;

    setBusy(false);
    setStatus(QString());
    if (m_rotateButton)
        m_rotateButton->setChecked(true);
    if (m_preview3D)
    {
        m_preview3D->setAutoRotate(true);
        m_preview3D->resetView(kViewRadius);
    }

    renderItem();
    loadPreviewTexture();
    loadDetail();
}

void CommunitySkinDetailPage::renderItem()
{
    const QString name = m_item.name.trimmed();
    m_nameLabel->setText(name.isEmpty() ? tr("未命名皮肤") : name);

    const QString owner = m_item.ownerName.trimmed();
    m_authorLabel->setText(owner.isEmpty() ? tr("作者未知") : tr("作者：%1").arg(owner));

    m_modelChip->setText(m_item.model == QLatin1String("slim") ? tr("Slim（纤细）")
                                                                : tr("Classic（经典）"));

    QString stateText;
    QString stateKind = QStringLiteral("mine");
    if (m_item.mine)
        stateText = tr("我的作品");
    else if (!m_item.isPublic)
    {
        stateText = tr("私密");
        stateKind = QStringLiteral("private");
    }
    m_stateChip->setVisible(!stateText.isEmpty());
    if (!stateText.isEmpty())
    {
        m_stateChip->setText(stateText);
        m_stateChip->setProperty("kind", stateKind);
        m_stateChip->style()->unpolish(m_stateChip);
        m_stateChip->style()->polish(m_stateChip);
    }

    QStringList stats;
    stats << tr("♥ %1").arg(m_item.likes);
    stats << tr("下载 %1").arg(m_item.downloads);
    if (m_item.favorites > 0)
        stats << tr("收藏 %1").arg(m_item.favorites);
    if (m_item.comments > 0)
        stats << tr("评论 %1").arg(m_item.comments);
    m_statsLabel->setText(stats.join(QStringLiteral("   ·   ")));

    const bool hasTime = !m_item.createdAt.trimmed().isEmpty();
    m_timeLabel->setText(hasTime ? tr("上传于 %1").arg(m_item.createdAt.trimmed()) : QString());
    m_timeLabel->setVisible(hasTime);

    m_applyStationBtn->setText(m_item.mine ? tr("穿上这件皮肤") : tr("穿上到皮肤站"));
}

void CommunitySkinDetailPage::loadPreviewTexture()
{
    if (!m_preview3D)
        return;

    // 先给默认皮肤兜底，PNG 下载完成后替换
    m_preview3D->setSkin(SkinDownloader::defaultSkin());
    m_preview3D->setModelType(m_item.model == QLatin1String("slim") ? SkinModelType::Slim
                                                                    : SkinModelType::Default);

    const QString url = m_item.url;
    if (url.isEmpty())
        return;
    const int id = m_item.id;
    fetchPng(url, [this, id](bool ok, const QByteArray &data) {
        // 期间又点开了别的作品：这次结果直接丢掉
        if (id != m_item.id || !m_preview3D)
            return;
        QImage image;
        if (ok)
            image.loadFromData(data);
        if (image.isNull())
            return;
        m_preview3D->setSkin(image);
    });
}

void CommunitySkinDetailPage::loadDetail()
{
    const int id = m_item.id;
    if (id <= 0)
        return;

    getJson(QStringLiteral("/api/textures/%1").arg(id),
            [this, id](bool ok, int httpStatus, const QJsonObject &root) {
                if (id != m_item.id)
                    return;

                if (!ok)
                {
                    if (httpStatus == 401 || httpStatus == 403)
                    {
                        setStatus(tr("皮肤站需要登录后才能查看作品详情，请先登录论坛账号。"), true);
                        emit loginRequested();
                    }
                    else if (httpStatus == 404)
                    {
                        setStatus(tr("这件作品已经不在皮肤库了。"), true);
                    }
                    else
                    {
                        setStatus(tr("作品详情没加载出来，检查网络后再试。"), true);
                    }
                    return;
                }

                const QJsonObject texture = root.value(QStringLiteral("texture")).toObject();
                if (texture.isEmpty())
                    return;

                const QString name = texture.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    m_item.name = name;
                const QString model = texture.value(QStringLiteral("model")).toString();
                if (!model.isEmpty())
                    m_item.model = model;

                m_item.likes = texture.value(QStringLiteral("likes")).toInt(m_item.likes);
                m_item.downloads = texture.value(QStringLiteral("downloads")).toInt(m_item.downloads);
                m_item.comments = texture.value(QStringLiteral("comments")).toInt(m_item.comments);
                m_item.favorites = root.value(QStringLiteral("favorite_count")).toInt(m_item.favorites);
                m_item.isPublic = texture.value(QStringLiteral("public")).toBool(m_item.isPublic);

                const qint64 created = qint64(texture.value(QStringLiteral("created_at")).toDouble());
                if (created > 0)
                    m_item.createdAt =
                        QDateTime::fromSecsSinceEpoch(created).toString(QStringLiteral("yyyy-MM-dd"));

                const QJsonObject owner = texture.value(QStringLiteral("owner")).toObject();
                if (!owner.isEmpty())
                {
                    const QString player = owner.value(QStringLiteral("player_name")).toString();
                    const QString display = owner.value(QStringLiteral("display_name")).toString();
                    const QString username = owner.value(QStringLiteral("username")).toString();
                    if (!player.isEmpty())
                        m_item.ownerName = player;
                    else if (!display.isEmpty())
                        m_item.ownerName = display;
                    else if (!username.isEmpty())
                        m_item.ownerName = username;
                }

                renderItem();

                // 详情里的纹理地址才是最新的（作品换过图时按新地址重下）
                const QString url = texture.value(QStringLiteral("url")).toString();
                if (!url.isEmpty() && url != m_item.url)
                {
                    m_item.url = url;
                    loadPreviewTexture();
                }
            });
}

void CommunitySkinDetailPage::refresh()
{
    if (m_item.id <= 0)
        return;
    setBusy(false);
    setStatus(tr("正在刷新详情…"));
    renderItem();
    loadPreviewTexture();
    loadDetail();
}

void CommunitySkinDetailPage::refreshPreviewAfterTransition()
{
    if (m_preview3D)
        m_preview3D->repaint();
}

void CommunitySkinDetailPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("error", error ? QStringLiteral("true") : QStringLiteral("false"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void CommunitySkinDetailPage::setBusy(bool busy)
{
    m_busy = busy;
    if (m_applyStationBtn)
        m_applyStationBtn->setEnabled(!busy);
    if (m_applyOfflineBtn)
        m_applyOfflineBtn->setEnabled(!busy);
    if (m_downloadBtn)
        m_downloadBtn->setEnabled(!busy);
}

// ── 动作：穿上到皮肤站 ───────────────────────────────────────────────────

void CommunitySkinDetailPage::applyToSkinStation()
{
    if (m_busy)
        return;

    if (!ForumAccountService::instance()->hasSession())
    {
        setStatus(tr("先登录论坛账号，才能把皮肤穿到皮肤站角色上。"), true);
        emit loginRequested();
        return;
    }

    const SkinLibraryItem item = m_item;
    setBusy(true);
    setStatus(tr("正在穿上「%1」…").arg(item.name));

    postJson(QStringLiteral("/api/textures/%1/apply").arg(item.id), QJsonObject(),
             [this, item](bool ok, int httpStatus, const QJsonObject &root) {
                 Q_UNUSED(httpStatus);
                 if (ok)
                 {
                     Q_UNUSED(root);
                     setBusy(false);
                     setStatus(tr("皮肤已更新，下次进游戏生效。"));
                     NotificationManager::showSuccess(this,
                                                      tr("已穿上「%1」，下次进游戏生效").arg(item.name));
                     return;
                 }
                 // 他人作品可能不允许直接穿：回退为上传一份副本到自己的衣柜再穿
                 uploadCopyAndApply();
             });
}

void CommunitySkinDetailPage::uploadCopyAndApply()
{
    const SkinLibraryItem item = m_item;
    setStatus(tr("正在把「%1」保存到你的衣柜…").arg(item.name));

    fetchPng(item.url, [this, item](bool ok, const QByteArray &png) {
        if (!ok || png.isEmpty())
        {
            setBusy(false);
            setStatus(tr("下载皮肤失败，稍后再试。"), true);
            return;
        }

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("kind"), QStringLiteral("skin"));
        query.addQueryItem(QStringLiteral("model"),
                           item.model.isEmpty() ? QStringLiteral("classic") : item.model);
        query.addQueryItem(QStringLiteral("name"),
                           (item.name.isEmpty() ? QStringLiteral("skin") : item.name).left(40));
        query.addQueryItem(QStringLiteral("public"), QStringLiteral("0"));

        postPng(QStringLiteral("/api/textures?%1").arg(query.toString(QUrl::FullyEncoded)), png,
                [this, item](bool ok, int httpStatus, const QJsonObject &root) {
                    if (!ok)
                    {
                        setBusy(false);
                        setStatus(tr("上传失败（%1），稍后再试。").arg(httpStatus), true);
                        return;
                    }
                    const int copyId = extractTextureId(root);
                    if (copyId <= 0)
                    {
                        setBusy(false);
                        setStatus(tr("上传成功，但没有拿到皮肤编号，请到皮肤站手动穿上。"), true);
                        return;
                    }
                    setStatus(tr("已保存到你的衣柜，正在穿上…"));
                    postJson(QStringLiteral("/api/textures/%1/apply").arg(copyId), QJsonObject(),
                             [this, item](bool ok, int httpStatus, const QJsonObject &) {
                                 Q_UNUSED(httpStatus);
                                 setBusy(false);
                                 if (ok)
                                 {
                                     setStatus(tr("已保存到你的衣柜并穿上，下次进游戏生效。"));
                                     NotificationManager::showSuccess(
                                         this, tr("已穿上「%1」，下次进游戏生效").arg(item.name));
                                 }
                                 else
                                 {
                                     setStatus(
                                         tr("皮肤已存入你的衣柜，穿上失败，可到皮肤站手动穿上。"), true);
                                 }
                             });
                });
    });
}

// ── 动作：应用为离线皮肤 ─────────────────────────────────────────────────

void CommunitySkinDetailPage::applyToOfflineAccount()
{
    if (m_busy)
        return;

    const SkinLibraryItem item = m_item;

    // 收集离线账户（无 serverUrl 的）
    const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
    QStringList offlineNames;
    for (const AccountInfo &account : accounts)
    {
        if (isOfflineAccount(account) && !account.username.isEmpty())
            offlineNames.append(account.username);
    }

    if (offlineNames.isEmpty())
    {
        AppMessageBox::warning(this, tr("没有离线账户"),
                               tr("先到「社区 → 账户管理」创建一个离线账户，再把皮肤应用过去。"));
        return;
    }

    // 选离线账户
    QDialog picker(this);
    picker.setWindowTitle(tr("选择离线账户"));
    picker.setMinimumWidth(320);
    auto *layout = new QVBoxLayout(&picker);
    auto *list = new QListWidget(&picker);
    for (const QString &name : offlineNames)
        list->addItem(name);
    if (list->count() > 0)
        list->setCurrentRow(0);
    layout->addWidget(list);
    auto *buttons = new QHBoxLayout();
    auto *okBtn = new QPushButton(tr("应用"), &picker);
    auto *cancelBtn = new QPushButton(tr("取消"), &picker);
    buttons->addStretch(1);
    buttons->addWidget(cancelBtn);
    buttons->addWidget(okBtn);
    layout->addLayout(buttons);
    connect(okBtn, &QPushButton::clicked, &picker, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &picker, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &picker, &QDialog::accept);

    if (picker.exec() != QDialog::Accepted || !list->currentItem())
        return;
    const QString username = list->currentItem()->text();

    setBusy(true);
    setStatus(tr("正在把「%1」应用为 %2 的离线皮肤…").arg(item.name, username));

    fetchPng(item.url, [this, item, username](bool ok, const QByteArray &data) {
        QImage image;
        if (ok)
            image.loadFromData(data);
        const QImage skin = SkinDownloader::normalizeSkinTexture(image);
        if (skin.isNull())
        {
            setBusy(false);
            setStatus(tr("这张图不是有效的皮肤文件（需要 64×64 或 64×32）。"), true);
            return;
        }
        if (!SkinDownloader::saveOfflineSkin(username, skin))
        {
            setBusy(false);
            setStatus(tr("保存失败：没有写入权限或磁盘满了。"), true);
            return;
        }
        setBusy(false);
        setStatus(tr("已应用为 %1 的离线皮肤。").arg(username));
        NotificationManager::showSuccess(this, tr("「%1」已应用为 %2 的离线皮肤").arg(item.name, username));
    });
}

// ── 动作：导出 PNG ───────────────────────────────────────────────────────

void CommunitySkinDetailPage::downloadPng()
{
    if (m_busy)
        return;

    const SkinLibraryItem item = m_item;
    QString name = item.name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("skin");
    // 文件名里的非法字符换成下划线，避免不同平台上保存失败
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存皮肤 PNG"),
        QDir::homePath() + QLatin1Char('/') + name + QStringLiteral(".png"),
        tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    setBusy(true);
    setStatus(tr("正在下载「%1」…").arg(name));

    fetchPng(item.url, [this, item, path](bool ok, const QByteArray &data) {
        setBusy(false);
        if (!ok || data.isEmpty())
        {
            setStatus(tr("下载失败，稍后再试。"), true);
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            setStatus(tr("保存失败：没有写入权限或磁盘满了。"), true);
            return;
        }
        setStatus(tr("已保存到 %1").arg(path));
        NotificationManager::showSuccess(this, tr("「%1」已保存到 %2").arg(item.name, path));
    });
}

// ── 网络小工具（与皮肤库列表页同款逻辑） ──────────────────────────────────

QString CommunitySkinDetailPage::skinSiteBaseUrl() const
{
    const QString fromEnv = qEnvironmentVariable("BLOCKBOX_SKIN_SITE_URL");
    if (!fromEnv.trimmed().isEmpty())
        return fromEnv.trimmed().remove(QLatin1Char('/'));
    return QStringLiteral("https://skin.mcblockbox.top");
}

void CommunitySkinDetailPage::sendRequest(const QString &path, const QString &method,
                                          const QByteArray &contentType, const QByteArray &body,
                                          JsonCb done)
{
    const QString base = skinSiteBaseUrl();
    QUrl url(path.startsWith(QStringLiteral("http")) ? path : base + path);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (ForumAccountService::instance()->hasSession())
        request.setRawHeader("Authorization",
                             "Bearer " + ForumAccountService::instance()->token().toUtf8());

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("POST"))
        reply = m_net->post(request, body);
    else
        reply = m_net->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        if (status <= 0)
        {
            if (done)
                done(false, status, QJsonObject());
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (status >= 400)
        {
            QJsonObject errorRoot;
            if (doc.isObject())
                errorRoot = doc.object();
            if (done)
                done(false, status, errorRoot);
            return;
        }
        // PNG 等非 JSON 回包：当成成功但内容为空
        QJsonObject root;
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            root = doc.object();
        if (done)
            done(true, status, root);
    });
}

void CommunitySkinDetailPage::getJson(const QString &path, JsonCb done)
{
    sendRequest(path, QStringLiteral("GET"), QByteArray(), QByteArray(), std::move(done));
}

void CommunitySkinDetailPage::postJson(const QString &path, const QJsonObject &body, JsonCb done)
{
    sendRequest(path, QStringLiteral("POST"), "application/json",
                QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(done));
}

void CommunitySkinDetailPage::postPng(const QString &path, const QByteArray &png, JsonCb done)
{
    sendRequest(path, QStringLiteral("POST"), "image/png", png, std::move(done));
}

void CommunitySkinDetailPage::fetchPng(
    const QString &url, const std::function<void(bool, const QByteArray &)> &done)
{
    if (url.isEmpty())
    {
        if (done)
            done(false, QByteArray());
        return;
    }
    if (m_pngCache.contains(url))
    {
        if (done)
            done(true, m_pngCache.value(url));
        return;
    }

    // 站内相对地址（/textures/xxx.png）拼皮肤站域名，完整地址直接用
    const QString base = skinSiteBaseUrl();
    QNetworkRequest request(QUrl(url.startsWith(QStringLiteral("http")) ? url : base + url));
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, done]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();
        const bool ok = status >= 200 && status < 300 && !raw.isEmpty();
        if (ok)
            m_pngCache.insert(url, raw);
        if (done)
            done(ok, raw);
    });
}

int CommunitySkinDetailPage::extractTextureId(const QJsonObject &root)
{
    // 上传接口回包结构不确定：直接带 id，或包在 texture/item/data 里
    if (root.value(QStringLiteral("id")).isDouble())
        return root.value(QStringLiteral("id")).toInt();
    for (const QString &key :
         {QStringLiteral("texture"), QStringLiteral("item"), QStringLiteral("data")})
    {
        const QJsonObject inner = root.value(key).toObject();
        if (inner.value(QStringLiteral("id")).isDouble())
            return inner.value(QStringLiteral("id")).toInt();
    }
    return 0;
}
