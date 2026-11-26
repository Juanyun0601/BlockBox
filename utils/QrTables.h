/**
 * @file   QrTables.h
 * @brief  二维码标准表（版本 / 纠错分块 / 校正图形）与 GF(256) 运算
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 编码器（QrCode.cpp）和解码器（QrDecode.cpp）都要用这几张表，
 * 放在一处避免两边写岔。只覆盖本项目用得到的版本 1-10、纠错等级 L / M。
 */
#pragma once

#include <QVector>
#include <QtGlobal>

namespace QrTables
{

/** 一个纠错等级下某个版本的分块方案 */
struct EcBlockInfo
{
    int eccPerBlock = 0;   ///< 每块纠错码字数
    int group1Blocks = 0;  ///< 第一组块数
    int group1Data = 0;    ///< 第一组每块数据码字数
    int group2Blocks = 0;  ///< 第二组块数（0 表示没有）
    int group2Data = 0;    ///< 第二组每块数据码字数
};

enum EcLevel
{
    EcLow = 0,   ///< L，约 7% 恢复能力
    EcMedium = 1 ///< M，约 15% 恢复能力
};

/** 支持的版本范围：1-10（21x21 ~ 57x57） */
constexpr int kMinVersion = 1;
constexpr int kMaxVersion = 10;

/** 纠错等级 L 的分块方案，下标为版本号 */
const EcBlockInfo &ecLow(int version);

/** 纠错等级 M 的分块方案，下标为版本号 */
const EcBlockInfo &ecMedium(int version);

/** 按等级取分块方案（只支持 L / M） */
const EcBlockInfo &ec(int version, EcLevel level);

/** 该版本的总码字数（数据 + 纠错） */
int totalCodewords(int version);

/** 该版本该等级的数据码字数 */
int dataCodewords(int version, EcLevel level);

/** 该版本的校正图形中心坐标（版本 1 为空） */
QVector<int> alignmentPositions(int version);

/** GF(256) 乘法 / 除法 / 幂（本原多项式 0x11D） */
quint8 gfMultiply(quint8 a, quint8 b);
quint8 gfDivide(quint8 a, quint8 b);
quint8 gfPower(quint8 a, int exponent);

} // namespace QrTables
