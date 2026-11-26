/**
 * @file   QrDecode.cpp
 * @brief  二维码解码：从相机取景图 / 相册图片里读回文本
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 解码流程（中文注释标在各步骤上）：
 *   1. 灰度化 → 分块自适应二值化（手机上拍屏常有明暗不均，全局阈值会瞎）；
 *   2. 按行扫 1:1:3:1:1 的游程找回字形，再竖向、横向交叉校验 → 定位图形；
 *   3. 从候选里挑出「直角等边」的三个，靠叉积判断哪边是上、哪边是左；
 *   4. 用三个定位图形 + 右下角校正图形（找不到就用平行四边形估）求透视变换；
 *   5. 反变换回来逐格采样、读格式信息（BCH 容错）、去掩码、按蛇形取码字；
 *   6. 分块做里德-所罗门纠错（Berlekamp-Massey + Chien + 解线性方程求错值）；
 *   7. 按模式（数字 / 字母数字 / 字节）拼回文本。
 * 只支持版本 1-10、纠错等级 L / M —— 本项目自己生成的登录二维码都落在这个范围。
 */
#include "QrCode.h"

#include "QrTables.h"

#include <QImage>
#include <QPointF>
#include <QVector>

#include <cmath>

namespace QrCode
{
namespace
{

// ────────────────────────────────────────────────────────── 二值图

struct BinaryImage
{
    int width = 0;
    int height = 0;
    QVector<quint8> pixels; ///< 1 = 深色

    bool valid() const { return width > 0 && height > 0 && pixels.size() == width * height; }

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    bool dark(int x, int y) const
    {
        if (!inside(x, y))
            return false;
        return pixels.at(y * width + x) != 0;
    }

    bool darkAtPoint(const QPointF &point) const
    {
        return dark(int(std::floor(point.x())), int(std::floor(point.y())));
    }
};

/** 灰度化 + 限制长边：太大的照片先缩一下，省时间也减少噪点 */
QVector<quint8> toGray(const QImage &image, int *outWidth, int *outHeight)
{
    QImage source = image;
    if (source.format() != QImage::Format_Grayscale8)
        source = source.convertToFormat(QImage::Format_Grayscale8);
    const int kMaxSide = 1600;
    if (qMax(source.width(), source.height()) > kMaxSide)
        source = source.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    *outWidth = source.width();
    *outHeight = source.height();
    QVector<quint8> gray(source.width() * source.height());
    for (int y = 0; y < source.height(); ++y)
    {
        const uchar *line = source.constScanLine(y);
        for (int x = 0; x < source.width(); ++x)
            gray[y * source.width() + x] = line[x];
    }
    return gray;
}

/** 分块自适应二值化：对比度足够的块用自己的均值，低对比度的块借周围可靠块的阈值 */
BinaryImage binarizeAdaptive(const QVector<quint8> &gray, int width, int height)
{
    BinaryImage result;
    if (gray.isEmpty())
        return BinaryImage{};
    result.width = width;
    result.height = height;
    result.pixels = QVector<quint8>(width * height, 0);

    const int kBlock = 8;
    const int blocksX = (width + kBlock - 1) / kBlock;
    const int blocksY = (height + kBlock - 1) / kBlock;
    const int kMinContrast = 12;

    QVector<int> thresholds(blocksX * blocksY, 0);
    QVector<bool> reliable(blocksX * blocksY, false);
    qint64 globalSum = 0;
    qint64 globalCount = 0;

    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            const int x0 = bx * kBlock;
            const int y0 = by * kBlock;
            const int x1 = qMin(x0 + kBlock, width);
            const int y1 = qMin(y0 + kBlock, height);
            int minValue = 255;
            int maxValue = 0;
            qint64 sum = 0;
            int count = 0;
            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const int value = gray.at(y * width + x);
                    minValue = qMin(minValue, value);
                    maxValue = qMax(maxValue, value);
                    sum += value;
                    ++count;
                }
            }
            if (count == 0)
                continue;
            globalSum += sum;
            globalCount += count;
            const int index = by * blocksX + bx;
            if (maxValue - minValue > kMinContrast)
            {
                thresholds[index] = int(sum / count);
                reliable[index] = true;
            }
        }
    }

    const int globalAverage = globalCount > 0 ? int(globalSum / globalCount) : 128;
    QVector<int> finalThresholds(blocksX * blocksY, globalAverage);
    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            const int index = by * blocksX + bx;
            if (reliable.at(index))
            {
                finalThresholds[index] = thresholds.at(index);
                continue;
            }
            qint64 sum = 0;
            int count = 0;
            for (int dy = -2; dy <= 2; ++dy)
            {
                for (int dx = -2; dx <= 2; ++dx)
                {
                    const int nx = bx + dx;
                    const int ny = by + dy;
                    if (nx < 0 || ny < 0 || nx >= blocksX || ny >= blocksY)
                        continue;
                    const int neighbour = ny * blocksX + nx;
                    if (!reliable.at(neighbour))
                        continue;
                    sum += thresholds.at(neighbour);
                    ++count;
                }
            }
            finalThresholds[index] = count > 0 ? int(sum / count) : globalAverage;
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const int by = y / kBlock;
        for (int x = 0; x < width; ++x)
        {
            const int bx = x / kBlock;
            const int threshold = finalThresholds.at(by * blocksX + bx);
            result.pixels[y * width + x] = gray.at(y * width + x) < threshold ? 1 : 0;
        }
    }
    return result;
}

