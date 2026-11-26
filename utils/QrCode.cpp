/**
 * @file   QrCode.cpp
 * @brief  二维码编码 / 渲染 + 扫码登录码的文本约定
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 编码器按 QR 标准（ISO/IEC 18004）实现，只做本项目需要的那一小块：
 *   - 字节模式（UTF-8 文本），纠错等级优先 M、放不下退到 L；
 *   - 版本 1-10（21x21 到 57x57），足够放下一条扫码登录地址；
 *   - 8 种掩码全试一遍，按标准罚分挑最好的一个；
 *   - 版本 ≥ 7 会写版本信息块。
 * 解码器在 QrDecode.cpp（同属 namespace QrCode）。
 */
#include "QrCode.h"

#include "QrTables.h"

#include <QByteArray>
#include <QBrush>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QUrl>
#include <QUrlQuery>

#include <climits>

namespace
{

using QrTables::EcLevel;
using QrTables::EcLow;
using QrTables::EcMedium;

/** 格式信息里纠错等级的两位编码：L = 01，M = 00 */
int formatLevelBits(EcLevel level) { return level == EcLow ? 1 : 0; }

// ────────────────────────────────────────────────────────── 里德-所罗门

/** 生成多项式（去掉最高次项后的系数），次数为 degree */
QVector<quint8> rsDivisor(int degree)
{
    QVector<quint8> result(degree, 0);
    result[degree - 1] = 1;
    quint8 root = 1;
    for (int i = 0; i < degree; ++i)
    {
        for (int j = 0; j < degree; ++j)
        {
            result[j] = QrTables::gfMultiply(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = QrTables::gfMultiply(root, 0x02);
    }
    return result;
}

/** 数据码字 → 纠错码字（多项式取余） */
QVector<quint8> rsRemainder(const QVector<quint8> &data, const QVector<quint8> &divisor)
{
    QVector<quint8> result(divisor.size(), 0);
    for (quint8 byte : data)
    {
        const quint8 factor = static_cast<quint8>(byte ^ result[0]);
        result.removeFirst();
        result.append(0);
        for (int i = 0; i < divisor.size(); ++i)
            result[i] ^= QrTables::gfMultiply(divisor[i], factor);
    }
    return result;
}

// ────────────────────────────────────────────────────────── 位流

class BitBuffer
{
public:
    void appendBits(quint32 value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
            m_bits.append(((value >> i) & 1u) != 0);
    }

    void appendByte(quint8 value) { appendBits(value, 8); }

    int size() const { return m_bits.size(); }

    /** 按 8 位一组打包成码字（末尾不足 8 位补 0） */
    QVector<quint8> toBytes() const
    {
        QVector<quint8> out((m_bits.size() + 7) / 8, 0);
        for (int i = 0; i < m_bits.size(); ++i)
        {
            if (m_bits.at(i))
                out[i >> 3] = static_cast<quint8>(out[i >> 3] | (0x80 >> (i & 7)));
        }
        return out;
    }

private:
    QVector<bool> m_bits;
};

// ────────────────────────────────────────────────────────── 掩码 / 罚分

bool maskBit(int mask, int row, int column)
{
    switch (mask)
    {
    case 0: return (row + column) % 2 == 0;
    case 1: return row % 2 == 0;
    case 2: return column % 3 == 0;
    case 3: return (row + column) % 3 == 0;
    case 4: return (row / 2 + column / 3) % 2 == 0;
    case 5: return (row * column) % 2 + (row * column) % 3 == 0;
    case 6: return ((row * column) % 2 + (row * column) % 3) % 2 == 0;
    default: return ((row + column) % 2 + (row * column) % 3) % 2 == 0;
    }
}

/** 标准罚分：连续同色、2x2 同色块、类回字形、深浅比例，用来挑最不容易误读的掩码 */
int penaltyScore(const QrCode::Matrix &matrix)
{
    const int size = matrix.size();
    int score = 0;

    // 规则 1：行 / 列里连续 5 个以上同色
    for (int row = 0; row < size; ++row)
    {
        bool current = matrix[row][0];
        int run = 1;
        for (int col = 1; col < size; ++col)
        {
            if (matrix[row][col] == current)
            {
                ++run;
                if (run == 5)
                    score += 3;
                else if (run > 5)
                    ++score;
            }
            else
            {
                current = matrix[row][col];
                run = 1;
            }
        }
    }
    for (int col = 0; col < size; ++col)
    {
        bool current = matrix[0][col];
        int run = 1;
        for (int row = 1; row < size; ++row)
        {
            if (matrix[row][col] == current)
            {
                ++run;
                if (run == 5)
                    score += 3;
                else if (run > 5)
                    ++score;
            }
            else
            {
                current = matrix[row][col];
                run = 1;
            }
        }
    }

    // 规则 2：2x2 同色块
    for (int row = 0; row + 1 < size; ++row)
    {
        for (int col = 0; col + 1 < size; ++col)
        {
            const bool value = matrix[row][col];
            if (value == matrix[row][col + 1] && value == matrix[row + 1][col]
                && value == matrix[row + 1][col + 1])
                score += 3;
        }
    }

    // 规则 3：1:1:3:1:1 这类容易和定位图形混淆的花纹
    static const int kPattern[7] = {1, 0, 1, 1, 1, 0, 1};
    const auto matches = [&matrix, size](int row, int col, bool horizontal) {
        for (int i = 0; i < 7; ++i)
        {
            const int r = horizontal ? row : row + i;
            const int c = horizontal ? col + i : col;
            if (r >= size || c >= size)
                return false;
            if (int(matrix[r][c]) != kPattern[i])
                return false;
        }
        return true;
    };
    const auto clearRun = [&matrix, size](int row, int col, bool horizontal, int length) {
        for (int i = 0; i < length; ++i)
        {
            const int r = horizontal ? row : row + i;
            const int c = horizontal ? col + i : col;
            if (r < 0 || c < 0 || r >= size || c >= size)
                return false;
            if (matrix[r][c])
                return false;
        }
        return true;
    };
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col + 7 <= size; ++col)
        {
            if (!matches(row, col, true))
                continue;
            // 前面或后面留 4 个浅色模块才算一条「像定位图形的花纹」
            if (clearRun(row, col - 4, true, 4) || clearRun(row, col + 7, true, 4))
                score += 40;
        }
    }
    for (int col = 0; col < size; ++col)
    {
        for (int row = 0; row + 7 <= size; ++row)
        {
            if (!matches(row, col, false))
                continue;
            if (clearRun(row - 4, col, false, 4) || clearRun(row + 7, col, false, 4))
                score += 40;
        }
    }

    // 规则 4：深色比例偏离 50% 的程度
    int dark = 0;
    for (const auto &row : matrix)
        for (bool value : row)
            dark += value ? 1 : 0;
    const int percent = dark * 100 / (size * size);
    score += (qAbs(percent - 50) / 5) * 10;

    return score;
}

/** 格式信息的 BCH(15,5) 编码 */
quint32 formatBits(EcLevel level, int mask)
{
    const quint32 data = (quint32(formatLevelBits(level)) << 3) | quint32(mask);
    quint32 remainder = data << 10;
    for (int i = 14; i >= 10; --i)
    {
        if ((remainder >> i) & 1u)
            remainder ^= 0x537u << (i - 10);
    }
    return ((data << 10) | remainder) ^ 0x5412u;
}

/** 版本信息的 BCH(18,6) 编码（版本 ≥ 7 才有） */
quint32 versionBits(int version)
{
    quint32 remainder = quint32(version) << 12;
    for (int i = 17; i >= 12; --i)
    {
        if ((remainder >> i) & 1u)
            remainder ^= 0x1F25u << (i - 12);
    }
    return (quint32(version) << 12) | remainder;
}

} // namespace

