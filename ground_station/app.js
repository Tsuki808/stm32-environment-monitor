"use strict";

const MAX_POINTS = 240;
const DEFAULT_CONFIG = Object.freeze({
  warnMv: 1200,
  alarmMv: 2200,
  uploadMs: 500,
  buzzer: 1,
  seq: 0,
});
const EVENT_LOG_SIZE = 32;
const AI_ENDPOINT = "/api/deepseek";

const COLORS = {
  bg: "#071016",
  grid: "rgba(148, 163, 184, 0.18)",
  text: "#dbeafe",
  muted: "#93a4b8",
  light: "#22d3ee",
  warn: "#f59e0b",
  alarm: "#ef4444",
  normal: "#22c55e",
};

const el = {
  linkPill: document.querySelector("#link-pill"),
  linkText: document.querySelector("#link-text"),
  exportBtn: document.querySelector("#export-btn"),
  clearLogBtn: document.querySelector("#clear-log-btn"),
  manualForm: document.querySelector("#manual-form"),
  manualCommand: document.querySelector("#manual-command"),
  quickCommands: document.querySelector("#quick-commands"),
  stateWord: document.querySelector("#state-word"),
  levelWord: document.querySelector("#level-word"),
  riskText: document.querySelector("#risk-text"),
  lightValue: document.querySelector("#light-value"),
  tempValue: document.querySelector("#temp-value"),
  humiValue: document.querySelector("#humi-value"),
  gasValue: document.querySelector("#gas-value"),
  rxCount: document.querySelector("#rx-count"),
  crcCount: document.querySelector("#crc-count"),
  dropCount: document.querySelector("#drop-count"),
  modeText: document.querySelector("#mode-text"),
  srcText: document.querySelector("#src-text"),
  uptimeText: document.querySelector("#uptime-text"),
  seqText: document.querySelector("#seq-text"),
  rawLevelText: document.querySelector("#raw-level-text"),
  errText: document.querySelector("#err-text"),
  diagnosisText: document.querySelector("#diagnosis-text"),
  aiStatus: document.querySelector("#ai-status"),
  aiStatusText: document.querySelector("#ai-status-text"),
  aiAnalyzeBtn: document.querySelector("#ai-analyze-btn"),
  aiChatLog: document.querySelector("#ai-chat-log"),
  terminalLog: document.querySelector("#terminal-log"),
  canvas: document.querySelector("#telemetry-canvas"),
};

const state = {
  timer: 0,
  index: 0,
  elapsedMs: 0,
  rx: 0,
  crc: 0,
  drop: 0,
  history: [],
  logs: [],
  eventLogs: [],
  aiMessages: [],
  lastRow: null,
  lastConfig: null,
  lastStat: null,
  config: { ...DEFAULT_CONFIG },
  stats: {
    alarmTotal: 0,
    warnTotal: 0,
    faultTotal: 0,
    maxLevel: 0,
    lightMax: 0,
    lightMin: null,
    keyTotal: 0,
    saveTotal: 0,
    overflowTotal: 0,
  },
};

const ctx = el.canvas.getContext("2d");
const STATE_CODE = Object.freeze({
  NORMAL: 0,
  WARN: 1,
  ALARM: 2,
  FAULT: 5,
  UNKNOWN: 255,
});