/** 全局 Otsu 阈值二值化：自适应阈值在整张小图 / 均匀背景上偶尔会失败，用它兜底 */
BinaryImage binarizeOtsu(const QVector<quint8> &gray, int width, int height)
{
    BinaryImage result;
    if (gray.isEmpty())
        return BinaryImage{};
    result.width = width;
    result.height = height;
    result.pixels = QVector<quint8>(width * height, 0);

    int histogram[256] = {};
    for (quint8 value : gray)
        ++histogram[value];

    const qint64 total = gray.size();
    qint64 sum = 0;
    for (int i = 0; i < 256; ++i)
        sum += qint64(i) * histogram[i];

    qint64 sumBackground = 0;
    qint64 weightBackground = 0;
    double bestVariance = -1;
    int threshold = 128;
    for (int i = 0; i < 256; ++i)
    {
        weightBackground += histogram[i];
        if (weightBackground == 0)
            continue;
        const qint64 weightForeground = total - weightBackground;
        if (weightForeground == 0)
            break;
        sumBackground += qint64(i) * histogram[i];
        const double meanBackground = double(sumBackground) / double(weightBackground);
        const double meanForeground = double(sum - sumBackground) / double(weightForeground);
        const double diff = meanBackground - meanForeground;
        const double variance = double(weightBackground) * double(weightForeground) * diff * diff;
        if (variance > bestVariance)
        {
            bestVariance = variance;
            threshold = i;
        }
    }

    for (int i = 0; i < gray.size(); ++i)
        result.pixels[i] = gray.at(i) <= threshold ? 1 : 0;
    return result;
}

// ────────────────────────────────────────────────────────── 定位图形

struct FinderPattern
{
    double x = 0;
    double y = 0;
    double module = 0;
};

/** 一段连续同色像素 */
struct Run
{
    int start = 0;
    int length = 0;
    bool dark = false;
};

bool ratioMatches(int length, double expected, double tolerance)
{
    if (expected <= 0)
        return false;
    return length >= expected * (1.0 - tolerance) && length <= expected * (1.0 + tolerance);
}

/**
 * 竖向交叉校验：以候选点为起点，向上、向下分别数「深 / 浅 / 深」三段，
 * 期望是 1.5 : 1 : 1 个模块（中心三个模块各占一半），借此确认这确实是回字形。
 */
bool crossCheckVertical(const BinaryImage &bin, int centerX, int centerY, double module,
                        double *centerYOut, double *moduleOut)
{
    int up = 0;
    int upLight = 0;
    int upDark = 0;
    int down = 0;
    int downLight = 0;
    int downDark = 0;

    int y = centerY;
    while (bin.dark(centerX, y))
    {
        ++up;
        --y;
    }
    while (bin.inside(centerX, y) && !bin.dark(centerX, y))
    {
        ++upLight;
        --y;
    }
    while (bin.dark(centerX, y))
    {
        ++upDark;
        --y;
    }

    y = centerY + 1;
    while (bin.dark(centerX, y))
    {
        ++down;
        ++y;
    }
    while (bin.inside(centerX, y) && !bin.dark(centerX, y))
    {
        ++downLight;
        ++y;
    }
    while (bin.dark(centerX, y))
    {
        ++downDark;
        ++y;
    }

    if (up == 0 || down == 0 || upLight == 0 || downLight == 0 || upDark == 0 || downDark == 0)
        return false;

    const double centralRun = double(up + down);
    if (!ratioMatches(int(centralRun), module * 3.0, 0.6))
        return false;
    if (!ratioMatches(upLight, module, 0.7) || !ratioMatches(downLight, module, 0.7))
        return false;
    if (!ratioMatches(upDark, module, 0.8) || !ratioMatches(downDark, module, 0.8))
        return false;

    const double verticalModule = (centralRun + upLight + downLight + upDark + downDark) / 7.0;
    if (qAbs(verticalModule - module) > module * 0.7)
        return false;

    *centerYOut = double(centerY) - double(up) + centralRun / 2.0;
    *moduleOut = (module + verticalModule) / 2.0;
    return true;
}

/** 横向交叉校验（同竖向），顺便把中心横坐标修正到亚像素 */
bool crossCheckHorizontal(const BinaryImage &bin, int centerX, int centerY, double module,
                          double *centerXOut, double *moduleOut)
{
    int left = 0;
    int leftLight = 0;
    int leftDark = 0;
    int right = 0;
    int rightLight = 0;
    int rightDark = 0;

    int x = centerX;
    while (bin.dark(x, centerY))
    {
        ++left;
        --x;
    }
    while (bin.inside(x, centerY) && !bin.dark(x, centerY))
    {
        ++leftLight;
        --x;
    }
    while (bin.dark(x, centerY))
    {
        ++leftDark;
        --x;
    }

    x = centerX + 1;
    while (bin.dark(x, centerY))
    {
        ++right;
        ++x;
    }
    while (bin.inside(x, centerY) && !bin.dark(x, centerY))
    {
        ++rightLight;
        ++x;
    }
    while (bin.dark(x, centerY))
    {
        ++rightDark;
        ++x;
    }

    if (left == 0 || right == 0 || leftLight == 0 || rightLight == 0 || leftDark == 0
        || rightDark == 0)
        return false;

    const double centralRun = double(left + right);
    if (!ratioMatches(int(centralRun), module * 3.0, 0.6))
        return false;
    if (!ratioMatches(leftLight, module, 0.7) || !ratioMatches(rightLight, module, 0.7))
        return false;
    if (!ratioMatches(leftDark, module, 0.8) || !ratioMatches(rightDark, module, 0.8))
        return false;

    const double horizontalModule = (centralRun + leftLight + rightLight + leftDark + rightDark) / 7.0;
    if (qAbs(horizontalModule - module) > module * 0.7)
        return false;

    *centerXOut = double(centerX) - double(left) + centralRun / 2.0;
    *moduleOut = (module + horizontalModule) / 2.0;
    return true;
}

