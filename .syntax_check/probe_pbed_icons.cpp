/**
 * 探针：验证投影方块编辑器的调色板/「当前方块」下拉框图标确实来自实例客户端 jar 的贴图。
 * 用法：probe_pbed_icons.exe <instancePath> <litematic>
 * 返回 0 表示调色板图标与 InstanceTextureLoader::blockIcon 完全一致。
 */
#include "components/ProjectionBlockEditorDialog.h"
#include "utils/Schematic/InstanceTextureLoader.h"

#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QPixmap>
#include <QString>
#include <cstdio>

namespace
{
QImage iconImage(const QIcon &icon, int size)
{
    return icon.pixmap(QSize(size, size)).toImage().convertToFormat(QImage::Format_ARGB32);
}

bool sameImage(const QImage &a, const QImage &b)
{
    return !a.isNull() && !b.isNull() && a.size() == b.size() && a == b;
}

bool hasMagentaAndBlue(const QImage &im)
{
    bool magenta = false, blue = false;
    for (int y = 0; y < im.height(); ++y)
    {
        for (int x = 0; x < im.width(); ++x)
        {
            const QRgb c = im.pixel(x, y);
            if (qRed(c) > 200 && qGreen(c) < 60 && qBlue(c) > 200)
                magenta = true;
            if (qRed(c) < 60 && qGreen(c) < 60 && qBlue(c) > 200)
                blue = true;
        }
    }
    return magenta && blue;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 3)
    {
        std::printf("usage: probe_pbed_icons <instancePath> <litematic>\n");
        return 2;
    }
    const QString inst = QString::fromLocal8Bit(argv[1]);
    const QString file = QString::fromLocal8Bit(argv[2]);

    auto *loader = InstanceTextureLoader::sharedForInstance(inst);
    if (!loader)
    {
        std::printf("[FAIL] InstanceTextureLoader::sharedForInstance 返回空\n");
        return 2;
    }
    std::printf("[env] loader valid=%d count=%d tilesPerRow=%d atlas=%d\n",
                loader->isValid() ? 1 : 0, loader->textureCount(), loader->tilesPerRow(),
                loader->atlasSize());

    ProjectionBlockEditorDialog dlg;
    if (!dlg.loadProjection(file, inst))
    {
        std::printf("[FAIL] loadProjection 失败\n");
        return 2;
    }

    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("projPaletteList"));
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("projBlockCombo"));
    if (!list)
    {
        std::printf("[FAIL] 找不到 projPaletteList\n");
        return 2;
    }
    std::printf("[env] 调色板项数=%d 下拉框项数=%d\n", list->count(), combo ? combo->count() : -1);

    int checked = 0, matched = 0, textured = 0, fallback = 0;
    for (int i = 0; i < list->count(); ++i)
    {
        QListWidgetItem *item = list->item(i);
        if (!item)
            continue;
        const QString name = item->data(Qt::UserRole + 1).toString();
        if (name.isEmpty() || name == QLatin1String("minecraft:air"))
            continue; // 空气画的是虚线空心框，不来自贴图，和材料清单一样不会出现
        const QImage got = iconImage(item->icon(), 20);
        const QImage expect = iconImage(loader->blockIcon(name, 20), 20);
        if (expect.isNull())
        {
            // 实例 jar 里没有这张贴图：材料清单同样没有图标，编辑器回退纯色块即可
            ++fallback;
            continue;
        }
        ++checked;
        if (sameImage(got, expect))
            ++matched;
        else
            std::printf("[mismatch] i=%d name=%s text=%s gotNull=%d got=%dx%d expect=%dx%d\n",
                        i, name.toUtf8().constData(), item->text().toUtf8().constData(),
                        got.isNull() ? 1 : 0, got.width(), got.height(), expect.width(),
                        expect.height());
        if (hasMagentaAndBlue(got))
            ++textured;
        if (name == QLatin1String("minecraft:stone"))
        {
            std::printf("[stone] item=%d iconNull=%d expectNull=%d same=%d colors=%d\n", i,
                        item->icon().isNull() ? 1 : 0, expect.isNull() ? 1 : 0,
                        sameImage(got, expect) ? 1 : 0, hasMagentaAndBlue(got) ? 1 : 0);
        }
    }
    std::printf("[palette] 有贴图项=%d 与 blockIcon 一致=%d 双色=%d 无贴图回退=%d\n", checked,
                matched, textured, fallback);

    if (combo)
    {
        const QImage got = iconImage(combo->itemIcon(combo->currentIndex()), 20);
        std::printf("[combo] current=%d iconNull=%d 双色=%d text=%s\n", combo->currentIndex(),
                    got.isNull() ? 1 : 0, hasMagentaAndBlue(got) ? 1 : 0,
                    combo->currentText().toUtf8().constData());
    }

    // 全量一致 + 至少 stone 图标来自假贴图，才算通过
    const bool ok = checked > 0 && matched == checked && textured > 0;
    std::printf("[%s] checked=%d matched=%d textured=%d fallback=%d\n",
                ok ? "PASS" : "FAIL", checked, matched, textured, fallback);
    return ok ? 0 : 1;
}