namespace QrCode
{

// ────────────────────────────────────────────────────────── 编码

Matrix encodeMatrix(const QString &text, QString *error)
{
    const QByteArray payload = text.toUtf8();
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return Matrix();
    };

    if (payload.isEmpty())
        return fail(QStringLiteral("内容为空"));

    // 挑一个放得下的最小版本：优先 M 级纠错，实在放不下退到 L
    const auto fits = [&payload](int version, EcLevel level) {
        const int countBits = version <= 9 ? 8 : 16;
        const int needBits = 4 + countBits + payload.size() * 8;
        return needBits <= QrTables::dataCodewords(version, level) * 8;
    };

    int version = 0;
    EcLevel level = EcMedium;
    for (int candidate = QrTables::kMinVersion; candidate <= QrTables::kMaxVersion; ++candidate)
    {
        if (fits(candidate, EcMedium))
        {
            version = candidate;
            break;
        }
    }
    if (version == 0)
    {
        level = EcLow;
        for (int candidate = QrTables::kMinVersion; candidate <= QrTables::kMaxVersion; ++candidate)
        {
            if (fits(candidate, EcLow))
            {
                version = candidate;
                break;
            }
        }
    }
    if (version == 0)
        return fail(QStringLiteral("内容太长，放不进二维码"));