/** 扫全图找候选定位图形 */
QVector<FinderPattern> findFinders(const BinaryImage &bin)
{
    QVector<FinderPattern> found;
    if (!bin.valid())
        return found;

    for (int y = 0; y < bin.height; ++y)
    {
        QVector<Run> runs;
        int x = 0;
        while (x < bin.width)
        {
            const bool dark = bin.dark(x, y);
            const int start = x;
            while (x < bin.width && bin.dark(x, y) == dark)
                ++x;
            runs.append({start, x - start, dark});
        }

        for (int i = 0; i + 4 < runs.size(); ++i)
        {
            if (!runs.at(i).dark || runs.at(i + 1).dark || !runs.at(i + 2).dark
                || runs.at(i + 3).dark || !runs.at(i + 4).dark)
                continue;

            const int total = runs.at(i).length + runs.at(i + 1).length + runs.at(i + 2).length
                              + runs.at(i + 3).length + runs.at(i + 4).length;
            if (total < 7)
                continue; // 太小了，多半是噪点

            const double module = total / 7.0;
            if (!ratioMatches(runs.at(i).length, module, 0.7)
                || !ratioMatches(runs.at(i + 1).length, module, 0.7)
                || !ratioMatches(runs.at(i + 2).length, module * 3.0, 0.6)
                || !ratioMatches(runs.at(i + 3).length, module, 0.7)
                || !ratioMatches(runs.at(i + 4).length, module, 0.7))
                continue;

            const int centerX = runs.at(i + 2).start + runs.at(i + 2).length / 2;
            double centerY = y;
            double verticalModule = module;
            if (!crossCheckVertical(bin, centerX, y, module, &centerY, &verticalModule))
                continue;

            double finalX = centerX;
            double finalModule = verticalModule;
            if (!crossCheckHorizontal(bin, centerX, int(centerY + 0.5), module, &finalX, &finalModule))
                continue;

            // 同一条定位图形会被很多行扫到，靠得近就合并
            bool merged = false;
            for (FinderPattern &existing : found)
            {
                if (qAbs(existing.x - finalX) < finalModule * 2.0
                    && qAbs(existing.y - centerY) < finalModule * 2.0)
                {
                    existing.x = (existing.x + finalX) / 2.0;
                    existing.y = (existing.y + centerY) / 2.0;
                    existing.module = (existing.module + finalModule) / 2.0;
                    merged = true;
                    break;
                }
            }
            if (merged)
                continue;
            found.append({finalX, centerY, finalModule});
        }
    }
    return found;
}

double distanceBetween(const FinderPattern &a, const FinderPattern &b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

/** 一组「左上 / 右上 / 左下」定位图形 */
struct FinderTriple
{
    FinderPattern topLeft;
    FinderPattern topRight;
    FinderPattern bottomLeft;
    double score = 0; ///< 两条直角边之和，越大说明越可能是真的 QR
};

/**
 * 从候选里挑出所有「直角等边」的三元组，按可信度从高到低排。
 *
 * 为什么不是只挑最好的一个：数据区里长得像回字的花纹也会被当成定位图形，
 * 偶尔能拼出一个几何上说得通、其实错位的三角形。一个一个试，配合后面的
 * 「采样完再核对三个角」的检查，错的会在很早期就被否掉。
 */
QVector<FinderTriple> pickFinderTriples(const QVector<FinderPattern> &patterns, int maxCount)
{
    QVector<FinderTriple> result;
    const int count = patterns.size();
    if (count < 3 || maxCount <= 0)
        return result;

    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            for (int k = j + 1; k < count; ++k)
            {
                const FinderPattern &a = patterns.at(i);
                const FinderPattern &b = patterns.at(j);
                const FinderPattern &c = patterns.at(k);

                // 模块尺寸要相近，否则多半是把别的图案凑进来了
                const double maxModule = qMax(a.module, qMax(b.module, c.module));
                const double minModule = qMin(a.module, qMin(b.module, c.module));
                if (maxModule > minModule * 1.4)
                    continue;

                const double ab = distanceBetween(a, b);
                const double ac = distanceBetween(a, c);
                const double bc = distanceBetween(b, c);

                // 最长的边是斜边，它的对角就是左上角
                int corner = -1;
                double leg1 = 0;
                double leg2 = 0;
                double hypotenuse = 0;
                if (ab >= ac && ab >= bc)
                {
                    corner = k;
                    leg1 = ac;
                    leg2 = bc;
                    hypotenuse = ab;
                }
                else if (ac >= ab && ac >= bc)
                {
                    corner = j;
                    leg1 = ab;
                    leg2 = bc;
                    hypotenuse = ac;
                }
                else
                {
                    corner = i;
                    leg1 = ab;
                    leg2 = ac;
                    hypotenuse = bc;
                }
                if (corner < 0 || hypotenuse <= 0 || leg1 <= 0 || leg2 <= 0)
                    continue;

                // 两条直角边方向会被透视压缩得不一样长，所以只要求「大致等长 + 直角」，
                // 斜边再核一下（口径放松一点，错的组合后面采样时会被否掉）
                if (qAbs(leg1 - leg2) > qMax(leg1, leg2) * 0.5)
                    continue;
                if (qAbs(std::sqrt(leg1 * leg1 + leg2 * leg2) - hypotenuse) > hypotenuse * 0.35)
                    continue;

                const FinderPattern &originProbe = patterns.at(corner);
                const FinderPattern &firstProbe = patterns.at(corner == i ? j : i);
                const FinderPattern &secondProbe = patterns.at(corner == k ? j : k);
                const double ux = firstProbe.x - originProbe.x;
                const double uy = firstProbe.y - originProbe.y;
                const double vx = secondProbe.x - originProbe.x;
                const double vy = secondProbe.y - originProbe.y;
                if (qAbs(ux * vx + uy * vy) / (leg1 * leg2) > 0.4)
                    continue; // 不是直角

                const FinderPattern *first = nullptr;
                const FinderPattern *second = nullptr;
                if (corner == i)
                {
                    first = &patterns.at(j);
                    second = &patterns.at(k);
                }
                else if (corner == j)
                {
                    first = &patterns.at(i);
                    second = &patterns.at(k);
                }
                else
                {
                    first = &patterns.at(i);
                    second = &patterns.at(j);
                }

                // 叉积 > 0 说明 first 在右边、second 在下边（图片坐标系 y 向下）
                const double cross = (first->x - originProbe.x) * (second->y - originProbe.y)
                                     - (first->y - originProbe.y) * (second->x - originProbe.x);
                const FinderPattern *right = cross > 0 ? first : second;
                const FinderPattern *below = cross > 0 ? second : first;

                FinderTriple triple;
                triple.topLeft = originProbe;
                triple.topLeft.module = (originProbe.module + right->module + below->module) / 3.0;
                triple.topRight = *right;
                triple.bottomLeft = *below;
                triple.score = leg1 + leg2;
                result.append(triple);
            }
        }
    }

    std::sort(result.begin(), result.end(),
              [](const FinderTriple &left, const FinderTriple &right) {
                  return left.score > right.score;
              });
    if (result.size() > maxCount)
        result.resize(maxCount);
    return result;
}

