#include "MedalShareDialog.h"

#include "NotificationManager.h"
#include "utils/AndroidBridge.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const char kCardName[] = "medalShareCard";

/** 预览图统一切圆角：QLabel 的 border-radius 只画边框、不裁内容 */
QPixmap roundedPreview(const QImage &image, int radius)
{
    if (image.isNull())
        return QPixmap();

    QPixmap canvas(image.size());
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, image.width(), image.height()), radius, radius);
    painter.setClipPath(clip);
    painter.drawImage(0, 0, image);
    painter.end();
    return canvas;
}

} // namespace

void MedalShareDialog::show(QWidget *parent, const QImage &poster, const QString &suggestedName)
{
    if (poster.isNull())
    {
        NotificationManager::showError(parent, QObject::tr("分享图生成失败，请稍后再试"));
        return;
    }
    MedalShareDialog dialog(poster, suggestedName, parent);
    dialog.exec();
}

MedalShareDialog::MedalShareDialog(const QImage &poster, const QString &suggestedName,
                                   QWidget *parent)
    : AppDialogBase(parent)
    , m_poster(poster)
    , m_suggestedName(suggestedName)
{
    setObjectName(QStringLiteral("medalShareDialog"));
    setWindowTitle(tr("勋章分享图"));

    initUI();
    initStyle();
    refreshPreview();
}

// ───────────────────────────── UI ─────────────────────────────

void MedalShareDialog::initUI()
{
    // 按屏幕可用区域定预览大小：手机上不至于顶出屏幕，桌面端也不浪费空间
    const QScreen *screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1280, 800);
    const int maxWidth = qBound(260, int(available.width() * 0.68), 420);
    const int maxHeight = qBound(340, int(available.height() * 0.62), 560);
    const QSize previewSize =
        m_poster.isNull() ? QSize(300, 400)
                          : m_poster.size().scaled(QSize(maxWidth, maxHeight), Qt::KeepAspectRatio);

    auto *card = new QWidget(this);
    card->setObjectName(QString::fromLatin1(kCardName));
    card->setFixedWidth(qMax(340, previewSize.width() + 48));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 16);
    cardLayout->setSpacing(10);

    auto *title = new QLabel(tr("勋章分享图"), card);
    title->setObjectName(QStringLiteral("medalShareTitle"));
    cardLayout->addWidget(title);

    auto *subtitle = new QLabel(tr("发到群聊、社区或朋友圈，让好友看看你的勋章墙。"), card);
    subtitle->setObjectName(QStringLiteral("medalShareSubtitle"));
    subtitle->setWordWrap(true);
    cardLayout->addWidget(subtitle);

    m_preview = new QLabel(card);
    m_preview->setObjectName(QStringLiteral("medalSharePreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedSize(previewSize);
    m_preview->setCursor(Qt::PointingHandCursor);
    m_preview->setToolTip(tr("点一下复制图片"));
    // 点图直接复制，比来回找按钮顺手
    m_preview->installEventFilter(this);
    cardLayout->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_sizeHint = new QLabel(card);
    m_sizeHint->setObjectName(QStringLiteral("medalShareSizeHint"));
    m_sizeHint->setAlignment(Qt::AlignCenter);
    m_sizeHint->setText(tr("%1 × %2 高清 PNG").arg(m_poster.width()).arg(m_poster.height()));
    cardLayout->addWidget(m_sizeHint);

    auto *footer = new QWidget(card);
    auto *footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(0, 2, 0, 0);
    footerRow->setSpacing(8);

    m_saveButton = new QPushButton(tr("保存图片…"), footer);
    m_saveButton->setObjectName(QStringLiteral("medalSharePrimary"));
    m_saveButton->setCursor(Qt::PointingHandCursor);
    connect(m_saveButton, &QPushButton::clicked, this, &MedalShareDialog::saveImage);
    footerRow->addWidget(m_saveButton);

    m_copyButton = new QPushButton(tr("复制图片"), footer);
    m_copyButton->setObjectName(QStringLiteral("medalShareGhost"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    connect(m_copyButton, &QPushButton::clicked, this, &MedalShareDialog::copyImage);
    footerRow->addWidget(m_copyButton);

    m_shareButton = new QPushButton(tr("分享…"), footer);
    m_shareButton->setObjectName(QStringLiteral("medalShareGhost"));
    m_shareButton->setCursor(Qt::PointingHandCursor);
    m_shareButton->setVisible(AndroidBridge::isAndroidRuntime());
    connect(m_shareButton, &QPushButton::clicked, this, &MedalShareDialog::shareImage);
    footerRow->addWidget(m_shareButton);

    footerRow->addStretch(1);

    m_closeButton = new QPushButton(tr("关闭"), footer);
    m_closeButton->setObjectName(QStringLiteral("medalShareGhost"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    footerRow->addWidget(m_closeButton);

    cardLayout->addWidget(footer);

    // 基类会按屏幕把卡片最小尺寸压回屏内；先把内容高度算准，
    // 免得卡片被当成 0 高度收成一条
    card->setMinimumHeight(card->sizeHint().height());

    auto *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);
}

void MedalShareDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = !tm || tm->currentTheme() == ThemeManager::LightTheme;
    const QString themeColor = tm ? tm->currentThemeColor() : QStringLiteral("#2e7d32");

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 250)" : "rgba(40, 40, 44, 250)";
    const QString cardBorder = isLight ? "rgba(214, 222, 214, 235)" : "rgba(92, 96, 92, 235)";
    const QString titleColor = isLight ? "#17251a" : "#eef5ef";
    const QString subColor   = isLight ? "#5c6b60" : "#a9b7ad";
    const QString muted      = isLight ? "#93a198" : "#7d8b82";
    const QString hoverBg    = isLight ? "rgba(15, 23, 42, 0.06)" : "rgba(255, 255, 255, 0.07)";
    const QString ghostBorder = isLight ? "#cdd8cd" : "#525c54";

    QString style = QStringLiteral(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%CARD% {"
        "    background-color: %CARD_BG%;"
        "    border: 1px solid %CARD_BORDER%;"
        "    border-radius: 20px;"
        "}"
        "QLabel#medalShareTitle { color: %TITLE%; font-size: 16px; font-weight: 800;"
        "    background: transparent; }"
        "QLabel#medalShareSubtitle { color: %SUB%; font-size: 12.5px; background: transparent; }"
        "QLabel#medalSharePreview { background: transparent; border-radius: 16px; }"
        "QLabel#medalShareSizeHint { color: %MUTED%; font-size: 11.5px; background: transparent; }"
        "QPushButton#medalSharePrimary {"
        "    background: %THEME%; border: none; border-radius: 10px;"
        "    color: #ffffff; padding: 8px 20px; font-size: 12.5px; font-weight: 700;"
        "}"
        "QPushButton#medalSharePrimary:hover { background: %THEME_HOVER%; }"
        "QPushButton#medalShareGhost {"
        "    background: transparent; border: 1px solid %GHOST_BORDER%; border-radius: 10px;"
        "    color: %SUB%; padding: 8px 16px; font-size: 12.5px;"
        "}"
        "QPushButton#medalShareGhost:hover { background: %HOVER%; }"
    );
    style.replace(QStringLiteral("%CARD%"), QString::fromLatin1(kCardName));
    style.replace(QStringLiteral("%CARD_BG%"), cardBg);
    style.replace(QStringLiteral("%CARD_BORDER%"), cardBorder);
    style.replace(QStringLiteral("%TITLE%"), titleColor);
    style.replace(QStringLiteral("%SUB%"), subColor);
    style.replace(QStringLiteral("%MUTED%"), muted);
    style.replace(QStringLiteral("%THEME%"), themeColor);
    const QColor themeColorValue(themeColor);
    style.replace(QStringLiteral("%THEME_HOVER%"),
                  themeColorValue.isValid() ? themeColorValue.lighter(115).name() : themeColor);
    style.replace(QStringLiteral("%GHOST_BORDER%"), ghostBorder);
    style.replace(QStringLiteral("%HOVER%"), hoverBg);

    setStyleSheet(style);
}

bool MedalShareDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_preview && event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            copyImage();
            return true;
        }
    }
    return AppDialogBase::eventFilter(watched, event);
}

