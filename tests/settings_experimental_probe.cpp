/**
 * @file   settings_experimental_probe.cpp
 * @brief  实验性内容开关探针：验证「设置 > 高级设置 > 实验性内容 > 基岩版」卡片
 *         与顶栏版本切换入口的联动
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow：
 *   1. 确认默认关闭时顶栏不显示 Java 版/基岩版 切换入口；
 *   2. 进入 设置 > 高级设置，确认「实验性内容」卡片与「基岩版」开关存在并截图；
 *   3. 通过真实开关控件开启，确认设置已持久化、顶栏出现切换入口；
 *   4. 再次关闭，确认设置回退、顶栏恢复原状。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "components/TopBar.h"
#include "pages/SettingsPage.h"
#include "utils/SettingsManager.h"

#include <cstdio>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget) return false;
    const QPixmap pm = widget->grab();
    const bool ok = pm.save(path);
    qInfo().noquote() << "[shot]" << path << pm.width() << "x" << pm.height() << (ok ? "ok" : "FAILED");
    return ok;
}

int visibleChildCount(QWidget *w)
{
    int n = 0;
    if (!w) return n;
    for (QObject *child : w->children()) {
        if (auto *cw = qobject_cast<QWidget *>(child); cw && cw->isVisible()) ++n;
    }
    return n;
}

// 「基岩版」开关位于「实验性内容」卡片内，按卡片标题定位
CollapsibleSectionCard *findExperimentalCard(QWidget *page, QString *cardTitles = nullptr)
{
    if (!page) return nullptr;
    CollapsibleSectionCard *found = nullptr;
    for (auto *card : page->findChildren<CollapsibleSectionCard *>()) {
        if (cardTitles) cardTitles->append(card->title());
        if (card->title().contains(QStringLiteral("实验性")))
            found = card;
    }
    return found;
}

CustomCheckBox *bedrockToggleIn(CollapsibleSectionCard *card)
{
    if (!card) return nullptr;
    const auto toggles = card->findChildren<CustomCheckBox *>();
    return toggles.isEmpty() ? nullptr : toggles.first();
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    SettingsManager *settings = SettingsManager::instance();
    settings->setExperimentalBedrockEnabled(false);

    MainWindow w;
    w.resize(1200, 800);
    w.show();
    QApplication::processEvents();

    TopBar *topBar = w.findChild<TopBar *>();
    const QString dir = QDir::currentPath();
    qInfo() << "startup experimental enabled =" << settings->isExperimentalBedrockEnabled()
            << " topbar visible children =" << visibleChildCount(topBar);

    // 1.2s 后走真实导航
    QTimer::singleShot(1200, &app, [&w]() {
        QMetaObject::invokeMethod(&w, "onParentNavClicked", Qt::DirectConnection, Q_ARG(int, 2));
        QApplication::processEvents();
    });

    // 3.2s 后切到「高级设置」并截图
    QTimer::singleShot(3200, &app, [&app, &w, topBar, settings, dir]() {
        auto *settingsPage = w.findChild<SettingsPage *>();
        auto *stack = settingsPage
                          ? settingsPage->findChild<QStackedWidget *>(QStringLiteral("settingsContentStack"))
                          : nullptr;
        if (!stack) {
            qCritical() << "PROBE-FAIL: settingsContentStack not found";
            app.exit(1);
            return;
        }
        stack->setCurrentIndex(5); // 高级设置
        QApplication::processEvents();

        QWidget *advancedPage = stack->currentWidget();
        qInfo() << "current settings page =" << (advancedPage ? advancedPage->objectName() : QStringLiteral("<null>"));

        QString cardTitles;
        CollapsibleSectionCard *card = findExperimentalCard(advancedPage, &cardTitles);
        CustomCheckBox *bedrockToggle = bedrockToggleIn(card);
        qInfo().noquote() << "cards on advanced page:" << cardTitles;
        if (!bedrockToggle) {
            qCritical() << "PROBE-FAIL: 基岩版 toggle not found";
            savePng(&w, dir + QStringLiteral("/experimental_page_off.png"));
            app.exit(2);
            return;
        }
        qInfo() << "基岩版 toggle checked at startup =" << bedrockToggle->isChecked() << " (expected false)";

        savePng(&w, dir + QStringLiteral("/experimental_page_off.png"));
        savePng(card, dir + QStringLiteral("/experimental_card_off.png"));
        savePng(topBar, dir + QStringLiteral("/experimental_topbar_off.png"));

        // 模拟用户打开开关
        bedrockToggle->setChecked(true);
        QApplication::processEvents();
        qInfo() << "after toggling ON -> settings.isExperimentalBedrockEnabled() ="
                << settings->isExperimentalBedrockEnabled()
                << " toggle checked =" << bedrockToggle->isChecked()
                << " topbar visible children =" << visibleChildCount(topBar);
        // 等开关滑块动画结束再截图
        QTimer::singleShot(700, &app, [&w, card, topBar, dir]() {
            savePng(&w, dir + QStringLiteral("/experimental_page_on.png"));
            savePng(card, dir + QStringLiteral("/experimental_card_on.png"));
            savePng(topBar, dir + QStringLiteral("/experimental_topbar_on.png"));
        });
    });

    // 5.0s 后再关闭开关并收尾
    QTimer::singleShot(5000, &app, [&app, &w, topBar, settings, dir]() {
        auto *settingsPage = w.findChild<SettingsPage *>();
        auto *stack = settingsPage
                          ? settingsPage->findChild<QStackedWidget *>(QStringLiteral("settingsContentStack"))
                          : nullptr;
        CustomCheckBox *bedrockToggle = bedrockToggleIn(findExperimentalCard(stack ? stack->currentWidget() : nullptr));
        if (bedrockToggle) {
            bedrockToggle->setChecked(false);
            QApplication::processEvents();
            qInfo() << "after toggling OFF -> settings.isExperimentalBedrockEnabled() ="
                    << settings->isExperimentalBedrockEnabled()
                    << " toggle checked =" << bedrockToggle->isChecked()
                    << " topbar visible children =" << visibleChildCount(topBar);
        } else {
            settings->setExperimentalBedrockEnabled(false);
        }
        savePng(topBar, dir + QStringLiteral("/experimental_topbar_off2.png"));

        QFile f1(dir + QStringLiteral("/experimental_topbar_off.png"));
        QFile f2(dir + QStringLiteral("/experimental_topbar_on.png"));
        QFile f3(dir + QStringLiteral("/experimental_topbar_off2.png"));
        if (f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly) && f3.open(QIODevice::ReadOnly)) {
            const QByteArray a = f1.readAll(), b = f2.readAll(), c = f3.readAll();
            qInfo() << "topbar off bytes =" << a.size() << " on bytes =" << b.size()
                    << " off2 bytes =" << c.size() << " off!=on =" << (a != b) << " off2!=on =" << (c != b);
        }

        qInfo() << "EXPERIMENTAL-PROBE-DONE";
        app.quit();
    });

    return app.exec();
}

