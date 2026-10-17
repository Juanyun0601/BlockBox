#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QWidget>
#include <QFile>
#include <QRegularExpression>
#include <QPixmap>
#include <QSize>
#include <cstdio>

static QPushButton *makeBottomBtn(const QString &text, bool icon, const QString &obj)
{
    QPushButton *b = new QPushButton(text);
    b->setObjectName(obj);
    if (icon) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(16, 16));
    }
    return b;
}

// variant: 0 = allSel, 1 = noSel ; policy for stats
static void bar(int state, QSizePolicy::Policy pol, const char *tag)
{
    QWidget *w = new QWidget;
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(4,4,4,4);
    l->setSpacing(4);
    QString all = state == 0 ? QStringLiteral("取消全选") : QStringLiteral("全选");
    l->addWidget(makeBottomBtn(all, false, "bottomActionBtn"));
    l->addWidget(makeBottomBtn(QStringLiteral("打开文件夹"), true, "bottomActionBtn"));
    if (state == 0) {
        l->addWidget(makeBottomBtn(QStringLiteral("详情"), true, "bottomActionBtn"));
    } else {
        l->addWidget(makeBottomBtn(QStringLiteral("粘贴"), true, "bottomActionBtn"));
    }
    QLabel *stats = new QLabel(QStringLiteral("共 123 个模组 (120 启用, 3 禁用)"));
    stats->setObjectName("statusLabel");
    stats->setSizePolicy(pol, QSizePolicy::Preferred);
    l->addWidget(stats);
    l->addStretch();
    printf("%-28s sizeHint=%3d minHint=%3d\n", tag, w->sizeHint().width(), w->minimumSizeHint().width());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) return 1;
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@([A-Z0-9_.]+)@")), QStringLiteral("#888888"));
    app.setStyleSheet(qss);

    bar(1, QSizePolicy::Minimum,   "noSel stats=Minimum ");
    bar(1, QSizePolicy::Preferred, "noSel stats=Preferred");
    bar(1, QSizePolicy::Ignored,   "noSel stats=Ignored ");
    bar(1, QSizePolicy::Expanding, "noSel stats=Expand  ");
    bar(0, QSizePolicy::Minimum,   "allSel stats=Minimum ");
    bar(0, QSizePolicy::Preferred, "allSel stats=Preferred");
    bar(0, QSizePolicy::Ignored,   "allSel stats=Ignored ");
    return 0;
}
