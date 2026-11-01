/** 诊断：账户页登录视图在不同窗口高度下是否需要上下滚动 */
#include "pages/ForumAccountPage.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFrame>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

namespace
{
QPushButton *findVisibleButton(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
    {
        if (button->isVisible() && button->text() == text)
            return button;
    }
    return nullptr;
}

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

int main(int argc, char **argv)
{
    QSettings forumSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    const QStringList keys = {QStringLiteral("forum/baseUrl"), QStringLiteral("forum/sessionToken"),
                              QStringLiteral("forum/sessionBaseUrl"),
                              QStringLiteral("forum/cachedUser")};
    QMap<QString, QVariant> backup;
    for (const QString &key : keys)
        backup.insert(key, forumSettings.value(key));

    QApplication app(argc, argv);
    forumSettings.setValue(QStringLiteral("forum/baseUrl"), QStringLiteral("http://127.0.0.1:3210"));
    forumSettings.remove(QStringLiteral("forum/sessionToken"));
    forumSettings.remove(QStringLiteral("forum/sessionBaseUrl"));
    forumSettings.remove(QStringLiteral("forum/cachedUser"));
    forumSettings.sync();

    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new ForumAccountPage(&host);
    layout->addWidget(page);
    host.resize(1200, 800);
    host.show();
    pump(600);

    QPushButton *entry = findVisibleButton(page, QStringLiteral("登录"));
    fprintf(stderr, "[fit] 登录入口 %s\n", entry ? "找到" : "没找到");
    if (entry)
        entry->click();
    pump(500);

    auto *scroll = page->findChild<QScrollArea *>(QStringLiteral("fbScroll"));
    auto *card = page->findChild<QFrame *>(QStringLiteral("fbAuthCard"));
    if (!scroll || !card)
    {
        fprintf(stderr, "[fit] 找不到滚动区或登录卡\n");
        return 2;
    }

    const int heights[] = {800, 744, 700, 660, 620, 580, 540, 500, 460};
    for (int h : heights)
    {
        host.resize(1200, h);
        pump(200);
        fprintf(stderr,
                "[fit] window_h=%d viewport_h=%d content_h=%d scrollMax=%d card_h=%d "
                "card_top=%d card_bottom=%d\n",
                h, scroll->viewport()->height(), scroll->widget()->height(),
                scroll->verticalScrollBar()->maximum(), card->height(), card->y(),
                card->y() + card->height());
    }

    auto *stack = page->findChild<QStackedWidget *>(QStringLiteral("fbViewStack"));
    if (stack)
    {
        QWidget *current = stack->currentWidget();
        fprintf(stderr,
                "[fit] stack current=%d stack_hint=%dx%d stack_min=%dx%d page_hint=%dx%d "
                "page_min=%dx%d content_hint=%dx%d content_min=%dx%d card_hint=%dx%d card_min=%dx%d\n",
                stack->currentIndex(), stack->sizeHint().width(), stack->sizeHint().height(),
                stack->minimumSizeHint().width(), stack->minimumSizeHint().height(),
                current ? current->sizeHint().width() : -1,
                current ? current->sizeHint().height() : -1,
                current ? current->minimumSizeHint().width() : -1,
                current ? current->minimumSizeHint().height() : -1,
                scroll->widget()->sizeHint().width(), scroll->widget()->sizeHint().height(),
                scroll->widget()->minimumSizeHint().width(),
                scroll->widget()->minimumSizeHint().height(), card->sizeHint().width(),
                card->sizeHint().height(), card->minimumSizeHint().width(),
                card->minimumSizeHint().height());
    }

    host.resize(1200, 500);
    pump(300);
    host.grab().save(QStringLiteral("probe_out/login_fit_500.png"));

    for (auto it = backup.cbegin(); it != backup.cend(); ++it)
    {
        if (it.value().isValid())
            forumSettings.setValue(it.key(), it.value());
        else
            forumSettings.remove(it.key());
    }
    forumSettings.sync();
    return 0;
}