const TELEMETRY_TRACE = [
  {
    light: 820,
    state: "NORMAL",
    level: 0,
    risk: 0,
    src: "NONE",
    event: "SYSTEM_START",
  },
  { light: 880, state: "NORMAL", level: 0, risk: 0, src: "NONE" },
  { light: 960, state: "NORMAL", level: 0, risk: 0, src: "NONE" },
  { light: 1080, state: "NORMAL", level: 0, risk: 0, src: "NONE" },
  {
    light: 1210,
    state: "WARN",
    level: 1,
    risk: 1,
    src: "LIGHT",
    event: "WARN_ENTER",
  },
  { light: 1360, state: "WARN", level: 1, risk: 1, src: "LIGHT" },
  { light: 1580, state: "WARN", level: 1, risk: 1, src: "LIGHT" },
  { light: 1860, state: "WARN", level: 1, risk: 2, src: "LIGHT" },
  { light: 2140, state: "WARN", level: 1, risk: 3, src: "LIGHT" },
  {
    light: 2260,
    state: "ALARM",
    level: 2,
    risk: 4,
    src: "LIGHT",
    event: "ALARM_ENTER_L1",
  },
  { light: 2360, state: "ALARM", level: 2, risk: 4, src: "LIGHT" },
  { light: 2470, state: "ALARM", level: 2, risk: 5, src: "LIGHT" },
  { light: 2580, state: "ALARM", level: 2, risk: 5, src: "LIGHT" },
  {
    light: 2710,
    state: "ALARM",
    level: 3,
    risk: 6,
    src: "LIGHT",
    event: "ALARM_ESCALATE_L2",
  },
  { light: 2860, state: "ALARM", level: 3, risk: 7, src: "LIGHT" },
  {
    light: 3010,
    state: "ALARM",
    level: 4,
    risk: 8,
    src: "LIGHT",
    event: "ALARM_ESCALATE_L3",
  },
  { light: 2920, state: "ALARM", level: 4, risk: 7, src: "LIGHT" },
  { light: 2660, state: "ALARM", level: 4, risk: 6, src: "LIGHT" },
  { light: 2210, state: "ALARM", level: 4, risk: 4, src: "LIGHT" },
  { light: 1720, state: "ALARM", level: 4, risk: 2, src: "LIGHT" },
  {
    light: 1080,
    state: "ALARM",
    level: 3,
    risk: 0,
    src: "LIGHT",
    event: "ALARM_L3_EXIT",
  },
  {
    light: 960,
    state: "ALARM",
    level: 2,
    risk: 0,
    src: "LIGHT",
    event: "ALARM_L2_EXIT",
  },
  {
    light: 900,
    state: "WARN",
    level: 1,
    risk: 0,
    src: "LIGHT",
    event: "ALARM_L1_EXIT",
  },
  {
    light: 850,
    state: "NORMAL",
    level: 0,
    risk: 0,
    src: "NONE",
    event: "WARN_EXIT",
  },
  { light: 820, state: "NORMAL", level: 0, risk: 0, src: "NONE" },
  { light: 840, state: "NORMAL", level: 0, risk: 0, src: "NONE" },
];

function calcChecksum(payload) {
  let checksum = 0;
  for (let i = 0; i < payload.length; i += 1) checksum ^= payload.charCodeAt(i);
  return checksum;
}

function frameWithChecksum(payload) {
  return `${payload}*${calcChecksum(payload).toString(16).toUpperCase().padStart(2, "0")}`;
}

function buildCommand(command) {
  const clean = command.trim();
  if (!clean) return "";
  if (clean.startsWith("@") && clean.includes("*"))
    return clean.replace(/[\r\n]+$/g, "");
  const payload = clean.startsWith("@")
    ? clean.split("*", 1)[0]
    : `@CMD,${clean}`;
  return frameWithChecksum(payload);
}

function setLink(text) {
  el.linkPill.classList.add("online");
  el.linkPill.classList.remove("run", "error");
  el.linkText.textContent = text;
}

function log(message, tone = "rx") {
  const time = new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3,
  }).format(new Date());
  const prefix =
    tone === "error"
      ? "ERR"
      : tone === "tx"
        ? "TX"
        : tone === "info"
          ? "SYS"
          : "RX";
  state.logs.push(`[${time}] ${prefix}  ${message}`);
  if (state.logs.length > 260) state.logs.shift();
  el.terminalLog.textContent = state.logs.join("\n");
  el.terminalLog.scrollTop = el.terminalLog.scrollHeight;
}

