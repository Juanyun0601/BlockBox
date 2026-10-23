// 临时探针：用真实 Modrinth 描述（markdown→html）检查「详细信息」是否被裁切
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QWidget>
#include <QtMath>

#include "pages/ContentDetailPage.h"
#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

static QString longHtml()
{
    QString html = QStringLiteral("<h3>模组介绍</h3><p>这是一段简介文字，用来测试高度是否正常。</p>");
    for (int i = 0; i < 30; ++i) {
        html += QStringLiteral("<p>第 %1 段：Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                               "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>")
                    .arg(i + 1);
    }
    return html;
}

static QString markdownHtml()
{
    QFile f(QStringLiteral("appleskin.md"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return longHtml();
    const QString md = QString::fromUtf8(f.readAll());
    QTextDocument doc;
    doc.setMarkdown(md);
    return doc.toHtml();
}

static ModInfo makeInfo()
{
    ModInfo info;
    info.id = QStringLiteral("probe-mod");
    info.name = QStringLiteral("AppleSkin");
    info.englishName = QStringLiteral("AppleSkin");
    info.chineseName = QStringLiteral("AppleSkin");
    info.description = QStringLiteral("Food/hunger-related HUD improvements");
    info.detailedDescription = markdownHtml();
    info.author = QStringLiteral("squeek502");
    info.source = QStringLiteral("modrinth");
    info.modType = QStringLiteral("mod");
    info.categories = QStringList{QStringLiteral("food"), QStringLiteral("utility")};
    info.gameVersions = QStringList{QStringLiteral("1.21.11")};
    info.loaders = QStringList{QStringLiteral("fabric")};
    info.downloadCount = 98673970;
    info.followers = 18394;
    info.datePublished = QDateTime(QDate(2021, 11, 20), QTime(23, 37));
    info.dateModified = QDateTime(QDate(2026, 9, 17), QTime(23, 59));
    return info;
}

static void settle(QWidget *page)
{
    for (int i = 0; i < 40; ++i)
        QApplication::processEvents();
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
    for (int i = 0; i < 40; ++i)
        QApplication::processEvents();
}

static void dump(const char *tag, QWidget *page)
{
    QTextBrowser *b = page->findChild<QTextBrowser *>(QStringLiteral("modDetailDescBrowser"));
    QScrollArea *scroll = page->findChild<QScrollArea *>(QStringLiteral("modDetailScrollArea"));
    if (!b) {
        qDebug().noquote() << tag << "未找到描述浏览器";
        return;
    }
    const int docH = qCeil(b->document()->size().height());
    qDebug().noquote() << tag
                       << "控件高" << b->height()
                       << "文档高" << docH
                       << "视口高" << b->viewport()->height()
                       << "视口宽" << b->viewport()->width()
                       << "文档宽" << b->document()->textWidth()
                       << "内部滚动上限" << b->verticalScrollBar()->maximum()
                       << "卡片高" << (b->parentWidget() ? b->parentWidget()->height() : -1)
                       << "页面滚动上限" << (scroll ? scroll->verticalScrollBar()->maximum() : -1)
                       << "frameWidth" << b->frameWidth()
                       << "contentsRect高" << b->contentsRect().height()
                       << "margins" << b->contentsMargins().top() << b->contentsMargins().bottom()
                       << "docMargin" << b->document()->documentMargin()
                      ;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const ModInfo info = makeInfo();

    {
        ContentDetailPage page(ContentType::Mod);
        page.resize(1200, 800);
        page.setModInfo(info);
        page.show();
        settle(&page);
        dump("[Content 1200x800]", &page);
        page.grab().save(QStringLiteral("content_appleskin_1200.png"));

        page.resize(900, 700);
        settle(&page);
        dump("[Content 900x700]", &page);
        page.grab().save(QStringLiteral("content_appleskin_900.png"));
    }

    return 0;
}


