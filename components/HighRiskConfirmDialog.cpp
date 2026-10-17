#include "HighRiskConfirmDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

namespace {

const char kRiskRed[] = "#e5484d";

} // namespace

HighRiskConfirmDialog::HighRiskConfirmDialog(const QString &title, const QString &message,
                                             const QString &trustAction, QWidget *parent)
    : AppDialogBase(parent)
    , m_message(message)
    , m_trustAction(trustAction)
{
    setObjectName(QStringLiteral("highRiskConfirmDialog"));
    setWindowTitle(title);
    initUI();
}

void HighRiskConfirmDialog::initUI()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString cardBg     = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString textColor  = isLight ? "#4a4a4a" : "#c9c9c9";
    const QString subColor   = isLight ? "#8a8a8a" : "#909090";

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("highRiskConfirmCard"));
    card->setMinimumWidth(420);
    card->setMaximumWidth(560);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 22);
    cardLayout->setSpacing(14);

    // 顶部：红色警示图标 + 标题
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(14);
    topLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *iconLabel = new QLabel(card);
    iconLabel->setFixedSize(44, 44);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(makeWarningPixmap());
    topLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    QVBoxLayout *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    titleCol->setContentsMargins(0, 0, 0, 0);

    QLabel *titleLabel = new QLabel(windowTitle(), card);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 15px; font-weight: 700;")
                                  .arg(QLatin1String(kRiskRed)));
    titleCol->addWidget(titleLabel);

    topLayout->addLayout(titleCol, 1);
    cardLayout->addLayout(topLayout);

    // 正文
    QLabel *msgLabel = new QLabel(m_message, card);
    msgLabel->setWordWrap(true);
    msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msgLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(textColor));
    cardLayout->addWidget(msgLabel);

    // 复选确认
    m_confirmBox = new QCheckBox(
        tr("我已了解风险，信任并允许该插件%1").arg(m_trustAction), card);
    m_confirmBox->setStyleSheet(QStringLiteral(
        "QCheckBox { color: %1; font-size: 13px; font-weight: 600; spacing: 6px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px;"
        "  border: 1px solid %1; background: transparent; }"
        "QCheckBox::indicator:hover { background: rgba(229, 72, 77, 26); }"
        "QCheckBox::indicator:checked { background: %1; }")
        .arg(QLatin1String(kRiskRed)));
    cardLayout->addWidget(m_confirmBox);

    cardLayout->addSpacing(6);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->addStretch();

    QPushButton *cancelBtn = new QPushButton(tr("拒绝"), card);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 12px; padding: 7px 22px; font-size: 13px; }"
        "QPushButton:hover { background-color: rgba(0, 0, 0, 28); }")
        .arg(cardBg, textColor, cardBorder));
    btnLayout->addWidget(cancelBtn);

    m_okBtn = new QPushButton(tr("信任并%1").arg(m_trustAction), card);
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setEnabled(false);
    m_okBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none;"
        " border-radius: 12px; padding: 7px 22px; font-size: 13px; font-weight: 600; }"
        "QPushButton:disabled { background-color: #c9c9c9; color: #f5f5f5; }")
        .arg(themeColor));
    btnLayout->addWidget(m_okBtn);

    cardLayout->addLayout(btnLayout);

    card->setStyleSheet(QStringLiteral(
        "QWidget#highRiskConfirmCard { background-color: %1; border: 1px solid %2;"
        " border-radius: 16px; }").arg(cardBg, cardBorder));

    rootLayout->addWidget(card, 0, Qt::AlignCenter);

    connect(m_confirmBox, &QCheckBox::toggled, m_okBtn, &QPushButton::setEnabled);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        m_confirmed = true;
        accept();
    });
}

QPixmap HighRiskConfirmDialog::makeWarningPixmap() const
{
    const int s = 44;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(QLatin1String(kRiskRed)));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, s, s);

    QFont f = p.font();
    f.setPointSize(18);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, s, s), Qt::AlignCenter, QStringLiteral("!"));

    return pm;
}