void MedalShareDialog::refreshPreview()
{
    if (!m_preview || m_poster.isNull())
        return;
    // 按屏幕缩放比渲染预览图，高分屏上不糊
    const QScreen *screen = m_preview->screen();
    const qreal dpr = qMax(1.0, screen ? screen->devicePixelRatio()
                                       : m_preview->devicePixelRatioF());
    const int radius = qRound(16 * dpr);
    const QImage scaled = m_poster.scaled(m_preview->size() * dpr, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
    QPixmap pixmap = roundedPreview(scaled, radius);
    if (!pixmap.isNull())
        pixmap.setDevicePixelRatio(dpr);
    m_preview->setPixmap(pixmap);
}

// ───────────────────────────── 三个出口 ─────────────────────────────

void MedalShareDialog::saveImage()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();
    const QString suggested =
        directory + QLatin1Char('/')
        + (m_suggestedName.isEmpty() ? QStringLiteral("方块盒子-勋章墙.png") : m_suggestedName);

    QString path = QFileDialog::getSaveFileName(this, tr("保存勋章分享图"), suggested,
                                                tr("图片 (*.png *.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");

    if (!m_poster.save(path))
    {
        NotificationManager::showError(this, tr("图片保存失败，请换个位置再试"));
        return;
    }
    NotificationManager::showInfo(this, tr("分享图已保存到 %1").arg(path));
    accept();
}

void MedalShareDialog::copyImage()
{
    if (m_poster.isNull())
        return;
    if (QClipboard *clipboard = QApplication::clipboard())
        clipboard->setImage(m_poster);
    NotificationManager::showInfo(this, tr("分享图已复制，可以直接粘贴到聊天窗口或社区"));
    accept();
}

void MedalShareDialog::shareImage()
{
    // 系统分享面板只认文件：先把图落到临时目录，分享完随手清掉
    const QString path = QDir::tempPath() + QStringLiteral("/blockbox-medal-%1.png")
                                                 .arg(QDateTime::currentDateTime().toString(
                                                     QStringLiteral("yyyyMMddHHmmss")));
    if (!m_poster.save(path))
    {
        NotificationManager::showError(this, tr("图片保存失败，请换个位置再试"));
        return;
    }
    if (!AndroidBridge::shareFile(path, QStringLiteral("image/png")))
    {
        QFile::remove(path);
        NotificationManager::showError(this, tr("这台设备暂时不能直接分享，可以先保存图片"));
        return;
    }
    accept();
}