function updateCounters() {
  el.rxCount.textContent = String(state.rx);
  el.crcCount.textContent = String(state.crc);
  el.dropCount.textContent = String(state.drop);
}

function formatNumber(value, digits = 0) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "--";
  return new Intl.NumberFormat("zh-CN", {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  }).format(number);
}

function fieldsFromLine(line) {
  const payload = line.split("*", 1)[0];
  const type = payload.split(",", 1)[0].slice(1);
  const fields = {};
  for (const part of payload.split(",").slice(1)) {
    const index = part.indexOf("=");
    if (index > 0) fields[part.slice(0, index)] = part.slice(index + 1);
  }
  return { type, payload, fields };
}

function verifyFrame(line) {
  if (!line.startsWith("@")) return true;
  const star = line.lastIndexOf("*");
  if (star < 0) {
    state.drop += 1;
    return false;
  }
  const payload = line.slice(0, star);
  const received = Number.parseInt(line.slice(star + 1, star + 3), 16);
  if (!Number.isFinite(received) || received !== calcChecksum(payload)) {
    state.crc += 1;
    return false;
  }
  return true;
}

function normalizeLevel(rawState, rawLevel) {
  const stateName = String(rawState || "UNKNOWN").toUpperCase();
  const level = Number.parseInt(rawLevel, 10);
  if (!Number.isFinite(level)) return "--";
  if (stateName === "ALARM" && level >= 2 && level <= 4) return `L${level - 1}`;
  if (stateName === "ALARM" && level >= 1 && level <= 3) return `L${level}`;
  if (stateName === "WARN") return "WARN";
  if (stateName === "NORMAL") return "0";
  if (stateName.includes("FAULT")) return "FAULT";
  return String(level);
}

function setSystemState(systemState, rawLevel) {
  const normalized = String(systemState || "UNKNOWN").toUpperCase();
  el.stateWord.className = "";
  el.stateWord.classList.add(
    normalized === "NORMAL"
      ? "state-normal-text"
      : normalized === "WARN"
        ? "state-warn-text"
        : normalized === "ALARM"
          ? "state-alarm-text"
          : normalized.includes("FAULT")
            ? "state-fault-text"
            : "state-offline-text",
  );
  el.stateWord.textContent = normalized;
  el.levelWord.textContent = normalizeLevel(normalized, rawLevel);
}

function parseEnv(line) {
  const { fields } = fieldsFromLine(line);
  const row = {
    pcTime: new Date().toISOString(),
    seq: fields.seq || "--",
    ms: Number(fields.ms || 0),
    mode: fields.mode || "PRO_LITE",
    light: Number(fields.light),
    state: String(fields.state || "UNKNOWN").toUpperCase(),
    rawLevel: fields.level ?? "--",
    displayLevel: normalizeLevel(fields.state, fields.level),
    risk: Number(fields.risk || 0),
    src: fields.src || "NONE",
    err: fields.err || "00",
  };

  state.lastRow = row;
  state.history.push(row);
  if (state.history.length > MAX_POINTS) state.history.shift();

  if (Number.isFinite(row.light)) {
    state.stats.lightMax = Math.max(state.stats.lightMax, row.light);
    state.stats.lightMin =
      state.stats.lightMin === null
        ? row.light
        : Math.min(state.stats.lightMin, row.light);
  }
  const numericLevel = Number.parseInt(row.rawLevel, 10);
  if (Number.isFinite(numericLevel))
    state.stats.maxLevel = Math.max(state.stats.maxLevel, numericLevel);

  el.lightValue.textContent = formatNumber(row.light, 0);
  el.tempValue.textContent = "N/A";
  el.humiValue.textContent = "N/A";
  el.gasValue.textContent = "N/A";
  el.modeText.textContent = row.mode;
  el.srcText.textContent = row.src;
  el.riskText.textContent = `risk ${formatNumber(row.risk, 0)} / 10`;
  el.uptimeText.textContent = `${formatNumber(row.ms / 1000, 1)} s`;
  el.seqText.textContent = String(row.seq);
  el.rawLevelText.textContent = String(row.rawLevel);
  el.errText.textContent = row.err;
  setSystemState(row.state, row.rawLevel);
  updateDiagnosis(row);
  drawChart();
}