// ────────────────────────────────────────────────────────── 透视变换

/** 四对点求 3x3 单应矩阵（h[2][2] 固定为 1） */
bool solveHomography(const QVector<QPointF> &source, const QVector<QPointF> &target, double h[3][3])
{
    if (source.size() != 4 || target.size() != 4)
        return false;

    double matrix[8][9] = {};
    for (int i = 0; i < 4; ++i)
    {
        const double u = source.at(i).x();
        const double v = source.at(i).y();
        const double x = target.at(i).x();
        const double y = target.at(i).y();

        double *row0 = matrix[i * 2];
        row0[0] = u;
        row0[1] = v;
        row0[2] = 1;
        row0[6] = -u * x;
        row0[7] = -v * x;
        row0[8] = x;

        double *row1 = matrix[i * 2 + 1];
        row1[3] = u;
        row1[4] = v;
        row1[5] = 1;
        row1[6] = -u * y;
        row1[7] = -v * y;
        row1[8] = y;
    }

    for (int col = 0; col < 8; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row)
        {
            if (qAbs(matrix[row][col]) > qAbs(matrix[pivot][col]))
                pivot = row;
        }
        if (qAbs(matrix[pivot][col]) < 1e-9)
            return false;
        if (pivot != col)
        {
            for (int k = 0; k < 9; ++k)
                std::swap(matrix[col][k], matrix[pivot][k]);
        }
        const double divisor = matrix[col][col];
        for (int k = col; k < 9; ++k)
            matrix[col][k] /= divisor;
        for (int row = 0; row < 8; ++row)
        {
            if (row == col)
                continue;
            const double factor = matrix[row][col];
            if (qAbs(factor) < 1e-12)
                continue;
            for (int k = col; k < 9; ++k)
                matrix[row][k] -= factor * matrix[col][k];
        }
    }

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            h[i][j] = 0;
    for (int i = 0; i < 8; ++i)
        h[i / 3][i % 3] = matrix[i][8];
    h[2][2] = 1.0;
    return true;
}

QPointF applyHomography(const double h[3][3], double u, double v)
{
    const double denominator = h[2][0] * u + h[2][1] * v + h[2][2];
    if (qAbs(denominator) < 1e-9)
        return QPointF(-1, -1);
    return QPointF((h[0][0] * u + h[0][1] * v + h[0][2]) / denominator,
                   (h[1][0] * u + h[1][1] * v + h[1][2]) / denominator);
}

/**
 * 找校正图形候选：斜着拍屏幕时，「三个回字 + 平行四边形」推出来的右下角能差十来个模块，
 * 所以这里给一个大范围搜索，把范围内所有「5x5 小回字」都捞出来，按离预测点由近到远排队，
 * 由上层的解码尝试逐个去试。
 */
