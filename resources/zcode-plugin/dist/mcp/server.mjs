/**
 * 方块盒子桥接（zcode-blockbox-plugin）— stdio MCP 服务器
 *
 * 作用：把本机「方块盒子」启动器内置的本地 MCP 服务器接进 ZCode，让 ZCode 拥有和
 * 方块盒子「AI 助手」同一套本地能力（查实例 / 扫模组 / 读日志 / 分析崩溃 /
 * 查存档 / 备份世界 / 按种子找结构 / 探服务器 / 启停模组 / 启动游戏……）。
 *
 * 工作方式（全部在本机完成，不出内网）：
 *   1. 读方块盒子自己的设置，拿到 MCP 端口与访问令牌（Windows 为注册表
 *      HKCU\Software\BlockBox\BlockBox\mcp；也可用 BLOCKBOX_MCP_PORT /
 *      BLOCKBOX_MCP_TOKEN 环境变量覆盖，对应插件 userConfig）；
 *   2. 探测 http://127.0.0.1:<端口>/health，确认方块盒子正在跑、MCP 已开启；
 *   3. 向方块盒子的 Streamable HTTP MCP 端点完成握手（initialize → 保存
 *      Mcp-Session-Id → initialized → tools/list），缓存远端工具清单；
 *   4. 对 ZCode 侧（stdio JSON-RPC）暴露：远端工具透传 + 本地诊断工具
 *      blockbox_status / blockbox_reconnect；
 *   5. 定时复检：方块盒子启动后自动接上，关掉后自动摘掉，令牌/端口/写开关
 *      变化会自动重连，并通知宿主 tools/list 变了。
 *
 * 设计约束：零第三方依赖，只用 Node 内置模块（Node 20+）。
 * 协议约束：stdout 只走 JSON-RPC，日志全走 stderr。
 */

import { execFile } from "node:child_process";
import { createInterface } from "node:readline";
import process from "node:process";

/* ------------------------------------------------------------------ *
 * 常量与小工具
 * ------------------------------------------------------------------ */

/** 方块盒子 MCP 默认端口（与方块盒子设置里的默认值一致）。 */
const DEFAULT_PORT = 17890;

/** 默认复检间隔（毫秒）。 */
const DEFAULT_POLL_MS = 20000;

/** 最小复检间隔。 */
const MIN_POLL_MS = 5000;

/** /health 探测超时。 */
const HEALTH_TIMEOUT_MS = 1500;

/** 普通 MCP 请求超时（方块盒子里有联网/扫描类工具，给足时间）。 */
const CALL_TIMEOUT_MS = 180000;

/** 握手类请求超时。 */
const HANDSHAKE_TIMEOUT_MS = 20000;

/** 方块盒子写操作工具（需要在它设置里单独开启，且调用时带 confirm）。 */
const WRITE_TOOLS = new Set([
  "manage_mods",
  "manage_world_backup",
  "launch_game",
  "download_instance",
  "modify_instance",
  "download_resource",
]);

/** 本机诊断工具（tools/call 由本脚本自己处理，不透传）。 */
const LOCAL_TOOLS = [
  {
    name: "blockbox_status",
    title: "方块盒子桥接状态",
    description:
      "检查方块盒子（Minecraft 启动器）本地 MCP 桥接是否就绪：端口、令牌是否读到、方块盒子是否在运行、"
      + "服务端开放多少工具、已接入的方块盒子工具数量，并给出排查顺序。"
      + "当用户问「你能不能看我方块盒子的实例/模组/存档」或者 mcp__blockbox__ 工具没出现时，先调它。",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
  },
  {
    name: "blockbox_reconnect",
    title: "重连方块盒子",
    description:
      "重新读取方块盒子的端口与令牌、重新探测并重建 MCP 连接。用户在方块盒子里刚开启或关闭了本地 MCP、"
      + "改过端口、重新生成过令牌，或者 blockbox_status 显示已可用但工具迟迟不出现时调用。",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
  },
];

/** stderr 日志。env BLOCKBOX_BRIDGE_DEBUG=1 时也打印 info。 */
function log(level, message, error) {
  const debug = process.env.BLOCKBOX_BRIDGE_DEBUG === "1";
  if (!debug && level === "info") return;
  const line = `[blockbox-bridge] ${message}`;
  if (error === undefined) process.stderr.write(`${line}\n`);
  else process.stderr.write(`${line} ${error?.stack ?? error}\n`);
}

/** 延时。 */
function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** 只保留令牌首尾各 4 位，避免完整令牌进入对话记录。 */
function maskToken(token) {
  const text = String(token ?? "");
  if (!text) return "";
  if (text.length <= 10) return "***";
  return `${text.slice(0, 4)}…${text.slice(-4)}`;
}

