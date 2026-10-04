/**
 * @file   stacked_show_event_probe.cpp
 * @brief  验证 QStackedWidget 切页时，页面内的子控件是否收到 Show/Hide 事件。
 *         决定 SettingsPage 能否用 showEvent 作为“回到设置页即刷新”的触发器。
 */
#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cstdio>

class EventLogger : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void showEvent(QShowEvent *e) override
    {
        std::printf("  [%s] ShowEvent\n", objectName().toUtf8().constData());
        QLabel::showEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        std::printf("  [%s] HideEvent\n", objectName().toUtf8().constData());
        QLabel::hideEvent(e);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QStackedWidget stack;
    stack.resize(300, 200);

    // 页面1：scrollArea 包着子控件（模拟 SettingsPage 结构）
    auto *scroll = new QScrollArea;
    auto *page1Content = new EventLogger(QStringLiteral("settings-content"));
    page1Content->setObjectName(QStringLiteral("settings-content"));
    scroll->setWidget(page1Content);
    scroll->setWidgetResizable(true);
    stack.addWidget(scroll);

    // 页面2：普通页
    auto *page2 = new EventLogger(QStringLiteral("download-page"));
    page2->setObjectName(QStringLiteral("download-page"));
    stack.addWidget(page2);

    stack.show();
    app.processEvents();
    std::printf("initial show:\n");

    std::printf("switch to page2 (hide page1's scrollArea):\n");
    stack.setCurrentIndex(1);
    app.processEvents();

    std::printf("switch back to page1:\n");
    stack.setCurrentIndex(0);
    app.processEvents();

    std::printf("done\n");
    return 0;
}
