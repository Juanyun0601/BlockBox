/**
 * @file   human_check_probe.cpp
 * @brief  滑块人机验证弹窗的离屏探针：连本地论坛，截图 + 自动拖一次滑块
 *
 * 用法：human_check_probe.exe <论坛地址> <截图目录>
 * 退出码：0 通过 / 3 答错 / 4 超时 / 2 其它
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

QImage imageFromField(const QJsonValue &value)
{
    QString text = value.toString();
    const int comma = text.indexOf(QLatin1Char(','));
    if (text.startsWith(QStringLiteral("data:")) && comma > 0)
        text = text.mid(comma + 1);
    QImage image;
    image.loadFromData(QByteArray::fromBase64(text.toLatin1()), "PNG");
    return image;
}

/// 跟 scripts/smoke.js 的 solvePuzzle 同一套思路：拿拼图去比对背景里被压暗的缺口
int solvePuzzle(const QJsonObject &challenge, double *scoreOut)
{
    const QImage bg = imageFromField(challenge.value(QStringLiteral("bg")))
                          .convertToFormat(QImage::Format_RGB32);
    const QImage piece = imageFromField(challenge.value(QStringLiteral("piece_img")))
                             .convertToFormat(QImage::Format_ARGB32);
    const QJsonObject pieceInfo = challenge.value(QStringLiteral("piece")).toObject();
    const int pad = pieceInfo.value(QStringLiteral("pad")).toInt();
    const int size = pieceInfo.value(QStringLiteral("size")).toInt();
    const int span = pieceInfo.value(QStringLiteral("w")).toInt();
    const int width = challenge.value(QStringLiteral("width")).toInt();
    const int height = challenge.value(QStringLiteral("height")).toInt();
    const int holeY = pieceInfo.value(QStringLiteral("top")).toInt() + pad;

    if (bg.isNull() || piece.isNull() || span <= 0 || size <= 0)
        return -1;

    const auto opaque = [&](int u, int v) {
        if (u < 0 || v < 0 || u >= span || v >= span)
            return false;
        return qAlpha(piece.pixel(u, v)) > 200;
    };

    QVector<QPoint> inner;
    for (int v = 2; v < span - 2; v += 2) {
        for (int u = 2; u < span - 2; u += 2) {
            bool edge = false;
            for (int dy = -2; dy <= 2 && !edge; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (!opaque(u + dx, v + dy)) {
                        edge = true;
                        break;
                    }
                }
            }
            if (!edge)
                inner.append(QPoint(u, v));
        }
    }

    static const double kChannelScale[3] = {0.45, 0.46, 0.50};
    static const double kChannelBias[3] = {4.0, 7.0, 16.0};

    int bestX = -1;
    double bestScore = 1e30;
    for (int x = 0; x <= width - size; ++x) {
        double score = 0.0;
        int count = 0;
        for (const QPoint &p : inner) {
            const int px = x + p.x() - pad;
            const int py = holeY + p.y() - pad;
            if (px < 0 || py < 0 || px >= width || py >= height)
                continue;
            const QRgb piecePixel = piece.pixel(p.x(), p.y());
            const QRgb bgPixel = bg.pixel(px, py);
            const int pieceChannels[3] = {qRed(piecePixel), qGreen(piecePixel), qBlue(piecePixel)};
            const int bgChannels[3] = {qRed(bgPixel), qGreen(bgPixel), qBlue(bgPixel)};
            for (int c = 0; c < 3; ++c) {
                const double expect = pieceChannels[c] * kChannelScale[c] + kChannelBias[c];
                const double diff = bgChannels[c] - expect;
                score += diff * diff;
            }
            ++count;
        }
        if (count < 50)
            continue;
        const double avg = score / count;
        if (avg < bestScore) {
            bestScore = avg;
            bestX = x;
        }
    }

    if (scoreOut)
        *scoreOut = bestScore;
    return bestX;
}

QWidget *findSlideBar(QWidget *dialog)
{
    const QList<QWidget *> children = dialog->findChildren<QWidget *>();
    for (QWidget *w : children) {
        if (w->width() == kBarWidth && w->height() == kSlideHeight && w->isVisible())
            return w;
    }
    return nullptr;
}

void dragSlide(QWidget *bar, int answer, int pad, int maxAnswer)
{
    const int value = qBound(0, answer - pad, maxAnswer);
    const double progress = maxAnswer <= 0 ? 0.0 : double(value) / double(maxAnswer);
    const double maxHandleX = qMax<double>(kHandleMargin, kBarWidth - kHandleWidth - kHandleMargin);
    const double targetHandleX = kHandleMargin + progress * (maxHandleX - kHandleMargin);
    const double grabOffset = kHandleWidth / 2.0;
    const int startX = int(kHandleMargin + grabOffset);
    const int endX = int(targetHandleX + grabOffset);
    const int y = kSlideHeight / 2;

    const auto send = [&](QEvent::Type type, int x, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(x, y), bar->mapToGlobal(QPointF(x, y)),
                          Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(bar, &event);
    };

    send(QEvent::MouseButtonPress, startX, Qt::LeftButton);
    waitMs(120);
    const int steps = 6;
    for (int i = 1; i <= steps; ++i) {
        const int x = startX + (endX - startX) * i / steps;
        send(QEvent::MouseMove, x, Qt::LeftButton);
        waitMs(40);
    }
    waitMs(400);
    send(QEvent::MouseButtonRelease, endX, Qt::NoButton);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString baseUrl = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("http://127.0.0.1:3211");
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                    : QStringLiteral("D:/BlockBox/.tmp-shots");
    // 第三个参数：故意拖偏多少像素（用来截「没对准」的状态图）
    const int offset = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 0;
    // 第四个参数：dark 时切成深色主题再截图
    const bool wantDark = argc > 4 && QString::fromLocal8Bit(argv[4]) == QLatin1String("dark");
    QDir().mkpath(outDir);

    SettingsManager *settings = SettingsManager::instance();
    const QString previousUrl = settings->forumBaseUrl();
    settings->setForumBaseUrl(baseUrl);

    if (wantDark)
        ThemeManager::setTheme(&app, ThemeManager::DarkTheme);

    ForumAccountService *service = ForumAccountService::instance();
    service->clearHumanCheck();

    QWidget host;
    host.resize(1000, 720);
    host.show();
    waitMs(120);

    int exitCode = 2;
    QEventLoop loop;
    HumanCheckDialog *dialog = nullptr;

    // 先接信号再建弹窗：弹窗一构造就会去拉题，晚了就收不到
    QObject::connect(service, &ForumAccountService::humanChallengeReady, &app,
                     [&](const QJsonObject &challenge) {
                         if (!dialog)
                             return;
                         qDebug() << "challenge" << challenge.value(QStringLiteral("id")).toString()
                                  << "w" << challenge.value(QStringLiteral("width")).toInt()
                                  << "h" << challenge.value(QStringLiteral("height")).toInt();
                         QTimer::singleShot(200, &loop, [&, challenge] {
                             dialog->grab().save(outDir + QStringLiteral("/qt-captcha-1.png"));

                             double score = 0.0;
                             const int answer = solvePuzzle(challenge, &score);
                             const QJsonObject pieceInfo =
                                 challenge.value(QStringLiteral("piece")).toObject();
                             const int pad = pieceInfo.value(QStringLiteral("pad")).toInt();
                             const int span = pieceInfo.value(QStringLiteral("w")).toInt();
                             const int maxAnswer =
                                 challenge.value(QStringLiteral("width")).toInt() - span;
                             qDebug() << "solved x" << answer << "score" << score
                                      << "pad" << pad << "max" << maxAnswer;

                             // 把背景和缺口附近放大存一份，方便肉眼确认缺口看不看得见
                             const QImage bgImage = imageFromField(
                                 challenge.value(QStringLiteral("bg")));
                             bgImage.save(outDir + QStringLiteral("/qt-bg.png"));
                             const int holeTop =
                                 pieceInfo.value(QStringLiteral("top")).toInt();
                             const QRect crop(qMax(0, answer - 70), qMax(0, holeTop - 30),
                                              220, 200);
                             bgImage.copy(crop)
                                 .scaled(660, 600, Qt::KeepAspectRatio, Qt::FastTransformation)
                                 .save(outDir + QStringLiteral("/qt-bg-zoom.png"));

                             QWidget *bar = findSlideBar(dialog);
                             qDebug() << "slide bar" << bar;
                             if (!bar) {
                                 exitCode = 5;
                                 loop.quit();
                                 return;
                             }
                             dragSlide(bar, answer + offset, pad, maxAnswer);
                             QTimer::singleShot(500, &loop, [&] {
                                 dialog->grab().save(outDir + QStringLiteral("/qt-captcha-2.png"));
                             });
                         });
                     });

    QObject::connect(service, &ForumAccountService::humanCheckPassed, &app, [&] {
        qDebug() << "PASSED token" << service->humanCheckToken().left(16);
        if (dialog)
            dialog->grab().save(outDir + QStringLiteral("/qt-captcha-passed.png"));
        exitCode = 0;
        loop.quit();
    });

    QObject::connect(service, &ForumAccountService::humanCheckFailed, &app,
                     [&](const QString &message) {
                         qDebug() << "FAILED" << message;
                         if (dialog)
                             dialog->grab().save(outDir + QStringLiteral("/qt-captcha-failed.png"));
                         // 答错后界面会先提示、再换一张新图，隔一会儿再截两张
                         QTimer::singleShot(500, &app, [&] {
                             if (dialog)
                                 dialog->grab().save(outDir
                                                     + QStringLiteral("/qt-captcha-failed-500.png"));
                         });
                         QTimer::singleShot(1800, &app, [&] {
                             if (dialog)
                                 dialog->grab().save(outDir
                                                     + QStringLiteral("/qt-captcha-failed-1800.png"));
                             exitCode = 3;
                             loop.quit();
                         });
                     });

    dialog = new HumanCheckDialog(&host);
    dialog->show();
    waitMs(400);

    QTimer::singleShot(25000, &loop, [&] {
        qDebug() << "TIMEOUT";
        if (dialog)
            dialog->grab().save(outDir + QStringLiteral("/qt-captcha-timeout.png"));
        exitCode = 4;
        loop.quit();
    });

    loop.exec();

    service->clearHumanCheck();
    settings->setForumBaseUrl(previousUrl);
    qDebug() << "exit" << exitCode;
    return exitCode;
}

