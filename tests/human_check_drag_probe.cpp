/**
 * @file   human_check_drag_probe.cpp
 * @brief  滑块拖动时拼图到底有没有跟着走的探针：逐步拖、每步截一张拼图区
 *
 * 用法：human_check_drag_probe.exe <论坛地址> <截图目录>
 * 输出：drag-step-N.png（每一步的拼图区），并在 stdout 打印每步的滑块值；
 *       退出码 0 表示走完全程。
 */
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

#include "components/HumanCheckDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {

constexpr int kSlideHeight = 44;
constexpr int kHandleWidth = 54;
constexpr int kHandleMargin = 4;
constexpr int kBarWidth = 320;

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QWidget *findBySize(QWidget *root, int w, int h)
{
    const QList<QWidget *> children = root->findChildren<QWidget *>();
    for (QWidget *child : children)
    {
        if (child->width() == w && child->height() == h && child->isVisible())
            return child;
    }
    return nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString baseUrl = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("http://127.0.0.1:3211");
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                    : QStringLiteral("D:/BlockBox/.tmp-shots");
    QDir().mkpath(outDir);

    SettingsManager *settings = SettingsManager::instance();
    const QString previousUrl = settings->forumBaseUrl();
    settings->setForumBaseUrl(baseUrl);

    ForumAccountService *service = ForumAccountService::instance();
    service->clearHumanCheck();

    QWidget host;
    host.resize(1000, 720);
    host.show();
    waitMs(120);

    QEventLoop loop;
    HumanCheckDialog *dialog = nullptr;

    QObject::connect(service, &ForumAccountService::humanChallengeReady, &app,
                     [&](const QJsonObject &challenge) {
                         if (!dialog)
                             return;
                         const int pad = challenge.value(QStringLiteral("piece")).toObject()
                                             .value(QStringLiteral("pad"))
                                             .toInt();
                         const int span = challenge.value(QStringLiteral("piece")).toObject()
                                              .value(QStringLiteral("w"))
                                              .toInt();
                         const int maxAnswer =
                             challenge.value(QStringLiteral("width")).toInt() - span;

                         QTimer::singleShot(200, &loop, [&, pad, maxAnswer] {
                             QWidget *puzzle = findBySize(dialog, 320, 180);
                             QWidget *bar = findBySize(dialog, kBarWidth, kSlideHeight);
                             qDebug() << "puzzle" << puzzle << "bar" << bar << "pad" << pad
                                      << "max" << maxAnswer;
                             if (!puzzle || !bar)
                             {
                                 loop.quit();
                                 return;
                             }

                             const double maxHandleX =
                                 qMax<qreal>(kHandleMargin, kBarWidth - kHandleWidth - kHandleMargin);
                             const double span = maxHandleX - kHandleMargin;
                             const int y = kSlideHeight / 2;
                             const int startX = int(kHandleMargin + kHandleWidth / 2.0);

                             const auto send = [&](QEvent::Type type, int x, Qt::MouseButtons buttons) {
                                 QMouseEvent event(type, QPointF(x, y), bar->mapToGlobal(QPointF(x, y)),
                                                   Qt::LeftButton, buttons, Qt::NoModifier);
                                 QCoreApplication::sendEvent(bar, &event);
                             };

                             // 按在手柄中间，拖到 92% 的位置，分 8 步走
                             send(QEvent::MouseButtonPress, startX, Qt::LeftButton);
                             waitMs(60);
                             puzzle->grab().save(
                                 outDir + QStringLiteral("/drag-step-0.png"));
                             qDebug() << "step 0 value 0";

                             const int steps = 8;
                             for (int i = 1; i <= steps; ++i)
                             {
                                 const double ratio = 0.92 * i / steps;
                                 const double handleX = kHandleMargin + ratio * span;
                                 const int cursorX = int(handleX + kHandleWidth / 2.0);
                                 send(QEvent::MouseMove, cursorX, Qt::LeftButton);
                                 waitMs(60);
                                 const double progress = (handleX - kHandleMargin) / span;
                                 const int value = qRound(progress * maxAnswer);
                                 qDebug() << "step" << i << "value" << value;
                                 puzzle->grab().save(outDir
                                                     + QStringLiteral("/drag-step-%1.png").arg(i));
                             }

                             send(QEvent::MouseButtonRelease,
                                  int(kHandleMargin + 0.92 * span + kHandleWidth / 2.0), Qt::NoButton);
                             QTimer::singleShot(400, &loop, &QEventLoop::quit);
                         });
                     });

    dialog = new HumanCheckDialog(&host);
    dialog->show();
    waitMs(400);

    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();

    service->clearHumanCheck();
    settings->setForumBaseUrl(previousUrl);
    qDebug() << "done";
    return 0;
}
