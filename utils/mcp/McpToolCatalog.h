/**
 * @file   McpToolCatalog.h
 * @brief  本地 MCP 服务器对外开放的工具目录
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 工具名 / 参数说明直接复用 AI 工作模式的 resources/ai_agent_tools.json，
 * 这里只维护「哪些工具对外开放」以及读写属性，避免两套工具描述逐渐跑偏。
 */
#ifndef BLOCKBOX_MCP_TOOL_CATALOG_H
#define BLOCKBOX_MCP_TOOL_CATALOG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace McpToolCatalog {

/** 工具是否在对外开放清单内 */
bool isKnown(const QString &name);

/** 工具是否写操作（需要「允许写操作」开关 + confirm=true） */
bool isWrite(const QString &name);

/** 是否带破坏性（删除模组、覆盖存档等），用于 MCP annotations */
bool isDestructive(const QString &name);

/**
 * @brief 生成 MCP tools/list 的 tools 数组
 * @param allowWrite 是否包含写操作工具；关闭时只返回只读工具
 */
QJsonArray tools(bool allowWrite);

/** 生成客户端 initialize 时展示的能力摘要（中文一句话） */
QString summary(bool allowWrite);

} // namespace McpToolCatalog

#endif // BLOCKBOX_MCP_TOOL_CATALOG_H
