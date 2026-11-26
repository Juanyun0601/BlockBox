/**
 * 方块盒子桥接（dsh-blockbox-bridge）— Host 半
 *
 * 作用：把本机「方块盒子」启动器内置的本地 MCP 服务器接进 DSH，让 DSH 拥有和
 * 方块盒子「AI 助手」同一套本地能力（查实例 / 扫模组 / 读日志 / 分析崩溃 /
 * 查存档 / 备份世界 / 按种子找结构 / 探服务器 / 启停模组 / 启动游戏……）。
 *
 * 工作方式（全部在本机完成，不出内网）：
 *   1. 读方块盒子自己的设置，拿到 MCP 端口与访问令牌（Windows 为注册表
 *      HKCU\Software\BlockBox\BlockBox\mcp；也可用 config 覆盖）；
 *   2. 探测 http://127.0.0.1:<端口>/health，确认方块盒子正在跑、MCP 已开启；
 *   3. 通过 DSH 的 mcp-manager 注册一个名为 blockbox 的 MCP 连接，
 *      它的工具会自动以 mcp__blockbox__<工具名> 出现在模型工具列表里；
 *   4. 定时复检：方块盒子启动后自动接上，关掉后自动摘掉，令牌/端口/写开关
 *      变化会自动重连。
 *
 * 设计约束：零第三方依赖，只用 Node 内置模块 + DSH 现有服务。
 */

import { execFile } from "node:child_process";

/** 插件名，与 cordis.patch.yml 里的 name 对应。 */
export const name = "blockbox-bridge";

/** 依赖的服务：工具注册表、MCP 服务器管理器。 */
export const inject = ["tools", "mcpManager"];

/** 注册到 mcp-manager 的服务器名，决定工具前缀 mcp__blockbox__。 */
const SERVER_NAME = "blockbox";

/** 模型侧工具名前缀。 */
const TOOL_PREFIX = `mcp__${SERVER_NAME}__`;

/** 方块盒子 MCP 默认端口（与方块盒子设置里的默认值一致）。 */
const DEFAULT_PORT = 17890;

/** 默认复检间隔。 */
const DEFAULT_POLL_MS = 20000;

/** /health 探测超时。 */
const HEALTH_TIMEOUT_MS = 1500;

/** Windows 注册表里方块盒子 MCP 设置所在位置。 */
const REG_PATH = "HKCU\\Software\\BlockBox\\BlockBox\\mcp";

/** 方块盒子写操作工具（需要在它设置里单独开启，且调用时带 confirm）。 */
const WRITE_TOOLS = [
  "manage_mods",
  "manage_world_backup",
  "launch_game",
  "download_instance",
  "modify_instance",
  "download_resource",
];

/* ------------------------------------------------------------------ *
 * 小工具
 * ------------------------------------------------------------------ */

/** 延时。 */
function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** 只保留令牌首尾各 4 位，避免完整令牌进入对话记录。 */
function maskToken(token) {
  const text = String(token ?? "");
  if (text.length <= 10) return text ? "***" : "";
  return `${text.slice(0, 4)}…${text.slice(-4)}`;
}

/** 调 reg.exe 读一段注册表，失败时返回空串（不抛错）。 */
function regQuery(path) {
  return new Promise((resolve) => {
    execFile(
      "reg",
      ["query", path],
      { windowsHide: true, timeout: 5000, encoding: "utf8" },
      (error, stdout) => resolve(error ? "" : String(stdout ?? "")),
    );
  });
}

/**
 * @brief 解析 reg query 的输出
 * @param text 原始输出
 * @return { [name]: { type, value } }
 */
function parseRegValues(text) {
  const values = {};
  for (const line of String(text).split(/\r?\n/u)) {
    // 形如：    token    REG_SZ    004d...
    const matched = /^\s{2,}(\S+)\s{2,}(REG_[A-Z_]+)\s{2,}(.*)$/u.exec(line);
    if (!matched) continue;
    values[matched[1]] = { type: matched[2], value: matched[3].trim() };
  }
  return values;
}

/** QSettings 的布尔值可能是 true/false 或 0x1/0x0。 */
function readBool(entry) {
  if (!entry) return undefined;
  const raw = String(entry.value ?? "").trim().toLowerCase();
  if (raw === "true" || raw === "1" || raw === "0x1") return true;
  if (raw === "false" || raw === "0" || raw === "0x0") return false;
  return undefined;
}