QVector<QPointF> findAlignmentCandidates(const BinaryImage &bin, const QPointF &predicted,
                                         double module, int maxCount)
{
    QVector<QPointF> result;
    if (predicted.x() < 0 || predicted.y() < 0 || module < 1.0)
        return result;

    const int searchRadius = qMax(6, int(module * 18));
    const int left = qMax(0, int(predicted.x()) - searchRadius);
    const int right = qMin(bin.width - 1, int(predicted.x()) + searchRadius);
    const int top = qMax(0, int(predicted.y()) - searchRadius);
    const int bottom = qMin(bin.height - 1, int(predicted.y()) + searchRadius);
    if (right - left < 3 || bottom - top < 3)
        return result;

    // 校正图形是「深 5x5 外框 + 浅 3x3 + 深中心」，只按 1:1:1:1:1 找会撞上数据区里的随机花纹，
    // 所以候选点还要把周围 5x5 的结构核一遍。
    const auto looksLikeAlignment = [&](const QPointF &point) {
        int mismatches = 0;
        for (int dr = -2; dr <= 2; ++dr)
        {
            for (int dc = -2; dc <= 2; ++dc)
            {
                const bool expected = qMax(qAbs(dr), qAbs(dc)) != 1;
                const QPointF sample(point.x() + dc * module, point.y() + dr * module);
                if (bin.darkAtPoint(sample) != expected)
                    ++mismatches;
            }
        }
        return mismatches <= 4;
    };

    struct Candidate
    {
        double distance = 0;
        QPointF point;
    };
    QVector<Candidate> candidates;

    for (int y = top; y <= bottom; ++y)
    {
        QVector<Run> runs;
        int x = left;
        while (x <= right)
        {
            const bool dark = bin.dark(x, y);
            const int start = x;
            while (x <= right && bin.dark(x, y) == dark)
                ++x;
            runs.append({start, x - start, dark});
        }
        for (int i = 0; i + 4 < runs.size(); ++i)
        {
            if (!runs.at(i).dark || runs.at(i + 1).dark || !runs.at(i + 2).dark
                || runs.at(i + 3).dark || !runs.at(i + 4).dark)
                continue;
            bool ok = true;
            for (int k = 0; k < 5 && ok; ++k)
            {
                if (!ratioMatches(runs.at(i + k).length, module, 0.6))
                    ok = false;
            }
            if (!ok)
                continue;

            const double centerX = runs.at(i + 2).start + runs.at(i + 2).length / 2.0;
            const QPointF candidate(centerX, double(y));
            if (!looksLikeAlignment(candidate))
                continue;

            bool merged = false;
            for (Candidate &existing : candidates)
            {
                if (std::hypot(existing.point.x() - candidate.x(),
                               existing.point.y() - candidate.y())
                    < module * 1.5)
                {
                    existing.point = QPointF((existing.point.x() + candidate.x()) / 2.0,
                                             (existing.point.y() + candidate.y()) / 2.0);
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                Candidate entry;
                entry.point = candidate;
                entry.distance = std::hypot(candidate.x() - predicted.x(),
                                            candidate.y() - predicted.y());
                candidates.append(entry);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.distance < b.distance; });
    for (const Candidate &candidate : candidates)
    {
        if (result.size() >= maxCount)
            break;
        result.append(candidate.point);
    }
    return result;
}

/** 采样结果里三个角是不是还像回字形（不像就说明透视方向估歪了，别浪费时间做纠错） */
bool findersLookRight(const Matrix &matrix)
{
    const int size = matrix.size();
    const auto count = [&](int top, int left) {
        int mismatches = 0;
        for (int dr = 0; dr <= 6; ++dr)
        {
            for (int dc = 0; dc <= 6; ++dc)
            {
                const bool expected = (dr == 0 || dr == 6 || dc == 0 || dc == 6)
                                      || (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4);
                if (matrix.at(top + dr).at(left + dc) != expected)
                    ++mismatches;
            }
        }
        return mismatches;
    };
    const int first = count(0, 0);
    const int second = count(0, size - 7);
    const int third = count(size - 7, 0);
    return first <= 6 && second <= 6 && third <= 6;
}

// ────────────────────────────────────────────────────────── 读码

/** 格式信息的 BCH(15,5) 编码（levelBits：L=01 / M=00 / Q=11 / H=10） */
quint32 formatCode(int levelBits, int mask)
{
    const quint32 data = (quint32(levelBits) << 3) | quint32(mask);
    quint32 remainder = data << 10;
    for (int i = 14; i >= 10; --i)
    {
        if ((remainder >> i) & 1u)
            remainder ^= 0x537u << (i - 10);
    }
    return ((data << 10) | remainder) ^ 0x5412u;
}

int popCount(quint32 value)
{
    int count = 0;
    while (value)
    {
        count += int(value & 1u);
        value >>= 1;
    }
    return count;
}

/** 读格式信息：两处副本一起看，允许最多 3 位出错 */
bool readFormatInfo(const Matrix &matrix, QrTables::EcLevel *level, int *mask)
{
    const int size = matrix.size();
    if (size < 21)
        return false;
    quint32 copy1 = 0;
    quint32 copy2 = 0;
    for (int i = 0; i <= 5; ++i)
    {
        if (matrix.at(i).at(8))
            copy1 |= 1u << i;
    }
    if (matrix.at(7).at(8))
        copy1 |= 1u << 6;
    if (matrix.at(8).at(8))
        copy1 |= 1u << 7;
    if (matrix.at(8).at(7))
        copy1 |= 1u << 8;
    for (int i = 9; i < 15; ++i)
    {
        if (matrix.at(8).at(14 - i))
            copy1 |= 1u << i;
    }
    for (int i = 0; i < 8; ++i)
    {
        if (matrix.at(8).at(size - 1 - i))
            copy2 |= 1u << i;
    }
    for (int i = 8; i < 15; ++i)
    {
        if (matrix.at(size - 15 + i).at(8))
            copy2 |= 1u << i;
    }

    int bestLevel = -1;
    int bestMask = 0;
    int bestDistance = 99;
    for (int levelBits = 0; levelBits < 4; ++levelBits)
    {
        for (int candidateMask = 0; candidateMask < 8; ++candidateMask)
        {
            const quint32 code = formatCode(levelBits, candidateMask);
            const int d = qMin(popCount(code ^ copy1), popCount(code ^ copy2));
            if (d < bestDistance)
            {
                bestDistance = d;
                bestLevel = levelBits;
                bestMask = candidateMask;
            }
        }
    }
    if (bestLevel < 0 || bestDistance > 3)
        return false;

    // 只认得 L / M 两档（本项目自己生成的码只会用这两档）
    if (bestLevel == 1)
        *level = QrTables::EcLow;
    else if (bestLevel == 0)
        *level = QrTables::EcMedium;
    else
        return false;

    *mask = bestMask;
    return true;
}

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

/** 标出功能区（定位、校正、时序、格式 / 版本信息），这些位置不参与取数据 */
Matrix buildFunctionMap(int version)
{
    const int size = version * 4 + 17;
    Matrix map(size, QVector<bool>(size, false));
    const auto mark = [&](int row, int col) {
        if (row >= 0 && col >= 0 && row < size && col < size)
            map[row][col] = true;
    };

    for (int i = 0; i < size; ++i)
    {
        mark(6, i);
        mark(i, 6);
    }
    const auto markFinder = [&](int top, int left) {
        for (int dr = -1; dr <= 7; ++dr)
            for (int dc = -1; dc <= 7; ++dc)
                mark(top + dr, left + dc);
    };
    markFinder(0, 0);
    markFinder(0, size - 7);
    markFinder(size - 7, 0);

    const QVector<int> align = QrTables::alignmentPositions(version);
    for (int i = 0; i < align.size(); ++i)
    {
        for (int j = 0; j < align.size(); ++j)
        {
            const int row = align.at(i);
            const int col = align.at(j);
            if ((row == 6 && col == 6) || (row == 6 && col == size - 7)
                || (row == size - 7 && col == 6))
                continue;
            for (int dr = -2; dr <= 2; ++dr)
                for (int dc = -2; dc <= 2; ++dc)
                    mark(row + dr, col + dc);
        }
    }

    for (int i = 0; i <= 8; ++i)
    {
        if (i == 6)
            continue;
        mark(8, i);
        mark(i, 8);
    }
    for (int i = 0; i < 8; ++i)
    {
        mark(8, size - 1 - i);
        mark(size - 1 - i, 8);
    }
    if (version >= 7)
    {
        for (int i = 0; i < 18; ++i)
        {
            const int a = size - 11 + i % 3;
            const int b = i / 3;
            mark(b, a);
            mark(a, b);
        }
    }
    return map;
}

/** 里德-所罗门纠错：原地修好 codewords，返回是否成功 */
bool reedSolomonDecode(QVector<quint8> *codewords, int eccCount)
{
    using namespace QrTables;
    const int count = codewords->size();
    if (eccCount <= 0 || count <= eccCount)
        return false;

    // 综合多项式：S_j = Σ r_i · α^(j·(count-1-i))
    const auto syndromesMatch = [&](QVector<quint8> *out) {
        out->resize(eccCount);
        bool clean = true;
        for (int j = 0; j < eccCount; ++j)
        {
            quint8 sum = 0;
            for (int i = 0; i < count; ++i)
            {
                if (codewords->at(i) == 0)
                    continue;
                sum ^= gfMultiply(codewords->at(i), gfPower(0x02, j * (count - 1 - i)));
            }
            (*out)[j] = sum;
            if (sum != 0)
                clean = false;
        }
        return clean;
    };

    QVector<quint8> syndromes;
    if (syndromesMatch(&syndromes))
        return true; // 没错，直接用

    // Berlekamp-Massey：求错误位置多项式
    QVector<quint8> locator(1, 1);
    QVector<quint8> previous(1, 1);
    int locatorDegree = 0;
    int shift = 1;
    quint8 lastDiscrepancy = 1;

    for (int step = 0; step < eccCount; ++step)
    {
        quint8 discrepancy = syndromes.at(step);
        for (int i = 1; i <= locatorDegree; ++i)
        {
            if (i < locator.size())
                discrepancy ^= gfMultiply(locator.at(i), syndromes.at(step - i));
        }
        if (discrepancy == 0)
        {
            ++shift;
            continue;
        }
        const QVector<quint8> saved = locator;
        const quint8 factor = gfDivide(discrepancy, lastDiscrepancy);
        if (locator.size() < previous.size() + shift)
            locator.resize(previous.size() + shift, 0);
        for (int i = 0; i < previous.size(); ++i)
            locator[i + shift] ^= gfMultiply(factor, previous.at(i));
        if (2 * locatorDegree <= step)
        {
            locatorDegree = step + 1 - locatorDegree;
            previous = saved;
            lastDiscrepancy = discrepancy;
            shift = 1;
        }
        else
        {
            ++shift;
        }
    }

    if (locatorDegree <= 0 || locatorDegree > eccCount / 2)
        return false;

    // Chien 搜索：找出错在哪几个码字上
    QVector<int> errorPositions;
    QVector<quint8> errorLocators;
    for (int position = 0; position < count; ++position)
    {
        const quint8 x = gfPower(0x02, count - 1 - position);
        const quint8 xInverse = gfDivide(1, x);
        quint8 value = 0;
        quint8 power = 1;
        for (int i = 0; i < locator.size(); ++i)
        {
            value ^= gfMultiply(locator.at(i), power);
            power = gfMultiply(power, xInverse);
        }
        if (value == 0)
        {
            errorPositions.append(position);
            errorLocators.append(x);
        }
    }
    if (errorPositions.size() != locatorDegree || errorPositions.isEmpty())
        return false;

    // 解线性方程求每个错误位置上的错值：Σ e_k · X_k^j = S_j
    const int errorCount = errorPositions.size();
    QVector<QVector<quint8>> equations(errorCount, QVector<quint8>(errorCount + 1, 0));
    for (int row = 0; row < errorCount; ++row)
    {
        for (int col = 0; col < errorCount; ++col)
            equations[row][col] = gfPower(errorLocators.at(col), row);
        equations[row][errorCount] = syndromes.at(row);
    }

    for (int col = 0; col < errorCount; ++col)
    {
        int pivot = col;
        while (pivot < errorCount && equations.at(pivot).at(col) == 0)
            ++pivot;
        if (pivot == errorCount)
            return false;
        if (pivot != col)
            equations.swapItemsAt(pivot, col);

        const quint8 divisor = equations.at(col).at(col);
        for (int k = col; k <= errorCount; ++k)
            equations[col][k] = gfDivide(equations.at(col).at(k), divisor);

        for (int row = 0; row < errorCount; ++row)
        {
            if (row == col)
                continue;
            const quint8 factor = equations.at(row).at(col);
            if (factor == 0)
                continue;
            for (int k = col; k <= errorCount; ++k)
                equations[row][k] ^= gfMultiply(factor, equations.at(col).at(k));
        }
    }

    for (int i = 0; i < errorCount; ++i)
        (*codewords)[errorPositions.at(i)] ^= equations.at(i).at(errorCount);

    // 修完再验一遍，全零才算真的修好了
    QVector<quint8> check;
    return syndromesMatch(&check);
}

/** 从码字里读出文本（支持数字 / 字母数字 / 字节三种模式） */
QString parsePayload(const QVector<quint8> &data, int version)
{
    QByteArray bytes;
    QString text;
    int bitIndex = 0;
    const int totalBits = data.size() * 8;
    const auto readBits = [&](int count) -> int {
        if (count <= 0 || bitIndex + count > totalBits)
        {
            bitIndex = totalBits;
            return -1;
        }
        int value = 0;
        for (int i = 0; i < count; ++i)
        {
            const quint8 byte = data.at(bitIndex >> 3);
            value = (value << 1) | int((byte >> (7 - (bitIndex & 7))) & 1u);
            ++bitIndex;
        }
        return value;
    };

    static const char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    const bool largeVersion = version >= 10;

    while (bitIndex + 4 <= totalBits)
    {
        const int mode = readBits(4);
        if (mode < 0 || mode == 0)
            break; // 0000 = 结束符
        if (mode == 0x4) // 字节模式
        {
            const int length = readBits(largeVersion ? 16 : 8);
            if (length < 0)
                return QString();
            for (int i = 0; i < length; ++i)
            {
                const int value = readBits(8);
                if (value < 0)
                    return QString();
                bytes.append(char(value));
            }
            continue;
        }
        if (mode == 0x2) // 字母数字模式
        {
            const int length = readBits(largeVersion ? 11 : 9);
            if (length < 0)
                return QString();
            for (int i = 0; i < length;)
            {
                if (i + 1 < length)
                {
                    const int value = readBits(11);
                    if (value < 0 || value >= 45 * 45)
                        return QString();
                    text.append(QLatin1Char(kAlnum[value / 45]));
                    text.append(QLatin1Char(kAlnum[value % 45]));
                    i += 2;
                }
                else
                {
                    const int value = readBits(6);
                    if (value < 0 || value >= 45)
                        return QString();
                    text.append(QLatin1Char(kAlnum[value]));
                    ++i;
                }
            }
            continue;
        }
        if (mode == 0x1) // 数字模式
        {
            const int length = readBits(largeVersion ? 12 : 10);
            if (length < 0)
                return QString();
            for (int i = 0; i < length;)
            {
                const int remain = length - i;
                const int digits = remain >= 3 ? 3 : remain;
                const int chunkBits = digits == 3 ? 10 : (digits == 2 ? 7 : 4);
                const int value = readBits(chunkBits);
                if (value < 0)
                    return QString();
                QString part = QString::number(value);
                while (part.size() < digits)
                    part.prepend(QLatin1Char('0'));
                text.append(part);
                i += digits;
            }
            continue;
        }
        return QString(); // 汉字等模式本项目用不到
    }

    if (!bytes.isEmpty())
        return QString::fromUtf8(bytes);
    return text;
}

/** 用单应矩阵把点阵采下来（每格取中心 + 四个邻点投票） */
Matrix sampleMatrix(const BinaryImage &bin, const double h[3][3], int size, double module)
{
    Matrix matrix(size, QVector<bool>(size, false));
    const double offset = qMax(0.35, module * 0.22);
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            const QPointF center = applyHomography(h, col + 0.5, row + 0.5);
            if (center.x() < 0 || center.y() < 0 || center.x() >= bin.width
                || center.y() >= bin.height)
                return Matrix();

            const QPointF samples[5] = {
                center,
                QPointF(center.x() - offset, center.y()),
                QPointF(center.x() + offset, center.y()),
                QPointF(center.x(), center.y() - offset),
                QPointF(center.x(), center.y() + offset),
            };
            int darkVotes = 0;
            for (const QPointF &point : samples)
            {
                if (bin.darkAtPoint(point))
                    ++darkVotes;
            }
            matrix[row][col] = darkVotes >= 3;
        }
    }
    return matrix;
}