function updateDiagnosis(row) {
  const parts = [];
  if (!Number.isFinite(row.light)) {
    parts.push("ENV 帧中 light 字段不是有效数字。");
  } else if (row.light >= state.config.alarmMv) {
    parts.push(
      `PA0=${row.light} mV，超过报警阈值 ${state.config.alarmMv} mV。`,
    );
  } else if (row.light >= state.config.warnMv) {
    parts.push(`PA0=${row.light} mV，超过预警阈值 ${state.config.warnMv} mV。`);
  } else {
    parts.push(`PA0=${row.light} mV，低于预警阈值 ${state.config.warnMv} mV。`);
  }
  if (row.state === "ALARM")
    parts.push(
      `系统处于 ALARM，原始 level=${row.rawLevel}，显示为 ${row.displayLevel}。`,
    );
  if (row.state === "WARN") parts.push("系统处于 WARN，PA2 预警灯应点亮。");
  if (row.state === "NORMAL")
    parts.push("系统处于 NORMAL，PA1 正常指示灯应点亮。");
  if (state.crc > 0 || state.drop > 0)
    parts.push("链路计数存在异常，应核对帧格式和校验。");
  el.diagnosisText.textContent = parts.join(" ");
}

function stateCode(name) {
  const normalized = String(name || "UNKNOWN").toUpperCase();
  if (normalized.includes("FAULT")) return STATE_CODE.FAULT;
  return STATE_CODE[normalized] ?? STATE_CODE.UNKNOWN;
}

function runTimeSeconds() {
  return Math.floor((state.lastRow?.ms || state.elapsedMs || 0) / 1000);
}

function recordEvent(eventName) {
  const evt = String(eventName || "").trim();
  if (!evt) return;
  const row = state.lastRow;
  const level = Number.parseInt(row?.rawLevel ?? 0, 10);
  const risk = Number(row?.risk ?? 0);
  const light = Number(row?.light ?? 0);
  state.eventLogs.push({
    ts: runTimeSeconds(),
    evt: evt.slice(0, 23),
    lv: Number.isFinite(level) ? level : 0,
    st: stateCode(row?.state),
    risk: Number.isFinite(risk) ? risk : 0,
    err: row?.err || "00",
    light: Number.isFinite(light) ? light : 0,
  });
  if (state.eventLogs.length > EVENT_LOG_SIZE) state.eventLogs.shift();

  if (evt === "WARN_ENTER") state.stats.warnTotal += 1;
  if (evt === "ALARM_ENTER_L1") state.stats.alarmTotal += 1;
  if (evt.includes("FAULT") || evt.includes("ADC_"))
    state.stats.faultTotal += 1;
  if (evt === "KEY_PRESSED") state.stats.keyTotal += 1;
}

function configPayload() {
  return [
    "@CFG",
    "LLW=0",
    `LHW=${state.config.warnMv}`,
    "LLA=0",
    `LHA=${state.config.alarmMv}`,
    "THW=0",
    "THA=0",
    "HLW=0",
    "HHW=0",
    "HLA=0",
    "HHA=0",
    "GHW=0",
    "GHA=0",
    `UP=${state.config.uploadMs}`,
    "MODE=PRO_LITE",
    `BZ=${state.config.buzzer}`,
    `SEQ=${state.config.seq}`,
  ].join(",");
}

