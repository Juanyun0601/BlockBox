#include "NotificationManager.h"
#include "NotificationCard.h"
#include "NetworkCheckDialog.h"

#include "platform.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPropertyAnimation>
#include <QWidget>

NotificationManager *NotificationManager::m_instance = nullptr;

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

NotificationManager::~NotificationManager()
{
    m_instance = nullptr;
    // 卡片挂在各自的父控件下，由父控件负责销毁；这里只清空指针表。
    // 退出时若再对卡片调用 deleteLater()，会用到已经销毁的事件循环而崩溃。
    m_cards.clear();
}

NotificationManager *NotificationManager::instance()
{
    if (!m_instance) {
        // 挂到 qApp 下，应用退出时随 qApp 一并析构，残留卡片也会被清理
        m_instance = new NotificationManager(QCoreApplication::instance());
    }
    return m_instance;
}

void NotificationManager::showSuccess(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Success, parent, text, duration);
}

void NotificationManager::showInfo(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Info, parent, text, duration);
}

void NotificationManager::showError(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Error, parent, text, duration);
}

void NotificationManager::showErrorWithNetworkCheck(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Error, parent, text, duration,
                          NotificationCard::tr("网络自检"));
}

QString NotificationManager::withDetail(const QString &titleTemplate, const QString &titleArg,
                                        const QString &detail)
{
    QString format = titleTemplate;
    if (!format.contains(QLatin1String("%1"))) {
        // 连标题参数都丢了：把参数补在前面，至少不吞信息
        format.prepend(QLatin1String("%1 "));
    }
    if (!format.contains(QLatin1String("%2"))) {
        // 译文漏了 %2：详情换行接在后面，效果与模板一致
        return format.arg(titleArg) + QLatin1String("\r\n") + detail;
    }
    // 两个占位符一次性替换：标题参数里万一含有 %2 也不会被再次解析
    return format.arg(titleArg, detail);
}

void NotificationManager::showCard(NotificationCard::Type type, QWidget *parent,
                                    const QString &text, int duration,
                                    const QString &actionText)
{
    // 卡片统一挂到窗口上：调用方多半传的是页面控件，页面带顶栏 / 侧栏偏移，
    // 按页面坐标算「左下角」会把提示推到窗口外面
    QWidget *host = parent ? parent->window() : nullptr;
    NotificationCard *card = new NotificationCard(text, type, duration, actionText, host);

    // 窗口尺寸一变就重新贴左下角，卡片不会留在旧位置露出窗口外
    if (host && !m_hosts.contains(host)) {
        m_hosts.insert(host);
        host->installEventFilter(this);
        connect(host, &QObject::destroyed, this, [this, host]() { m_hosts.remove(host); });
    }

    connect(card, &NotificationCard::dismissRequested,
            this, &NotificationManager::removeCard);
    // 点了「网络自检」：以宿主窗口为父弹出网络连通性自检对话框
    connect(card, &NotificationCard::actionClicked, this, [host](NotificationCard *card) {
        Q_UNUSED(card);
        NetworkCheckDialog::runCheck(host);
    });
    // 父控件先于本管理器销毁时（例如关闭窗口）把卡片指针摘掉，避免列表里留下悬空指针
    connect(card, &QObject::destroyed, this, [this, card]() { m_cards.removeAll(card); });

    m_cards.append(card);

    NotificationRecord rec;
    rec.text = text;
    rec.type = type;
    rec.timestamp = QDateTime::currentDateTime();
    m_history.append(rec);

    if (m_history.size() > 500)
        m_history.removeFirst();

    // 先让旧卡片让出位置（新卡此时还没显示），再让新卡从左边滑入底部
    repositionAll();
    card->startSlideIn();
}

void NotificationManager::removeCard(NotificationCard *card)
{
    m_cards.removeOne(card);
    repositionAll();
}

QList<NotificationRecord> NotificationManager::history()
{
    return instance()->m_history;
}

namespace {
/// 通知卡片的水平落点：桌面靠左下角；手机端屏幕窄且左下角是侧边导航，
/// 压在上面会挡住操作，改为水平居中（悬停暂停等悬停交互在触屏上也没有意义）
int cardX(const NotificationCard *card, QWidget *win)
{
    if (Platform::getFormFactor() == Platform::FormFactor::Mobile)
        return qMax(8, (win->width() - card->width()) / 2);
    return 16;
}
}

void NotificationManager::clearHistory()
{
    instance()->m_history.clear();
}

void NotificationManager::repositionAll(bool animate)
{
    if (m_cards.isEmpty()) return;

    // 卡片挂在自己的窗口上，pos() 就是窗口坐标，和下面的落点计算同一套坐标系
    QWidget *host = m_cards.first()->parentWidget();
    if (!host || !host->isVisible()) return;

    int y = host->height() - 16;

    for (int i = m_cards.size() - 1; i >= 0; --i) {
        NotificationCard *card = m_cards.at(i);
        QWidget *cardHost = card->parentWidget();
        if (!cardHost) continue;

        int targetY;
        if (cardHost == host) {
            y -= card->height();
            // 卡片叠得太多时压在窗口顶部，别再往上顶出窗口
            targetY = qMax(8, y);
            y -= 8;
        } else {
            // 挂在别的窗口上（例如对话框里的提示）：各自贴自己的左下角
            targetY = qMax(8, cardHost->height() - card->height() - 16);
        }

        // 正在滑出的卡片：位置先替它留着，但不要再往下走去停动画 / 改坐标。
        // 退场动画一旦被 stopPosAnimations() 打断，finished 就不会发出，
        // 卡片会永远停在屏幕上（倒计时显示 0s），这就是「提示有时候不消失」的来源
        if (card->isDismissing())
            continue;

        // 卡片比窗口还宽（窄窗 / 手机竖屏）时拉回窗口内，右侧留 8px
        const int x = qBound(8, cardX(card, cardHost),
                             qMax(8, cardHost->width() - card->width() - 8));

        const QPoint targetPos(x, targetY);
        if (card->pos() == targetPos)
            continue;

        // 重排前先停掉正在跑的滑入 / 旧的重排动画，别让两个动画抢同一个位置
        card->stopPosAnimations();
        if (!animate) {
            card->move(targetPos);
            continue;
        }

        QPropertyAnimation *anim = new QPropertyAnimation(card, "pos", card);
        anim->setDuration(200);
        anim->setStartValue(card->pos());
        anim->setEndValue(targetPos);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

bool NotificationManager::eventFilter(QObject *watched, QEvent *event)
{
    // 宿主窗口被拖动 / 缩放时卡片要立刻跟着换位，否则会停在旧坐标、露出窗口外
    if (event->type() == QEvent::Resize && m_hosts.contains(static_cast<QWidget *>(watched)))
        repositionAll(false);
    return QObject::eventFilter(watched, event);
}
