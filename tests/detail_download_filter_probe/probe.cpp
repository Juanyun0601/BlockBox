/**
 * @file   probe.cpp
 * @brief  资源详情页「下载列表」实例筛选探针（真实控件，离屏渲染）
 *
 * 验证 pages/ContentDetailPage.cpp:
 *   - 资源包/数据包/光影包/世界 不显示「全部加载器」下拉
 *   - setInstanceFilter(实例版本, "fabric") 之后，下载卡片仍然可见
 *   - 模组页面保留加载器筛选，且加载器不匹配时确实会把卡片筛掉
 *     （负控制：证明原问题的成因就是"拿实例加载器去筛没有加载器的资源"）
 *
 * 构建：链接 release/*.o 静态库 + 新编译的 ContentDetailPage.o
 * 运行：QT_QPA_PLATFORM=offscreen probe.exe
 */
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QStringList>
#include <QWidget>

#include <cstdio>

#include "pages/ContentDetailPage.h"
#include "utils/mod/ModData.h"

namespace {

int g_fail = 0;

void check(bool ok, const QString &what)
{
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.toLocal8Bit().constData());
    if (!ok) ++g_fail;
}

int countVisibleCards(QWidget *root)
{
    int n = 0;
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (w->objectName() == QLatin1String("modDetailDownloadCard") && w->isVisible())
            ++n;
    }
    return n;
}

bool hasLoaderCombo(QWidget *root)
{
    const QList<QComboBox *> combos = root->findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemText(i) == QStringLiteral("全部加载器"))
                return true;
        }
    }
    return false;
}

ModVersionFile makeFile(const QString &version, const QStringList &gameVersions,
                        const QStringList &loaders, const QString &releaseType)
{
    ModVersionFile file;
    file.version = version;
    file.gameVersions = gameVersions;
    file.loaders = loaders;
    file.releaseType = releaseType;
    file.fileName = version + QStringLiteral(".jar");
    file.downloadUrl = QStringLiteral("https://example.invalid/") + file.fileName;
    return file;
}

ModInfo makeInfo(const QString &id, const QString &name, const QStringList &loaders)
{
    ModInfo info;
    info.id = id;
    info.name = name;
    info.source = QStringLiteral("modrinth");
    info.gameVersions = QStringList{QStringLiteral("1.21.4")};
    info.loaders = loaders;
    info.latestVersion = QStringLiteral("1.0.0");
    return info;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // ── 1) 资源包：实例带 fabric 加载器进入详情页 ──
    {
        ModInfo info = makeInfo(QStringLiteral("probe_resourcepack"), QStringLiteral("Probe 资源包"), {});
        info.versionFiles << makeFile(QStringLiteral("1.0.0"), {QStringLiteral("1.21.4")}, {}, QStringLiteral("release"))
                          << makeFile(QStringLiteral("0.9.0"), {QStringLiteral("1.21.4")}, {}, QStringLiteral("release"))
                          << makeFile(QStringLiteral("0.8.0"), {QStringLiteral("1.20.6")}, {}, QStringLiteral("release"));

        ContentDetailPage page(ContentType::ResourcePack);
        page.setModInfo(info);
        page.setInstanceFilter(QStringLiteral("1.21.4"), QStringLiteral("fabric"));
        page.show();
        app.processEvents();

        check(!hasLoaderCombo(&page), QStringLiteral("资源包详情页不显示「全部加载器」下拉"));
        const int visible = countVisibleCards(&page);
        check(visible == 2, QStringLiteral("资源包详情页按实例版本筛出 2 张下载卡片（实际 %1）").arg(visible));
    }

    // ── 2) 数据包 / 光影包：同样不应被实例加载器筛空 ──
    {
        const QList<ContentType> types{ContentType::DataPack, ContentType::ShaderPack, ContentType::World};
        const QStringList names{QStringLiteral("数据包"), QStringLiteral("光影包"), QStringLiteral("世界")};
        for (int i = 0; i < types.size(); ++i) {
            ModInfo info = makeInfo(QStringLiteral("probe_other_%1").arg(i), names.at(i), {});
            info.versionFiles << makeFile(QStringLiteral("1.0.0"), {QStringLiteral("1.21.4")}, {}, QStringLiteral("release"));

            ContentDetailPage page(types.at(i));
            page.setModInfo(info);
            page.setInstanceFilter(QStringLiteral("1.21.4"), QStringLiteral("fabric"));
            page.show();
            app.processEvents();

            check(!hasLoaderCombo(&page), QStringLiteral("%1 详情页不显示「全部加载器」下拉").arg(names.at(i)));
            const int visible = countVisibleCards(&page);
            check(visible == 1, QStringLiteral("%1 详情页仍能看到 1 张下载卡片（实际 %2）").arg(names.at(i)).arg(visible));
        }
    }

    // ── 3) 模组：保留加载器筛选，且不匹配时确实筛掉（负控制） ──
    {
        ModInfo info = makeInfo(QStringLiteral("probe_mod"), QStringLiteral("Probe 模组"),
                                {QStringLiteral("fabric"), QStringLiteral("forge")});
        info.versionFiles << makeFile(QStringLiteral("2.0.0"), {QStringLiteral("1.21.4")},
                                      {QStringLiteral("fabric")}, QStringLiteral("release"))
                          << makeFile(QStringLiteral("2.0.0-forge"), {QStringLiteral("1.21.4")},
                                      {QStringLiteral("forge")}, QStringLiteral("release"));

        ContentDetailPage page(ContentType::Mod);
        page.setModInfo(info);
        page.setInstanceFilter(QStringLiteral("1.21.4"), QStringLiteral("fabric"));
        page.show();
        app.processEvents();

        check(hasLoaderCombo(&page), QStringLiteral("模组详情页保留「全部加载器」下拉"));
        const int visible = countVisibleCards(&page);
        check(visible == 1, QStringLiteral("模组详情页按 fabric 筛出 1 张卡片（实际 %1）").arg(visible));

        // 负控制：换成不存在的加载器后列表应为空，说明筛空确实由加载器条件造成
        page.setInstanceFilter(QStringLiteral("1.21.4"), QStringLiteral("quilt"));
        app.processEvents();
        const int visibleQuilt = countVisibleCards(&page);
        check(visibleQuilt == 0,
              QStringLiteral("负控制：加载器不匹配的模组页面卡片数 0（实际 %1）").arg(visibleQuilt));
    }

    if (g_fail == 0)
        std::printf("\nALL CHECKS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_fail);
    std::printf("fail=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
