// --- Utilities ---
const $ = (sel) => document.querySelector(sel);
const swatches = $("#swatches");
const statusText = $("#statusText");
const dot = $("#dot");
const preview = $("#preview");

const toast = (msg) => {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.classList.add("show");
  setTimeout(() => el.classList.remove("show"), 1600);
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

// --- Network helpers (ESP endpoints) ---
function baseUrl() {
  return document.getElementById("baseUrl").value.trim().replace(/\/+$/, "");
}
function apiKey() {
  return document.getElementById("authToken").value.trim();
}

async function request(rawUrl, method = "POST") {
  const res = await fetch(rawUrl, { method, mode: "cors", keepalive: true });
  if (!res.ok) {
    const t = await res.text().catch(() => res.statusText);
    throw new Error(`HTTP ${res.status}: ${t}`);
  }
  return res.json().catch(() => ({}));
}

async function sendBtn(name) {
  const url = baseUrl();
  if (!url) throw new Error("Base URL is empty");
  const key = apiKey();
  const qs = new URLSearchParams({ name });
  if (key) qs.set("key", key);
  return request(`${url}/btn?${qs.toString()}`);
}
async function sendPower(state) {
  const url = baseUrl();
  if (!url) throw new Error("Base URL is empty");
  const key = apiKey();
  const qs = new URLSearchParams({ state });
  if (key) qs.set("key", key);
  return request(`${url}/power?${qs.toString()}`);
}
async function ping() {
  const url = baseUrl();
  if (!url) return setStatus("Not connected", "warn");
  try {
    const res = await fetch(`${url}/health`, { mode: "cors" });
    setStatus(res.ok ? "Connected" : "Unreachable", res.ok ? "ok" : "bad");
  } catch {
    setStatus("Unreachable", "bad");
  }
}
function setStatus(msg, level = "warn") {
  statusText.textContent = msg;
  dot.style.background =
    level === "ok" ? "var(--ok)" : level === "bad" ? "var(--bad)" : "var(--warn)";
  dot.style.boxShadow =
    level === "ok"
      ? "0 0 14px rgba(34,197,94,.5)"
      : level === "bad"
      ? "0 0 14px rgba(239,68,68,.5)"
      : "0 0 14px rgba(245,158,11,.5)";
}

// --- Swatches (mapped to your learned button names) ---
const defaultColors = [
  // name, hex, button slug (your firmware maps these to NEC codes)
  ["Red", "#ff0000", "red1"],
  ["Orange", "#ff8400ff", "red2"],
  ["Orange-Yellow", "#ffbb00ff", "red3"],
  ["Yellow-Orange", "#ffea00ff", "red4"],
  ["Yellow", "#ffff00", "red5"],
  ["Green", "#00ff44ff", "green1"],
  ["Green-Cyan", "#00ffaeff", "green2"],
  ["Cyan", "#00ffeaff", "green3"],
  ["Blue-Cyan", "#00eeffff", "green4"],
  ["Light-Blue", "#00d5ffff", "green5"],
  ["Blue", "#0000ff", "blue1"],
  ["Dusty-Blue", "#0095ffff", "blue2"],
  ["Purple-Blue", "#9c7dffff", "blue3"],
  ["Purple", "#8000ff", "blue4"],
  ["Pink", "#ff33aa", "blue5"],
  ["White", "#ffffff", "white"],
];

let currentCode = "white";

function setPreview(hexOrCss) {
  preview.style.background = hexOrCss;
  preview.textContent = typeof hexOrCss === "string" ? hexOrCss.toUpperCase().slice(0, 16) : "—";
}

// Build grid
function buildSwatches() {
  swatches.innerHTML = "";
  for (const [name, fill, code] of defaultColors) {
    const btn = document.createElement("button");
    btn.className = "swatch";
    btn.title = name;
    btn.style.background = fill;
    btn.addEventListener("click", async () => {
      currentCode = code;
      setPreview(typeof fill === "string" ? fill : "#000");
      try {
        await sendBtn(code);
        setStatus("Connected", "ok");
        toast(name);
      } catch {
        setStatus("Error", "bad");
        toast("Send failed");
      }
    });
    swatches.appendChild(btn);
  }
}

// --- Brightness with press-and-hold auto-repeat ---
function attachRepeater(el, code, intervalMs = 300) {
  let t;
  const fire = async () => {
    try {
      await sendBtn(code);
      setStatus("Connected", "ok");
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }
  };
  const down = (e) => {
    e.preventDefault();
    fire();
    t = setInterval(fire, intervalMs);
  };
  const up = () => {
    clearInterval(t);
    t = null;
  };
  el.addEventListener("mousedown", down);
  el.addEventListener("touchstart", down, { passive: false });
  ["mouseup", "mouseleave", "touchend", "touchcancel"].forEach((ev) => el.addEventListener(ev, up));
}

// --- Save / Load ---
function loadSaved() {
  const cfg = getConfig();
  document.getElementById("baseUrl").value = cfg.baseUrl || "http://192.168.31.57";
  document.getElementById("authToken").value = cfg.authToken || "";
}
function save() {
  setConfig({ baseUrl: baseUrl(), authToken: apiKey() });
  toast("Saved");
  ping();
}

// --- Wire up UI ---
document.getElementById("saveBtn").addEventListener("click", save);
document.getElementById("pingBtn").addEventListener("click", ping);

// Power
$("#btnPowerOn").addEventListener("click", async () => {
  try {
    await sendPower("on");
    setStatus("Connected", "ok");
    toast("Power ON");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});
$("#btnPowerOff").addEventListener("click", async () => {
  try {
    await sendPower("off");
    setStatus("Connected", "ok");
    toast("Power OFF");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});
$("#btnWhite").addEventListener("click", async () => {
  try {
    await sendBtn("white");
    currentCode = "white";
    setStatus("Connected", "ok");
    toast("White");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});

// Brightness repeaters
attachRepeater($("#btnDim"), "bright_down", 280);
attachRepeater($("#btnBright"), "bright_up", 280);

// Modes
document.querySelectorAll(".mode").forEach((b) => {
  b.addEventListener("click", async () => {
    try {
      await sendBtn(b.dataset.name);
      setStatus("Connected", "ok");
      toast(`${b.dataset.name} mode`);
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }
  });
});

// Boot
buildSwatches();
loadSaved();
setPreview("—");
ping();