function statPayload() {
  const minVal = state.stats.lightMin === null ? 0 : state.stats.lightMin;
  return [
    "@STAT",
    `ALARM_TOTAL=${state.stats.alarmTotal}`,
    `WARN_TOTAL=${state.stats.warnTotal}`,
    `FAULT_TOTAL=${state.stats.faultTotal}`,
    `MAX_LEVEL=${state.stats.maxLevel}`,
    `L_MAX=${state.stats.lightMax}`,
    `L_MIN=${minVal}`,
    `RUN=${runTimeSeconds()}`,
    `KEY=${state.stats.keyTotal}`,
    `SAVE=${state.stats.saveTotal}`,
    `DROP=${state.drop}`,
    `OVF=${state.stats.overflowTotal}`,
  ].join(",");
}

function emitLogDump() {
  state.eventLogs.forEach((entry, index) => {
    processLine(
      frameWithChecksum(
        [
          "@LOG",
          `idx=${index}`,
          `ts=${entry.ts}`,
          `evt=${entry.evt}`,
          `lv=${entry.lv}`,
          `st=${entry.st}`,
          `risk=${entry.risk}`,
          `err=${entry.err}`,
          `light=${entry.light}`,
        ].join(","),
      ),
    );
  });
  processLine(frameWithChecksum(`@LOG,END,count=${state.eventLogs.length}`));
}

function refreshConfigDependentUi() {
  if (state.lastRow) updateDiagnosis(state.lastRow);
  drawChart();
}

function restartTelemetryTimer() {
  if (!state.timer) return;
  clearInterval(state.timer);
  state.timer = window.setInterval(emitTelemetry, state.config.uploadMs);
}

function saveConfigSnapshot() {
  state.config.seq += 1;
  state.stats.saveTotal += 1;
  recordEvent("CFG_SAVE");
}

function resetConfigToDefault() {
  state.config.warnMv = DEFAULT_CONFIG.warnMv;
  state.config.alarmMv = DEFAULT_CONFIG.alarmMv;
  state.config.uploadMs = DEFAULT_CONFIG.uploadMs;
  state.config.buzzer = DEFAULT_CONFIG.buzzer;
  recordEvent("CFG_DEFAULT");
  saveConfigSnapshot();
  restartTelemetryTimer();
  refreshConfigDependentUi();
}

function parseSetCommand(clean) {
  if (!clean.startsWith("SET,")) return null;
  const body = clean.slice(4);
  const eq = body.indexOf("=");
  if (eq <= 0) return null;
  const key = body.slice(0, eq);
  const rawValue = body.slice(eq + 1);
  if (key.length > 8 || !/^\d+$/.test(rawValue)) return null;
  const value = Number.parseInt(rawValue, 10);
  if (!Number.isFinite(value) || value > 65535) return null;
  return { key, value };
}

function applySetCommand(key, value) {
  let eventName = "";
  if (key === "LHW" || key === "LWW" || key === "WARN") {
    if (value >= state.config.alarmMv) return false;
    state.config.warnMv = value;
    eventName = "CFG_SET_WARN";
  } else if (key === "LHA" || key === "LAA" || key === "ALARM") {
    if (value <= state.config.warnMv || value >= 3300) return false;
    state.config.alarmMv = value;
    eventName = "CFG_SET_ALARM";
  } else if (key === "UP") {
    if (value < 100 || value > 5000) return false;
    state.config.uploadMs = value;
    eventName = "CFG_SET_UPLOAD";
    restartTelemetryTimer();
  } else if (key === "BZ") {
    if (value > 1) return false;
    state.config.buzzer = value;
    eventName = "CFG_SET_BUZZER";
  } else {
    return false;
  }
  recordEvent(eventName);
  refreshConfigDependentUi();
  return true;
}

