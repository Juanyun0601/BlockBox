/**
 * @file   file_filter_probe.cpp
 * @brief  实例管理页本地资源多维筛选行为探针
 *
 * 构造真实 InstanceFilePage（资源包 / 截图 / 存档三种类型），合成临时实例目录，
 * 断言：筛选行控件构成、状态/时间/大小/排序筛选、智能分类（大文件/最近修改/
 * 可更新/未分类）、禁用项 .disabled 显示裁剪与已禁用芯片、空态文案（纯筛空
 * 不出现下载按钮）、截图页隐藏状态下拉。
 */
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QPoint>
#include <QScrollArea>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include "components/ContentViewSwitch.h"
#include "pages/InstanceFilePage.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char *msg)
{
    if (ok)
    {
        ++g_pass;
        std::fprintf(stderr, "[PASS] %s\n", msg);
    }
    else
    {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s\n", msg);
    }
}

// 旋转事件循环直到 pred 为真（或超时）
template <typename Pred>
static bool spinUntil(Pred pred, int timeoutMs = 8000)
{
    QElapsedTimer t;
    t.start();
    while (!pred())
    {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return true;
}

// 页面内所有带 filePath 属性的卡片（列表卡 + 瀑布流卡）
// 重建时旧卡先被移出布局再 deleteLater，待销毁的旧卡不算——
// 只统计仍在父布局中的，避免新旧并存的中间态误判
static QList<QWidget *> cardsOf(InstanceFilePage *page)
{
    QList<QWidget *> out;
    const auto all = page->findChildren<QWidget *>();
    for (QWidget *w : all)
    {
        if (!w->property("filePath").isValid())
            continue;
        QWidget *parent = w->parentWidget();
        QLayout *lay = parent ? parent->layout() : nullptr;
        if (lay && lay->indexOf(w) >= 0)
            out.append(w);
    }
    return out;
}

static int visibleCardCount(InstanceFilePage *page)
{
    int n = 0;
    for (QWidget *c : cardsOf(page))
    {
        if (!c->isHidden())
            ++n;
    }
    return n;
}

// 按布局顺序取卡片 filePath（列表视图为单列纵向）
static QStringList layoutFilePaths(QWidget *container)
{
    QStringList out;
    QLayout *lay = container->layout();
    if (!lay)
        return out;
    for (int i = 0; i < lay->count(); ++i)
    {
        QLayoutItem *item = lay->itemAt(i);
        QWidget *w = item ? item->widget() : nullptr;
        if (w && w->property("filePath").isValid())
            out.append(w->property("filePath").toString());
    }
    return out;
}

static QWidget *containerOf(InstanceFilePage *page)
{
    QList<QWidget *> cards = cardsOf(page);
    return cards.isEmpty() ? nullptr : cards.first()->parentWidget();
}

// 按选项文本在页面所有下拉里查找
static QComboBox *comboWithItem(InstanceFilePage *page, const QString &itemText)
{
    const auto combos = page->findChildren<QComboBox *>();
    for (QComboBox *cb : combos)
    {
        for (int i = 0; i < cb->count(); ++i)
        {
            if (cb->itemText(i) == itemText)
                return cb;
        }
    }
    return nullptr;
}

static void selectByItemText(QComboBox *cb, const QString &itemText)
{
    for (int i = 0; i < cb->count(); ++i)
    {
        if (cb->itemText(i) == itemText)
        {
            cb->setCurrentIndex(i);
            return;
        }
    }
}

static void writeSizedFile(const QString &path, qint64 size)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite | QIODevice::Truncate))
        return;
    f.resize(size);
    f.close();
}

static void setMTime(const QString &path, const QDateTime &dt)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return;
    f.setFileTime(dt, QFileDevice::FileModificationTime);
    f.close();
}

static bool waitForCards(InstanceFilePage *page, int expected)
{
    return spinUntil([page, expected]() {
        return cardsOf(page).size() == expected;
    });
}

