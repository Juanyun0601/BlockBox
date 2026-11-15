/**
 * @file   HomeHotThreadsPanel.cpp
 * @brief  首页论坛热门帖栏实现
 * @author BlockBox Team
 * @date   2026-11-15
 */

#include "HomeHotThreadsPanel.h"

#include "ForumKit.h"
#include "OutlinedLabel.h"
#include "layouts/FlowLayout.h"
#include "utils/ForumAccountService.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// 论坛热门帖同步间隔：热门榜变化不快，五分钟刷一次足够新鲜
constexpr int kPollIntervalMs = 5 * 60 * 1000;
// 首页这一栏最多摆多少张帖子卡片，其余的去社区页看
constexpr int kMaxCards = 8;
// 卡片理想宽度：容器够宽时一排多摆几张，窄窗口自动减少列数
constexpr int kIdealCardWidth = 240;
// 卡片之间 / 行之间的间距
constexpr int kCardSpacing = 12;

} // namespace

HomeHotThreadsPanel::HomeHotThreadsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("homeHotThreadsPanel"));
    setAttribute(Qt::WA_StyledBackground, false);

    buildUi();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &HomeHotThreadsPanel::refresh);

    // 会话过期不影响本栏：热门帖匿名就能看，保持原样即可
}

HomeHotThreadsPanel::~HomeHotThreadsPanel() = default;

void HomeHotThreadsPanel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active)
    {
        m_pollTimer->start();
        refresh();
    }
    else
    {
        m_pollTimer->stop();
    }
}

void HomeHotThreadsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // ── 标题行：论坛热门帖 · 查看全部 ──
    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_titleLabel = new OutlinedLabel(tr("论坛热门帖"), this);
    m_titleLabel->setObjectName(QStringLiteral("homeSectionTitle"));
    header->addWidget(m_titleLabel);
    header->addStretch(1);

    m_openButton = new QPushButton(tr("查看全部"), this);
    m_openButton->setObjectName(QStringLiteral("homeLinkBtn"));
    m_openButton->setCursor(Qt::PointingHandCursor);
    m_openButton->setToolTip(tr("打开社区论坛页"));
    connect(m_openButton, &QPushButton::clicked, this,
            &HomeHotThreadsPanel::viewAllRequested);
    header->addWidget(m_openButton);

    root->addLayout(header);

    // ── 帖子卡片流：按宽度自动换行分列 ──
    m_listHost = new QWidget(this);
    m_listHost->setObjectName(QStringLiteral("homeHotThreadsListHost"));
    m_listLayout = new FlowLayout(m_listHost, 0, kCardSpacing, kCardSpacing);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listHost->installEventFilter(this);
    root->addWidget(m_listHost);

    // ── 空态行：加载中 / 加载失败 / 论坛还没有帖子时占住卡片流的位置 ──
    m_stateBar = new QWidget(this);
    m_stateBar->setObjectName(QStringLiteral("homeHotThreadsState"));
    auto *stateRow = new QHBoxLayout(m_stateBar);
    stateRow->setContentsMargins(0, 8, 0, 8);
    stateRow->setSpacing(12);
    stateRow->addStretch(1);

    m_stateLabel = new QLabel(m_stateBar);
    m_stateLabel->setObjectName(QStringLiteral("homeFriendsStateText"));
    stateRow->addWidget(m_stateLabel);

    m_stateBtn = new QPushButton(m_stateBar);
    m_stateBtn->setObjectName(QStringLiteral("homeFriendsStateBtn"));
    m_stateBtn->setCursor(Qt::PointingHandCursor);
    m_stateBtn->setFixedHeight(30);
    connect(m_stateBtn, &QPushButton::clicked, this, [this]() {
        if (m_stateAction)
            m_stateAction();
    });
    stateRow->addWidget(m_stateBtn);
    stateRow->addStretch(1);

    m_stateBar->hide();
    root->addWidget(m_stateBar);

    // 首次显示先占一行提示，等帖子数据回来再由 render() 换成卡片
    showState(tr("正在加载热门帖子…"));
}

bool HomeHotThreadsPanel::eventFilter(QObject *watched, QEvent *event)
{
    // 容器宽度变化后重新分列：延迟一拍执行，避免在布局激活过程中增删子项
    if (watched == m_listHost && event->type() == QEvent::Resize && m_listHost->isVisible())
        scheduleRebuild();
    return QWidget::eventFilter(watched, event);
}

void HomeHotThreadsPanel::refresh()
{
    loadThreads();
    // 还没有任何数据时先显示「正在加载」，避免短时间停在旧空态上
    if (m_threads.isEmpty() && !m_loadedOnce)
    {
        m_builtSignature.clear();
        render();
    }
}

void HomeHotThreadsPanel::loadThreads()
{
    if (m_loading)
        return;
    m_loading = true;

    QPointer<HomeHotThreadsPanel> guard(this);
    // 热门榜匿名就能看：直接走论坛账户服务的统一请求（自动带人机验证令牌与重试）
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/threads?sort=hot&per_page=8"),
        [guard](const QJsonObject &root) {
            if (!guard)
                return;
            QVector<HotThread> threads;
            const QJsonArray array = root.value(QStringLiteral("threads")).toArray();
            for (const QJsonValue &value : array)
            {
                const QJsonObject item = value.toObject();
                HotThread info;
                info.id = item.value(QStringLiteral("id")).toInt();
                if (info.id <= 0)
                    continue;
                info.title = item.value(QStringLiteral("title")).toString();
                info.boardName = item.value(QStringLiteral("board")).toObject()
                                     .value(QStringLiteral("name")).toString();
                info.replies = item.value(QStringLiteral("reply_count")).toInt();
                info.views = item.value(QStringLiteral("views")).toInt();
                info.lastPostAt =
                    static_cast<qint64>(item.value(QStringLiteral("last_post_at")).toDouble());
                threads.append(info);
            }
            guard->m_loading = false;
            guard->m_threads = threads;
            guard->m_failed = false;
            guard->m_loadedOnce = true;
            guard->render();
        },
        [guard](const QString &, int) {
            if (!guard)
                return;
            guard->m_loading = false;
            guard->m_failed = true;
            guard->m_loadedOnce = true;
            guard->render();
        });
}

