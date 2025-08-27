// --- Utilities ---
const $ = (sel) => document.querySelector(sel);
const preview = $("#preview");
const colorPicker = $("#colorPicker");
const brightness = $("#brightness");
const brightnessVal = $("#brightnessVal");
const swatches = $("#swatches");
const statusText = $("#statusText");
const dot = $("#dot");

const toast = (msg) => {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.classList.add("show");
  setTimeout(() => el.classList.remove("show"), 1800);
};

const clamp = (n, min, max) => Math.max(min, Math.min(max, n));

function hexToRgb(hex) {
  const s = hex.replace("#", "");
  const bigint = parseInt(s, 16);
  const r = (bigint >> 16) & 255;
  const g = (bigint >> 8) & 255;
  const b = bigint & 255;
  return { r, g, b };
}

const setPreview = (hex) => {
  preview.style.background = hex;
  preview.textContent = hex.toUpperCase();
  const rgb = hexToRgb(hex);
  const lum = (0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b) / 255;
  preview.style.color = lum > 0.6 ? "#111" : "#fff";
  preview.style.textShadow =
    lum > 0.6 ? "0 1px 0 rgba(0,0,0,0.25)" : "0 1px 0 rgba(255,255,255,0.25)";
};

// --- Persistence ---
const STORAGE_KEY = "pi-ir-remote";
function getConfig() {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY)) || {};
  } catch {
    return {};
  }
}
function setConfig(cfg) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(cfg));
}

// --- Network helpers for ESP endpoints ---
function baseUrl() {
  return document.getElementById("baseUrl").value.trim().replace(/\/+$/, "");
}
function apiKey() {
  // Reuse your existing "authToken" input; we’ll send it as ?key=...
  return document.getElementById("authToken").value.trim();
}

async function getJson(url) {
  const res = await fetch(url, { mode: "cors", keepalive: true });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  // health returns JSON; color/ir return JSON too ({"ok":true})
  return res.json().catch(() => ({}));
}

// Scale r/g/b by brightness% on the client
function applyBrightness({ r, g, b }, pct) {
  const f = clamp(pct, 0, 100) / 100;
  return {
    r: Math.round(r * f),
    g: Math.round(g * f),
    b: Math.round(b * f),
  };
}

// ESP8266 expects query params on POST; body is unused.
async function sendColor(hex) {
  const url = baseUrl();
  if (!url) throw new Error("Base URL is empty");
  const key = apiKey();
  const { r, g, b } = hexToRgb(hex);
  const scaled = applyBrightness({ r, g, b }, parseInt(brightness.value || "100", 10));

  const qs = new URLSearchParams({
    r: String(scaled.r),
    g: String(scaled.g),
    b: String(scaled.b),
    ...(key ? { key } : {}),
  });

  const res = await fetch(`${url}/color?${qs.toString()}`, {
    method: "POST",
    mode: "cors",
    keepalive: true,
  });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  return res.json().catch(() => ({}));
}

// --- IR command mapping (fill these with your real codes later) ---
/*
  Put your learned codes here once you have them (using an IR receiver or known docs).
  Example:
  IR_MAP = {
    power_on:  { proto: "NEC", code: "0x00FFA25D", bits: 32 },
    power_off: { proto: "NEC", code: "0x00FFE21D", bits: 32 },
    white:     { proto: "NEC", code: "0x00FF02FD", bits: 32 },
    warm:      { proto: "NEC", code: "0x00FF22DD", bits: 32 },
  };
*/
let IR_MAP = {};

async function sendIrByName(name) {
  const url = baseUrl();
  if (!url) throw new Error("Base URL is empty");
  const key = apiKey();
  const cmd = IR_MAP[name];
  if (!cmd) throw new Error(`IR command "${name}" not mapped yet`);

  const qs = new URLSearchParams({
    proto: cmd.proto,
    code: cmd.code,
    bits: String(cmd.bits || 32),
    ...(key ? { key } : {}),
  });

  const res = await fetch(`${url}/ir?${qs.toString()}`, {
    method: "POST",
    mode: "cors",
    keepalive: true,
  });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  return res.json().catch(() => ({}));
}

async function ping() {
  const url = baseUrl();
  if (!url) return setStatus("Not connected", "warn");
  try {
    const res = await fetch(`${url}/health`, { mode: "cors" });
    if (res.ok) setStatus("Connected", "ok");
    else setStatus("Unreachable", "bad");
  } catch {
    setStatus("Unreachable", "bad");
  }
}

