/**
 * @file   MemoryAllocator.h
 * @brief  自动内存分配器 - 检测系统内存并计算推荐游戏内存分配值
 * @author BlockBox Team
 * @date   2026-06-19
 *
 * 参考开源项目:
 *   - HMCL (Hello Minecraft! Launcher): 按总内存比例分配, 默认 40%
 *   - MultiMC / Prism Launcher: 总内存的 50%, 上限 8GB, 下限 1GB
 *   - PCL2: 总内存的 40%, 上限 8GB, 512MB 对齐
 */

#pragma once

#include <cstdint>

/**
 * @brief 内存分配模式
 *
 * 参考 HMCL / PCL2 的多档位分配策略:
 *   - 普通: 保守分配，为系统预留更多内存
 *   - 优化: 均衡分配，兼顾游戏性能与系统稳定性
 *   - 极致: 激进分配，最大化游戏可用内存
 */
enum class MemoryAllocationMode
{
    Normal = 0,    // 普通: 总内存的 40%
    Optimized = 1, // 优化: 总内存的 50%
    Extreme = 2    // 极致: 总内存的 60%
};

/**
 * @brief 内存分配建议结果
 */
struct MemoryAllocation
{
    int minMemoryMb = 1024;   // 推荐最小内存 (MB)
    int maxMemoryMb = 4096;   // 推荐最大内存 (MB)
    int totalSystemMemoryMb = 0;   // 系统总内存 (MB)
    int availableMemoryMb = 0;     // 系统可用内存 (MB)
    MemoryAllocationMode mode = MemoryAllocationMode::Optimized; // 当前分配模式
};

/**
 * @brief 自动内存分配器
 *
 * 根据系统总内存和可用内存，计算推荐的 Minecraft 内存分配值。
 * 支持三种分配模式: 普通、优化、极致。
 * 线程安全。
 */
class MemoryAllocator
{
public:
    /**
     * @brief 获取单例实例
     * @return MemoryAllocator 单例指针
     */
    static MemoryAllocator* instance();

    /**
     * @brief 检测系统总内存 (MB)
     * @return 系统总物理内存，单位 MB
     */
    static int detectTotalSystemMemory();

    /**
     * @brief 检测系统可用内存 (MB)
     * @return 系统当前可用物理内存，单位 MB
     */
    static int detectAvailableMemory();

    /**
     * @brief 计算推荐的内存分配
     *
     * 根据系统总内存和分配模式，计算推荐的 Minecraft 内存分配值。
     *
     * 各模式分配策略:
     *
     *   【普通】总内存比例 40%，安全上限 70%:
     *     - < 4GB:   512MB ~ min(1536MB, 总内存×40%)
     *     - 4~8GB:   1024MB ~ min(3072MB, 总内存×40%)
     *     - 8~16GB:  1024MB ~ min(6144MB, 总内存×40%)
     *     - > 16GB:  1024MB ~ min(8192MB, 总内存×40%)
     *
     *   【优化】总内存比例 50%，安全上限 75%:
     *     - < 4GB:   512MB ~ min(2048MB, 总内存×50%)
     *     - 4~8GB:   1024MB ~ min(4096MB, 总内存×50%)
     *     - 8~16GB:  1024MB ~ min(8192MB, 总内存×50%)
     *     - > 16GB:  1024MB ~ min(12288MB, 总内存×50%)
     *
     *   【极致】总内存比例 60%，安全上限 80%:
     *     - < 4GB:   512MB ~ min(2048MB, 总内存×60%)
     *     - 4~8GB:   1024MB ~ min(4096MB, 总内存×60%)
     *     - 8~16GB:  1024MB ~ min(8192MB, 总内存×60%)
     *     - > 16GB:  1024MB ~ min(16384MB, 总内存×60%)
     *
     *   - 值以 512MB 为单位对齐
     *
     * @param mode 分配模式
     * @return 内存分配建议
     */
    MemoryAllocation calculateRecommendedAllocation(
        MemoryAllocationMode mode = MemoryAllocationMode::Optimized);

    /**
     * @brief 将内存值对齐到指定步长
     * @param valueMb 原始内存值 (MB)
     * @param stepMb  对齐步长 (MB)，默认 512
     * @return 对齐后的内存值
     */
    static int alignToStep(int valueMb, int stepMb = 512);

    /**
     * @brief 获取模式对应的显示名称
     * @param mode 分配模式
     * @return 中文名称
     */
    static const char* modeDisplayName(MemoryAllocationMode mode);

private:
    MemoryAllocator() = default;
    ~MemoryAllocator() = default;

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

    static MemoryAllocator* m_instance;
};