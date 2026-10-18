#include "TranslateSwitchCard.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

TranslateSwitchCard::TranslateSwitchCard(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    if (parent)
        parent->installEventFilter(this);
}

void TranslateSwitchCard::buildUi()
{
    setObjectName(QStringLiteral("contentTranslateCard"));
    setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    m_statusLabel = new QLabel(tr("AI 翻译中…"), this);
    m_statusLabel->setObjectName(QStringLiteral("contentTranslateStatus"));
    m_statusLabel->hide();
    layout->addWidget(m_statusLabel);

    auto makeSegmentBtn = [this, layout](const QString &text, const QString &objectName,
                                         const QString &tip) {
        QPushButton *btn = new QPushButton(text, this);
        btn->setObjectName(objectName);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tip);
        btn->setFixedHeight(28);
        btn->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(btn);
        return btn;
    };

    m_enBtn = makeSegmentBtn(QStringLiteral("EN"), QStringLiteral("contentTranslateEnBtn"),
                             tr("显示英文原文"));
    m_zhBtn = makeSegmentBtn(QStringLiteral("中"), QStringLiteral("contentTranslateZhBtn"),
                             tr("用 AI 翻译当前页面"));
    m_enBtn->setChecked(true);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 46));
    setGraphicsEffect(shadow);

    connect(m_enBtn, &QPushButton::clicked, this, [this]() {
        setChineseMode(false);
        emit modeChangeRequested(false);
    });
    connect(m_zhBtn, &QPushButton::clicked, this, [this]() {
        setChineseMode(true);
        if (m_busy)
            return;
        emit modeChangeRequested(true);
    });

    adjustSize();
    reposition();
    show();
}

void TranslateSwitchCard::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (m_statusLabel)
        m_statusLabel->setVisible(busy);
    if (m_zhBtn)
        m_zhBtn->setEnabled(!busy);
    adjustSize();
    reposition();
}

void TranslateSwitchCard::setChineseMode(bool chinese)
{
    if (m_enBtn)
        m_enBtn->setChecked(!chinese);
    if (m_zhBtn)
        m_zhBtn->setChecked(chinese);
}

bool TranslateSwitchCard::isChineseMode() const
{
    return m_zhBtn && m_zhBtn->isChecked();
}

void TranslateSwitchCard::reposition()
{
    QWidget *parent = parentWidget();
    if (!parent)
        return;

    const int margin = 24;
    adjustSize();
    const QSize hint = sizeHint();
    const int cardW = qMax(hint.width(), 96);
    const int cardH = qMax(hint.height(), 36);
    setGeometry(qMax(0, parent->width() - cardW - margin),
                qMax(0, parent->height() - cardH - margin),
                cardW, cardH);
}

bool TranslateSwitchCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        reposition();
    return QWidget::eventFilter(watched, event);
}