/** 带超时的 fetch，超时返回明确的错误对象。 */
async function fetchWithTimeout(url, options, timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, { ...options, signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

/* ------------------------------------------------------------------ *
 * 自动发现：从方块盒子设置里读端口与令牌
 * ------------------------------------------------------------------ */

const REG_PATH = "HKCU\\Software\\BlockBox\\BlockBox\\mcp";

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

/** 解析 reg query 的输出为 { name: value }。 */
function parseRegValues(text) {
  const values = {};
  for (const line of String(text).split(/\r?\n/u)) {
    // 形如：    token    REG_SZ    004d...
    const matched = /^\s{2,}(\S+)\s{2,}REG_[A-Z_]+\s{2,}(.*)$/u.exec(line);
    if (!matched) continue;
    values[matched[1]] = matched[2].trim();
  }
  return values;
}

/** QSettings 的布尔值可能是 true/false 或 0x1/0x0。 */
function readBool(raw) {
  const text = String(raw ?? "").trim().toLowerCase();
  if (text === "true" || text === "1" || text === "0x1") return true;
  if (text === "false" || text === "0" || text === "0x0") return false;
  return undefined;
}

/**
 * @brief 解析环境变量里的端口/令牌覆盖
 * @return { port, token } 未设置的项为 0 / ""
 */
function envOverrides() {
  const port = Number(process.env.BLOCKBOX_MCP_PORT);
  const token = String(process.env.BLOCKBOX_MCP_TOKEN ?? "").trim();
  return { port: Number.isInteger(port) && port > 0 ? port : 0, token };
}

/* ------------------------------------------------------------------ *
 * 方块盒子 HTTP MCP 客户端
 * ------------------------------------------------------------------ */

const CLIENT_INFO = {
  name: "zcode-blockbox-plugin",
  title: "ZCode 方块盒子桥接",
  version: "0.1.0",
};

const PROTOCOL_VERSIONS = ["2025-06-18", "2025-03-26", "2024-11-05"];

/** 一个指向方块盒子 MCP 端点的会话。 */
class BlockBoxClient {
  constructor(port, token) {
    this.endpoint = `http://127.0.0.1:${port}/mcp`;
    this.token = token;
    this.sessionId = "";
    this.protocolVersion = "";
    this.tools = [];          // 远端工具清单缓存（原样 schema）
    this.remoteWrite = false; // 方块盒子侧「允许写操作」开关（/health 的 write 字段）
  }

  /** 请求头：鉴权 + JSON。 */
  headers(extra = {}) {
    const head = {
      "Content-Type": "application/json",
      Accept: "application/json, text/event-stream",
      ...extra,
    };
    if (this.token) head.Authorization = `Bearer ${this.token}`;
    if (this.sessionId) head["Mcp-Session-Id"] = this.sessionId;
    return head;
  }

  /**
   * @brief 发一条 JSON-RPC 请求/通知给方块盒子
   * @return 请求的 result；通知或失败时返回 null（失败原因在 error）
   */
  async rpc(message, timeoutMs) {
    const response = await fetchWithTimeout(this.endpoint, {
      method: "POST",
      headers: this.headers(),
      body: JSON.stringify(message),
    }, timeoutMs);

    // 会话过期：让上层重新握手
    if (response.status === 404 && this.sessionId) {
      const error = new Error("SESSION_EXPIRED");
      error.code = "SESSION_EXPIRED";
      throw error;
    }
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    if (message.id === undefined) return null; // 通知无响应体

    const contentType = String(response.headers.get("content-type") ?? "");
    if (contentType.includes("text/event-stream")) {
      // 服务端选择用 SSE 应答：读到 message 事件里的 data 即可
      const text = await response.text();
      for (const chunk of text.split(/\r?\n\r?\n/u)) {
        const dataLine = chunk
          .split(/\r?\n/u)
          .find((line) => line.startsWith("data:"));
        if (!dataLine) continue;
        const payload = JSON.parse(dataLine.slice(5).trim());
        if (payload?.id === message.id) return payload.result ?? null;
      }
      return null;
    }

    const payload = await response.json();
    if (payload?.error) {
      throw new Error(`RPC ${payload.error.code}: ${payload.error.message ?? ""}`);
    }
    return payload?.result ?? null;
  }

  /**
   * @brief 完整握手：initialize → 记 session id → initialized → tools/list
   */
  async handshake() {
    let lastError = null;
    for (const version of PROTOCOL_VERSIONS) {
      try {
        const response = await fetchWithTimeout(this.endpoint, {
          method: "POST",
          headers: this.headers(),
          body: JSON.stringify({
            jsonrpc: "2.0",
            id: 1,
            method: "initialize",
            params: { protocolVersion: version, capabilities: {}, clientInfo: CLIENT_INFO },
          }),
        }, HANDSHAKE_TIMEOUT_MS);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const sessionId = response.headers.get("mcp-session-id");
        const contentType = String(response.headers.get("content-type") ?? "");
        let result;
        if (contentType.includes("text/event-stream")) {
          const text = await response.text();
          const dataLine = text
            .split(/\r?\n\r?\n/u)
            .map((chunk) => chunk.split(/\r?\n/u).find((line) => line.startsWith("data:")))
            .find(Boolean);
          result = dataLine ? JSON.parse(dataLine.slice(5).trim())?.result : null;
        } else {
          result = (await response.json())?.result;
        }
        if (!result) throw new Error("initialize 无结果");

        this.protocolVersion = String(result.protocolVersion ?? version);
        this.sessionId = sessionId ?? "";
        // 有的实现要求 session id 在后续请求头里，有的放在初始化结果里
        if (!this.sessionId && typeof result.sessionId === "string") {
          this.sessionId = result.sessionId;
        }

        await this.rpc({ jsonrpc: "2.0", method: "notifications/initialized" }, HANDSHAKE_TIMEOUT_MS)
          .catch(() => {});

        const listing = await this.rpc({
          jsonrpc: "2.0",
          id: 2,
          method: "tools/list",
          params: {},
        }, HANDSHAKE_TIMEOUT_MS);
        this.tools = Array.isArray(listing?.tools) ? listing.tools : [];
        return;
      } catch (error) {
        lastError = error;
        // 协议版本不受支持（-32602 等）时降级重试，其余错误直接放弃本次握手
        const retryable = String(error?.message ?? "").includes("RPC -32602")
          || String(error?.message ?? "").includes("RPC -32601");
        if (!retryable) break;
      }
    }
    throw lastError ?? new Error("握手失败");
  }

  /** 调远端工具。 */
  async callTool(name, args) {
    try {
      return await this.rpc({
        jsonrpc: "2.0",
        id: Date.now() % 1000000,
        method: "tools/call",
        params: { name, arguments: args ?? {} },
      }, CALL_TIMEOUT_MS);
    } catch (error) {
      if (error?.code === "SESSION_EXPIRED") {
        await this.handshake();
        return this.rpc({
          jsonrpc: "2.0",
          id: Date.now() % 1000000,
          method: "tools/call",
          params: { name, arguments: args ?? {} },
        }, CALL_TIMEOUT_MS);
      }
      throw error;
    }
  }
}

/* ------------------------------------------------------------------ *
 * 桥接状态机：发现 → 探测 → 接上/摘除 → 定时复检
 * ------------------------------------------------------------------ */

/** 桥接全局状态。 */
const state = {
  port: envOverrides().port || DEFAULT_PORT,
  token: envOverrides().token,
  health: null,      // { ok, data?, error? }
  client: null,      // BlockBoxClient 实例
  clientKey: "",     // "endpoint|token|write" 签名，变化才重建
  checkedAt: 0,
  lastError: null,
  ticking: false,
};

/** 通知宿主工具清单变了。 */
function notifyToolsChanged() {
  send({ jsonrpc: "2.0", method: "notifications/tools/list_changed" });
}

/** 探测方块盒子 /health。 */
async function probeHealth(port) {
  try {
    const response = await fetchWithTimeout(
      `http://127.0.0.1:${port}/health`,
      { headers: { Accept: "application/json" } },
      HEALTH_TIMEOUT_MS,
    );
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
  }
}

/**
 * @brief 一轮「发现 → 探测 → 连接/断开」
 * @param reason 触发原因，仅用于日志
 */
async function tick(reason) {
  if (state.ticking) return;
  state.ticking = true;
  try {
    // 1. 自动发现（env 覆盖优先，其次注册表）
    const env = envOverrides();
    let port = env.port;
    let token = env.token;
    if (!port || !token) {
      if (process.platform === "win32") {
        const values = parseRegValues(await regQuery(REG_PATH));
        if (!port) port = Number(values?.port) || 0;
        if (!token) token = String(values?.token ?? "").trim();
      }
    }
    if (!port) port = DEFAULT_PORT;
    state.port = port;
    state.token = token;

    // 2. 探测
    const health = await probeHealth(port);
    state.health = health;
    state.checkedAt = Date.now();

    if (!health.ok) {
      if (state.client) {
        state.client = null;
        state.clientKey = "";
        notifyToolsChanged();
        log("info", `方块盒子 MCP 不可用（${health.error}），已摘掉远端工具`);
      }
      return;
    }

    // 3. 写开关影响服务端工具清单，变了就重连一次拿新清单
    const writeFlag = health.data?.write === true;
    const clientKey = `http://127.0.0.1:${port}/mcp|${token}|${writeFlag ? "w" : "r"}`;
    if (state.client && state.clientKey === clientKey) return; // 一切照旧

    // 4. 握手 + 拉工具清单
    const client = new BlockBoxClient(port, token);
    await client.handshake();
    client.remoteWrite = writeFlag;
    const wasClient = state.client;
    state.client = client;
    state.clientKey = clientKey;
    state.lastError = null;
    log("info", `已连接方块盒子 ${client.endpoint}（${client.tools.length} 个工具）`);
    // 工具集合变了（首次接入 / 写开关变化 / 工具增减）才打扰宿主
    const signature = (tools) => tools.map((tool) => tool?.name ?? "").join(",");
    if (!wasClient || signature(wasClient.tools) !== signature(client.tools)) {
      notifyToolsChanged();
    }
  } catch (error) {
    state.lastError = String(error?.message ?? error);
    log("warn", `复检失败（${reason}）`, error);
    if (state.client) {
      state.client = null;
      state.clientKey = "";
      notifyToolsChanged();
    }
  } finally {
    state.ticking = false;
  }
}

/* ------------------------------------------------------------------ *
 * 本地诊断工具实现
 * ------------------------------------------------------------------ */

/** 已接入的远端工具数量。 */
function remoteToolCount() {
  return state.client?.tools.length ?? 0;
}

/** 生成给人看的诊断报告。 */
function report() {
  const health = state.health;
  const lines = ["方块盒子 · ZCode 桥接"];
  lines.push(`地址：http://127.0.0.1:${state.port}/mcp`);
  lines.push(
    state.token
      ? `令牌：已读取（${maskToken(state.token)}）`
      : "令牌：未读取到（先在方块盒子设置页打开「本地 MCP 服务器」，它才会生成令牌）",
  );

  if (health?.ok) {
    lines.push(`方块盒子：运行中，版本 ${String(health.data?.version ?? "未知")}`);
    const total = Number(health.data?.tools ?? 0);
    lines.push(`服务端工具：${total} 个（${health.data?.write ? "含写操作" : "只读"}）`);
  } else {
    lines.push(`方块盒子 MCP 服务：不可用（${health?.error ?? "尚未检测"}）`);
    lines.push("排查顺序：方块盒子是否开着 → 设置 → AI 助手 → 勾选「启用本地 MCP 服务器」→ 端口是否与设置里一致。");
  }

  const count = remoteToolCount();
  const samples = (state.client?.tools ?? [])
    .map((tool) => tool?.name ?? "")
    .filter(Boolean)
    .slice(0, 6);
  lines.push(count > 0 ? `已接入 ZCode：${count} 个工具` : "已接入 ZCode：0 个工具（服务可用后会自动出现）");
  if (samples.length > 0) {
    lines.push(`例如：${samples.map((name) => `mcp__blockbox__${name}`).join("、")}`);
  }

  const writeAllowed = health?.ok
    ? health.data?.write === true
    : undefined;
  if (writeAllowed === true) {
    lines.push("写操作：方块盒子侧已允许（启停模组、备份/恢复存档、下载实例、启动游戏都可用，调用时仍要带 confirm: true）。");
  } else if (writeAllowed === false) {
    lines.push("写操作：方块盒子侧未开启；如需启停模组、备份/恢复存档、启动游戏，去方块盒子设置里打开「允许写操作」（仍要每次带 confirm: true）。");
  }
  if (state.lastError) lines.push(`最近错误：${state.lastError}`);
  return lines.join("\n");
}

/** 本地诊断工具的执行。 */
async function callLocalTool(name) {
  if (name === "blockbox_status") {
    await tick("status");
  } else if (name === "blockbox_reconnect") {
    state.client = null;
    state.clientKey = "";
    await tick("reconnect");
    // 连接建立与 tools/list 是异步的，稍等一下再统计，报告更贴近真实。
    for (let i = 0; i < 10 && remoteToolCount() === 0 && state.health?.ok; i += 1) {
      await delay(300);
    }
  } else {
    throw new Error(`unknown tool: ${name}`);
  }
  return {
    content: [{ type: "text", text: report() }],
  };
}

/* ------------------------------------------------------------------ *
 * stdio JSON-RPC 服务器（对 ZCode 侧）
 * ------------------------------------------------------------------ */

/** 输出一条 JSON-RPC 消息到 stdout。 */
function send(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

/** 回应一个请求。 */
function reply(id, result) {
  send({ jsonrpc: "2.0", id, result });
}

/** 回应一个错误。 */
function replyError(id, code, message) {
  send({ jsonrpc: "2.0", id, error: { code, message } });
}

/** 当前对外暴露的完整工具清单（本地诊断 + 远端透传）。 */
function toolCatalog() {
  const remote = (state.client?.tools ?? []).map((tool) => ({
    name: String(tool?.name ?? ""),
    title: typeof tool?.title === "string" ? tool.title : undefined,
    description: typeof tool?.description === "string"
      ? tool.description
      : "方块盒子工具",
    inputSchema: tool?.inputSchema ?? { type: "object", properties: {} },
  })).filter((tool) => tool.name && !tool.name.startsWith("blockbox_"));
  return [...LOCAL_TOOLS, ...remote];
}

/** 按 MCP 规范归一化 tools/call 的结果。 */
function normalizeResult(result) {
  const content = Array.isArray(result?.content) ? result.content : [];
  const isError = result?.isError === true;
  const structured = result?.structuredContent;
  if (structured !== undefined) {
    return { content, isError, structuredContent: structured };
  }
  return { content, isError };
}

/** 处理一条来自宿主的请求。 */
async function handleMessage(message) {
  const { id, method, params } = message;

  if (method === "initialize") {
    reply(id, {
      protocolVersion: typeof params?.protocolVersion === "string"
        ? params.protocolVersion
        : "2025-06-18",
      capabilities: {
        tools: { listChanged: true },
      },
      serverInfo: { name: "blockbox", title: "方块盒子", version: "0.1.0" },
    });
    return;
  }

  // 通知不回应
  if (method === "notifications/initialized") return;
  if (id === undefined) return;

  if (method === "ping") {
    reply(id, {});
    return;
  }

  if (method === "tools/list") {
    reply(id, { tools: toolCatalog() });
    return;
  }

  if (method === "tools/call") {
    const name = String(params?.name ?? "");
    try {
      if (name === "blockbox_status" || name === "blockbox_reconnect") {
        reply(id, await callLocalTool(name));
        return;
      }
      const client = state.client;
      if (!client) {
        reply(id, {
          content: [{
            type: "text",
            text: "方块盒子 MCP 服务当前不可用，工具没有接入。"
              + "先调 blockbox_status 看方块盒子是否在运行、本地 MCP 是否已启用；"
              + "必要时再调 blockbox_reconnect。",
          }],
          isError: true,
        });
        return;
      }
      reply(id, normalizeResult(await client.callTool(name, params?.arguments ?? {})));
    } catch (error) {
      log("warn", `tools/call ${name} 失败`, error);
      reply(id, {
        content: [{
          type: "text",
          text: `调用失败：${String(error?.message ?? error)}。`
            + "可先调 blockbox_status 检查方块盒子状态；令牌/端口变更后用 blockbox_reconnect 重连。",
        }],
        isError: true,
      });
    }
    return;
  }

  if (method === "resources/list" || method === "resources/templates/list" || method === "prompts/list") {
    reply(id, method === "prompts/list" ? { prompts: [] } : { resources: [] });
    return;
  }

  replyError(id, -32601, `Method not found: ${method}`);
}

/* ------------------------------------------------------------------ *
 * 启动
 * ------------------------------------------------------------------ */

// stdin 按行读 JSON-RPC
const stdin = createInterface({ input: process.stdin });
stdin.on("line", (line) => {
  const text = line.trim();
  if (!text) return;
  let message;
  try {
    message = JSON.parse(text);
  } catch (error) {
    log("warn", "收到无法解析的输入", error);
    return;
  }
  if (!message || typeof message.method !== "string") return;
  handleMessage(message).catch((error) => {
    log("warn", "处理消息时出错", error);
    if (message.id !== undefined) {
      replyError(message.id, -32603, `Internal error: ${String(error?.message ?? error)}`);
    }
  });
});
stdin.on("close", () => process.exit(0));

process.on("uncaughtException", (error) => {
  log("warn", "未捕获异常", error);
});

// 首轮发现 + 定时复检
void tick("startup");
const pollRaw = Number(process.env.BLOCKBOX_POLL_MS);
const pollMs = Number.isFinite(pollRaw) && pollRaw >= MIN_POLL_MS ? pollRaw : DEFAULT_POLL_MS;
setInterval(() => {
  void tick("poll");
}, pollMs).unref();
