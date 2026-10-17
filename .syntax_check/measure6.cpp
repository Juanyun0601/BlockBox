#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QRegularExpression>
#include <QPixmap>
#include <QSize>
#include <cstdio>

static void dump(const char *name, QWidget *w)
{
    w->ensurePolished();
    printf("%-30s fontPx=%2d sizeHint=%3d minHint=%3d\n", name, w->font().pixelSize(),
           w->sizeHint().width(), w->minimumSizeHint().width());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QFile f("styles/style.qss");
    if (!f.open(QFile::ReadOnly)) { printf("cannot open qss\n"); return 1; }
    QString qss = QString::fromUtf8(f.readAll());
    // same token regex as ThemeManager
    qss.replace(QRegularExpression(QStringLiteral("@([A-Z0-9_.]+)@")), QStringLiteral("#888888"));
    app.setStyleSheet(qss);

    auto makeCombo = [](const char *on, const QList<QPair<QString, int>> &items, int minWidth) {
        QComboBox *c = new QComboBox;
        c->setObjectName(QString::fromUtf8(on));
        c->setFixedHeight(28);
        if (minWidth > 0) c->setMinimumWidth(minWidth);
        for (auto &it : items) c->addItem(it.first, it.second);
        return c;
    };

    // filter combos WITHOUT the 104 min (content-driven width)
    QComboBox *status = makeCombo("filterCombo", {{"全部",0},{"已启用",1},{"已禁用",2}}, 0);
    dump("status (content)", status);
    QComboBox *time = makeCombo("filterCombo", {{"全部时间",0},{"最近 7 天",1},{"最近 30 天",2},{"最近一年",3}}, 0);
    dump("time (content)", time);
    QComboBox *size = makeCombo("filterCombo", {{"全部大小",0},{"小于 10 MB",1},{"10 ~ 100 MB",2},{"大于 100 MB",3}}, 0);
    dump("size (content)", size);
    QComboBox *sort = makeCombo("searchSortCombo", {{"名称 A→Z",0},{"名称 Z→A",1},{"大小 大→小",2},{"大小 小→大",3},{"时间 新→旧",4},{"时间 旧→新",5}}, 0);
    dump("sort (content)", sort);

    // with current 104 min
    QComboBox *status2 = makeCombo("filterCombo", {{"全部",0},{"已启用",1},{"已禁用",2}}, 104);
    dump("status (min104)", status2);

    // category row pieces
    QComboBox *cat = new QComboBox;
    cat->setObjectName("categoryCombo");
    cat->setFixedHeight(28);
    cat->setMinimumWidth(130);
    cat->setMinimumContentsLength(5);
    cat->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cat->addItem("全部分类", 0); cat->addItem("辅助", 1);
    dump("categoryCombo", cat);

    QLabel *catLabel = new QLabel(QStringLiteral("分类:"));
    catLabel->setObjectName("filterLabel");
    dump("categoryLabel 分类:", catLabel);

    QPushButton *mg = new QPushButton(QStringLiteral("管理分类"));
    mg->setObjectName("categoryManageBtn");
    mg->setFixedHeight(28);
    dump("categoryManageBtn", mg);

    // bottom action buttons
    auto makeBottomBtn = [](const QString &text, bool icon) {
        QPushButton *b = new QPushButton(text);
        b->setObjectName("bottomActionBtn");
        if (icon) {
            QPixmap pm(16, 16);
            pm.fill(Qt::transparent);
            b->setIcon(QIcon(pm));
            b->setIconSize(QSize(16, 16));
        }
        return b;
    };
    dump("btn 全选", makeBottomBtn(QStringLiteral("全选"), false));
    dump("btn 打开文件夹+icon", makeBottomBtn(QStringLiteral("打开文件夹"), true));
    dump("btn 粘贴+icon", makeBottomBtn(QStringLiteral("粘贴"), true));
    dump("btn 详情(hidden anyway)", makeBottomBtn(QStringLiteral("详情"), true));

    // stats texts at statusLabel 12px
    auto statLabel = [](const QString &t) {
        QLabel *l = new QLabel(t);
        l->setObjectName("statusLabel");
        return l;
    };
    dump("stats 典型", statLabel(QStringLiteral("共 12 个模组 (10 启用, 2 禁用)")));
    dump("stats 3位", statLabel(QStringLiteral("共 123 个模组 (120 启用, 3 禁用)")));
    dump("stats 显示式", statLabel(QStringLiteral("显示 12 个模组 (共 45 个)")));
    dump("stats 扫描失败", statLabel(QStringLiteral("扫描失败: %1").arg(QStringLiteral("Access is denied"))));
    dump("stats 目录不存在", statLabel(QStringLiteral("存档目录不存在")));

    // search edit (min ~100 planned)
    QLineEdit *se = new QLineEdit;
    se->setObjectName("modSearchEdit");
    se->setFixedHeight(32);
    se->setMinimumWidth(100);
    dump("searchEdit(min100)", se);

    // tab buttons (fallback option)
    auto tabBtn = [](const QString &t) {
        QPushButton *b = new QPushButton(t);
        b->setObjectName("resourcesTabBtn");
        return b;
    };
    dump("tab 模组", tabBtn(QStringLiteral("模组")));
    dump("tab 存档", tabBtn(QStringLiteral("存档")));
    dump("tab 截图", tabBtn(QStringLiteral("截图")));
    dump("tab 资源包", tabBtn(QStringLiteral("资源包")));
    dump("tab 光影包", tabBtn(QStringLiteral("光影包")));

    return 0;
}
