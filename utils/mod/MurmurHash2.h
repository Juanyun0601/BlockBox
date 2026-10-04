#ifndef MURMURHASH2_H
#define MURMURHASH2_H

#include <QtGlobal>
#include <QByteArray>

/**
 * @brief MurmurHash2 实现 — 用于 CurseForge 指纹匹配
 *
 * 算法参数：
 *   - 种子 (seed) = 1
 *   - 需要先过滤掉文件中的空白字节（0x09, 0x0A, 0x0D, 0x20）
 *
 * 参考 HMCL / PCL2 / CurseForge 官方指纹系统实现
 */
class MurmurHash2
{
public:
    /** 计算 MurmurHash2（32位），默认 seed = 1 */
    static quint32 hash(const QByteArray &data, quint32 seed = 1);

    /**
     * @brief 计算 CurseForge 兼容的指纹哈希
     *
     * 流程：
     *   1. 遍历 data，跳过 0x09(tab), 0x0A(LF), 0x0D(CR), 0x20(space)
     *   2. 对过滤后的字节计算 MurmurHash2(seed=1)
     */
    static quint32 curseforgeFingerprint(const QByteArray &data);
};

#endif // MURMURHASH2_H
