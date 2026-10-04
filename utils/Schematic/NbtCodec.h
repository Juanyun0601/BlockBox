/**
 * @file   NbtCodec.h
 * @brief  通用 NBT（Named Binary Tag）编解码模块
 * @author BlockBox Team
 * @date   2026-08-02
 *
 * 提供与 Minecraft NBT 二进制格式兼容的通用解析与序列化能力，
 * 供 LitematicEditor（投影文件读写/格式转换）等模块复用。
 *
 * 本模块自包含、无状态、线程安全。NBT 标签均按大端序读写。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

/**
 * @brief NBT 标签类型 ID（NBT 官方规范）
 */
enum class NbtType : quint8
{
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12
};

/**
 * @brief NBT 标签节点（完整保留所有类型载荷，支持无损重写）
 */
struct NbtTag
{
    NbtType type = NbtType::End;
    QString name;                          ///< 标签名（Compound 子标签 / 根标签）
    qint64 intValue = 0;                   ///< Byte / Short / Int / Long 的统一 64 位值
    double doubleValue = 0.0;              ///< Float / Double 值
    QString stringValue;                   ///< String 值
    QByteArray byteArray;                  ///< ByteArray 值
    QList<qint32> intArray;                ///< IntArray 值
    QList<qint64> longArray;               ///< LongArray 值
    NbtType listElementType = NbtType::End; ///< List 元素类型
    QList<NbtTag> children;                ///< Compound 子标签或 List 元素
};

/**
 * @brief NBT 编解码静态工具集合
 */
namespace NbtCodec {

/**
 * @brief 解析 NBT 二进制数据（含根标签的类型字节与名称）
 * @param data 原始二进制（大端序，根标签为 Compound）
 * @return 解析成功返回根 NbtTag，失败返回 std::nullopt
 */
std::optional<NbtTag> parse(const QByteArray &data);

/**
 * @brief 序列化 NBT 根标签为二进制
 * @param root 根标签（应为 Compound）
 * @return 序列化结果；失败返回空 QByteArray
 */
QByteArray serialize(const NbtTag &root);

/**
 * @brief gzip 压缩（含 gzip 头与校验和）
 */
QByteArray gzipCompress(const QByteArray &raw, bool *ok = nullptr);

/**
 * @brief gzip 解压（自动识别 gzip 头）
 */
QByteArray gzipDecompress(const QByteArray &compressed, bool *ok = nullptr);

/**
 * @brief 读取 gzip 压缩的 NBT 文件（.litematic / .schematic / .schem）
 * @return 成功返回 true，root 为解析结果
 */
bool readCompressedFile(const QString &path, NbtTag &root);

/**
 * @brief 将 NBT 根标签 gzip 压缩后原子写入文件
 * @return 成功返回 true
 */
bool writeCompressedFile(const QString &path, const NbtTag &root);

/**
 * @brief 在 Compound 中按名称查找子标签（非 const 版本，返回可写指针）
 */
NbtTag *findChild(NbtTag &compound, const QString &name);

/**
 * @brief 在 Compound 中按名称查找子标签（const 版本）
 */
const NbtTag *findChild(const NbtTag &compound, const QString &name);

/**
 * @brief 读取 Compound 子标签的整数值（Byte/Short/Int/Long），失败返回默认值
 */
qint64 readIntValue(const NbtTag &compound, const QString &name, qint64 defaultValue = 0);

/**
 * @brief 读取 Compound 子标签的字符串值，失败返回默认值
 */
QString readStringValue(const NbtTag &compound, const QString &name,
                        const QString &defaultValue = QString());

// ---- 标签构造辅助 ----
NbtTag makeCompound(const QString &name = QString());
NbtTag makeString(const QString &name, const QString &value);
NbtTag makeInt(const QString &name, qint32 value);
NbtTag makeShort(const QString &name, qint16 value);
NbtTag makeLong(const QString &name, qint64 value);
NbtTag makeByte(const QString &name, qint8 value);
NbtTag makeByteArray(const QString &name, const QByteArray &value);
NbtTag makeIntArray(const QString &name, const QList<qint32> &value);
NbtTag makeLongArray(const QString &name, const QList<qint64> &value);
NbtTag makeList(const QString &name, NbtType elementType, const QList<NbtTag> &items);

} // namespace NbtCodec