QString decodeFromBinary(const BinaryImage &bin)
{
    if (!bin.valid())
        return QString();

    const QVector<FinderPattern> patterns = findFinders(bin);
    if (patterns.size() < 3)
        return QString();

    // 最多试三组「回字三元组」：数据区里长得像回字的花纹偶尔能拼出一个几何说得通的
    // 三角形，采样之后核对三个角就会被识破，换下一组重来即可。
    const QVector<FinderTriple> triples = pickFinderTriples(patterns, 3);

    for (const FinderTriple &triple : triples)
    {
        const FinderPattern &topLeft = triple.topLeft;
        const FinderPattern &topRight = triple.topRight;
        const FinderPattern &bottomLeft = triple.bottomLeft;

        const double horizontal = distanceBetween(topLeft, topRight);
        const double vertical = distanceBetween(topLeft, bottomLeft);
        const double module = (topLeft.module + topRight.module + bottomLeft.module) / 3.0;
        const double averageSpan = (horizontal + vertical) / 2.0;
        if (module <= 0.5 || averageSpan < module * 12)
            continue; // 太小了，拍糊了

        // 两个回字中心相隔「尺寸 - 7」个模块，反推尺寸后吸附到 4k+1
        int estimatedSize = int(std::lround(averageSpan / module)) + 7;
        estimatedSize = int(std::lround((estimatedSize - 17) / 4.0)) * 4 + 17;

        QVector<int> sizes = {estimatedSize, estimatedSize + 4, estimatedSize - 4};
        sizes.append(estimatedSize + 8);
        sizes.append(estimatedSize - 8);

        for (int size : sizes)
        {
            if (size < 21 || size > 57)
                continue;
            const int version = (size - 17) / 4;
            const double refinedModule = averageSpan / double(size - 7);

            QVector<QPointF> baseSource;
            QVector<QPointF> baseTarget;
            baseSource << QPointF(3.5, 3.5) << QPointF(size - 3.5, 3.5)
                       << QPointF(3.5, size - 3.5);
            baseTarget << QPointF(topLeft.x, topLeft.y) << QPointF(topRight.x, topRight.y)
                       << QPointF(bottomLeft.x, bottomLeft.y);

            /**
             * 按给定的右下角做一次完整解码尝试。
             * 右下角有两种来源：校正图形中心（更准，版本 ≥ 2 才有）和平行四边形估出来的角；
             * 两种都试一遍，一个不行就换另一个。
             */
            const auto attempt = [&](const QPointF &corner, double cornerModule) -> QString {
                QVector<QPointF> source = baseSource;
                QVector<QPointF> target = baseTarget;
                source << QPointF(cornerModule, cornerModule);
                target << corner;

                double homography[3][3];
                if (!solveHomography(source, target, homography))
                    return QString();

                const Matrix sampled = sampleMatrix(bin, homography, size, refinedModule);
                if (sampled.isEmpty() || !findersLookRight(sampled))
                    return QString();

                QrTables::EcLevel level = QrTables::EcMedium;
                int mask = 0;
                if (!readFormatInfo(sampled, &level, &mask))
                    return QString();

                // 去掩码（只动数据模块）
                const Matrix functions = buildFunctionMap(version);
                Matrix unmasked = sampled;
                for (int row = 0; row < size; ++row)
                {
                    for (int col = 0; col < size; ++col)
                    {
                        if (!functions.at(row).at(col) && maskBit(mask, row, col))
                            unmasked[row][col] = !sampled.at(row).at(col);
                    }
                }

                // 按蛇形顺序取码字（和编码时同一套走法）
                const int totalCodewords = QrTables::totalCodewords(version);
                QVector<quint8> stream;
                stream.reserve(totalCodewords);
                int currentByte = 0;
                int bitCount = 0;
                for (int right = size - 1; right >= 1; right -= 2)
                {
                    if (right == 6)
                        right = 5;
                    const bool upward = ((right + 1) & 2) == 0;
                    for (int vert = 0; vert < size; ++vert)
                    {
                        const int row = upward ? size - 1 - vert : vert;
                        for (int j = 0; j < 2; ++j)
                        {
                            const int col = right - j;
                            if (functions.at(row).at(col))
                                continue;
                            currentByte = (currentByte << 1) | (unmasked.at(row).at(col) ? 1 : 0);
                            if (++bitCount == 8)
                            {
                                stream.append(static_cast<quint8>(currentByte));
                                currentByte = 0;
                                bitCount = 0;
                            }
                        }
                    }
                }
                while (stream.size() > totalCodewords)
                    stream.removeLast();
                if (stream.size() != totalCodewords)
                    return QString();

                // 分块：数据码字逐块轮转交错，后面跟同样交错排列的纠错码字
                const QrTables::EcBlockInfo &info = QrTables::ec(version, level);
                const int blockCount = info.group1Blocks + info.group2Blocks;
                if (blockCount <= 0)
                    return QString();
                QVector<QVector<quint8>> dataBlocks(blockCount);
                QVector<QVector<quint8>> eccBlocks(blockCount, QVector<quint8>(info.eccPerBlock, 0));
                int maxData = 0;
                for (int b = 0; b < blockCount; ++b)
                {
                    const int dataSize = b < info.group1Blocks ? info.group1Data : info.group2Data;
                    dataBlocks[b] = QVector<quint8>(dataSize, 0);
                    maxData = qMax(maxData, dataSize);
                }

                int index = 0;
                for (int i = 0; i < maxData; ++i)
                {
                    for (int b = 0; b < blockCount; ++b)
                    {
                        if (i < dataBlocks.at(b).size())
                            dataBlocks[b][i] = stream.at(index++);
                    }
                }
                for (int i = 0; i < info.eccPerBlock; ++i)
                {
                    for (int b = 0; b < blockCount; ++b)
                        eccBlocks[b][i] = stream.at(index++);
                }
                if (index != stream.size())
                    return QString();

                QVector<quint8> payload;
                for (int b = 0; b < blockCount; ++b)
                {
                    QVector<quint8> block = dataBlocks.at(b);
                    block += eccBlocks.at(b);
                    if (!reedSolomonDecode(&block, info.eccPerBlock))
                        return QString();
                    for (int i = 0; i < dataBlocks.at(b).size(); ++i)
                        payload.append(block.at(i));
                }

                return parsePayload(payload, version);
            };

            // 右下角候选一：平行四边形估出来的角（版本 1 只有这一种）
            const QPointF parallelogram(topRight.x + bottomLeft.x - topLeft.x,
                                        topRight.y + bottomLeft.y - topLeft.y);
            QVector<QPointF> corners;
            QVector<double> cornerModules;
            if (version >= 2)
            {
                QVector<QPointF> roughSource = baseSource;
                QVector<QPointF> roughTarget = baseTarget;
                roughSource << QPointF(size - 3.5, size - 3.5);
                roughTarget << parallelogram;
                double rough[3][3];
                if (solveHomography(roughSource, roughTarget, rough))
                {
                    const QPointF predicted = applyHomography(rough, size - 6.5, size - 6.5);
                    const QVector<QPointF> alignments =
                        findAlignmentCandidates(bin, predicted, refinedModule, 4);
                    for (const QPointF &center : alignments)
                    {
                        corners.append(center);
                        cornerModules.append(size - 6.5);
                    }
                }
            }
            corners.append(parallelogram);
            cornerModules.append(size - 3.5);

            for (int i = 0; i < corners.size(); ++i)
            {
                const QString text = attempt(corners.at(i), cornerModules.at(i));
                if (!text.isEmpty())
                    return text;
            }
        }
    }
    return QString();
}

} // namespace

