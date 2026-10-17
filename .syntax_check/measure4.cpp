#include <QApplication>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cstdio>

static int testAt(int lineCount)
{
    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) return -1;
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@[A-Za-z0-9_]+@")), QStringLiteral("#888888"));
    QStringList lines = qss.split(QLatin1Char('\n'));
    if (lineCount > 0 && lineCount < lines.size())
        lines = lines.mid(0, lineCount);
    // count braces of prefix
    QString prefix = lines.join(QLatin1Char('\n'));
    int open = prefix.count(QLatin1Char('{')), close = prefix.count(QLatin1Char('}'));
    qApp->setStyleSheet(prefix);
    QLabel *l = new QLabel(QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));
    l->setObjectName("statusLabel");
    l->ensurePolished();
    int r = l->font().pixelSize();
    printf("cut=%d braces{=%d }=%d -> fontPx=%d\n", lineCount, open, close, r);
    delete l;
    return r;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testAt(10161);   // right after statusLabel rule
    testAt(5000);
    testAt(1500);
    testAt(70);      // just after QLabel global rule
    testAt(0);       // full
    return 0;
}
