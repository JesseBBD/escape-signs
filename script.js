// --- Utilities ---
const $ = (sel) => document.querySelector(sel);
const swatches = $("#swatches");
const statusText = $("#statusText");
const dot = $("#dot");
const preview = $("#preview");
const addToCycleToggle = $("#addToCycle");
const cycleListEl = $("#cycleList");
const cycleStartBtn = $("#cycleStart");
const cycleStopBtn = $("#cycleStop");
const cycleClearBtn = $("#cycleClear");
const cycleIntervalInput = $("#cycleInterval");

// NEW: Dim-between controls
const dimToggle = $("#dimToggle");
const dimTargetInput = $("#dimTarget");
const dimDelayInput = $("#dimDelay");

const toast = (msg) => {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.classList.add("show");
  setTimeout(() => el.classList.remove("show"), 1600);
};

const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const sleep = (ms) => new Promise((res) => setTimeout(res, ms));

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

// --- Preview + Brightness model ---
let currentCode = "white";
let previewBaseFill = "#ffffff"; // CSS background (hex or gradient)
let previewLabel = "White";
let currentBrightness = 1.0; // 0.0 .. 1.0
const BRIGHT_STEP = 1 / 16; // ~6.25% per press
const MIN_PREVIEW_BRIGHT = 0.56; // don't go fully black in UI

function syncPreviewBrightness() {
  const pct = Math.round(currentBrightness * 100);
  preview.style.background = previewBaseFill;
  preview.style.filter = `brightness(${currentBrightness})`;
  preview.textContent = `${previewLabel} • ${pct}%`;
}
function setPreview(fillCss, label) {
  previewBaseFill = fillCss;
  previewLabel = label ?? "—";
  syncPreviewBrightness();
}
function adjustBrightness(delta) {
  currentBrightness = clamp(currentBrightness + delta, MIN_PREVIEW_BRIGHT, 1.0);
  syncPreviewBrightness();
}

