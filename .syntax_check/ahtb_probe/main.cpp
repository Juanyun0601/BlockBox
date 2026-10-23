// 临时探针：验证 AutoHeightTextBrowser 在不同内容/宽度下高度等于内容高度、无内部滚动条
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

#include "components/AutoHeightTextBrowser.h"

static QString longHtml()
{
    QString html = QStringLiteral("<h3>详细信息</h3><p>这是一段简介文字。</p>");
    for (int i = 0; i < 60; ++i) {
        html += QStringLiteral("<p>第 %1 段：Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                               "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>")
                    .arg(i + 1);
    }
    return html;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget page;
    page.resize(760, 420);
    QVBoxLayout *outer = new QVBoxLayout(&page);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *content = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(content);
    lay->addWidget(new QLabel(QStringLiteral("页面顶部标题")));

    AutoHeightTextBrowser *browser = new AutoHeightTextBrowser;
    browser->setHtml(longHtml());
    lay->addWidget(browser);
    lay->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
    page.show();

    for (int i = 0; i < 40; ++i)
        app.processEvents();

    const int docH = qCeil(browser->document()->size().height());
    qDebug() << "[初始] 控件高" << browser->height()
             << "文档高" << docH
             << "内部竖条可见" << browser->verticalScrollBar()->isVisible()
             << "页面滚动上限" << scroll->verticalScrollBar()->maximum();
    page.grab().save(QStringLiteral("ahtb_before.png"));

    // 变窄后应重新撑高，文本不能丢
    page.resize(420, 420);
    for (int i = 0; i < 40; ++i)
        app.processEvents();
    qDebug() << "[变窄] 控件高" << browser->height()
             << "文档高" << qCeil(browser->document()->size().height())
             << "内部竖条可见" << browser->verticalScrollBar()->isVisible();
    page.grab().save(QStringLiteral("ahtb_narrow.png"));

    // 换成短文本后高度应回落
    browser->setHtml(QStringLiteral("<p>短描述一行。</p>"));
    for (int i = 0; i < 40; ++i)
        app.processEvents();
    qDebug() << "[短文本] 控件高" << browser->height()
             << "文档高" << qCeil(browser->document()->size().height());
    page.grab().save(QStringLiteral("ahtb_short.png"));

    return 0;
}
