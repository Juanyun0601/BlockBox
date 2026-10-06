#include "ContentViewSwitch.h"
#include "utils/ThemeManager.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPushButton>
#include <QStyle>

namespace {

// 列表图标：三横线（左对齐点 + 三线段），按设备像素比绘制以保证高分屏清晰
QIcon makeListIcon(const QColor &c, qreal dpr)
{
    const int px = qMax(1, qRound(18 * dpr));
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(dpr, dpr);
    QPen pen(c, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    for (int y : {4, 9, 14}) {
        p.drawLine(5, y, 15, y);
        p.drawPoint(2, y);
    }
    return QIcon(pm);
}

// 瀑布流图标：四宫格（右下格更高，体现不等高瀑布流）
QIcon makeMasonryIcon(const QColor &c, qreal dpr)
{
    const int px = qMax(1, qRound(18 * dpr));
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(dpr, dpr);
    p.setBrush(QBrush(c));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(2.0, 2.0, 6.0, 6.0), 1.5, 1.5);
    p.drawRoundedRect(QRectF(10.0, 2.0, 6.0, 3.5), 1.5, 1.5);
    p.drawRoundedRect(QRectF(2.0, 10.0, 6.0, 6.0), 1.5, 1.5);
    p.drawRoundedRect(QRectF(10.0, 7.5, 6.0, 8.5), 1.5, 1.5);
    return QIcon(pm);
}

} // namespace

ContentViewSwitch::ContentViewSwitch(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("contentViewSwitch");
    // Qt 只为裸 QWidget 自动设置该属性；子类不设置则样式表的背景/边框不绘制
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(32);

    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(3, 3, 3, 3);
    lay->setSpacing(2);

    auto makeBtn = [this]() {
        QPushButton *btn = new QPushButton(this);
        btn->setObjectName("contentViewSwitchBtn");
        btn->setFixedSize(30, 26);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(18, 18));
        btn->setCheckable(true);
        return btn;
    };

    m_listBtn = makeBtn();
    m_listBtn->setToolTip(tr("列表视图"));
    m_listBtn->setProperty("viewRole", "list");
    lay->addWidget(m_listBtn);

    m_masonryBtn = makeBtn();
    m_masonryBtn->setToolTip(tr("瀑布流视图"));
    m_masonryBtn->setProperty("viewRole", "masonry");
    lay->addWidget(m_masonryBtn);

    connect(m_listBtn, &QPushButton::clicked, this, [this]() {
        setViewMode(List);
    });
    connect(m_masonryBtn, &QPushButton::clicked, this, [this]() {
        setViewMode(Masonry);
    });

    // 图标配色依赖深浅主题，切换主题后重绘
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { updateButtons(); });

    updateButtons();
}

void ContentViewSwitch::setViewMode(ViewMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    updateButtons();
    emit viewModeChanged(mode);
}

void ContentViewSwitch::updateButtons()
{
    // 选中段是主色实心药丸 → 图标反白；未选中段用灰，深色主题提亮保证对比
    const QColor activeColor(Qt::white);
    const QColor idleColor(ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme
                               ? QColor("#64748B")
                               : QColor("#9CA3AF"));
    const qreal dpr = devicePixelRatioF();

    const bool listActive = (m_mode == List);
    const bool masonryActive = (m_mode == Masonry);

    m_listBtn->setIcon(makeListIcon(listActive ? activeColor : idleColor, dpr));
    m_listBtn->setProperty("active", listActive);
    m_listBtn->setChecked(listActive);

    m_masonryBtn->setIcon(makeMasonryIcon(masonryActive ? activeColor : idleColor, dpr));
    m_masonryBtn->setProperty("active", masonryActive);
    m_masonryBtn->setChecked(masonryActive);

    for (QPushButton *btn : {m_listBtn, m_masonryBtn}) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

QString ContentViewSwitch::persistedKey(const QString &pageKey)
{
    return QStringLiteral("bb_view_%1").arg(pageKey);
}

ContentViewSwitch::ViewMode ContentViewSwitch::loadPersisted(const QString &pageKey, ViewMode fallback)
{
    QSettings settings;
    const QString key = persistedKey(pageKey);
    const int v = settings.value(key, static_cast<int>(fallback)).toInt();
    return (v == static_cast<int>(List)) ? List : Masonry;
}

void ContentViewSwitch::savePersisted(const QString &pageKey, ViewMode mode)
{
    QSettings settings;
    settings.setValue(persistedKey(pageKey), static_cast<int>(mode));
    settings.sync();
}
