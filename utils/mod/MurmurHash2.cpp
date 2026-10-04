#include "MurmurHash2.h"

#include <QDebug>

// "murmur" 混合常量
#define MURMUR_MULTIPLIER 0x5bd1e995
#define MURMUR_ROTATE     24

static inline quint32 murmurRotateRight(quint32 x, quint32 r)
{
    return (x >> r) | (x << (32 - r));
}

quint32 MurmurHash2::hash(const QByteArray &data, quint32 seed)
{
    quint32 len = data.size();
    // 对齐到 4 字节
    const quint32 alignedLen = len & 0xfffffffc;
    const quint8 *blocks = reinterpret_cast<const quint8*>(data.constData());

    quint32 h = seed ^ len;

    // 每次处理 4 字节
    for (quint32 i = 0; i < alignedLen; i += 4)
    {
        quint32 k;
        // Little-endian 读取 4 字节
        k = static_cast<quint32>(blocks[i])
          | (static_cast<quint32>(blocks[i + 1]) << 8)
          | (static_cast<quint32>(blocks[i + 2]) << 16)
          | (static_cast<quint32>(blocks[i + 3]) << 24);

        k *= MURMUR_MULTIPLIER;
        k ^= murmurRotateRight(k, MURMUR_ROTATE);
        k *= MURMUR_MULTIPLIER;

        h *= MURMUR_MULTIPLIER;
        h ^= k;
    }

    // 处理尾部剩余字节
    const quint8 *tail = blocks + alignedLen;
    switch (len & 3)
    {
    case 3:
        h ^= static_cast<quint32>(tail[2]) << 16;
        Q_FALLTHROUGH();
    case 2:
        h ^= static_cast<quint32>(tail[1]) << 8;
        Q_FALLTHROUGH();
    case 1:
        h ^= static_cast<quint32>(tail[0]);
        h *= MURMUR_MULTIPLIER;
    }

    // 最终混淆
    h ^= murmurRotateRight(h, 13);
    h *= MURMUR_MULTIPLIER;
    h ^= murmurRotateRight(h, 15);

    return h;
}

quint32 MurmurHash2::curseforgeFingerprint(const QByteArray &data)
{
    // 过滤空白字节：跳过 0x09(tab), 0x0A(LF), 0x0D(CR), 0x20(space)
    QByteArray filtered;
    filtered.reserve(data.size());

    for (int i = 0; i < data.size(); ++i)
    {
        const char byte = data.at(i);
        if (byte != 0x09 && byte != 0x0A && byte != 0x0D && byte != 0x20)
            filtered.append(byte);
    }

    return hash(filtered, 1);
}
