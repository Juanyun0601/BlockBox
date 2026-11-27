# 方块盒子桥接（zcode-blockbox-plugin）

把本机「方块盒子」启动器内置的**本地 MCP 服务器**接进 ZCode，让 ZCode 拥有和方块盒子「AI 助手」同一套本地能力。

装上以后，模型侧会多出这些工具（前缀 `mcp__blockbox__`）：

- 只读：`list_instances`、`list_mods`、`read_game_log`、`read_crash_report`、`analyze_crash_log`、`check_mod_updates`、`list_saves`、`read_save_info`、`list_servers`、`ping_server`、`find_structure`、`read_litematic`、`list_download_tasks`、`get_settings`、`detect_java`、`check_loader_compatibility`、`get_game_versions`
- 写操作（方块盒子侧允许后可开）：`manage_mods`、`manage_world_backup`、`launch_game`、`download_instance`、`modify_instance`、`download_resource`
- 诊断：`blockbox_status`（连接状态与排查顺序）、`blockbox_reconnect`（重读设置、重建连接）

## 怎么启用

1. 打开方块盒子 → 设置 → **AI 助手** → **本地 MCP 服务器** → 勾选「启用本地 MCP 服务器」（端口默认 17890，不需要改）。
2. ZCode 里安装本插件：
   - 本地开发：ZCode 插件市场 → 添加 → **添加插件市场** → 粘贴本仓库的市场目录路径（见下）→ 安装 `zcode-blockbox-plugin`；
   - 之后新建会话，最多等一个复检周期（默认 20 秒），`mcp__blockbox__*` 工具就会出现；不确定就让模型跑一次 `blockbox_status`。

### 本地测试市场

在插件根目录上一级放一个 `marketplace.json`（市场根指向该目录）：

```json
{
  "name": "dev-blockbox",
  "plugins": [
    {
      "name": "zcode-blockbox-plugin",
      "source": "./zcode-blockbox-plugin",
      "version": "0.1.0",
      "displayName": "BlockBox Bridge",
      "displayName_i18n": { "zh-CN": "方块盒子桥接" },
      "category": "developer-tools"
    }
  ]
}
```

把 `D:\BlockBox` 粘贴为市场根目录即可（`marketplace.json` 已随本文件提供在仓库根时无需重写）。

方块盒子关掉或 MCP 关掉时，远端工具会自动从列表里摘掉，不会留下调不通的工具；再打开会自动接回来（也可以手动 `blockbox_reconnect`）。

## 它做了什么

- 从方块盒子自己的设置里读 MCP 端口和访问令牌（Windows：注册表 `HKCU\Software\BlockBox\BlockBox\mcp`），不用手填、不用复制粘贴；也可以用插件配置覆盖。
- 探测 `http://127.0.0.1:<端口>/health`，确认方块盒子真的在跑、MCP 真的开着。
- 与方块盒子的 Streamable HTTP MCP 端点握手，缓存工具清单并透传给 ZCode；令牌变了、端口变了、写开关变了都会自动重连。
- 提供中文诊断工具，出问题时模型能自己排查。

全部在本机 `127.0.0.1` 上完成，不联外网；ZCode 与方块盒子看到的是同一个访问令牌，方块盒子的「允许写操作」开关仍然有效，写操作每次还要带 `confirm: true`。

## 配置项（一般不用改）

ZCode 插件设置里可覆盖：

| 配置 | 默认 | 说明 |
|---|---|---|
| `port` | 读方块盒子设置，退回 17890 | 覆盖 MCP 端口 |
| `token` | 读方块盒子设置 | 覆盖访问令牌（令牌轮换后不想等自动重连时用） |
| `poll_ms` | 20000 | 复检间隔（毫秒，最小 5000） |

## 排查

| 现象 | 处理 |
|---|---|
| `blockbox_status` 说端口没有监听 | 方块盒子没开，或设置里没勾「启用本地 MCP 服务器」 |
| 服务可用但工具不出现 | 跑一次 `blockbox_reconnect`；还不行就重启会话 |
| 401 / invalid token | 方块盒子里点过「重新生成」令牌：等一个复检周期自动重连，或手动 `blockbox_reconnect` |
| 写操作工具不在列表里 | 方块盒子设置里打开「允许写操作」，然后 `blockbox_reconnect` |
| 工具调用需要 `confirm` | 写操作参数里带 `"confirm": true`；删模组、恢复存档前先向用户确认 |

## 环境要求

- Windows（自动发现端口/令牌走注册表）；其它平台请在插件配置里手填 `port` 和 `token`。
- Node.js ≥ 20（`node` 在 PATH 里，本插件用它启动桥接进程）。
- 方块盒子 ≥ 2026-11-22 的版本（内置本地 MCP 服务器）。