// Helpers to press brightness and reflect locally
async function pressBrightness(code, presses, delayMs) {
  for (let i = 0; i < presses; i++) {
    try {
      await sendBtn(code);
      setStatus("Connected", "ok");
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }
    adjustBrightness(code === "bright_up" ? +BRIGHT_STEP : -BRIGHT_STEP);
    if (delayMs > 0) await sleep(delayMs);
  }
}
function pressesNeeded(from, to) {
  const diff = from - to;
  return Math.max(0, Math.ceil(Math.abs(diff) / BRIGHT_STEP));
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

// ===== Cycle playlist state =====
let cycle = []; // array of {name, fill, code}
let cycleTimer = null;
let cycleIndex = 0;

// Render cycle list pills
function renderCycleList() {
  cycleListEl.innerHTML = "";
  if (!cycle.length) {
    cycleListEl.innerHTML = '<span class="muted">No colours queued.</span>';
    return;
  }
  cycle.forEach((item, i) => {
    const pill = document.createElement("button");
    pill.className = "pill";
    pill.style.background = item.fill;
    pill.title = `${item.name} (${item.code}) — click to remove`;
    pill.textContent = item.name;
    pill.addEventListener("click", () => {
      cycle.splice(i, 1);
      persistCycle();
      renderCycleList();
    });
    cycleListEl.appendChild(pill);
  });
}

// Persist cycle + options
function persistCycle() {
  const cfg = getConfig();
  cfg.cycle = cycle;
  cfg.cycleInterval = parseFloat(cycleIntervalInput.value) || 3;
  cfg.addToCycle = addToCycleToggle.checked;
  cfg.dimBetween = dimToggle.checked;
  cfg.dimTargetPct = clamp(parseFloat(dimTargetInput.value) || 70, 56, 100);
  cfg.dimDelay = Math.max(0, parseInt(dimDelayInput.value || "120", 10));
  setConfig(cfg);
}

// Restore cycle + options
function restoreCycle() {
  const cfg = getConfig();
  cycle = Array.isArray(cfg.cycle) ? cfg.cycle : [];
  cycleIntervalInput.value = cfg.cycleInterval ? String(cfg.cycleInterval) : "3";
  addToCycleToggle.checked = !!cfg.addToCycle;

  // NEW: restore dim settings
  dimToggle.checked = !!cfg.dimBetween;
  dimTargetInput.value = cfg.dimTargetPct ? String(cfg.dimTargetPct) : "70";
  dimDelayInput.value = cfg.dimDelay != null ? String(cfg.dimDelay) : "120";

  renderCycleList();
}

// Start cycling
function startCycle() {
  if (cycleTimer) return; // already running
  if (!cycle.length) {
    toast("Add colours first");
    return;
  }
  const intervalSec = Math.max(0.5, parseFloat(cycleIntervalInput.value) || 3);
  cycleIntervalInput.value = String(intervalSec);
  cycleIndex = 0;

  const tick = async () => {
    const startedAt = performance.now();
    const item = cycle[cycleIndex % cycle.length];
    cycleIndex++;

    // Dim-between logic
    const useDim = dimToggle.checked;
    const dipPct = clamp(parseFloat(dimTargetInput.value) || 70, 56, 100);
    const dipTarget = clamp(dipPct / 100, MIN_PREVIEW_BRIGHT, 1.0);
    const pressGap = Math.max(0, parseInt(dimDelayInput.value || "120", 10));

    const prevLevel = currentBrightness;

    if (useDim && prevLevel > dipTarget + 1e-6) {
      // number of presses to go down to dip target
      await pressBrightness("bright_down", pressesNeeded(prevLevel, dipTarget), pressGap);
    }

    // Switch colour
    setPreview(item.fill, item.name);
    currentCode = item.code;
    try {
      await sendBtn(item.code);
      setStatus("Connected", "ok");
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }

    // Bring brightness back up to previous level
    if (useDim && prevLevel > currentBrightness + 1e-6) {
      await pressBrightness("bright_up", pressesNeeded(prevLevel, currentBrightness), pressGap);
    }

    // schedule next, trying to keep overall interval consistent
    const elapsed = (performance.now() - startedAt) / 1000;
    const remaining = Math.max(0, intervalSec - elapsed);
    cycleTimer = setTimeout(tick, remaining * 1000);
  };

  // kick off first tick immediately
  tick();
  cycleStartBtn.disabled = true;
  cycleStopBtn.disabled = false;
}

// Stop cycling
function stopCycle() {
  if (cycleTimer) {
    clearTimeout(cycleTimer);
    cycleTimer = null;
  }
  cycleStartBtn.disabled = false;
  cycleStopBtn.disabled = true;
}

// Clear playlist
function clearCycle() {
  stopCycle();
  cycle = [];
  persistCycle();
  renderCycleList();
  toast("Cycle cleared");
}

// Build swatches with dual behavior (send vs add-to-cycle)
function buildSwatches() {
  swatches.innerHTML = "";
  for (const [name, fill, code] of defaultColors) {
    const btn = document.createElement("button");
    btn.className = "swatch";
    btn.title = name;
    btn.style.background = fill;

    btn.addEventListener("click", async () => {
      if (addToCycleToggle.checked) {
        // Add to playlist (allow duplicates to weight a colour)
        cycle.push({ name, fill, code });
        persistCycle();
        renderCycleList();
        toast(`Added ${name}`);
      } else {
        // Normal send
        currentCode = code;
        setPreview(fill, name);
        try {
          await sendBtn(code);
          setStatus("Connected", "ok");
          toast(name);
        } catch {
          setStatus("Error", "bad");
          toast("Send failed");
        }
      }
    });

    swatches.appendChild(btn);
  }
}

// --- Brightness with press-and-hold auto-repeat (manual buttons) ---
function attachRepeater(el, code, step, intervalMs = 300) {
  let t;
  const fire = async () => {
    try {
      await sendBtn(code);
      setStatus("Connected", "ok");
    } catch {
      setStatus("Error", "bad");
      toast("Send failed");
    }
    adjustBrightness(step); // reflect locally
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
  document.getElementById("baseUrl").value = cfg.baseUrl || "http://192.168.1.115";
  document.getElementById("authToken").value = cfg.authToken || "";
  // start with White @ 100%
  setPreview("#ffffff", "White");
  currentBrightness = 1.0;
  syncPreviewBrightness();
}
function save() {
  setConfig({
    baseUrl: baseUrl(),
    authToken: apiKey(),
    cycle,
    cycleInterval: parseFloat(cycleIntervalInput.value) || 3,
    addToCycle: addToCycleToggle.checked,
    dimBetween: dimToggle.checked,
    dimTargetPct: clamp(parseFloat(dimTargetInput.value) || 70, 56, 100),
    dimDelay: Math.max(0, parseInt(dimDelayInput.value || "120", 10)),
  });
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
    setPreview("#ffffff", "White");
    setStatus("Connected", "ok");
    toast("White");
  } catch {
    setStatus("Error", "bad");
    toast("Send failed");
  }
});

// Brightness repeaters (reflect ~6.25% per press)
attachRepeater($("#btnDim"), "bright_down", -BRIGHT_STEP, 280);
attachRepeater($("#btnBright"), "bright_up", BRIGHT_STEP, 280);

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
restoreCycle();
ping();

// Cycle controls
cycleStartBtn.addEventListener("click", () => {
  persistCycle();
  startCycle();
});
cycleStopBtn.addEventListener("click", stopCycle);
cycleClearBtn.addEventListener("click", clearCycle);
cycleIntervalInput.addEventListener("change", persistCycle);
addToCycleToggle.addEventListener("change", persistCycle);

// NEW: persist dim settings live
dimToggle.addEventListener("change", persistCycle);
dimTargetInput.addEventListener("change", persistCycle);
dimDelayInput.addEventListener("change", persistCycle);