function processLine(rawLine) {
  const line = rawLine.trim();
  if (!line) return;
  state.rx += 1;

  if (!verifyFrame(line)) {
    updateCounters();
    log(`校验失败或缺少 *CS：${line}`, "error");
    return;
  }

  const { type, payload, fields } = fieldsFromLine(line);
  if (type === "ENV") {
    parseEnv(line);
    log(payload);
  } else if (type === "CFG") {
    state.lastConfig = fields;
    log(`配置：${payload}`);
  } else if (type === "STAT") {
    state.lastStat = fields;
    log(`统计：${payload}`);
  } else if (type === "EVT") {
    recordEvent(fields.msg || "");
    log(`事件：${fields.msg || payload}`);
  } else if (type === "ACK") {
    log(`确认：${payload}`);
  } else if (type === "NACK") {
    log(`拒绝：${payload}`, "error");
  } else if (type === "LOG") {
    log(`日志：${payload}`);
  } else {
    log(payload);
  }
  updateCounters();
}

function emitTelemetry() {
  const point = TELEMETRY_TRACE[state.index % TELEMETRY_TRACE.length];
  const seq = state.index + 1;
  state.elapsedMs += state.config.uploadMs;
  const ms = state.elapsedMs;
  const payload = [
    "@ENV",
    `seq=${seq}`,
    `ms=${ms}`,
    "mode=PRO_LITE",
    `light=${point.light}`,
    "temp=0.0",
    "humi=0.0",
    "gas=0",
    `state=${point.state}`,
    `level=${point.level}`,
    `risk=${point.risk}`,
    `src=${point.src}`,
    "err=00",
  ].join(",");
  processLine(frameWithChecksum(payload));
  if (point.event)
    processLine(frameWithChecksum(`@EVT,msg=${point.event},ms=${ms}`));
  state.index += 1;
}

function startLockedFeed() {
  clearInterval(state.timer);
  setLink("LINK ONLINE");
  log("地面站链路已建立：USART1 115200 8N1，PRO_LITE 单 ADC 遥测。", "info");
  processLine(frameWithChecksum("@EVT,msg=SYSTEM_START,ms=0"));
  emitTelemetry();
  state.timer = window.setInterval(emitTelemetry, state.config.uploadMs);
}

function sendCommand(command) {
  const frame = buildCommand(command);
  if (!frame) return;
  log(frame, "tx");
  commandResponse(command);
}

function commandResponse(command) {
  const clean = command
    .trim()
    .replace(/^@CMD,/, "")
    .split("*", 1)[0];
  if (clean === "STAT?") {
    processLine(frameWithChecksum(statPayload()));
  } else if (clean === "CFG?") {
    processLine(frameWithChecksum(configPayload()));
  } else if (clean === "LOG?") {
    emitLogDump();
  } else if (clean === "CLRLOG" || clean === "LOGCLR") {
    state.eventLogs = [];
    processLine(frameWithChecksum("@ACK,cmd=CLRLOG"));
  } else if (clean.startsWith("SET,")) {
    const parsed = parseSetCommand(clean);
    if (!parsed) {
      processLine(frameWithChecksum("@NACK,cmd=CMD,err=UNKNOWN_CMD"));
    } else if (applySetCommand(parsed.key, parsed.value)) {
      processLine(
        frameWithChecksum(`@ACK,cmd=SET,key=${parsed.key},val=${parsed.value}`),
      );
    } else {
      processLine(
        frameWithChecksum(`@NACK,cmd=SET,err=BAD_VALUE,key=${parsed.key}`),
      );
    }
  } else if (clean === "SAVE") {
    saveConfigSnapshot();
    processLine(frameWithChecksum("@ACK,cmd=SAVE"));
  } else if (clean === "RESET" || clean === "DEFAULT") {
    state.index = 0;
    state.elapsedMs = 0;
    resetConfigToDefault();
    processLine(frameWithChecksum("@ACK,cmd=DEFAULT"));
    processLine(frameWithChecksum("@EVT,msg=CFG_DEFAULT_RESTORED,ms=0"));
  } else if (clean === "MODE=PRO_LITE" || clean === "MODE=LITE") {
    processLine(frameWithChecksum("@ACK,cmd=MODE=PRO_LITE"));
    processLine(frameWithChecksum("@EVT,msg=MODE_PRO_LITE,ms=0"));
  } else if (clean === "MODE=PRO" || clean === "MODE=BASIC") {
    processLine(frameWithChecksum("@NACK,cmd=MODE,err=HW_SINGLE_ADC_ONLY"));
  } else {
    processLine(frameWithChecksum("@NACK,cmd=CMD,err=UNKNOWN_CMD"));
  }
}

