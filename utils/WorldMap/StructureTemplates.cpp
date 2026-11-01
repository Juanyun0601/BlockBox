/**
 * @file   StructureTemplates.cpp
 * @brief  原版结构模板 NBT 的载入与解析
 * @author BlockBox Team
 * @date   2026-10-28
 */

#include "StructureTemplates.h"

#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>

#include "utils/Schematic/NbtCodec.h"
#include "utils/Schematic/SchematicDocument.h"

namespace {

/** 模板在资源系统里的根目录（resources_data.qrc） */
const char *kTemplatePrefix = ":/utils/WorldMap/structures/";

const NbtTag *findChildTag(const NbtTag &parent, const char *name)
{
    for (const NbtTag &child : parent.children)
        if (child.name == QLatin1String(name))
            return &child;
    return nullptr;
}

QString stringOf(const NbtTag &tag)
{
    return tag.type == NbtType::String ? tag.stringValue : QString();
}

/**
 * palette 条目的方块名标签。
 *
 * 1.21.4 及更早：`Name`；26.x（新版结构模板格式）起改成小写 `id`，
 * 两种都要认，否则新版本模板会整片解析成空。
 */
const NbtTag *paletteNameTag(const NbtTag &entry)
{
    const NbtTag *tag = findChildTag(entry, "Name");
    return tag ? tag : findChildTag(entry, "id");
}

/** palette 条目里的方块属性表（旧版 `Properties` / 新版 `properties`） */
QMap<QString, QString> paletteProperties(const NbtTag &entry)
{
    QMap<QString, QString> props;
    const NbtTag *propsTag = findChildTag(entry, "Properties");
    if (!propsTag)
        propsTag = findChildTag(entry, "properties");
    if (propsTag)
        for (const NbtTag &p : propsTag->children)
            props.insert(p.name, stringOf(p));
    return props;
}

/** palette 条目 → 方块状态串 */
QString paletteStateString(const QString &name, const QMap<QString, QString> &props)
{
    SchematicBlockState state;
    state.name = name;
    state.properties = props;
    return state.toString();
}

/** 该方块是否只是模板辅助方块（结构空位 / 标记方块 / 拼图方块） */
bool isHelperName(const QString &name)
{
    return name == QLatin1String("minecraft:structure_void")
        || name == QLatin1String("minecraft:structure_block")
        || name == QLatin1String("minecraft:jigsaw");
}

/**
 * @brief 读取整数三元组（坐标 / 尺寸）
 *
 * 1.21 起部分模板把坐标写成 TAG_List<TAG_Int> 而不是 TAG_Int_Array，两种都要认。
 */
QVector<int> intValues(const NbtTag *tag)
{
    QVector<int> out;
    if (!tag)
        return out;
    if (tag->type == NbtType::IntArray)
    {
        for (qint32 v : tag->intArray)
            out.append(v);
        return out;
    }
    if (tag->type == NbtType::List)
    {
        for (const NbtTag &child : tag->children)
            out.append(int(child.intValue));
    }
    return out;
}

bool readVec3(const NbtTag *tag, int &x, int &y, int &z)
{
    const QVector<int> values = intValues(tag);
    if (values.size() < 3)
        return false;
    x = values.at(0);
    y = values.at(1);
    z = values.at(2);
    return true;
}

} // namespace

std::shared_ptr<const StructureTemplate> StructureTemplate::load(const QString &id)
{
    static QMutex mutex;
    static QHash<QString, std::shared_ptr<const StructureTemplate>> cache;

    QMutexLocker locker(&mutex);
    const auto cached = cache.constFind(id);
    if (cached != cache.constEnd())
        return cached.value();

    auto tpl = std::make_shared<StructureTemplate>();
    tpl->m_id = id;

    NbtTag root;
    const QString path = QLatin1String(kTemplatePrefix) + id + QLatin1String(".nbt");
    if (NbtCodec::readCompressedFile(path, root) && tpl->parseFromNbt(root))
    {
        cache.insert(id, tpl);
        return tpl;
    }

    // 解析失败同样缓存空结果，避免每次预览重复读盘
    cache.insert(id, nullptr);
    return nullptr;
}

