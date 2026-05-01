"use strict";

const MAX_POINTS = 180;
const COLORS = {
  light: "#25f4ff",
  temp: "#ff4d6d",
  humi: "#65a7ff",
  gas: "#3dffb5",
  muted: "#8ea2bf",
  grid: "rgba(148, 163, 184, 0.14)",
  bg: "#050914"
};

const el = {
  linkPill: document.querySelector("#link-pill"),
  linkText: document.querySelector("#link-text"),
  connectBtn: document.querySelector("#connect-btn"),
  demoBtn: document.querySelector("#demo-btn"),
  exportBtn: document.querySelector("#export-btn"),
  clearLogBtn: document.querySelector("#clear-log-btn"),
  resetBtn: document.querySelector("#reset-btn"),
  configForm: document.querySelector("#config-form"),
  manualForm: document.querySelector("#manual-form"),
  cfgKey: document.querySelector("#cfg-key"),
  cfgValue: document.querySelector("#cfg-value"),
  manualCommand: document.querySelector("#manual-command"),
  orbCore: document.querySelector("#orb-core"),
  stateWord: document.querySelector("#state-word"),
  levelWord: document.querySelector("#level-word"),
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
  autoText: document.querySelector("#auto-text"),
  diagnosisText: document.querySelector("#diagnosis-text"),
  terminalLog: document.querySelector("#terminal-log"),
  canvas: document.querySelector("#telemetry-canvas"),
  serialHelp: document.querySelector("#serial-help")
};

const state = {
  port: null,
  reader: null,
  writer: null,
  connected: false,
  demo: false,
  demoTimer: 0,
  rx: 0,
  crc: 0,
  drop: 0,
  lastState: "OFFLINE",
  alarmStart: 0,
  history: [],
  logs: []
};

const ctx = el.canvas.getContext("2d");

function calcChecksum(payload) {
  let checksum = 0;
  for (const ch of payload) checksum ^= ch.charCodeAt(0);
  return checksum;
}

function frameWithChecksum(payload) {
  return `${payload}*${calcChecksum(payload).toString(16).toUpperCase().padStart(2, "0")}`;
}

function buildCommand(command) {
  if (command.startsWith("@") && command.includes("*")) return `${command}\r\n`;
  const payload = command.startsWith("@") ? command.split("*", 1)[0] : `@CMD,${command}`;
  return `${frameWithChecksum(payload)}\r\n`;
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

function fieldsFromLine(line) {
  const payload = line.split("*", 1)[0];
  const fields = {};
  for (const part of payload.split(",").slice(1)) {
    const index = part.indexOf("=");
    if (index > 0) fields[part.slice(0, index)] = part.slice(index + 1);
  }
  return { payload, fields };
}

function log(message, tone = "info") {
  const time = new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3
  }).format(new Date());
  const prefix = tone === "error" ? "ERR" : tone === "tx" ? "TX" : "RX";
  state.logs.push(`[${time}] ${prefix}  ${message}`);
  if (state.logs.length > 220) state.logs.shift();
  el.terminalLog.textContent = state.logs.join("\n");
  el.terminalLog.scrollTop = el.terminalLog.scrollHeight;
}

function setLink(mode, text) {
  el.linkPill.classList.toggle("online", mode === "online");
  el.linkPill.classList.toggle("demo", mode === "demo");
  el.linkText.textContent = text;
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
    maximumFractionDigits: digits
  }).format(number);
}

function setSystemState(systemState, level, source) {
  const normalized = String(systemState || "UNKNOWN").toUpperCase();
  const className = normalized === "NORMAL" ? "state-normal" :
    normalized === "WARN" ? "state-warn" :
    normalized === "ALARM" ? "state-alarm" :
    normalized.includes("FAULT") ? "state-fault" : "state-offline";

  el.orbCore.className = `orb-core ${className}`;
  el.stateWord.textContent = normalized;
  el.levelWord.textContent = `LEVEL ${level ?? "--"}`;
  el.srcText.textContent = source || "NONE";

  if (normalized !== state.lastState) {
    state.lastState = normalized;
    state.alarmStart = performance.now();
  }
}

