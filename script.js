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
  // We reuse your "authToken" input; we send it as ?key=...
  return document.getElementById("authToken").value.trim();
}

async function getJson(url) {
  const res = await fetch(url, { mode: "cors", keepalive: true });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  return res.json().catch(() => ({}));
}

// Scale r/g/b by brightness% client-side
function applyBrightness({ r, g, b }, pct) {
  const f = clamp(pct, 0, 100) / 100;
  return {
    r: Math.round(r * f),
    g: Math.round(g * f),
    b: Math.round(b * f),
  };
}

// Send /color as POST with query params (?r=&g=&b=&key=...)
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

// --- IR command mapping ------------------------------
// PRESET A: Example NEC codes (common on small 21/24-key remotes).
// These may or may not match your strip controller — try them first.
// If they don't work, you'll replace with learned codes from your remote.
const IR_MAP_PRESET_NEC = {
  power_on: { proto: "NEC", code: "0x00FFA25D", bits: 32 }, // example
  power_off: { proto: "NEC", code: "0x00FFE21D", bits: 32 }, // example
  white: { proto: "NEC", code: "0x00FF02FD", bits: 32 }, // example
  warm: { proto: "NEC", code: "0x00FF22DD", bits: 32 }, // example
  red: { proto: "NEC", code: "0x00FF9867", bits: 32 }, // example
  green: { proto: "NEC", code: "0x00FF38C7", bits: 32 }, // example
  blue: { proto: "NEC", code: "0x00FF18E7", bits: 32 }, // example
  flash: { proto: "NEC", code: "0x00FFB04F", bits: 32 }, // example
  strobe: { proto: "NEC", code: "0x00FF6897", bits: 32 }, // example
  fade: { proto: "NEC", code: "0x00FF30CF", bits: 32 }, // example
  smooth: { proto: "NEC", code: "0x00FF10EF", bits: 32 }, // example
};

// If you later capture your *real* codes, put them here:
const IR_MAP_YOURS = {
  // power_on:  { proto: "NEC", code: "0xYOURCODE", bits: 32 },
  // power_off: { proto: "NEC", code: "0xYOURCODE", bits: 32 },
  // white:     { proto: "NEC", code: "0xYOURCODE", bits: 32 },
  // warm:      { proto: "NEC", code: "0xYOURCODE", bits: 32 },
  // red:       { proto: "NEC", code: "0xYOURCODE", bits: 32 },
  // ...
};

// Choose which map to use by default:
let IR_MAP = IR_MAP_PRESET_NEC;

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

// Dev helper: try any raw hex quickly from console: window.tryIR("0x00FF02FD")
window.tryIR = async (hex, proto = "NEC", bits = 32) => {
  const url = baseUrl();
  const key = apiKey();
  const qs = new URLSearchParams({ proto, code: hex, bits: String(bits), ...(key ? { key } : {}) });
  const res = await fetch(`${url}/ir?${qs.toString()}`, { method: "POST", mode: "cors" });
  return res.ok;
};

// -----------------------------------------------------

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
  // Default to ESP mDNS; swap to raw IP if you prefer (e.g., http://192.168.31.57)
  document.getElementById("baseUrl").value = cfg.baseUrl || "http://ir-d1.local";
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

// Mode/command buttons use IR mapping
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

// Power + white/warm buttons
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
document.getElementById("btnWhite").addEventListener("click", async () => {
  try {
    await sendIrByName("white");
    setStatus("Connected", "ok");
    toast("White");
  } catch (e) {
    console.error(e);
    setStatus("Error", "bad");
    toast(e.message.includes("not mapped") ? "Map IR codes first" : "Send failed");
  }
});
document.getElementById("btnWarm").addEventListener("click", async () => {
  try {
    await sendIrByName("warm");
    setStatus("Connected", "ok");
    toast("Warm white");
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