    const int size = version * 4 + 17;
    const QrTables::EcBlockInfo &info = QrTables::ec(version, level);
    const int dataCapacity = QrTables::dataCodewords(version, level);

    // 数据比特：模式指示（0100 字节模式）+ 字符计数 + 内容
    BitBuffer bits;
    bits.appendBits(0x4, 4);
    bits.appendBits(quint32(payload.size()), version <= 9 ? 8 : 16);
    for (char byte : payload)
        bits.appendByte(static_cast<quint8>(byte));

    // 结束符、补零到整字节、再交替填空码字
    const int capacityBits = dataCapacity * 8;
    const int terminator = qMin(4, capacityBits - bits.size());
    if (terminator > 0)
        bits.appendBits(0, terminator);
    if (bits.size() % 8 != 0)
        bits.appendBits(0, 8 - bits.size() % 8);
    for (int pad = 0xEC; bits.size() < capacityBits; pad ^= 0xEC ^ 0x11)
        bits.appendByte(static_cast<quint8>(pad));

    const QVector<quint8> data = bits.toBytes();

    // 分块算纠错码字，再按标准交错排列
    QVector<QVector<quint8>> blocks;
    QVector<QVector<quint8>> eccBlocks;
    const QVector<quint8> divisor = rsDivisor(info.eccPerBlock);
    int offset = 0;
    const auto pushBlock = [&](int count, int blockSize) {
        for (int i = 0; i < count; ++i)
        {
            QVector<quint8> block(blockSize);
            for (int j = 0; j < blockSize; ++j)
                block[j] = data[offset + j];
            offset += blockSize;
            blocks.append(block);
            eccBlocks.append(rsRemainder(block, divisor));
        }
    };
    pushBlock(info.group1Blocks, info.group1Data);
    pushBlock(info.group2Blocks, info.group2Data);

    QVector<quint8> codewords;
    int maxData = 0;
    for (const auto &block : blocks)
        maxData = qMax(maxData, int(block.size()));
    for (int i = 0; i < maxData; ++i)
    {
        for (const auto &block : blocks)
        {
            if (i < block.size())
                codewords.append(block.at(i));
        }
    }
    for (int i = 0; i < info.eccPerBlock; ++i)
    {
        for (const auto &ecc : eccBlocks)
        {
            if (i < ecc.size())
                codewords.append(ecc.at(i));
        }
    }

    // 先把功能图形（定位、校正、时序、格式 / 版本信息区）画好
    Matrix matrix(size, QVector<bool>(size, false));
    Matrix isFunction(size, QVector<bool>(size, false));

    const auto setFunction = [&](int row, int col, bool dark) {
        if (row < 0 || col < 0 || row >= size || col >= size)
            return;
        matrix[row][col] = dark;
        isFunction[row][col] = true;
    };

    // 时序图形
    for (int i = 0; i < size; ++i)
    {
        setFunction(6, i, i % 2 == 0);
        setFunction(i, 6, i % 2 == 0);
    }

