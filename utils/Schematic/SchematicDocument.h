/**
 * @file   SchematicDocument.h
 * @brief  投影方块级文档模型（逐格读写 .litematic）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 提供对 .litematic 投影文件在"方块级"上的建/读/改/写能力，供投影方块编辑器
 * （原生插件）复用。这是启动器内置的通用核心，插件侧只负责界面与交互。
 *
 * 核心概念：
 *   - 区域（Region）：一个矩形空间，Position(最小角) + Size(跨度)。
 *     本类将其归一化为 (originX,Y,Z) 与 (sizeX,Y,Z)（绝对正值）。
 *   - 调色板（Palette）：区域内的方块状态列表，按下标索引。
 *   - 方块索引（BlockIndices）：按体积序（y*layer + z*W + x，layer=W*L）排列，
 *     每个元素是对应调色板下标；索引 0 恒为 minecraft:air。
 *   - BlockStates：LongArray 位打包，bitsPerBlock = max(2, ceilLog2(paletteSize))，
 *     索引 i 起始位 = i*bitsPerBlock，值可跨两个 long（MSB→LSB 序）。
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

struct NbtTag; // fwd（于 NbtCodec.h 定义）

/**
 * @brief 单个方块状态（方块 ID + 属性表）
 */
struct SchematicBlockState
{
    QString name;                       ///< 方块 ID，如 "minecraft:stone"
    QMap<QString, QString> properties;  ///< 属性，如 "type" -> "top"

    bool operator==(const SchematicBlockState &o) const
    {
        return name == o.name && properties == o.properties;
    }
    bool operator!=(const SchematicBlockState &o) const { return !(*this == o); }

    /** 规范字符串："minecraft:stone" 或 "minecraft:log[axis=y]" */
    QString toString() const
    {
        if (properties.isEmpty())
            return name;
        QStringList parts;
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
            parts.append(it.key() + QLatin1Char('=') + it.value());
        return name + QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
    }

    /**
     * @brief 由规范字符串解析方块状态
     * @param str 如 "minecraft:stone" 或 "minecraft:log[axis=y]"
     */
    static SchematicBlockState fromString(const QString &str)
    {
        SchematicBlockState s;
        const int bracket = str.indexOf(QLatin1Char('['));
        if (bracket < 0)
        {
            s.name = str.trimmed();
            return s;
        }
        s.name = str.left(bracket).trimmed();
        const QString propStr = str.mid(bracket + 1);
        if (propStr.endsWith(QLatin1Char(']')))
        {
            const QStringList pairs = propStr.left(propStr.size() - 1).split(QLatin1Char(','));
            for (const QString &pair : pairs)
            {
                const int eq = pair.indexOf(QLatin1Char('='));
                if (eq > 0)
                    s.properties.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
            }
        }
        return s;
    }

    bool isAir() const { return name == QLatin1String("minecraft:air"); }
};

/**
 * @brief 投影方块级文档
 *
 * 使用方式：
 *   SchematicDocument doc;
 *   if (!doc.load(path, &err)) { ... }
 *   doc.block(0, 60, 0);                 // 读取某格方块
 *   doc.setBlock(0, 61, 0, state, &err); // 放置/替换某格
 *   doc.setBlock(0, 61, 0, {}, &err);    // properties 为空即清为空气（破坏）
 *   doc.saveCopy(path, &err);            // 写回新文件（或就地覆盖）
 */
class SchematicDocument
{
public:
    // ------------------------------------------------------------------ 构建/加载
    bool load(const QString &filePath, QString *error = nullptr);
    bool isLoaded() const { return m_loaded; }
    void clear();

    // ------------------------------------------------------------------ 元信息
    QString filePath() const { return m_filePath; }
    QString name() const { return m_name; }
    QString author() const { return m_author; }
    QString description() const { return m_description; }
    int version() const { return m_version; }               ///< Litematica 格式版本
    int minecraftDataVersion() const { return m_minecraftDataVersion; }

    // ------------------------------------------------------------------ 几何
    int originX() const { return m_originX; }
    int originY() const { return m_originY; }
    int originZ() const { return m_originZ; }
    int sizeX() const { return m_sizeX; }
    int sizeY() const { return m_sizeY; }
    int sizeZ() const { return m_sizeZ; }
    int volume() const { return m_sizeX * m_sizeY * m_sizeZ; }
    int layerSize() const { return m_sizeX * m_sizeZ; }

    /** 世界坐标是否落在投影包围盒内 */
    bool contains(int x, int y, int z) const;

    // ------------------------------------------------------------------ 方块读写
    /**
     * @brief 读取 (x,y,z) 处方块状态（世界坐标，未在盒内返回空气）
     */
    SchematicBlockState block(int x, int y, int z) const;

    /**
     * @brief 读取体积序索引 i 处的方块
     */
    SchematicBlockState blockAt(int i) const;

    /**
     * @brief 放置/替换 (x,y,z) 处方块
     * @param state 空 name（或 air）表示破坏该格
     * @param error 输出失败原因
     * @return true 成功
     */
    bool setBlock(int x, int y, int z, const SchematicBlockState &state, QString *error = nullptr);

    /**
     * @brief 破坏 (x,y,z) 处方块（置为空气）
     */
    bool clearBlock(int x, int y, int z, QString *error = nullptr);

    // ------------------------------------------------------------------ 批量操作
    /**
     * @brief 将某种方块批量替换为另一种
     * @param from 源方块状态（isAir 表示空气）
     * @param to   目标方块状态（isAir 表示空气）
     * @return 替换的方块数量
     */
    int replaceAll(const SchematicBlockState &from, const SchematicBlockState &to,
                   QString *error = nullptr);

    /**
     * @brief 将整个投影空间填充为某种方块
     * @param state 目标方块状态（isAir 表示清空整个区域）
     * @return 受影响格数
     */
    int fillAll(const SchematicBlockState &state, QString *error = nullptr);

    /**
     * @brief 在指定区域内填充方块（含边界）
     * @return 受影响格数
     */
    int fillBox(int x0, int y0, int z0, int x1, int y1, int z1,
                const SchematicBlockState &state, QString *error = nullptr);

    /**
     * @brief 收集当前调色板中所有出现的方块状态（含空气），按首次出现顺序
     */
    QList<SchematicBlockState> palette(bool includeAir = true) const;

    /**
     * @brief 统计各方块类型数量（按 toString 聚合），可用于材料清单
     */
    QMap<QString, int> countByState() const;

    /**
     * @brief 读取当前文档为体积序的方块状态数组（便于外部渲染）
     */
    QVector<SchematicBlockState> dumpAll() const;

    // ------------------------------------------------------------------ 保存
    /**
     * @brief 保存到指定路径（可等于原文件即就地覆盖）
     * @return true 成功
     */
    bool saveCopy(const QString &outputPath, QString *error = nullptr);

private:
    // 调色板索引（0 = air）
    int paletteIndexOf(const SchematicBlockState &state);
    // 世界坐标 -> 体积序索引；越界返回 -1
    int indexOf(int x, int y, int z) const;

    // NBT 构建辅助
    bool writeToNbt(NbtTag &root) const;   // 前置声明在下方

    bool        m_loaded = false;
    QString     m_filePath;
    QString     m_name;
    QString     m_author;
    QString     m_description;
    int         m_version = 6;
    int         m_minecraftDataVersion = 0;
    int         m_originX = 0, m_originY = 0, m_originZ = 0;
    int         m_sizeX = 0, m_sizeY = 0, m_sizeZ = 0;

    QList<SchematicBlockState> m_palette;   // 调色板
    QList<int> m_indices;                   // 体积序方块索引
};