/** 探测方块盒子 /health。 */
async function probeHealth(port) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), HEALTH_TIMEOUT_MS);
  try {
    const response = await fetch(`http://127.0.0.1:${port}/health`, {
      signal: controller.signal,
      headers: { Accept: "application/json" },
    });
    if (!response.ok) return { ok: false, error: `HTTP ${response.status}` };
    const data = await response.json();
    if (data?.ok !== true) return { ok: false, error: "方块盒子返回了非预期内容" };
    return { ok: true, data };
  } catch (error) {
    const aborted = error?.name === "AbortError";
    const code = String(error?.cause?.code ?? "");
    const reason = aborted
      ? "连接超时（端口没有响应）"
      : code === "ECONNREFUSED"
        ? "端口没有监听：方块盒子没开，或本地 MCP 服务器未启用"
        : code === "ECONNRESET" || code === "EACCES"
          ? `本机连接被拒绝（${code}）`
          : String(error?.message ?? error);
    return { ok: false, error: reason };
  } finally {
    clearTimeout(timer);
  }
}

/* ------------------------------------------------------------------ *
 * 插件入口
 * ------------------------------------------------------------------ */

/**
 * @param ctx     Cordis 上下文
 * @param config  cordis.patch.yml 里该行的 config
 */
export function apply(ctx, config = {}) {
  const options = {
    port: Number(config?.port) > 0 ? Number(config.port) : 0,
    token: typeof config?.token === "string" ? config.token.trim() : "",
    pollMs: Number(config?.pollMs) >= 5000 ? Number(config.pollMs) : DEFAULT_POLL_MS,
    systemPrompt: config?.systemPrompt !== false,
  };

  const log = (level, message, error) => {
    try {
      if (process.env.BLOCKBOX_BRIDGE_DEBUG === "1" && level !== "error") {
        console.error(`[blockbox-bridge] ${message}`, error === undefined ? "" : error);
      }
      const logger = ctx.logger;
      if (typeof logger?.[level] === "function") {
        if (error === undefined) logger[level](message);
        else logger[level](message, error);
      }
    } catch {
      // 日志失败不影响主流程
    }
  };

  /** 桥接的实时状态。 */
  const state = {
    port: options.port || DEFAULT_PORT,
    endpoint: `http://127.0.0.1:${options.port || DEFAULT_PORT}/mcp`,
    token: options.token,
    allowWrite: undefined,
    registryWrite: undefined,
    enabled: undefined,
    version: "",
    health: null,           // { ok, data?, error? }
    registration: null,     // mcpManager.register() 返回的注销函数
    registeredWith: "",     // 当前连接对应的 "endpoint|token" 签名
    lastError: null,
    checkedAt: 0,
  };

  /**
   * @brief 从方块盒子设置里发现端口与令牌
   */
  async function discover() {
    const values = process.platform === "win32" ? parseRegValues(await regQuery(REG_PATH)) : {};
    const port = options.port || Number(values?.port?.value) || DEFAULT_PORT;
    const token = options.token || String(values?.token?.value ?? "").trim();
    state.port = port;
    state.token = token;
    state.endpoint = `http://127.0.0.1:${port}/mcp`;
    state.registryWrite = readBool(values?.allowWrite);
    state.enabled = readBool(values?.enabled);
    return { port, token };
  }

  /** 断开当前 MCP 连接（工具随之从模型侧消失）。 */
  function detach() {
    if (typeof state.registration === "function") {
      try {
        state.registration();
      } catch (error) {
        log("warn", "blockbox-bridge: 断开旧连接时出错", error);
      }
    }
    state.registration = null;
    state.registeredWith = "";
  }

  /**
   * @brief 按当前发现结果注册（或保持）MCP 连接
   * @return 是否新建了连接
   */
  function attach() {
    const signature = `${state.endpoint}|${state.token}|${state.allowWrite === true ? "w" : "r"}`;
    if (typeof state.registration === "function" && state.registeredWith === signature) return false;

    detach();
    try {
      state.registration = ctx.mcpManager.register(SERVER_NAME, {
        transport: "streamable-http",
        url: state.endpoint,
        headers: state.token ? { Authorization: `Bearer ${state.token}` } : {},
        // 方块盒子里有联网/扫描类工具（查更新、扫大实例），给足调用时间。
        toolCallTimeoutMs: 180000,
        startupTimeoutMs: 20000,
      });
      state.registeredWith = signature;
      state.lastError = null;
      log("info", `blockbox-bridge: 已连接方块盒子 ${state.endpoint}`);
      return true;
    } catch (error) {
      state.registration = null;
      state.registeredWith = "";
      state.lastError = String(error?.message ?? error);
      log("warn", "blockbox-bridge: 注册方块盒子 MCP 连接失败", error);
      return false;
    }
  }

  /** 统计当前已接入 DSH 的方块盒子工具数量。 */
  function toolCount() {
    try {
      return ctx.tools.schemas().filter((schema) => String(schema?.name ?? "").startsWith(TOOL_PREFIX)).length;
    } catch {
      return 0;
    }
  }

  /** 列出几个已接入的工具名，给状态报告当例子。 */
  function sampleTools(limit = 6) {
    try {
      return ctx.tools
        .schemas()
        .map((schema) => String(schema?.name ?? ""))
        .filter((toolName) => toolName.startsWith(TOOL_PREFIX))
        .slice(0, limit)
        .map((toolName) => toolName.slice(TOOL_PREFIX.length));
    } catch {
      return [];
    }
  }

  let ticking = false;

  /**
   * @brief 一轮「发现 → 探测 → 连接/断开」
   * @param reason 触发原因，仅用于日志
   */
  async function tick(reason) {
    if (ticking) return;
    ticking = true;
    try {
      await discover();
      const health = await probeHealth(state.port);
      state.health = health;
      state.checkedAt = Date.now();

      if (health.ok) {
        state.version = String(health.data?.version ?? "");
        // 写开关影响服务端工具清单，变了就重连一次拿新清单
        const writeFlag = health.data?.write === true;
        const changed = state.allowWrite !== undefined && state.allowWrite !== writeFlag;
        state.allowWrite = writeFlag;
        if (changed && typeof state.registration === "function") detach();
        attach();
      } else if (typeof state.registration === "function") {
        log("info", `blockbox-bridge: 方块盒子 MCP 不可用（${health.error}），先摘掉旧工具`);
        detach();
      }
    } catch (error) {
      state.lastError = String(error?.message ?? error);
      log("warn", `blockbox-bridge: 复检失败（${reason}）`, error);
    } finally {
      ticking = false;
    }
  }

  /** 生成给人看的诊断报告。 */
  function report() {
    const health = state.health;
    const lines = ["方块盒子 · 本地 MCP 桥接"];
    lines.push(`地址：${state.endpoint}`);
    lines.push(
      state.token
        ? `令牌：已读取（${maskToken(state.token)}）`
        : "令牌：未读取到（先在方块盒子设置页打开「本地 MCP 服务器」，它才会生成令牌）",
    );

    if (health?.ok) {
      lines.push(`方块盒子：运行中，版本 ${state.version || "未知"}`);
      const total = Number(health.data?.tools ?? 0);
      lines.push(`服务端工具：${total} 个（${health.data?.write ? "含写操作" : "只读"}）`);
    } else {
      lines.push(`方块盒子 MCP 服务：不可用（${health?.error ?? "尚未检测"}）`);
      lines.push("排查顺序：方块盒子是否开着 → 设置 → AI 助手 → 勾选「启用本地 MCP 服务器」→ 端口是否与设置里一致。");
    }

    const count = toolCount();
    const samples = sampleTools();
    const writeAllowed = health?.ok ? state.allowWrite === true : state.registryWrite === true;
    lines.push(count > 0 ? `已接入 DSH：${count} 个工具` : "已接入 DSH：0 个工具（服务可用后会自动出现）");
    if (samples.length > 0) lines.push(`例如：${samples.map((n) => `${TOOL_PREFIX}${n}`).join("、")}`);
    if (writeAllowed) {
      lines.push("写操作：方块盒子侧已允许（启停模组、备份/恢复存档、下载实例、启动游戏都可用，调用时仍要带 confirm）。");
    } else {
      lines.push("写操作：方块盒子侧未开启；如需启停模组、备份/恢复存档、启动游戏，去方块盒子设置里打开「允许写操作」（仍要每次带 confirm）。");
    }
    if (state.lastError) lines.push(`最近错误：${state.lastError}`);
    return lines.join("\n");
  }

  /* ---------------- 工具 ---------------- */

  const disposers = [];

  disposers.push(
    ctx.tools.register({
      name: "blockbox_status",
      title: "方块盒子桥接状态",
      description:
        "检查方块盒子（Minecraft 启动器）本地 MCP 桥接是否就绪：端口、令牌是否读到、方块盒子是否在运行、"
        + "服务端开放多少工具、DSH 这边已经接入几个 mcp__blockbox__ 工具，并给出排查顺序。"
        + "当用户问「你能不能看我方块盒子的实例/模组/存档」或者 mcp__blockbox__ 工具没出现时，先调它。",
      parameters: { type: "object", properties: {}, additionalProperties: false },
      output: {
        schema: {
          type: "object",
          properties: {
            connected: { type: "boolean", description: "方块盒子 MCP 服务是否可用。" },
            tools: { type: "number", description: "已接入 DSH 的方块盒子工具数量。" },
            report: { type: "string", description: "人类可读的诊断报告。" },
          },
          required: ["connected", "tools", "report"],
          additionalProperties: false,
        },
        render: (_args, value) => [{ type: "text", text: String(value?.report ?? "") }],
      },
      async execute() {
        await tick("status");
        return { connected: state.health?.ok === true, tools: toolCount(), report: report() };
      },
    }),
  );

  disposers.push(
    ctx.tools.register({
      name: "blockbox_reconnect",
      title: "重连方块盒子",
      description:
        "重新读取方块盒子的端口与令牌、重新探测并重建 MCP 连接。用户在方块盒子里刚开启或关闭了本地 MCP、"
        + "改过端口、重新生成过令牌，或者 blockbox_status 显示已可用但工具迟迟不出现时调用。",
      parameters: { type: "object", properties: {}, additionalProperties: false },
      output: {
        schema: {
          type: "object",
          properties: {
            connected: { type: "boolean", description: "重连后服务是否可用。" },
            tools: { type: "number", description: "已接入 DSH 的方块盒子工具数量。" },
            report: { type: "string", description: "重连结果。" },
          },
          required: ["connected", "tools", "report"],
          additionalProperties: false,
        },
        render: (_args, value) => [{ type: "text", text: String(value?.report ?? "") }],
      },
      async execute() {
        detach();
        await tick("reconnect");
        // 连接建立与 tools/list 是异步的，稍等一下再统计，报告更贴近真实。
        for (let i = 0; i < 10 && toolCount() === 0 && state.health?.ok; i += 1) await delay(300);
        return { connected: state.health?.ok === true, tools: toolCount(), report: report() };
      },
    }),
  );

  /* ---------------- 系统提示词 ---------------- */

  if (options.systemPrompt) {
    ctx.inject(["systemPrompt"], (sctx) => {
      sctx.effect(() =>
        sctx.systemPrompt.section({
          name: "blockbox-bridge",
          order: 600,
          text: [
            "方块盒子（本机 Minecraft 启动器）已通过 mcp__blockbox__* 工具接入：实例、模组、日志、崩溃报告、存档、服务器列表、结构坐标、资源下载等。",
            "用户提到方块盒子、Minecraft 实例/模组/客户端/存档/服务器时，优先用这些工具，别让用户自己开软件翻菜单。",
            "写操作（manage_mods、manage_world_backup、launch_game、download_instance、modify_instance、download_resource）要方块盒子设置里开了「允许写操作」，且每次必须带 confirm: true；删除模组、恢复存档前先向用户确认。",
            "这些工具没出现、或调用报连接失败时，先调 blockbox_status 看方块盒子是否在运行、本地 MCP 是否已启用，再决定是否 blockbox_reconnect。",
          ].join("\n"),
        }),
      );
    });
  }

  /* ---------------- 生命周期 ---------------- */

  // 插件被卸载时摘掉 MCP 连接，避免留下半死的工具。
  ctx.effect(() => () => detach());

  void tick("startup");
  // 定时复检用 timer 服务（cordis-plugin-timer），没有它的精简 profile 里就不复检。
  ctx.inject(["timer"], (tctx) => {
    tctx.effect(() =>
      tctx.setInterval(() => {
        void tick("poll");
      }, options.pollMs),
    );
  });

  return () => {
    for (const dispose of disposers) {
      if (typeof dispose === "function") dispose();
    }
  };
}
