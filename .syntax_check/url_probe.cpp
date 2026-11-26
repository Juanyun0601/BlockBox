#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString body = QStringLiteral("问题描述：\n测试\n\n方块盒子版本：beta10\n系统：Windows 11");
    body.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QUrl url(QStringLiteral("mailto:") + QStringLiteral("support@mcblockbox.top"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("subject"), QStringLiteral("方块盒子问题反馈（beta10）"));
    q.addQueryItem(QStringLiteral("body"), body);
    url.setQuery(q);
    printf("%s\n", qPrintable(url.toString(QUrl::FullyEncoded)));
    return 0;
}
