#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QWidget>
#include <QFile>
#include <QRegularExpression>
#include <QPixmap>
#include <QSize>
#include <cstdio>

static void dump(const char *name, QWidget *w)
{
    printf("%-34s sizeHint=%3d minHint=%3d\n", name,
           w->sizeHint().width(), w->minimumSizeHint().width());
}

static QComboBox *makeCombo(const char *on, const QList<QPair<QString, int>> &items)
{
    QComboBox *c = new QComboBox;
    c->setObjectName(QString::fromUtf8(on));
    c->setFixedHeight(28);
    for (auto &it : items) c->addItem(it.first, it.second);
    return c;
}

static QPushButton *makeBottomBtn(const QString &text, bool icon)
{
    QPushButton *b = new QPushButton(text);
    b->setObjectName("bottomActionBtn");
    if (icon) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(16, 16));
    }
    return b;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) { printf("cannot open qss\n"); return 1; }
    QString qss = QString::fromUtf8(f.readAll());
    qss.replace(QRegularExpression(QStringLiteral("@([A-Z0-9_.]+)@")), QStringLiteral("#888888"));
    app.setStyleSheet(qss);

    // combo policies
    QComboBox *pc = makeCombo("filterCombo", {{"全部",0},{"已启用",1}});
    printf("combo hPolicy=%d (Expanding=%d Fixed=%d Minimum=%d)\n",
           pc->sizePolicy().horizontalPolicy(),
           (int)QSizePolicy::Expanding, (int)QSizePolicy::Fixed, (int)QSizePolicy::Minimum);

    // ===== Instance row1: search(min100) + 分类label + catCombo(min130) + manageBtn, spacing 6 =====
    {
        QWidget *w = new QWidget;
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        l->setSpacing(6);
        QLineEdit *se = new QLineEdit; se->setFixedHeight(32); se->setMinimumWidth(100);
        l->addWidget(se, 1);
        QLabel *cl = new QLabel(QStringLiteral("分类:")); cl->setObjectName("filterLabel");
        l->addWidget(cl);
        QComboBox *cat = new QComboBox;
        cat->setObjectName("categoryCombo");
        cat->setFixedHeight(28);
        cat->setMinimumWidth(130);
        cat->setMinimumContentsLength(5);
        cat->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cat->addItem("全部分类",0); cat->addItem("辅助",1);
        l->addWidget(cat);
        QPushButton *mg = new QPushButton(QStringLiteral("管理分类"));
        mg->setObjectName("categoryManageBtn"); mg->setFixedHeight(28);
        l->addWidget(mg);
        dump("Instance row1 (lim 406)", w);
    }

    // ===== Instance row2: 4 combos, spacing 6 and 4 =====
    for (int sp : {6, 4}) {
        QWidget *w = new QWidget;
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        l->setSpacing(sp);
        l->addWidget(makeCombo("filterCombo", {{"全部",0},{"已启用",1},{"已禁用",2}}));
        l->addWidget(makeCombo("filterCombo", {{"全部时间",0},{"最近 7 天",1},{"最近 30 天",2},{"最近一年",3}}));
        l->addWidget(makeCombo("filterCombo", {{"全部大小",0},{"小于 10 MB",1},{"10 ~ 100 MB",2},{"大于 100 MB",3}}));
        l->addWidget(makeCombo("searchSortCombo", {{"名称 A→Z",0},{"名称 Z→A",1},{"大小 大→小",2},{"大小 小→大",3},{"时间 新→旧",4},{"时间 旧→新",5}}));
        char nm[64];
        snprintf(nm, sizeof(nm), "combo row spacing=%d (lim 406)", sp);
        dump(nm, w);
    }

    // ===== Instance bottom bar: spacing 4 / 6, stats Minimum policy =====
    for (int sp : {4, 6}) {
        for (int variant = 0; variant < 3; ++variant) {
            QWidget *bar = new QWidget;
            bar->setFixedHeight(42);
            QHBoxLayout *l = new QHBoxLayout(bar);
            l->setContentsMargins(4,4,4,4);
            l->setSpacing(sp);
            l->addWidget(makeBottomBtn(QStringLiteral("全选"), false));
            l->addWidget(makeBottomBtn(QStringLiteral("打开文件夹"), true));
            l->addWidget(makeBottomBtn(QStringLiteral("粘贴"), true));
            QPushButton *detail = makeBottomBtn(QStringLiteral("详情"), true);
            detail->hide();
            l->addWidget(detail);
            QLabel *stats = new QLabel;
            stats->setObjectName("statusLabel");
            stats->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
            if (variant == 0) stats->setText(QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));
            else if (variant == 1) stats->setText(QStringLiteral("共 123 个模组 (120 启用, 3 禁用)"));
            else stats->setText(QStringLiteral("显示 999 个模组 (共 1234 个)"));
            l->addWidget(stats);
            l->addStretch();
            char nm[80];
            snprintf(nm, sizeof(nm), "bottombar sp=%d stats=%d (lim 434)", sp, variant);
            dump(nm, bar);
        }
    }

    // ===== Bedrock row2: same 4 combos margins 14 spacing 6 (lim 450 total, 422 inner) =====
    {
        QWidget *w = new QWidget;
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(14,4,14,4);
        l->setSpacing(6);
        l->addWidget(makeCombo("filterCombo", {{"全部",0},{"已启用",1},{"已禁用",2}}));
        l->addWidget(makeCombo("filterCombo", {{"全部时间",0},{"最近 7 天",1},{"最近 30 天",2},{"最近一年",3}}));
        l->addWidget(makeCombo("filterCombo", {{"全部大小",0},{"小于 10 MB",1},{"10 ~ 100 MB",2},{"大于 100 MB",3}}));
        l->addWidget(makeCombo("searchSortCombo", {{"名称 A→Z",0},{"名称 Z→A",1},{"大小 大→小",2},{"大小 小→大",3},{"时间 新→旧",4},{"时间 旧→新",5}}));
        dump("Bedrock row2 (lim 450)", w);
    }

    // ===== Tab bar fallback check: 5 tabs + stats + stretch, spacing 4 =====
    {
        QWidget *w = new QWidget;
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0,4,0,6);
        l->setSpacing(4);
        const char *tabs[] = {"模组","存档","截图","资源包","光影包"};
        for (const char *t : tabs) {
            QPushButton *b = new QPushButton(t);
            b->setObjectName("resourcesTabBtn");
            l->addWidget(b);
        }
        QLabel *stats = new QLabel(QStringLiteral("共 12 个模组 (10 启用, 2 禁用)"));
        stats->setObjectName("statusLabel");
        stats->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        l->addWidget(stats);
        l->addStretch();
        dump("tabbar+stats (lim 434)", w);
    }

    return 0;
}
