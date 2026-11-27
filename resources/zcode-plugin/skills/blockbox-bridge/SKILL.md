---
name: blockbox-bridge
description: 方块盒子（Minecraft 启动器）已通过 mcp__blockbox__* 工具接入 ZCode。用户提到方块盒子、Minecraft 实例/模组/客户端/存档/服务器/崩溃日志/启动游戏，或问"能不能看我方块盒子里…""帮我装个模组/备份存档/分析崩溃"时使用。提供实例、模组、日志、崩溃分析、存档、服务器、结构坐标、资源下载等本地工具。
---

# 方块盒子桥接使用说明

本机的「方块盒子」启动器通过本地 MCP 接入，工具前缀为 `mcp__blockbox__`。

## 何时用

用户提到方块盒子、Minecraft 实例/模组/客户端/存档/服务器时，优先用这些工具，别让用户自己开软件翻菜单。

## 工具速览

**诊断（先跑这两个再干活）**

| 工具 | 作用 |
|---|---|
| `blockbox_status` | 方块盒子是否在跑、令牌是否读到、接入了多少工具、排查顺序 |
| `blockbox_reconnect` | 方块盒子里刚开/关 MCP、改端口、重生成令牌后重建连接 |

**只读（默认可用）**

`list_instances` 实例列表、`list_mods` 模组清单、`read_game_log` 游戏日志、`read_crash_report` / `analyze_crash_log` 崩溃报告与分析、`check_mod_updates` 模组更新（联网较慢）、`list_saves` / `read_save_info` 存档与详情（种子/坐标/游戏模式）、`list_servers` / `ping_server` 服务器列表与在线状态、`find_structure` 按种子算结构坐标、`read_litematic` 投影文件材料清单、`list_download_tasks` 下载任务、`get_settings` 启动器配置、`detect_java` Java 环境、`check_loader_compatibility` 加载器兼容性、`get_game_versions` 游戏版本清单。

**写操作（需方块盒子设置开启「允许写操作」，且每次必须带 `"confirm": true`）**

`manage_mods` 启停/删除模组、`manage_world_backup` 备份/恢复存档、`launch_game` 启动游戏、`download_instance` 下载新实例、`modify_instance` 换版本/加载器、`download_resource` 下载资源到实例。

## 规则

- 写操作不带 `confirm: true` 时只会返回确认提示、不会执行——这是预期行为，需要真实执行就带上它。
- 删除模组、恢复存档属于高风险操作：先向用户确认，再带 `confirm: true` 执行。
- `launch_game` 会真的启动游戏；`download_instance` / `modify_instance` 会产生下载与文件改动，执行前告诉用户。

## 排查

工具没出现、调用报连接失败时：先 `blockbox_status`，看方块盒子是否在运行、本地 MCP 是否已启用；状态正常但工具缺失就 `blockbox_reconnect`；再不行让用户在方块盒子「设置 → AI 助手 → 本地 MCP 服务器」里确认开关、端口与令牌。