function updateDiagnosis(row) {
  const source = String(row.src || "NONE");
  const status = String(row.state || "UNKNOWN");
  const parts = [];

  if (status === "NORMAL") parts.push("系统处于正常状态，当前遥测未触发报警条件。");
  if (status === "WARN") parts.push("系统进入预警状态，建议观察趋势是否持续靠近阈值。");
  if (status === "ALARM") parts.push(`系统处于报警状态，报警等级 L${row.level ?? "--"}。`);
  if (status.includes("FAULT")) parts.push("系统报告传感器或采样故障，应优先检查硬件链路。");
  if (source.includes("LIGHT")) parts.push("光照通道异常：检查 LDR、电阻分压、遮光和接线。");
  if (source.includes("GAS")) parts.push("气体通道异常：检查模拟输入、电位器或 MQ 传感器供电。");
  if (source.includes("DHT")) parts.push("DHT11 异常：检查 PA4 数据线、上拉电阻、电源和时序。");
  if (source.includes("ADC")) parts.push("ADC 异常：检查 PA5/PA7 输入、电源和参考电压。");

  el.diagnosisText.textContent = parts.length ? parts.join(" ") : "已收到遥测，但状态字段不足以形成诊断。";
}

function parseEnv(line) {
  const { fields } = fieldsFromLine(line);
  const row = {
    pcTime: new Date().toISOString(),
    ms: Number(fields.ms || 0),
    mode: fields.mode || "--",
    light: Number(fields.light),
    temp: Number(fields.temp),
    humi: Number(fields.humi),
    gas: Number(fields.gas),
    state: fields.state || "UNKNOWN",
    level: fields.level || "0",
    src: fields.src || "NONE"
  };

  state.history.push(row);
  if (state.history.length > MAX_POINTS) state.history.shift();

  el.lightValue.textContent = formatNumber(row.light, 0);
  el.tempValue.textContent = formatNumber(row.temp, 1);
  el.humiValue.textContent = formatNumber(row.humi, 1);
  el.gasValue.textContent = formatNumber(row.gas, 0);
  el.modeText.textContent = row.mode;
  el.uptimeText.textContent = `${formatNumber(row.ms / 1000, 1)} s`;
  setSystemState(row.state, row.level, row.src);
  updateDiagnosis(row);
  updateAutoEscalation(row);
  drawChart();
}

function updateAutoEscalation(row) {
  if (row.state !== "ALARM") {
    el.autoText.textContent = "--";
    return;
  }
  const level = Number.parseInt(row.level, 10);
  if (level >= 3) {
    el.autoText.textContent = "MAX L3";
    return;
  }
  const elapsed = performance.now() - state.alarmStart;
  el.autoText.textContent = `${Math.max(0, 2000 - elapsed).toFixed(0)} ms`;
}

function processLine(rawLine) {
  const line = rawLine.trim();
  if (!line) return;
  state.rx += 1;
  if (!verifyFrame(line)) {
    updateCounters();
    log(`校验失败：${line}`, "error");
    return;
  }

  if (line.startsWith("@ENV")) parseEnv(line);
  else if (line.startsWith("@CFG")) log(`配置更新 ${fieldsFromLine(line).payload}`);
  else if (line.startsWith("@LOG")) log(`事件 ${fieldsFromLine(line).payload}`);
  else if (line.startsWith("@STAT")) log(`统计 ${fieldsFromLine(line).payload}`);
  else log(line);
  updateCounters();
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    el.serialHelp.showModal();
    log("当前浏览器不支持 Web Serial。请使用 Chrome/Edge，并通过 localhost 打开页面。", "error");
    return;
  }
  try {
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
    try {
      await state.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch (_) {
      log("串口控制线未设置，继续使用 115200 8N1 接收数据。", "error");
    }
    state.writer = state.port.writable.getWriter();
    state.connected = true;
    el.connectBtn.textContent = "断开串口";
    setLink("online", "串口在线");
    log("串口连接成功：115200 8N1");
    readLoop();
  } catch (error) {
    if (state.port) {
      try { await state.port.close(); } catch (_) {}
    }
    state.port = null;
    log(`串口连接失败：${error.message}`, "error");
  }
}

async function disconnectSerial() {
  state.connected = false;
  try {
    if (state.reader) await state.reader.cancel();
  } catch (_) {}
  try {
    if (state.writer) state.writer.releaseLock();
    if (state.port) await state.port.close();
  } catch (error) {
    log(`关闭串口异常：${error.message}`, "error");
  }
  state.reader = null;
  state.writer = null;
  state.port = null;
  el.connectBtn.textContent = "连接串口";
  setLink("offline", "离线");
  setSystemState("OFFLINE", "--", "NONE");
  log("串口已断开");
}

