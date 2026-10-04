/**
 * @file   java_empty_float_probe.cpp
 * @brief  复现：空状态标签建为 QListWidget 直接子级、列表非空时装作无事发生，
 *         该标签是否会浮在列表内容上方显示。
 */
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QListWidget w;
    w.setObjectName(QStringLiteral("javaListWidget"));
    w.resize(420, 220);

    // 与 SettingsJavaPage::initJavaManagerSettings 相同：标签建为 list 的直接子级
    auto *empty = new QLabel(QStringLiteral("尚未发现任何 Java 安装。"), &w);
    empty->setObjectName(QStringLiteral("javaEmptyLabel"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);

    // 列表非空（Java 已加载）→ refreshJavaList 只加行，从不隐藏标签
    for (int i = 0; i < 3; ++i) {
        auto *item = new QListWidgetItem;
        w.addItem(item);
        auto *row = new QLabel(QStringLiteral("JDK 17 row %1").arg(i));
        w.setItemWidget(item, row);
        item->setSizeHint(QSize(0, 60));
    }

    w.show();
    app.processEvents();

    std::printf("empty label: visible=%d geom=(%d,%d %dx%d) parent=%s\n",
                empty->isVisible(), empty->x(), empty->y(),
                empty->width(), empty->height(),
                empty->parent()->objectName().toUtf8().constData());

    w.grab().save(QStringLiteral("D:/BlockBox/_out/java_empty_float.png"));
    std::printf("saved\n");
    return 0;
}
