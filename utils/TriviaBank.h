/**
 * @file   TriviaBank.h
 * @brief  我的世界冷知识库：供加载界面「你知道吗」栏目随机展示
 * @author BlockBox Team
 * @date   2026-11-15
 */
#ifndef TRIVIABANK_H
#define TRIVIABANK_H

#include <QString>

/**
 * @brief 我的世界冷知识库
 *
 * 冷知识资源位于 :/resources/trivia/mc_trivia.txt，每行一条，
 * 内容与 我的世界400条冷知识.md 一致。懒加载并缓存，
 * randomTrivia() 随机抽取一条，且避免与上一条重复。
 */
namespace TriviaBank
{
    /// 随机取一条冷知识（第一次调用时解析资源文件并缓存）
    QString randomTrivia();

    /// 冷知识总条数（资源不可用时为 0）
    int count();

    /// 加载资源并缓存（可提前调用，界面首次展示时无需等待）
    void loadFromResource();
}

#endif // TRIVIABANK_H