function exportCsv() {
  if (!state.history.length) {
    log("没有可导出的 ENV 遥测。", "error");
    return;
  }
  const headers = [
    "pcTime",
    "seq",
    "ms",
    "mode",
    "light",
    "state",
    "rawLevel",
    "displayLevel",
    "risk",
    "src",
    "err",
  ];
  const rows = [headers.join(",")];
  for (const row of state.history)
    rows.push(headers.map((key) => JSON.stringify(row[key] ?? "")).join(","));
  const blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `ground-station-${new Date().toISOString().replace(/[:.]/g, "-")}.csv`;
  a.click();
  URL.revokeObjectURL(url);
  log(`已导出 ${state.history.length} 条 ENV 记录。`, "info");
}

function localAnalysis() {
  const row = state.lastRow;
  if (!row) return "当前还没有 ENV 遥测。";
  const lines = [
    `当前状态：${row.state}，显示等级 ${row.displayLevel}，风险 ${row.risk}/10。`,
    `PA0 ADC：${Number.isFinite(row.light) ? `${row.light} mV` : "无效"}；预警阈值 ${state.config.warnMv} mV，报警阈值 ${state.config.alarmMv} mV。`,
    `链路统计：RX=${state.rx}，CRC=${state.crc}，DROP=${state.drop}。`,
  ];
  if (row.state === "ALARM")
    lines.push("处理建议：降低 PA0 输入或检查光敏分压，观察报警是否逐级退出。");
  if (row.state === "WARN")
    lines.push("处理建议：ADC 输入接近阈值，继续观察趋势。");
  if (row.state === "NORMAL") lines.push("处理建议：系统处于正常监测区间。");
  return lines.join("\n");
}

function appendAiMessage(role, content, local = true) {
  state.aiMessages.push({ role, content, local });
  if (state.aiMessages.length > 12) state.aiMessages.shift();
  el.aiChatLog.textContent = "";
  for (const message of state.aiMessages) {
    const item = document.createElement("div");
    item.className = `ai-message ${message.role}${message.local ? " local" : ""}`;
    const label = document.createElement("strong");
    label.textContent =
      message.role === "user" ? "QUERY" : message.local ? "LOCAL" : "DEEPSEEK";
    const body = document.createElement("span");
    body.textContent = message.content;
    item.append(label, body);
    el.aiChatLog.appendChild(item);
  }
  el.aiChatLog.scrollTop = el.aiChatLog.scrollHeight;
}

async function analyzeTelemetry() {
  const prompt = localAnalysis();
  appendAiMessage("user", "分析当前遥测");
  el.aiStatus.className = "ai-status busy";
  el.aiStatusText.textContent = "分析中";
  try {
    const response = await fetch(AI_ENDPOINT, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        messages: [
          {
            role: "system",
            content:
              "你是 STM32F103C8 项目的地面站分析助手。只能基于给定遥测给出简短工程建议。",
          },
          { role: "user", content: prompt },
        ],
        temperature: 0.2,
        max_tokens: 500,
      }),
    });
    const data = await response.json().catch(() => ({}));
    const answer = data.choices?.[0]?.message?.content || data.reply || prompt;
    appendAiMessage("assistant", answer, data.local === true);
  } catch (_) {
    appendAiMessage("assistant", prompt, true);
  } finally {
    el.aiStatus.className = "ai-status ready";
    el.aiStatusText.textContent = "本地就绪";
  }
}

function resizeCanvas() {
  const rect = el.canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  el.canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  el.canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawChart();
}

