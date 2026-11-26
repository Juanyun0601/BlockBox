# 方块盒子桥接（dsh-blockbox-bridge）

把本机「方块盒子」启动器内置的**本地 MCP 服务器**接进 DeepSeek Harness，让 DSH 拥有和方块盒子「AI 助手」同一套本地能力。

装上以后，模型侧会多出这些工具：

- 只读：`list_instances`、`list_mods`、`read_game_log`、`read_crash_report`、`analyze_crash_log`、`check_mod_updates`、`list_saves`、`read_save_info`、`list_servers`、`ping_server`、`find_structure`、`read_litematic`、`list_download_tasks`、`get_settings`、`detect_java`、`check_loader_compatibility`、`get_game_versions`
- 写操作（方块盒子侧允许后可开）：`manage_mods`、`manage_world_backup`、`launch_game`、`download_instance`、`modify_instance`、`download_resource`

工具名统一为 `mcp__blockbox__<工具名>`，另外插件自己提供两个诊断工具：

| 工具 | 作用 |
|---|---|
| `blockbox_status` | 端口 / 令牌 / 方块盒子是否在跑 / 服务端工具数 / 已接入工具数 / 排查顺序 |
| `blockbox_reconnect` | 重新读设置、重新探测、重建连接 |

## 怎么启用

1. 打开方块盒子 → 设置 → **AI 助手** → **本地 MCP 服务器** → 勾选「启用本地 MCP 服务器」（端口默认 17890，不需要改）。
2. DSH 里装好本插件（插件 → 添加插件 → 选这个目录），**重启一次 DSH** 让插件加载。
3. 最多 20 秒，`mcp__blockbox__*` 工具就出现在对话里；不确定就让模型跑一次 `blockbox_status`。

方块盒子关掉或 MCP 关掉时，这些工具会自动从列表里摘掉，不会留下调不通的工具；再打开会自动接回来（也可以手动 `blockbox_reconnect`）。

### 方块盒子内的「一键安装」

方块盒子 → 设置 → AI 助手 → **DeepSeek Harness 插件** → 「一键安装」可以直接装：它把这份插件释放到
`<方块盒子数据目录>/dsh-plugin/blockbox-bridge/`，再写进本机的 DSH 配置（依赖 + 启用清单），重启 DSH 即可。

所以改完这个目录后，记得把同一份文件同步到 `C-BlockBox/resources/dsh-plugin/`（方块盒子打包时把它嵌进安装包）；
两边不一致时，软件里「一键安装」装出来的会是旧版本。

## 它做了什么

- 从方块盒子自己的设置里读 MCP 端口和访问令牌（Windows：注册表 `HKCU\Software\BlockBox\BlockBox\mcp`），不用手填、不用复制粘贴。目前只有 Windows 走自动发现，其它平台在 `config` 里填 `port` / `token` 即可。
- 探测 `http://127.0.0.1:<端口>/health`，确认方块盒子真的在跑、MCP 真的开着。
- 通过 DSH 的 `mcp-manager` 注册一条名为 `blockbox` 的 Streamable HTTP 连接；令牌变了、端口变了、写开关变了都会自动重连。
- 往系统提示词里加一小段说明，让模型知道"用户说方块盒子/Minecraft 实例/模组/存档时该用哪些工具、写操作要先确认"。

全部在本机 `127.0.0.1` 上完成，不联外网；DSH 与方块盒子看到的是同一个访问令牌，方块盒子的「允许写操作」开关仍然有效，写操作每次还要带 `confirm: true`。

## 配置项（一般不用改）

写在 `cordis.patch.yml` 的 `config` 里：

| 配置 | 默认 | 说明 |
|---|---|---|
| `port` | 读方块盒子设置，退回 17890 | 覆盖 MCP 端口 |
| `token` | 读方块盒子设置 | 覆盖访问令牌（令牌轮换后不想重启 DSH 时用） |
| `pollMs` | 20000 | 复检间隔（毫秒，最小 5000） |
| `systemPrompt` | true | 是否注入使用说明 |

## 排查

| 现象 | 处理 |
|---|---|
| `blockbox_status` 说端口没有监听 | 方块盒子没开，或设置里没勾「启用本地 MCP 服务器」 |
| 服务可用但工具不出现 | 跑一次 `blockbox_reconnect`；还不行就重启 DSH |
| 401 / invalid token | 方块盒子里点过「重新生成」令牌：等 20 秒自动重连，或手动 `blockbox_reconnect` |
| 写操作工具不在列表里 | 方块盒子设置里打开「允许写操作」，然后 `blockbox_reconnect` |

## 自测

`test/selftest.mjs` 用一个假的 ctx 跑完整流程（读注册表 → health → 握手 → tools/list → 状态报告），
需要配一个本地的方块盒子形状 MCP 桩（`D:\BlockBox\_tmp\blockbox-mcp-stub.mjs`）：

```bash
node D:/BlockBox/_tmp/blockbox-mcp-stub.mjs     # 另开一个终端
node test/selftest.mjs
node test/selftest.mjs --no-server              # 服务器没开的分支
```