bool StructureTemplate::parseFromNbt(const NbtTag &root)
{
    int sx = 0, sy = 0, sz = 0;
    if (readVec3(findChildTag(root, "size"), sx, sy, sz) && sx > 0 && sy > 0 && sz > 0)
    {
        m_sizeX = sx;
        m_sizeY = sy;
        m_sizeZ = sz;
    }

    const NbtTag *paletteTag = findChildTag(root, "palette");
    if (!paletteTag)
    {
        // 部分模板（沉船等）存了多套调色板，默认用第 0 套
        if (const NbtTag *palettes = findChildTag(root, "palettes"))
            if (!palettes->children.isEmpty())
                paletteTag = &palettes->children.first();
    }
    const NbtTag *blocksTag = findChildTag(root, "blocks");
    if (!paletteTag || !blocksTag)
        return false;

    // 1) palette：辅助方块先标为「跳过」，拼图方块稍后按 final_state 复原
    QVector<QString> rawNames;
    QVector<QMap<QString, QString>> rawProps;
    rawNames.reserve(paletteTag->children.size());
    rawProps.reserve(paletteTag->children.size());
    m_palette.reserve(paletteTag->children.size());
    for (const NbtTag &entry : paletteTag->children)
    {
        const NbtTag *nameTag = paletteNameTag(entry);
        const QString name = nameTag ? stringOf(*nameTag) : QString();
        const QMap<QString, QString> props = paletteProperties(entry);
        rawNames.append(name);
        rawProps.append(props);
        m_palette.append(isHelperName(name) ? QString()
                                            : paletteStateString(name, props));
    }

    // 2) blocks
    m_blocks.reserve(blocksTag->children.size());
    for (const NbtTag &blockTag : blocksTag->children)
    {
        StructureTemplateBlock block;
        if (!readVec3(findChildTag(blockTag, "pos"), block.x, block.y, block.z))
            continue;
        const NbtTag *stateTag = findChildTag(blockTag, "state");
        block.state = stateTag ? int(stateTag->intValue) : -1;
        if (block.state < 0 || block.state >= rawNames.size())
            continue;

        if (rawNames.at(block.state) == QLatin1String("minecraft:jigsaw"))
        {
            StructureTemplateJigsaw jigsaw;
            jigsaw.x = block.x;
            jigsaw.y = block.y;
            jigsaw.z = block.z;
            jigsaw.orientation = rawProps.at(block.state).value(QStringLiteral("orientation"));
            if (const NbtTag *nbtTag = findChildTag(blockTag, "nbt"))
            {
                jigsaw.name = stringOf(*findChildTag(*nbtTag, "name"));
                jigsaw.target = stringOf(*findChildTag(*nbtTag, "target"));
                jigsaw.pool = stringOf(*findChildTag(*nbtTag, "pool"));
                jigsaw.joint = stringOf(*findChildTag(*nbtTag, "joint"));
                jigsaw.finalState = stringOf(*findChildTag(*nbtTag, "final_state"));
                if (const NbtTag *pp = findChildTag(*nbtTag, "placement_priority"))
                    jigsaw.placementPriority = int(pp->intValue);
                if (const NbtTag *sp = findChildTag(*nbtTag, "selection_priority"))
                    jigsaw.selectionPriority = int(sp->intValue);
            }
            if (jigsaw.joint.isEmpty())
                jigsaw.joint = QStringLiteral("aligned");
            m_jigsaws.append(jigsaw);
            // 原版放置拼图方块时用 final_state 顶替，这里同样处理
            block.stateOverride = isHelperName(jigsaw.finalState)
                ? QString()
                : jigsaw.finalState;
        }
        m_blocks.append(block);
    }

    if (m_sizeX <= 0 || m_sizeY <= 0 || m_sizeZ <= 0)
    {
        // 少数模板缺 size 标签时按方块范围补齐
        int mx = 0, my = 0, mz = 0;
        for (const StructureTemplateBlock &b : m_blocks)
        {
            mx = qMax(mx, b.x + 1);
            my = qMax(my, b.y + 1);
            mz = qMax(mz, b.z + 1);
        }
        m_sizeX = mx;
        m_sizeY = my;
        m_sizeZ = mz;
    }
    return !m_blocks.isEmpty();
}