function setStatus(msg, level = "warn") {
  statusText.textContent = msg;
  dot.style.background =
    level === "ok" ? "var(--ok)" : level === "bad" ? "var(--bad)" : "var(--warn)";
}

// --- Debounce for color dragging ---
function debounce(fn, ms) {
  let t;
  return function (...args) {
    clearTimeout(t);
    t = setTimeout(() => fn.apply(this, args), ms);
  };
}

const debouncedSend = debounce(async (hex) => {
  try {
    await sendColor(hex);
    setStatus("Connected", "ok");
  } catch (e) {
    console.error(e);
    setStatus("Error", "bad");
    toast("Send failed");
  }
}, 120);

// --- Init ---
const defaultColors = [
  ["Red", "#ff0000"],
  ["Green", "#00ff00"],
  ["Blue", "#0000ff"],
  ["Yellow", "#ffff00"],
  ["Orange", "#ff7a00"],
  ["Purple", "#8000ff"],
  ["Pink", "#ff33aa"],
  ["Cyan", "#00ffff"],
  ["White", "#ffffff"],
  ["Warm", "#fff3d1"],
  ["Cool", "#e7f1ff"],
  ["Lime", "#bfff00"],
  ["Teal", "#15c2b8"],
  ["Magenta", "#ff00ff"],
  ["Amber", "#ffbf00"],
  ["Indigo", "#3f51b5"],
];

function buildSwatches() {
  for (const [name, hex] of defaultColors) {
    const btn = document.createElement("button");
    btn.className = "swatch";
    btn.title = name;
    btn.dataset.hex = hex;
    btn.style.background = hex;
    btn.textContent = " ";
    btn.addEventListener("click", async () => {
      colorPicker.value = hex;
      setPreview(hex);
      try {
        await sendColor(hex);
        setStatus("Connected", "ok");
        toast(`${name}`);
      } catch {
        setStatus("Error", "bad");
        toast("Send failed");
      }
    });
    swatches.appendChild(btn);
  }
}

function loadSaved() {
  const cfg = getConfig();
  // Default to your ESP mDNS name; change if you prefer the raw IP
  document.getElementById("baseUrl").value = cfg.baseUrl || "http://192.168.31.57";
  document.getElementById("authToken").value = cfg.authToken || ""; // used as ?key=
}

function save() {
  setConfig({
    baseUrl: baseUrl(),
    authToken: apiKey(),
  });
  toast("Saved");
  ping();
}

// Event wiring
document.getElementById("saveBtn").addEventListener("click", save);
document.getElementById("pingBtn").addEventListener("click", ping);

document.getElementById("sendColorBtn").addEventListener("click", async () => {
  const hex = colorPicker.value;
  setPreview(hex);
  try {
    await sendColor(hex);
    setStatus("Connected", "ok");
    toast("Color sent");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});

colorPicker.addEventListener("input", () => {
  const hex = colorPicker.value;
  setPreview(hex);
  debouncedSend(hex);
});

brightness.addEventListener("input", () => {
  brightnessVal.textContent = `${brightness.value}%`;
  debouncedSend(colorPicker.value);
});

// Mode/command buttons now use IR mapping (fill IR_MAP above)
document.querySelectorAll(".modes button").forEach((btn) => {
  btn.addEventListener("click", async () => {
    try {
      await sendIrByName(btn.dataset.mode);
      setStatus("Connected", "ok");
      toast(`${btn.dataset.mode} mode`);
    } catch (e) {
      console.error(e);
      setStatus("Error", "bad");
      toast(e.message.includes("not mapped") ? "Map IR codes first" : "Send failed");
    }
  });
});

// Power, White, Warm via IR mapping too
document.getElementById("btnOn").addEventListener("click", async () => {
  try {
    await sendIrByName("power_on");
    setStatus("Connected", "ok");
    toast("Power ON");
  } catch (e) {
    console.error(e);
    setStatus("Error", "bad");
    toast(e.message.includes("not mapped") ? "Map IR codes first" : "Send failed");
  }
});
document.getElementById("btnOff").addEventListener("click", async () => {
  try {
    await sendIrByName("power_off");
    setStatus("Connected", "ok");
    toast("Power OFF");
  } catch (e) {
    console.error(e);
    setStatus("Error", "bad");
    toast(e.message.includes("not mapped") ? "Map IR codes first" : "Send failed");
  }
});

// Boot
buildSwatches();
loadSaved();
setPreview(colorPicker.value);
brightnessVal.textContent = `${brightness.value}%`;
ping();
