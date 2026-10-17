#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QFile>
#include <QRegularExpression>
#include <cstdio>

static void testCut(int lineCount)
{
    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) return;
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@[A-Za-z0-9_]+@")), QStringLiteral("#888888"));
    QStringList lines = qss.split(QLatin1Char('\n'));
    if (lineCount > 0 && lineCount < lines.size())
        lines = lines.mid(0, lineCount);
    qApp->setStyleSheet(lines.join(QLatin1Char('\n')));

    struct T { const char *on; const char *cls; };
    const T tests[] = {
        {"titleLabel", "L"},
        {"parentNavTextLabel", "L"},
        {"aiChatStatsLabel", "L"},
        {"statusLabel", "L"},
        {"parentNavButton", "B"},
        {"bottomActionBtn", "B"},
        {"filterLabel", "L"},
    };
    printf("cut=%-6d", lineCount);
    for (const T &t : tests) {
        if (QString(t.cls) == "L") {
            QLabel *l = new QLabel("x");
            l->setObjectName(t.on);
            l->ensurePolished();
            printf(" %s=%d", t.on, l->font().pixelSize());
            delete l;
        } else {
            QPushButton *b = new QPushButton("x");
            b->setObjectName(t.on);
            b->ensurePolished();
            printf(" %s=%d", t.on, b->font().pixelSize());
            delete b;
        }
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testCut(600);
    testCut(3300);
    testCut(10161);
    testCut(0);
    return 0;
}
