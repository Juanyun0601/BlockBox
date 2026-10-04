/**
 * @file   CommandInjector.h
 * @brief  Minecraft 指令提取与一键注入工具
 * @author BlockBox Team
 * @date   2026-09-14
 *
 * 提供两类能力：
 *   1. extractCommands()：从 AI 回答（Markdown）中识别可执行的 Minecraft 指令，
 *      供 AI 聊天页渲染「复制 / 一键注入」卡片。
 *   2. injectToGame()：将单条指令注入到运行中的 Minecraft 游戏窗口
 *      （查找 javaw.exe 进程 → 置前窗口 → 模拟 / + Ctrl+V + Enter）。
 */

#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Minecraft 指令提取与注入工具类
 *
 * 所有方法均为静态方法，无需实例化。注入相关能力仅在 Windows 平台可用，
 * 非 Windows 平台调用 injectToGame() 会返回 false 并给出说明。
 */
class CommandInjector
{
public:
    /**
     * @brief 从 Markdown 文本中提取可执行的 Minecraft 指令
     * @param markdown AI 回答原文（Markdown）
     * @return 去重后的指令列表，每条均以 '/' 开头；无指令时返回空列表
     *
     * 识别规则：
     *   - 标记为 mc / minecraft / mcfunctions / commands 的代码块：块内每一非空行
     *     都视为指令（自动补 '/'，跳过以 // 或 # 开头的注释行）。
     *   - 其他代码块与正文：以 '/' 开头且首个单词是已知 Minecraft 指令名的行。
     *   - 正文中的行内代码 `` `/xxx` ``：同样按已知指令名校验。
     */
    static QStringList extractCommands(const QString &markdown);

    /**
     * @brief 将一条指令注入到运行中的 Minecraft 游戏窗口
     * @param command      要注入的指令（可带或不带前导 '/'）
     * @param errorMessage 可选输出参数：失败原因（中文，可直接展示给用户）
     * @return 成功注入返回 true；未找到游戏 / 不支持平台等返回 false
     *
     * 注入流程：定位 javaw.exe 进程 → 查找其可见窗口并置前 → 复制指令正文到剪贴板
     * → 模拟 '/' 打开聊天框 → 模拟 Ctrl+V 粘贴 → 模拟 Enter 执行。
     */
    static bool injectToGame(const QString &command, QString *errorMessage = nullptr);

    /**
     * @brief 检测当前是否有运行中的 Minecraft（javaw.exe）进程
     * @return 存在返回 true（非 Windows 平台恒为 false）
     */
    static bool isGameRunning();
};