    // 三个回字形定位图形 + 分隔带
    const auto drawFinder = [&](int top, int left) {
        for (int dr = -1; dr <= 7; ++dr)
        {
            for (int dc = -1; dc <= 7; ++dc)
            {
                const int row = top + dr;
                const int col = left + dc;
                const bool inside = dr >= 0 && dr <= 6 && dc >= 0 && dc <= 6;
                bool dark = false;
                if (inside)
                {
                    const bool ring = dr == 0 || dr == 6 || dc == 0 || dc == 6;
                    const bool core = dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4;
                    dark = ring || core;
                }
                setFunction(row, col, dark);
            }
        }
    };
    drawFinder(0, 0);
    drawFinder(0, size - 7);
    drawFinder(size - 7, 0);

    // 校正图形
    const QVector<int> align = QrTables::alignmentPositions(version);
    for (int i = 0; i < align.size(); ++i)
    {
        for (int j = 0; j < align.size(); ++j)
        {
            const int row = align.at(i);
            const int col = align.at(j);
            // 三个角的校正图形和定位图形重叠，跳过
            if ((row == 6 && col == 6) || (row == 6 && col == size - 7)
                || (row == size - 7 && col == 6))
                continue;
            for (int dr = -2; dr <= 2; ++dr)
            {
                for (int dc = -2; dc <= 2; ++dc)
                    setFunction(row + dr, col + dc, qMax(qAbs(dr), qAbs(dc)) != 1);
            }
        }
    }

    // 预先占住格式信息 / 版本信息的位置（内容稍后再写）
    for (int i = 0; i <= 8; ++i)
    {
        if (i == 6)
            continue; // 第 6 行 / 列是时序图形
        isFunction[8][i] = true;
        isFunction[i][8] = true;
    }
    for (int i = 0; i < 8; ++i)
    {
        isFunction[8][size - 1 - i] = true;
        isFunction[size - 1 - i][8] = true;
    }
    if (version >= 7)
    {
        for (int i = 0; i < 18; ++i)
        {
            const int a = size - 11 + i % 3;
            const int b = i / 3;
            isFunction[b][a] = true;
            isFunction[a][b] = true;
        }
    }

    // 数据码字按「从右下角开始、两列一组蛇形往上」铺进去
    int bitIndex = 0;
    const int totalBits = codewords.size() * 8;
    for (int right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            right = 5; // 第 6 列是时序图形，跳过
        const bool upward = ((right + 1) & 2) == 0;
        for (int vert = 0; vert < size; ++vert)
        {
            const int row = upward ? size - 1 - vert : vert;
            for (int j = 0; j < 2; ++j)
            {
                const int col = right - j;
                if (isFunction[row][col])
                    continue;
                bool dark = false;
                if (bitIndex < totalBits)
                {
                    const quint8 byte = codewords.at(bitIndex >> 3);
                    dark = ((byte >> (7 - (bitIndex & 7))) & 1u) != 0;
                    ++bitIndex;
                }
                matrix[row][col] = dark;
            }
        }
    }

    // 8 种掩码都试一遍，选罚分最低的
    int bestPenalty = INT_MAX;
    Matrix best = matrix;
    const auto putFormat = [&](Matrix &target, int mask) {
        const quint32 format = formatBits(level, mask);
        for (int i = 0; i <= 5; ++i)
            target[i][8] = ((format >> i) & 1u) != 0;
        target[7][8] = ((format >> 6) & 1u) != 0;
        target[8][8] = ((format >> 7) & 1u) != 0;
        target[8][7] = ((format >> 8) & 1u) != 0;
        for (int i = 9; i < 15; ++i)
            target[8][14 - i] = ((format >> i) & 1u) != 0;
        for (int i = 0; i < 8; ++i)
            target[8][size - 1 - i] = ((format >> i) & 1u) != 0;
        for (int i = 8; i < 15; ++i)
            target[size - 15 + i][8] = ((format >> i) & 1u) != 0;
        target[size - 8][8] = true; // 恒为深色的那个模块
    };

