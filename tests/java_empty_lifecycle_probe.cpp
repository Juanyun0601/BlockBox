/**
 * @file   java_empty_lifecycle_probe.cpp
 * @brief  验证空状态标签生命周期的三个关键 Qt 行为：
 *   1. clear() 会销毁已安装的 index widget 吗？销毁前是否隐藏？
 *   2. 列表已显示时，新建标签经 setItemWidget 安装会自动 show 吗？
 *   3. 非空刷新后（标签从未安装），它是否浮在列表上？
 */
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QTimer>
#include <cstdio>

static QLabel *installEmpty(QListWidget &w)
{
    auto *item = new QListWidgetItem;
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    w.addItem(item);
    auto *lab = new QLabel(QStringLiteral("尚未发现任何 Java 安装。"), &w);
    lab->setObjectName(QStringLiteral("javaEmptyLabel"));
    w.setItemWidget(item, lab);
    item->setSizeHint(lab->sizeHint().expandedTo(QSize(0, 80)));
    return lab;
}

static void addRow(QListWidget &w, int i)
{
    auto *item = new QListWidgetItem;
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    w.addItem(item);
    auto *row = new QLabel(QStringLiteral("JDK 17 row %1").arg(i));
    w.setItemWidget(item, row);
    item->setSizeHint(QSize(0, 60));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QListWidget w;
    w.resize(420, 260);
    w.show();
    app.processEvents();

    // 场景1：空状态已显示 → clear 加载 Java
    auto *lab1 = installEmpty(w);
    app.processEvents();
    std::printf("[1] installed&shown: visible=%d\n", lab1->isVisible());
    QPointer<QLabel> p1(lab1);
    w.clear();
    std::printf("[1] after clear() immediate: isNull=%d", p1.isNull());
    if (p1) std::printf(" visible=%d", p1->isVisible());
    std::printf("\n");
    addRow(w, 0);
    app.processEvents();
    std::printf("[1] after row+events: isNull=%d", p1.isNull());
    if (p1) std::printf(" visible=%d", p1->isVisible());
    std::printf("\n");

    // 场景2：列表已显示且非空 → 新建标签 setItemWidget 是否自动可见
    w.clear();
    app.processEvents();
    addRow(w, 1);
    app.processEvents();
    auto *lab2 = installEmpty(w); // 模拟空分支在已显示列表上新建
    app.processEvents();
    std::printf("[2] fresh label on visible list: visible=%d geom=%dx%d\n",
                lab2->isVisible(), lab2->width(), lab2->height());

    // 场景3：从未安装的标签（非空刷新遗留）是否浮层
    w.clear();
    app.processEvents();
    auto *floatLab = new QLabel(QStringLiteral("尚未发现遗留浮层"), &w);
    floatLab->setObjectName(QStringLiteral("javaEmptyLabel"));
    addRow(w, 2);
    app.processEvents();
    std::printf("[3] never-installed leftover: visible=%d geom=(%d,%d %dx%d)\n",
                floatLab->isVisible(), floatLab->x(), floatLab->y(),
                floatLab->width(), floatLab->height());

    return 0;
}
