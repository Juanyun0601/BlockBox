#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QRegularExpression>
#include <QPixmap>
#include <QSize>
#include <QFontMetrics>
#include <cstdio>

static void info(const char *name, QWidget *w)
{
    w->ensurePolished();
    QFont f = w->font();
    printf("%-26s style=%-24s fontPx=%d sizeHint=%d minHint=%d\n", name,
           w->style()->metaObject()->className(), f.pixelSize(),
           w->sizeHint().width(), w->minimumSizeHint().width());
}

static void fm(const char *name, const QFont &f, const QString &text)
{
    QFontMetrics m(f);
    printf("FM %-30s px=%d width=%d\n", name, f.pixelSize(), m.horizontalAdvance(text));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) { printf("cannot open qss\n"); return 1; }
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@[A-Za-z0-9_]+@")), QStringLiteral("#888888"));
    app.setStyleSheet(qss);

    QComboBox *status = new QComboBox;
    status->setObjectName("filterCombo");
    status->setFixedHeight(28);
    status->setMinimumWidth(104);
    status->addItem("全部", 0);
    status->addItem("已启用", 1);
    status->addItem("已禁用", 2);
    status->ensurePolished();
    info("statusCombo", status);

    QComboBox *size = new QComboBox;
    size->setObjectName("filterCombo");
    size->setFixedHeight(28);
    size->setMinimumWidth(104);
    size->addItem("全部大小", 0);
    size->addItem("小于 10 MB", 1);
    size->addItem("10 ~ 100 MB", 2);
    size->addItem("大于 100 MB", 3);
    size->ensurePolished();
    info("sizeCombo", size);

    QLabel *l = new QLabel("共 12 个模组 (10 启用, 2 禁用)");
    l->setObjectName("statusLabel");
    info("statsLabel", l);
    fm("stats with label font", l->font(), QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));

    QPushButton *b = new QPushButton("全选");
    b->setObjectName("bottomActionBtn");
    info("btn 全选", b);

    QLabel *cl = new QLabel("分类:");
    cl->setObjectName("filterLabel");
    info("categoryLabel", cl);

    // Raw metrics under QSS global font (14px YaHei) and 12px
    QFont f14 = QApplication::font();
    printf("appFont px=%d family=%s\n", f14.pixelSize(), qPrintable(f14.family()));
    QFont f12 = f14; f12.setPixelSize(12);
    fm("stats@12", f12, QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));
    fm("stats3@12", f12, QStringLiteral("共 123 个模组 (120 启用, 3 禁用)"));
    fm("10 ~ 100 MB@12", f12, QStringLiteral("10 ~ 100 MB"));
    fm("已禁用@12", f12, QStringLiteral("已禁用"));
    fm("分类:@12", f12, QStringLiteral("分类:"));
    fm("管理分类@12", f12, QStringLiteral("管理分类"));
    fm("全选@12", f12, QStringLiteral("全选"));
    fm("打开文件夹@12", f12, QStringLiteral("打开文件夹"));
    fm("粘贴@12", f12, QStringLiteral("粘贴"));

    return 0;
}