async function readLoop() {
  const decoder = new TextDecoder();
  let buffer = "";
  while (state.connected && state.port?.readable) {
    state.reader = state.port.readable.getReader();
    try {
      while (state.connected) {
        const { value, done } = await state.reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split(/\r?\n/);
        buffer = lines.pop() || "";
        for (const line of lines) processLine(line);
      }
    } catch (error) {
      if (state.connected) log(`读取异常：${error.message}`, "error");
    } finally {
      state.reader.releaseLock();
      state.reader = null;
    }
  }
}

async function sendCommand(command) {
  const clean = command.trim();
  if (!clean) return;
  if (state.demo) {
    log(`演示模式忽略硬件发送：${clean}`, "tx");
    return;
  }
  if (!state.connected || !state.writer) {
    log("串口未连接，无法发送命令。", "error");
    return;
  }
  const encoded = new TextEncoder().encode(buildCommand(clean));
  await state.writer.write(encoded);
  log(buildCommand(clean).trim(), "tx");
}

function toggleDemo() {
  if (state.demo) {
    clearInterval(state.demoTimer);
    state.demo = false;
    el.demoBtn.textContent = "启动演示";
    setLink(state.connected ? "online" : "offline", state.connected ? "串口在线" : "离线");
    log("演示模式停止");
    return;
  }
  state.demo = true;
  el.demoBtn.textContent = "停止演示";
  setLink("demo", "演示运行");
  log("演示模式启动：生成模拟遥测");
  state.demoTimer = window.setInterval(generateDemoFrame, 650);
  generateDemoFrame();
}

function generateDemoFrame() {
  const t = Date.now() / 1000;
  const ms = state.history.length ? state.history[state.history.length - 1].ms + 650 : 0;
  const light = 1900 + Math.sin(t * 0.8) * 620 + Math.random() * 80;
  const temp = 27 + Math.sin(t * 0.25) * 4 + Math.random() * 0.4;
  const humi = 56 + Math.cos(t * 0.31) * 10 + Math.random() * 1.2;
  const gas = 1250 + Math.sin(t * 0.55) * 780 + Math.random() * 90;
  const alarm = light > 2380 || gas > 1850;
  const warn = light > 2200 || gas > 1650;
  const systemState = alarm ? "ALARM" : warn ? "WARN" : "NORMAL";
  const level = alarm ? (gas > 2050 ? 3 : 2) : warn ? 1 : 0;
  const src = alarm || warn ? [light > 2200 ? "LIGHT" : "", gas > 1650 ? "GAS" : ""].filter(Boolean).join("|") : "NONE";
  const payload = `@ENV,ms=${Math.round(ms)},mode=PRO,light=${Math.round(light)},temp=${temp.toFixed(1)},humi=${humi.toFixed(1)},gas=${Math.round(gas)},state=${systemState},level=${level},src=${src},err=0`;
  processLine(frameWithChecksum(payload));
}

function exportCsv() {
  if (!state.history.length) {
    log("没有可导出的遥测数据。", "error");
    return;
  }
  const headers = ["pcTime", "ms", "mode", "light", "temp", "humi", "gas", "state", "level", "src"];
  const rows = [headers.join(",")];
  for (const row of state.history) rows.push(headers.map((key) => JSON.stringify(row[key] ?? "")).join(","));
  const blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `stm32-telemetry-${new Date().toISOString().replace(/[:.]/g, "-")}.csv`;
  a.click();
  URL.revokeObjectURL(url);
  log(`已导出 ${state.history.length} 条 CSV 记录`);
}

function resizeCanvas() {
  const rect = el.canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  el.canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  el.canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawChart();
}

function scaleValue(value, min, max, top, height) {
  if (max <= min) return top + height / 2;
  return top + height - ((value - min) / (max - min)) * height;
}