    for (int mask = 0; mask < 8; ++mask)
    {
        Matrix candidate = matrix;
        for (int row = 0; row < size; ++row)
        {
            for (int col = 0; col < size; ++col)
            {
                if (!isFunction[row][col] && maskBit(mask, row, col))
                    candidate[row][col] = !candidate[row][col];
            }
        }
        // 罚分要把格式信息一起算进去，否则角落的花纹评不准
        putFormat(candidate, mask);
        if (version >= 7)
        {
            const quint32 versionCode = versionBits(version);
            for (int i = 0; i < 18; ++i)
            {
                const bool bit = ((versionCode >> i) & 1u) != 0;
                const int a = size - 11 + i % 3;
                candidate[i / 3][a] = bit;
                candidate[a][i / 3] = bit;
            }
        }
        const int penalty = penaltyScore(candidate);
        if (penalty < bestPenalty)
        {
            bestPenalty = penalty;
            best = candidate;
        }
    }

    if (error)
        error->clear();
    return best;
}

QImage render(const QString &text, int sizePx, const QColor &dark, const QColor &light,
              QString *error)
{
    const Matrix matrix = encodeMatrix(text, error);
    if (matrix.isEmpty())
        return QImage();

    const int modules = matrix.size();
    const int quietZone = 4; // 标准要求四周至少留 4 个模块的静区，否则相机对不上焦
    const int totalModules = modules + quietZone * 2;
    const int scale = qMax(1, sizePx / totalModules);
    const int pixelSize = totalModules * scale;

    QImage image(pixelSize, pixelSize, QImage::Format_RGB32);
    image.fill(light.isValid() ? light : Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(dark.isValid() ? dark : Qt::black);
    for (int row = 0; row < modules; ++row)
    {
        for (int col = 0; col < modules; ++col)
        {
            if (!matrix.at(row).at(col))
                continue;
            painter.drawRect((col + quietZone) * scale, (row + quietZone) * scale, scale, scale);
        }
    }
    painter.end();

    // 按整数倍画出来可能比要求的边长小一点，用最近邻放大补齐，边缘依然锐利
    if (pixelSize < sizePx && sizePx > 0)
        return image.scaled(sizePx, sizePx, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return image;
}

// ────────────────────────────────────────────────────────── 有个性的外观

namespace
{

/** 这个模块是不是三个「回」字定位图形里的一块（它们单独画成圆角方框） */
bool insideFinderArea(int row, int col, int modules)
{
    const bool topLeft = row < 7 && col < 7;
    const bool topRight = row < 7 && col >= modules - 7;
    const bool bottomLeft = row >= modules - 7 && col < 7;
    return topLeft || topRight || bottomLeft;
}

/** 画中心那个方块徽标：白色圆角底板上放官方的那枚「方块盒子」方块 */
void drawCubeEmblem(QPainter &painter, const QRectF &plate, const QColor &brand)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xFF, 0xFF, 0xFF));
    const qreal plateRadius = plate.width() * 0.26;
    painter.drawRoundedRect(plate, plateRadius, plateRadius);

    // 官方方块图（官网 assets/img/cube.png 的同款，已缩成小尺寸放进资源里）。
    // 只加载一次：二维码每次都是重画，但图始终是同一张。
    static const QPixmap cube(QStringLiteral(":/Images/blockbox_cube.png"));

    // 裁到白底板里：小方块再怎么画也不会压到底板外面的数据模块
    painter.setClipRect(plate.toRect().adjusted(1, 1, -1, -1));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!cube.isNull())
    {
        // 方块图是竖长的（260x314）：按高度缩到牌子的 84%，水平居中，四周留一圈白边
        const qreal targetHeight = plate.height() * 0.84;
        const qreal targetWidth = targetHeight * cube.width() / qreal(cube.height());
        const QRectF target(plate.center().x() - targetWidth / 2.0,
                            plate.center().y() - targetHeight / 2.0, targetWidth, targetHeight);
        painter.drawPixmap(target, cube, QRectF(cube.rect()));
        painter.restore();
        return;
    }

    // 资源没打进包时的手绘兜底：等距小方块，顶面亮、左面中间调、右面暗
    const qreal cx = plate.center().x();
    const qreal cy = plate.center().y();
    const qreal w = plate.width() * 0.40;
    const qreal h = plate.height() * 0.25;
    const QColor top = brand.lighter(128);
    const QColor left = brand;
    const QColor right = brand.darker(138);

    QPolygonF topFace;
    topFace << QPointF(cx, cy - h * 1.55) << QPointF(cx + w, cy - h * 0.78)
            << QPointF(cx, cy) << QPointF(cx - w, cy - h * 0.78);
    painter.setBrush(top);
    painter.drawPolygon(topFace);

    QPolygonF leftFace;
    leftFace << QPointF(cx - w, cy - h * 0.78) << QPointF(cx, cy) << QPointF(cx, cy + h * 1.55)
             << QPointF(cx - w, cy + h * 0.78);
    painter.setBrush(left);
    painter.drawPolygon(leftFace);

    QPolygonF rightFace;
    rightFace << QPointF(cx + w, cy - h * 0.78) << QPointF(cx, cy) << QPointF(cx, cy + h * 1.55)
              << QPointF(cx + w, cy + h * 0.78);
    painter.setBrush(right);
    painter.drawPolygon(rightFace);
    painter.restore();
}

} // namespace