QString HomeHotThreadsPanel::dataSignature() const
{
    QStringList parts;
    parts << QStringLiteral("%1|%2|%3|%4")
                 .arg(m_loading ? 1 : 0)
                 .arg(m_failed ? 1 : 0)
                 .arg(m_loadedOnce ? 1 : 0)
                 .arg(m_threads.size());
    for (const HotThread &info : m_threads)
    {
        parts << QStringLiteral("T%1|%2|%3|%4|%5|%6")
                     .arg(info.id)
                     .arg(info.title, info.boardName)
                     .arg(info.replies)
                     .arg(info.views)
                     .arg(info.lastPostAt);
    }
    return parts.join(QLatin1Char('\n'));
}

int HomeHotThreadsPanel::calculateCardWidth() const
{
    const int availW = m_listHost ? m_listHost->width() : 0;
    if (availW <= 0)
        return kIdealCardWidth;

    // 一排能摆几张：宽窗口最多 6 张，窄窗口自动减到 1 张
    int cols = (availW + kCardSpacing) / (kIdealCardWidth + kCardSpacing);
    cols = qBound(1, cols, 6);
    const int cardW = (availW - (cols - 1) * kCardSpacing) / cols;
    return qMax(cardW, 160);
}

void HomeHotThreadsPanel::clearCards()
{
    if (!m_listLayout)
        return;
    while (QLayoutItem *item = m_listLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void HomeHotThreadsPanel::showState(const QString &text, const QString &actionText,
                                    const std::function<void()> &onAction)
{
    if (!m_stateBar)
        return;
    m_stateLabel->setText(text);
    m_stateAction = onAction;
    m_stateBtn->setText(actionText);
    m_stateBtn->setVisible(!actionText.isEmpty() && static_cast<bool>(onAction));
    m_listHost->hide();
    m_stateBar->show();
}

void HomeHotThreadsPanel::hideState()
{
    if (!m_stateBar)
        return;
    m_stateAction = nullptr;
    m_stateBar->hide();
    m_listHost->show();
}

void HomeHotThreadsPanel::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildPending = false;
        render();
    });
}

QWidget *HomeHotThreadsPanel::createThreadCard(const HotThread &info, int cardWidth)
{
    auto *card = new ForumKit::ClickableCard(m_listHost);
    card->setObjectName(QStringLiteral("homeHotThreadCard"));
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedWidth(cardWidth);
    card->setFixedHeight(58);

    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(10, 8, 10, 8);
    column->setSpacing(4);

    auto *titleLabel = new ForumKit::ElidedLabel(card);
    titleLabel->setObjectName(QStringLiteral("homeHotThreadTitle"));
    titleLabel->setFullText(info.title);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    column->addWidget(titleLabel);

    // 元信息行：板块胶囊 + 回复 / 浏览 + 最后回复时间
    auto *metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(6);

    if (!info.boardName.isEmpty())
    {
        auto *board = new QLabel(info.boardName, card);
        board->setObjectName(QStringLiteral("homeHotThreadBoard"));
        metaRow->addWidget(board, 0, Qt::AlignVCenter);
    }

    auto *meta = new QLabel(card);
    meta->setObjectName(QStringLiteral("homeHotThreadMeta"));
    QStringList metaParts;
    metaParts << tr("%1 回复").arg(info.replies)
              << tr("%1 浏览").arg(info.views);
    if (info.lastPostAt > 0)
        metaParts << ForumKit::relativeTime(info.lastPostAt);
    meta->setText(metaParts.join(QStringLiteral(" · ")));
    metaRow->addWidget(meta, 1, Qt::AlignVCenter);

    column->addLayout(metaRow);

    card->onClick = [this, threadId = info.id]() { emit threadOpenRequested(threadId); };
    card->setToolTip(info.title);

    return card;
}

void HomeHotThreadsPanel::render()
{
    if (!m_listLayout || !m_listHost || !m_stateBar)
        return;

    const int cardWidth = calculateCardWidth();
    const QString signature = dataSignature();
    if (signature == m_builtSignature && cardWidth == m_builtCardWidth)
        return;
    m_builtSignature = signature;
    m_builtCardWidth = cardWidth;

    clearCards();

    if (m_threads.isEmpty())
    {
        if (m_failed)
        {
            showState(tr("热门帖子没加载出来，稍后再试试"),
                      tr("重试"), [this]() { refresh(); });
        }
        else if (!m_loadedOnce || m_loading)
        {
            showState(tr("正在加载热门帖子…"));
        }
        else
        {
            showState(tr("论坛还没有热门帖子，去社区逛逛吧"),
                      tr("去社区"), [this]() { emit viewAllRequested(); });
        }
        return;
    }

    hideState();

    const int count = qMin(m_threads.size(), kMaxCards);
    for (int i = 0; i < count; ++i)
        m_listLayout->addWidget(createThreadCard(m_threads[i], cardWidth));

    // 让父布局重新查询本栏的 sizeHint（分列变化导致高度变化时才能正确传导）
    m_listHost->updateGeometry();
}