function drawSeries(area, values, color, label, unit) {
  const finite = values.filter(Number.isFinite);
  if (finite.length < 2) return;
  let min = Math.min(...finite);
  let max = Math.max(...finite);
  const pad = Math.max((max - min) * 0.16, 1);
  min -= pad;
  max += pad;

  ctx.save();
  ctx.beginPath();
  values.forEach((value, index) => {
    const x = area.left + (index / Math.max(1, values.length - 1)) * area.width;
    const y = scaleValue(value, min, max, area.top, area.height);
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.lineWidth = 9;
  ctx.strokeStyle = `${color}20`;
  ctx.stroke();
  ctx.lineWidth = 4;
  ctx.strokeStyle = `${color}66`;
  ctx.stroke();
  ctx.lineWidth = 2;
  ctx.strokeStyle = color;
  ctx.stroke();

  const gradient = ctx.createLinearGradient(0, area.top, 0, area.top + area.height);
  gradient.addColorStop(0, `${color}30`);
  gradient.addColorStop(1, `${color}00`);
  ctx.lineTo(area.left + area.width, area.top + area.height);
  ctx.lineTo(area.left, area.top + area.height);
  ctx.closePath();
  ctx.fillStyle = gradient;
  ctx.fill();

  ctx.fillStyle = color;
  ctx.font = "700 12px Cascadia Mono, Consolas, monospace";
  ctx.fillText(label, area.left, area.top + 16);
  ctx.fillStyle = COLORS.muted;
  ctx.textAlign = "right";
  ctx.fillText(`${formatNumber(values[values.length - 1], unit === "°C" || unit === "%" ? 1 : 0)} ${unit}`, area.left + area.width, area.top + 16);
  ctx.textAlign = "left";
  ctx.restore();
}

function drawChart() {
  const width = el.canvas.clientWidth;
  const height = el.canvas.clientHeight;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = COLORS.bg;
  ctx.fillRect(0, 0, width, height);

  const padding = { left: 26, right: 26, top: 24, bottom: 24 };
  const plotWidth = width - padding.left - padding.right;
  const rowHeight = (height - padding.top - padding.bottom - 36) / 4;
  const gap = 12;

  ctx.strokeStyle = COLORS.grid;
  ctx.lineWidth = 1;
  for (let i = 0; i <= 8; i += 1) {
    const x = padding.left + (plotWidth / 8) * i;
    ctx.beginPath();
    ctx.moveTo(x, padding.top);
    ctx.lineTo(x, height - padding.bottom);
    ctx.stroke();
  }

  if (!state.history.length) {
    ctx.fillStyle = COLORS.muted;
    ctx.font = "700 18px Microsoft YaHei UI, Segoe UI, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("等待串口数据，或点击启动演示", width / 2, height / 2);
    ctx.textAlign = "left";
    return;
  }

  const rows = state.history.slice(-MAX_POINTS);
  const channels = [
    ["LIGHT", rows.map((row) => row.light), COLORS.light, "mV"],
    ["TEMP", rows.map((row) => row.temp), COLORS.temp, "°C"],
    ["HUMI", rows.map((row) => row.humi), COLORS.humi, "%"],
    ["GAS", rows.map((row) => row.gas), COLORS.gas, "mV"]
  ];

  channels.forEach(([label, values, color, unit], index) => {
    const top = padding.top + index * (rowHeight + gap);
    const area = { left: padding.left, top, width: plotWidth, height: rowHeight };
    ctx.fillStyle = index % 2 === 0 ? "rgba(255,255,255,0.018)" : "rgba(37,244,255,0.018)";
    ctx.fillRect(area.left, area.top, area.width, area.height);
    ctx.strokeStyle = "rgba(148,163,184,0.12)";
    ctx.strokeRect(area.left, area.top, area.width, area.height);
    drawSeries(area, values, color, label, unit);
  });

  rows.forEach((row, index) => {
    if (!["ALARM", "FAULT", "FAULT_LOCK"].includes(row.state)) return;
    const x = padding.left + (index / Math.max(1, rows.length - 1)) * plotWidth;
    ctx.fillStyle = "rgba(255, 77, 109, 0.08)";
    ctx.fillRect(x, padding.top, Math.max(2, plotWidth / MAX_POINTS), height - padding.top - padding.bottom);
  });
}

el.connectBtn.addEventListener("click", () => {
  if (state.connected) disconnectSerial();
  else connectSerial();
});

el.demoBtn.addEventListener("click", toggleDemo);
el.exportBtn.addEventListener("click", exportCsv);
el.clearLogBtn.addEventListener("click", () => { state.logs = []; el.terminalLog.textContent = ""; });
el.resetBtn.addEventListener("click", () => sendCommand("RESET"));

el.configForm.addEventListener("submit", (event) => {
  event.preventDefault();
  sendCommand(`SET,${el.cfgKey.value}=${el.cfgValue.value}`);
});

el.manualForm.addEventListener("submit", (event) => {
  event.preventDefault();
  sendCommand(el.manualCommand.value);
});

window.addEventListener("resize", resizeCanvas, { passive: true });

if (!window.isSecureContext || !("serial" in navigator)) {
  log("提示：真实串口连接需要 Chrome/Edge，并通过 localhost 或 HTTPS 打开。演示模式可直接使用。", "error");
}

setLink("offline", "离线");
setSystemState("OFFLINE", "--", "NONE");
resizeCanvas();