QImage renderStyled(const QString &text, int sizePx, const Style &style, QString *error)
{
    const Matrix matrix = encodeMatrix(text, error);
    if (matrix.isEmpty())
        return QImage();

    const int modules = matrix.size();
    const int quietZone = 4;
    const int totalModules = modules + quietZone * 2;
    const int scale = qMax(1, sizePx / totalModules);
    const int pixelSize = totalModules * scale;

    QImage image(pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(style.light.isValid() ? style.light : QColor(0xFF, 0xFF, 0xFF));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QColor dark = style.dark.isValid() ? style.dark : QColor(0, 0, 0);
    QBrush fill = dark;
    if (style.dark2.isValid())
    {
        // 斜向渐变：左上主色 → 右下深一档，看起来像有光从左上打过来
        QLinearGradient gradient(0, 0, pixelSize, pixelSize);
        gradient.setColorAt(0.0, dark);
        gradient.setColorAt(1.0, style.dark2);
        fill = QBrush(gradient);
    }

    // 模块太小的时候圆角会把模块啃掉一块，直接退回方角最稳
    const qreal radiusRatio = scale >= 5 ? qBound<qreal>(0.0, style.moduleRadius, 0.5) : 0.0;
    const qreal moduleRadius = scale * radiusRatio;
    // 相邻的深色模块之间「挤」一点，圆角处才会连成一片而不是一颗颗的珠子；
    // 只在对面也是深色时才外扩，浅色区域（尤其定位图形外面那一圈静区）一格都不吃。
    const qreal grow = scale >= 5 ? scale * 0.16 : 0.0;
    const auto isDarkModule = [&](int row, int col) {
        return row >= 0 && col >= 0 && row < modules && col < modules && matrix.at(row).at(col)
               && !insideFinderArea(row, col, modules);
    };

    painter.setBrush(fill);
    for (int row = 0; row < modules; ++row)
    {
        for (int col = 0; col < modules; ++col)
        {
            if (!matrix.at(row).at(col) || insideFinderArea(row, col, modules))
                continue;
            const qreal left = isDarkModule(row, col - 1) ? grow : 0.0;
            const qreal right = isDarkModule(row, col + 1) ? grow : 0.0;
            const qreal top = isDarkModule(row - 1, col) ? grow : 0.0;
            const qreal bottom = isDarkModule(row + 1, col) ? grow : 0.0;
            const QRectF box((col + quietZone) * scale - left / 2,
                             (row + quietZone) * scale - top / 2, scale + (left + right) / 2,
                             scale + (top + bottom) / 2);
            if (moduleRadius > 0.01)
                painter.drawRoundedRect(box, moduleRadius, moduleRadius);
            else
                painter.drawRect(box);
        }
    }

    // 三个定位图形画成圆角方框：外框用描边、中心方块填充，比生硬的回字形更像「自家码」
    const qreal eyeRadius = scale * (scale >= 5 ? qBound<qreal>(0.0, style.eyeRadius, 0.5) : 0.0);
    const auto drawEye = [&](int originRow, int originCol) {
        const qreal x = (originCol + quietZone) * scale;
        const qreal y = (originRow + quietZone) * scale;
        const qreal size = 7.0 * scale;

        QPen ringPen(fill, scale);
        ringPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(ringPen);
        painter.setBrush(Qt::NoBrush);
        const qreal inset = scale / 2.0;
        painter.drawRoundedRect(QRectF(x + inset, y + inset, size - scale, size - scale),
                                eyeRadius, eyeRadius);

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const qreal coreSize = 3.0 * scale;
        const qreal coreX = x + (size - coreSize) / 2.0;
        const qreal coreY = y + (size - coreSize) / 2.0;
        if (scale >= 5)
            painter.drawRoundedRect(QRectF(coreX, coreY, coreSize, coreSize), eyeRadius * 0.9,
                                    eyeRadius * 0.9);
        else
            painter.drawRect(QRectF(coreX, coreY, coreSize, coreSize));
    };
    drawEye(0, 0);
    drawEye(0, modules - 7);
    drawEye(modules - 7, 0);

    // 中心徽标：只在大一点、纠错余量够的码上加，白底板把中间几格盖住，
    // 剩下的数据交给纠错码补回来（M 级大约能救回 15%）
    const int emblemModules = (scale >= 6 && modules >= 29) ? style.emblemModules : 0;
    if (emblemModules >= 3)
    {
        const int span = (emblemModules % 2 == 1) ? emblemModules : emblemModules + 1;
        const qreal plateSize = span * scale;
        const qreal centerModule = (modules - 1) / 2.0 + 0.5; // 模块坐标里的正中
        const qreal plateX = (centerModule + quietZone) * scale - plateSize / 2.0;
        const qreal plateY = plateX;
        drawCubeEmblem(painter, QRectF(plateX, plateY, plateSize, plateSize), dark);
    }
    painter.end();

    if (pixelSize < sizePx && sizePx > 0)
        return image.scaled(sizePx, sizePx, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}

// ────────────────────────────────────────────────────────── 扫码登录码的文本约定

QString makeLoginPayload(const QString &baseUrl, const QString &ticket)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.isEmpty() || ticket.trimmed().isEmpty())
        return QString();
    return QStringLiteral("%1/qrlogin.html?t=%2").arg(base, ticket.trimmed());
}

