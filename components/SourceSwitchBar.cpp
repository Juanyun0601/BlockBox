#include "SourceSwitchBar.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>

SourceSwitchBar::SourceSwitchBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("contentViewSwitch");
    // Qt 只为裸 QWidget 自动设置该属性；子类不设置则样式表的背景/边框不绘制
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(32);

    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(3, 3, 3, 3);
    lay->setSpacing(2);

    auto makeBtn = [this](const QString &text, const char *role) {
        QPushButton *btn = new QPushButton(text, this);
        btn->setObjectName("contentViewSwitchBtn");
        // 文本段宽于图标段；高度 26 与轨道内边距匹配（QSS 圆角 13 = 高度一半）
        btn->setFixedHeight(26);
        btn->setMinimumWidth(qMax(72, btn->fontMetrics().horizontalAdvance(text) + 28));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setCheckable(true);
        btn->setProperty("sourceRole", QLatin1String(role));
        return btn;
    };

    m_modrinthBtn = makeBtn(QStringLiteral("Modrinth"), "modrinth");
    lay->addWidget(m_modrinthBtn);

    m_curseforgeBtn = makeBtn(QStringLiteral("CurseForge"), "curseforge");
    lay->addWidget(m_curseforgeBtn);

    connect(m_modrinthBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeSource != QLatin1String("modrinth"))
            emit sourceSelected(QStringLiteral("modrinth"));
    });
    connect(m_curseforgeBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeSource != QLatin1String("curseforge"))
            emit sourceSelected(QStringLiteral("curseforge"));
    });

    updateButtons();
}

void SourceSwitchBar::setActiveSource(const QString &source)
{
    if (m_activeSource == source)
        return;
    m_activeSource = source;
    updateButtons();
}

void SourceSwitchBar::updateButtons()
{
    const bool mrActive = (m_activeSource == QLatin1String("modrinth"));
    const bool cfActive = (m_activeSource == QLatin1String("curseforge"));

    m_modrinthBtn->setProperty("active", mrActive);
    m_modrinthBtn->setChecked(mrActive);
    m_curseforgeBtn->setProperty("active", cfActive);
    m_curseforgeBtn->setChecked(cfActive);

    for (QPushButton *btn : {m_modrinthBtn, m_curseforgeBtn}) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}