function drawChart() {
  const width = el.canvas.clientWidth;
  const height = el.canvas.clientHeight;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = COLORS.bg;
  ctx.fillRect(0, 0, width, height);

  const pad = { left: 54, right: 22, top: 26, bottom: 42 };
  const plot = {
    left: pad.left,
    top: pad.top,
    width: width - pad.left - pad.right,
    height: height - pad.top - pad.bottom,
  };

  ctx.strokeStyle = COLORS.grid;
  ctx.lineWidth = 1;
  ctx.font = "12px Consolas, monospace";
  ctx.fillStyle = COLORS.muted;
  ctx.textAlign = "right";
  for (let mv = 0; mv <= 3300; mv += 550) {
    const y = plot.top + plot.height - (mv / 3300) * plot.height;
    ctx.beginPath();
    ctx.moveTo(plot.left, y);
    ctx.lineTo(plot.left + plot.width, y);
    ctx.stroke();
    ctx.fillText(String(mv), plot.left - 10, y + 4);
  }

  drawThreshold(
    plot,
    state.config.warnMv,
    COLORS.warn,
    `WARN ${state.config.warnMv} mV`,
  );
  drawThreshold(
    plot,
    state.config.alarmMv,
    COLORS.alarm,
    `ALARM ${state.config.alarmMv} mV`,
  );

  if (!state.history.length) {
    ctx.fillStyle = COLORS.muted;
    ctx.font = "700 18px Microsoft YaHei UI, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("等待遥测", width / 2, height / 2);
    return;
  }

  const rows = state.history.slice(-MAX_POINTS);
  ctx.strokeStyle = COLORS.light;
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  rows.forEach((row, index) => {
    const x = plot.left + (index / Math.max(1, rows.length - 1)) * plot.width;
    const clamped = Math.max(0, Math.min(3300, Number(row.light) || 0));
    const y = plot.top + plot.height - (clamped / 3300) * plot.height;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  const last = rows[rows.length - 1];
  const x = plot.left + plot.width;
  const y =
    plot.top +
    plot.height -
    (Math.max(0, Math.min(3300, Number(last.light) || 0)) / 3300) * plot.height;
  ctx.fillStyle = COLORS.light;
  ctx.beginPath();
  ctx.arc(x, y, 4, 0, Math.PI * 2);
  ctx.fill();
  ctx.textAlign = "right";
  ctx.fillStyle = COLORS.text;
  ctx.fillText(
    `${formatNumber(last.light)} mV`,
    plot.left + plot.width,
    plot.top + 16,
  );
}

function drawThreshold(plot, mv, color, label) {
  const y = plot.top + plot.height - (mv / 3300) * plot.height;
  ctx.strokeStyle = color;
  ctx.setLineDash([8, 8]);
  ctx.beginPath();
  ctx.moveTo(plot.left, y);
  ctx.lineTo(plot.left + plot.width, y);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.fillStyle = color;
  ctx.textAlign = "left";
  ctx.fillText(label, plot.left + 10, y - 7);
}

el.exportBtn.addEventListener("click", exportCsv);
el.clearLogBtn.addEventListener("click", () => {
  state.logs = [];
  el.terminalLog.textContent = "";
});
el.manualForm.addEventListener("submit", (event) => {
  event.preventDefault();
  sendCommand(el.manualCommand.value);
});
el.quickCommands.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-command]");
  if (!button) return;
  el.manualCommand.value = button.dataset.command;
  sendCommand(button.dataset.command);
});
el.aiAnalyzeBtn.addEventListener("click", analyzeTelemetry);
window.addEventListener("resize", resizeCanvas, { passive: true });

setLink("LINK ONLINE");
setSystemState("ONLINE", "--");
appendAiMessage(
  "assistant",
  "地面站已进入自动遥测状态：PRO_LITE 单 ADC 通道，本地命令响应与固件 protocol.c 保持一致。",
  true,
);
resizeCanvas();
startLockedFeed();