QString ticketFromPayload(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QString();

    // 票据只允许 URL 安全字符，长度与服务端对齐（16-128）
    const auto validTicket = [](const QString &value) {
        if (value.size() < 16 || value.size() > 128)
            return QString();
        for (const QChar ch : value)
        {
            const ushort code = ch.unicode();
            const bool ok = (code >= '0' && code <= '9') || (code >= 'a' && code <= 'z')
                            || (code >= 'A' && code <= 'Z') || code == '-' || code == '_';
            if (!ok)
                return QString();
        }
        return value;
    };

    // 自定义协议：blockbox://qrlogin?t=xxx（手工输入的码也走这里）
    if (trimmed.startsWith(QStringLiteral("blockbox://"), Qt::CaseInsensitive))
    {
        const QUrl url(trimmed);
        return validTicket(QUrlQuery(url).queryItemValue(QStringLiteral("t")));
    }

    // 标准形态：<论坛地址>/qrlogin.html?t=xxx
    const int queryPos = trimmed.indexOf(QLatin1Char('?'));
    if (queryPos < 0)
        return validTicket(trimmed); // 也允许直接粘贴票据本身
    const QString head = trimmed.left(queryPos);
    if (!head.contains(QStringLiteral("qrlogin"), Qt::CaseInsensitive))
        return QString();
    return validTicket(QUrlQuery(trimmed.mid(queryPos + 1)).queryItemValue(QStringLiteral("t")));
}

bool isLoginPayload(const QString &text)
{
    return !ticketFromPayload(text).isEmpty();
}

QString decodeFile(const QString &path)
{
    const QImage image(path);
    if (image.isNull())
        return QString();
    return decode(image);
}

} // namespace QrCode