// 排序/重建会 deleteLater 旧卡片：等「页面卡片数 == 期望」且
// 「当前布局内卡片数 == 期望」同时稳定保持 stableMs，排除新旧卡片并存的中间态
static bool waitForSettled(InstanceFilePage *page, int expected, int stableMs = 250)
{
    QElapsedTimer t;
    t.start();
    qint64 satisfiedSince = -1;
    while (t.elapsed() < 8000)
    {
        const bool ok = cardsOf(page).size() == expected
            && layoutFilePaths(containerOf(page)).size() == expected;
        if (ok)
        {
            if (satisfiedSince < 0)
                satisfiedSince = t.elapsed();
            else if (t.elapsed() - satisfiedSince >= stableMs)
                return true;
        }
        else
        {
            satisfiedSince = -1;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return false;
}

// ─────────────────────────── 资源包页 ───────────────────────────
static void testResourcePackPage(const QString &inst)
{
    std::fprintf(stderr, "\n=== 资源包页 ===\n");
    InstanceFilePage page(InstanceFilePage::ResourcePack);
    page.resize(1000, 700);
    page.show();
    page.setInstancePath(inst);

    check(waitForCards(&page, 4), "扫描出 4 个资源包卡片");

    // 1. 筛选行控件构成
    QComboBox *status = comboWithItem(&page, QStringLiteral("已禁用"));
    QComboBox *time = comboWithItem(&page, QStringLiteral("最近 7 天"));
    QComboBox *size = comboWithItem(&page, QStringLiteral("大于 100 MB"));
    QComboBox *sort = page.findChild<QComboBox *>(QStringLiteral("searchSortCombo"));
    QComboBox *cat = page.findChild<QComboBox *>(QStringLiteral("categoryCombo"));
    check(status != nullptr, "存在状态下拉（全部/已启用/已禁用）");
    check(time != nullptr, "存在时间下拉（含最近 7 天）");
    check(size != nullptr, "存在大小下拉（含大于 100 MB）");
    check(sort != nullptr, "存在排序下拉 searchSortCombo");
    check(sort && sort->count() == 6, "排序下拉 6 项");
    check(time && time->count() == 4, "时间下拉 4 项");
    check(size && size->count() == 4, "大小下拉 4 项");

    // 1b. 顶部两行结构与拖放（本轮改动）
    check(page.acceptDrops(), "页面接受拖放（拖入列表直接导入）");
    check(page.findChild<QPushButton *>(QStringLiteral("dragModeBtn")) == nullptr,
          "「拖入此处」按钮已移除");
    {
        QWidget *filterCard = page.findChild<QWidget *>(QStringLiteral("filterCard"));
        check(filterCard && filterCard->layout()
                  && filterCard->layout()->count() == 2,
              "顶部筛选卡恰好 2 行");
        auto sameRow = [](QWidget *a, QWidget *b) {
            return a && b && qAbs(a->mapToParent(QPoint(0, 0)).y()
                                  - b->mapToParent(QPoint(0, 0)).y()) < 8;
        };
        QLineEdit *search = page.findChild<QLineEdit *>(QStringLiteral("modSearchEdit"));
        ContentViewSwitch *vs = page.findChild<ContentViewSwitch *>();
        check(sameRow(search, cat) && sameRow(cat, vs),
              "行1 = 搜索框 + 视图切换 + 分类");
        check(sameRow(status, time) && sameRow(time, size) && sameRow(size, sort),
              "行2 = 状态/时间/大小/排序");
        QLabel *stats = page.findChild<QLabel *>(QStringLiteral("statusLabel"));
        check(sameRow(sort, stats), "行2 末尾为统计标签");
    }

    // 2. 分类下拉含智能筛选项
    if (cat)
    {
        QStringList items;
        for (int i = 0; i < cat->count(); ++i)
            items << cat->itemText(i);
        check(items.contains(QStringLiteral("全部")), "分类含「全部」");
        check(items.contains(QStringLiteral("可更新")), "分类含智能「可更新」");
        check(items.contains(QStringLiteral("最近修改")), "分类含智能「最近修改」");
        check(items.contains(QStringLiteral("大文件")), "分类含智能「大文件」");
        check(items.contains(QStringLiteral("未分类")), "分类含「未分类」");
        check(items.contains(QStringLiteral("可更新"))
                  && items.indexOf(QStringLiteral("可更新"))
                       < items.indexOf(QStringLiteral("未分类")),
              "智能项排在「未分类」之前");
    }

    QLabel *stats = page.findChild<QLabel *>(QStringLiteral("statusLabel"));
    check(stats && stats->text().contains(QStringLiteral("共 4")),
          "统计标签显示总数 4");

    // 3. 状态筛选：已禁用 → 只剩 c.zip.disabled
    selectByItemText(status, QStringLiteral("已禁用"));
    check(visibleCardCount(&page) == 1, "已禁用筛选后剩 1 张卡");
    check(stats && stats->text().contains(QStringLiteral("显示 1")),
          "统计标签显示「显示 1」");

    // 禁用卡片：名称裁剪 + 已禁用芯片
    {
        QWidget *disabledCard = nullptr;
        for (QWidget *c : cardsOf(&page))
        {
            if (c->property("filePath").toString().endsWith(QStringLiteral(".disabled")))
            {
                disabledCard = c;
                break;
            }
        }
        check(disabledCard != nullptr, "找到 .disabled 卡片");
        if (disabledCard)
        {
            bool nameOk = false, chipOk = false;
            const auto labels = disabledCard->findChildren<QLabel *>();
            for (QLabel *l : labels)
            {
                if (l->text() == QStringLiteral("c.zip"))
                    nameOk = true;
                if (l->text() == QStringLiteral("已禁用"))
                    chipOk = true;
                if (l->text().contains(QStringLiteral(".disabled")))
                    nameOk = false;   // 显示名不允许带后缀
            }
            check(nameOk, "禁用卡片名称显示 c.zip（已裁剪 .disabled）");
            check(chipOk, "禁用卡片带「已禁用」芯片");
        }
    }

    // 4. 状态筛选：已启用 → 3 张
    selectByItemText(status, QStringLiteral("已启用"));
    check(visibleCardCount(&page) == 3, "已启用筛选后剩 3 张卡");

    // 5. 时间筛选（d.zip 为 40 天前）
    selectByItemText(status, QStringLiteral("全部"));
    selectByItemText(time, QStringLiteral("最近 7 天"));
    check(visibleCardCount(&page) == 3, "最近 7 天筛掉 40 天前的 d.zip（剩 3）");
    selectByItemText(time, QStringLiteral("全部时间"));
    check(visibleCardCount(&page) == 4, "恢复全部时间后 4 张可见");

    // 6. 大小筛选：b.zip 为 101MB
    selectByItemText(size, QStringLiteral("大于 100 MB"));
    check(visibleCardCount(&page) == 1, "大于 100 MB 只剩 b.zip");
    selectByItemText(size, QStringLiteral("小于 10 MB"));
    check(visibleCardCount(&page) == 3, "小于 10 MB 剩 3 张（a/c/d）");
    selectByItemText(size, QStringLiteral("全部大小"));
    check(visibleCardCount(&page) == 4, "恢复全部大小后 4 张可见");

    // 7. 智能分类：大文件 / 最近修改 / 未分类
    if (cat)
    {
        selectByItemText(cat, QStringLiteral("大文件"));
        check(visibleCardCount(&page) == 1, "智能分类「大文件」剩 1 张");
        selectByItemText(cat, QStringLiteral("最近修改"));
        check(visibleCardCount(&page) == 3, "智能分类「最近修改」剩 3 张");
        selectByItemText(cat, QStringLiteral("未分类"));
        check(visibleCardCount(&page) == 4, "「未分类」显示全部 4 张（均未归类）");
        selectByItemText(cat, QStringLiteral("全部"));
        check(visibleCardCount(&page) == 4, "恢复「全部」后 4 张可见");
    }

    // 8. 筛空空态：已禁用 + 大于 100MB = 0，提示但不出下载按钮
    selectByItemText(status, QStringLiteral("已禁用"));
    selectByItemText(size, QStringLiteral("大于 100 MB"));
    QLabel *emptyLabel = page.findChild<QLabel *>(QStringLiteral("modPlaceholderLabel"));
    QPushButton *dlBtn = page.findChild<QPushButton *>(QStringLiteral("downloadSearchBtn"));
    check(visibleCardCount(&page) == 0, "叠加筛选后 0 张可见");
    check(emptyLabel && emptyLabel->isVisible()
              && emptyLabel->text().contains(QStringLiteral("没有符合筛选条件")),
          "纯筛空显示「没有符合筛选条件」提示");
    check(dlBtn && dlBtn->isHidden(), "纯筛空不出现「前往下载页搜索」按钮");
    check(stats && stats->text().contains(QStringLiteral("显示 0")),
          "统计标签显示「显示 0」");
    selectByItemText(status, QStringLiteral("全部"));
    selectByItemText(size, QStringLiteral("全部大小"));
    check(visibleCardCount(&page) == 4 && emptyLabel && emptyLabel->isHidden(),
          "清空筛选后恢复 4 张且空态提示消失");

    // 9. 搜索空态：有搜索词时仍引导下载页
    QLineEdit *search = page.findChild<QLineEdit *>(QStringLiteral("modSearchEdit"));
    check(search != nullptr, "存在搜索框");
    if (search)
    {
        search->setText(QStringLiteral("zzzzz"));
        QCoreApplication::processEvents();
        check(visibleCardCount(&page) == 0, "搜索无命中 0 张可见");
        check(dlBtn && dlBtn->isVisible(), "搜索空态显示下载按钮");
        search->setText(QString());
        QCoreApplication::processEvents();
        check(visibleCardCount(&page) == 4, "清空搜索恢复 4 张");
    }

    // 10. 排序：大小 大→小 → b,c,d,a（101MB > 3KB > 2KB > 1KB）
    if (sort)
    {
        sort->setCurrentIndex(2);   // 大小 大→小
        check(waitForSettled(&page, 4), "排序后卡片完成重建（4 张稳定）");
        QStringList paths = layoutFilePaths(containerOf(&page));
        check(paths.size() == 4, "布局内 4 张卡片");
        if (paths.size() == 4)
        {
            check(paths[0].endsWith(QStringLiteral("b.zip")), "大小降序首张为 b.zip");
            check(paths[1].endsWith(QStringLiteral("c.zip.disabled")), "第二张为 c.zip.disabled");
            check(paths[3].endsWith(QStringLiteral("a.zip")), "末张为 a.zip");
        }

        sort->setCurrentIndex(0);   // 名称 A→Z 复位
        check(waitForSettled(&page, 4), "名称排序重建完成");
        paths = layoutFilePaths(containerOf(&page));
        check(paths.size() == 4 && paths[0].endsWith(QStringLiteral("a.zip")),
              "名称升序首张为 a.zip");
    }

    // 11. 智能「可更新」（未检查更新 → 筛空，不崩溃）
    if (cat)
    {
        check(waitForSettled(&page, 4), "「可更新」测试前列表稳定");
        selectByItemText(cat, QStringLiteral("可更新"));
        check(spinUntil([&page]() { return visibleCardCount(&page) == 0; }),
              "未检查更新时「可更新」筛空");
        check(emptyLabel && emptyLabel->isVisible(), "「可更新」筛空有提示");
        selectByItemText(cat, QStringLiteral("全部"));
        check(spinUntil([&page]() { return visibleCardCount(&page) == 4; }),
              "退出「可更新」恢复 4 张");
    }
}

// ─────────────────────────── 截图页 ───────────────────────────
static void testScreenshotPage(const QString &inst)
{
    std::fprintf(stderr, "\n=== 截图页 ===\n");
    InstanceFilePage page(InstanceFilePage::Screenshot);
    page.resize(1000, 700);
    page.show();
    page.setInstancePath(inst);

    check(waitForCards(&page, 2), "扫描出 2 张截图卡片");

    check(comboWithItem(&page, QStringLiteral("已禁用")) == nullptr,
          "截图页隐藏状态下拉（无禁用概念）");
    check(comboWithItem(&page, QStringLiteral("最近 7 天")) != nullptr,
          "截图页保留时间下拉");
    check(comboWithItem(&page, QStringLiteral("全部大小")) != nullptr,
          "截图页保留大小下拉");
    check(page.findChild<QComboBox *>(QStringLiteral("searchSortCombo")) != nullptr,
          "截图页保留排序下拉");

    QComboBox *time = comboWithItem(&page, QStringLiteral("最近 7 天"));
    if (time)
    {
        selectByItemText(time, QStringLiteral("最近 7 天"));
        check(visibleCardCount(&page) == 1, "截图页时间筛选生效（1 张）");
        selectByItemText(time, QStringLiteral("全部时间"));
        check(visibleCardCount(&page) == 2, "恢复全部时间 2 张");
    }
}

// ─────────────────────────── 存档页 ───────────────────────────
static void testSavePage(const QString &inst)
{
    std::fprintf(stderr, "\n=== 存档页 ===\n");
    InstanceFilePage page(InstanceFilePage::Save);
    page.resize(1000, 700);
    page.show();
    page.setInstancePath(inst);

    check(waitForCards(&page, 2), "扫描出 2 个存档");

    QComboBox *status = comboWithItem(&page, QStringLiteral("已禁用"));
    QComboBox *size = comboWithItem(&page, QStringLiteral("全部大小"));
    check(status != nullptr, "存档页有状态下拉");
    if (status)
    {
        selectByItemText(status, QStringLiteral("已禁用"));
        check(visibleCardCount(&page) == 1, "存档页已禁用筛选剩 1 个（worldB.disabled）");
        selectByItemText(status, QStringLiteral("已启用"));
        check(visibleCardCount(&page) == 1, "存档页已启用筛选剩 1 个（worldA）");
        selectByItemText(status, QStringLiteral("全部"));
    }

    // 存档目录大小递归统计 → 排序可用
    QComboBox *sort = page.findChild<QComboBox *>(QStringLiteral("searchSortCombo"));
    if (sort && size)
    {
        selectByItemText(size, QStringLiteral("大于 100 MB"));
        check(visibleCardCount(&page) == 0, "存档目录小于 100MB，大小筛选筛空");
        QLabel *emptyLabel = page.findChild<QLabel *>(QStringLiteral("modPlaceholderLabel"));
        QPushButton *dlBtn = page.findChild<QPushButton *>(QStringLiteral("downloadSearchBtn"));
        check(emptyLabel && emptyLabel->isVisible(), "存档页筛空有提示");
        check(dlBtn && dlBtn->isHidden(), "存档页筛空不出现下载按钮");
        selectByItemText(size, QStringLiteral("全部大小"));
        check(visibleCardCount(&page) == 2, "恢复全部大小 2 个存档可见");
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("file_filter_probe"));

    // ── 合成实例目录 ──
    const QString inst = QDir::tempPath() + QStringLiteral("/bb_file_filter_probe_inst");
    QDir(inst).removeRecursively();
    QDir().mkpath(inst + QStringLiteral("/resourcepacks"));
    QDir().mkpath(inst + QStringLiteral("/screenshots"));
    QDir().mkpath(inst + QStringLiteral("/saves/worldA/region"));
    QDir().mkpath(inst + QStringLiteral("/saves/worldB.disabled"));

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime old40 = now.addDays(-40);

    writeSizedFile(inst + "/resourcepacks/a.zip", 1 * 1024);            // 1KB，最近
    writeSizedFile(inst + "/resourcepacks/b.zip", 101LL * 1024 * 1024); // 101MB，最近
    writeSizedFile(inst + "/resourcepacks/c.zip.disabled", 3 * 1024);   // 3KB，禁用
    writeSizedFile(inst + "/resourcepacks/d.zip", 2 * 1024);            // 2KB，40 天前
    setMTime(inst + "/resourcepacks/d.zip", old40);

    // 截图：a 截图最近、b 截图 40 天前（后缀 .png 即可，不校验图像内容）
    writeSizedFile(inst + "/screenshots/a.png", 2 * 1024);
    writeSizedFile(inst + "/screenshots/b.png", 2 * 1024);
    setMTime(inst + "/screenshots/b.png", old40);

    // 存档：worldA（含区域文件）/ worldB.disabled（禁用目录）
    writeSizedFile(inst + "/saves/worldA/level.dat", 1024);
    writeSizedFile(inst + "/saves/worldA/region/r.0.0.mca", 5 * 1024 * 1024);
    writeSizedFile(inst + "/saves/worldB.disabled/level.dat", 1024);

    // ── 三类页面行为断言 ──
    testResourcePackPage(inst);
    testScreenshotPage(inst);
    testSavePage(inst);

    std::fprintf(stderr, "\n======== 结果: %d PASS / %d FAIL ========\n", g_pass, g_fail);

    QDir(inst).removeRecursively();
    return g_fail == 0 ? 0 : 1;
}