QString decode(const QImage &image)
{
    if (image.isNull())
        return QString();

    int width = 0;
    int height = 0;
    const QVector<quint8> gray = toGray(image, &width, &height);
    if (gray.isEmpty())
        return QString();

    // 自适应阈值先试；不行再按整图 Otsu 阈值来一次（拍屏幕、纯色背景常这样救回来）
    QString text = decodeFromBinary(binarizeAdaptive(gray, width, height));
    if (!text.isEmpty())
        return text;
    text = decodeFromBinary(binarizeOtsu(gray, width, height));
    if (!text.isEmpty())
        return text;

    // 小图（截图里嵌的小二维码、缩略图）一个模块只剩一两个像素，先放大三倍再认一次
    if (qMax(width, height) < 360)
    {
        const QImage enlarged = image.scaled(image.width() * 3, image.height() * 3,
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        int enlargedWidth = 0;
        int enlargedHeight = 0;
        const QVector<quint8> enlargedGray = toGray(enlarged, &enlargedWidth, &enlargedHeight);
        if (!enlargedGray.isEmpty())
        {
            text = decodeFromBinary(binarizeAdaptive(enlargedGray, enlargedWidth, enlargedHeight));
            if (!text.isEmpty())
                return text;
            text = decodeFromBinary(binarizeOtsu(enlargedGray, enlargedWidth, enlargedHeight));
        }
    }
    return text;
}

} // namespace QrCode
