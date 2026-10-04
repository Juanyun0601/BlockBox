/**
 * @file   java_empty_state_probe.cpp
 * @brief  实测 QListWidget::clear() 对 setItemWidget 部件的处理：
 *         是否销毁？是否隐藏？几何是否残留？
 */
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QListWidget w;
    w.resize(400, 200);

    // 第一轮：空状态，标签作为 item widget 安装
    auto *item = new QListWidgetItem;
    w.addItem(item);
    auto *lab = new QLabel(QStringLiteral("EMPTY STATE"));
    lab->setObjectName(QStringLiteral("javaEmptyLabel"));
    w.setItemWidget(item, lab);
    QPointer<QLabel> p(lab);

    std::printf("installed: visible=%d geom=%dx%d parent=%s\n",
                lab->isVisible(), lab->width(), lab->height(),
                lab->parent() ? lab->parent()->objectName().toUtf8().constData() : "null");

    // 第二轮：加载了 Java，clear 后添加真实 item
    w.clear();
    std::printf("after clear() immediate: isNull=%d\n", p.isNull());
    if (p) {
        std::printf("  still alive: visible=%d geom=%dx%d parent=%s\n",
                    p->isVisible(), p->width(), p->height(),
                    p->parent() ? p->parent()->objectName().toUtf8().constData() : "null");
    }

    auto *item2 = new QListWidgetItem;
    w.addItem(item2);
    auto *row = new QLabel(QStringLiteral("JDK 21 row"));
    w.setItemWidget(item2, row);
    w.show();
    app.processEvents();

    if (p) {
        std::printf("after new row: isNull=%d visible=%d geom=%dx%d @(%d,%d)\n",
                    p.isNull(), p->isVisible(), p->width(), p->height(),
                    p->x(), p->y());
    }

    // 等待可能的 deleteLater
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
    std::printf("after event loop: isNull=%d\n", p.isNull());

    return 0;
}
