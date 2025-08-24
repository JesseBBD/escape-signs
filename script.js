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

// --- Network ---
function baseUrl() {
  return document.getElementById("baseUrl").value.trim();
}

async function request(path, body) {
  const url = baseUrl();
  if (!url) throw new Error("Base URL is empty");
  const token = document.getElementById("authToken").value.trim();
  const res = await fetch(url.replace(/\/$/, "") + path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: JSON.stringify(body),
    mode: "cors",
    keepalive: true,
  });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  return res.json().catch(() => ({}));
}

async function sendColor(hex) {
  const { r, g, b } = hexToRgb(hex);
  const bPct = clamp(parseInt(brightness.value, 10), 0, 100);
  const payload = { r, g, b, brightness: bPct };
  const data = await request("/color", payload);
  return data;
}

async function sendCommand(name) {
  const data = await request("/command", { name });
  return data;
}

async function sendPower(state) {
  const data = await request("/power", { state });
  return data;
}

async function ping() {
  const url = baseUrl();
  if (!url) return setStatus("Not connected", "warn");
  try {
    const res = await fetch(url.replace(/\/$/, "") + "/health", { mode: "cors" });
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
  document.getElementById("baseUrl").value = cfg.baseUrl || "http://raspberrypi.local:8000";
  document.getElementById("authToken").value = cfg.authToken || "";
}

function save() {
  setConfig({ baseUrl: baseUrl(), authToken: document.getElementById("authToken").value.trim() });
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

document.querySelectorAll(".modes button").forEach((btn) => {
  btn.addEventListener("click", async () => {
    try {
      await sendCommand(btn.dataset.mode);
      setStatus("Connected", "ok");
      toast(`${btn.dataset.mode} mode`);
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }
  });
});

document.getElementById("btnOn").addEventListener("click", async () => {
  try {
    await sendPower("on");
    setStatus("Connected", "ok");
    toast("Power ON");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});
document.getElementById("btnOff").addEventListener("click", async () => {
  try {
    await sendPower("off");
    setStatus("Connected", "ok");
    toast("Power OFF");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});
document.getElementById("btnWhite").addEventListener("click", async () => {
  try {
    await sendCommand("white");
    setStatus("Connected", "ok");
    toast("White");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});
document.getElementById("btnWarm").addEventListener("click", async () => {
  try {
    await sendCommand("warm");
    setStatus("Connected", "ok");
    toast("Warm white");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});

// Boot
buildSwatches();
loadSaved();
setPreview(colorPicker.value);
ping();
