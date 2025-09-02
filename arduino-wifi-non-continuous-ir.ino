// ===== Escape Sign Captive Portal + Live State Sync + IR Send (ESP8266, no RGB pins) =====
// Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266)
// Libs:  IRremoteESP8266 (install via Library Manager)

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ctype.h>
#include <stdlib.h>

// ---- IR ----
const uint16_t IR_PIN = D2; // HW-489 via transistor: SIG->D2, VCC->5V, GND->G
IRsend irsend(IR_PIN);

// ---- Wi-Fi / Captive Portal ----
const char *AP_SSID = "ESCAPE-SIGN";
const char *AP_PASS = "#BBD2025";
IPAddress apIP(192, 168, 4, 1);
DNSServer dns;
ESP8266WebServer server(80);

// ---- Shared state (for live sync) ----
String currentHex = "#00A3FF";
int currentLevel = 7;        // 0..7 for UI; device doesn't drive local RGB anymore
unsigned long currentTs = 0; // monotonic ts for clients

// ---- IR send throttling (prevents rapid repeat blasting) ----
unsigned long lastSendMs = 0;
String lastSentHex = "";
int lastSentLevel = -1;
const unsigned long IR_MIN_GAP_MS = 150; // block identical re-sends within 150ms

// ---- UI Page ----
const char *PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Escape Sign</title>
<style>
:root{--pad:16px;--radius:16px;--shadow:0 8px 24px rgba(0,0,0,.12)}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#0b0f14;color:#e8eef6}
header{padding:var(--pad);text-align:center}
.card{background:#121922;margin:var(--pad);padding:var(--pad);border-radius:var(--radius);box-shadow:var(--shadow)}
.row{display:grid;gap:14px}
@media(min-width:700px){.row{grid-template-columns:1fr 1fr}}
.preview{height:80px;border-radius:var(--radius);display:flex;align-items:center;justify-content:center;font-weight:600;letter-spacing:.5px;background:#111;border:1px solid #2a3340}
.controls label{font-size:14px;opacity:.85;display:block;margin:8px 0 6px}
input[type=color]{width:100%;height:52px;border:none;border-radius:12px;background:#0f1620}
input[type=range]{width:100%}
.grid{display:grid;grid-template-columns:repeat(8,40px);gap:10px}
.sw{width:40px;height:40px;border:1px solid transparent;border-radius:10px;cursor:pointer}
.sw:focus-visible{outline:2px solid #2a3340;outline-offset:2px}
.bar{display:flex;gap:10px;align-items:center;justify-content:space-between}
.pill{padding:6px 10px;background:#0f1620;border:1px solid #2a3340;border-radius:999px;font-size:12px;opacity:.9}
.btn{padding:10px 14px;border-radius:12px;border:0;background:#1a88ff;color:#fff;font-weight:600;cursor:pointer}
.btn:active{transform:translateY(1px)}
#toast{visibility:hidden;min-width:160px;background:#1a88ff;color:#fff;text-align:center;border-radius:999px;padding:10px 16px;position:fixed;left:50%;bottom:24px;transform:translateX(-50%)}
#toast.show{visibility:visible;animation:fadein .2s,fadeout .2s 1.6s}
@keyframes fadein{from{opacity:0;transform:translate(-50%,8px)}to{opacity:1;transform:translate(-50%,0)}}
@keyframes fadeout{from{opacity:1}to{opacity:0;visibility:hidden}}
.dot{width:10px;height:10px;border-radius:50%;background:#22c55e;display:inline-block;margin-right:8px}
</style></head><body>
<header><h2>Escape Sign</h2><div class="pill"><span class="dot"></span>Connected • Live</div></header>
<div class="card row">
  <div class="controls">
    <label>Colour picker</label>
    <input id="colorPicker" type="color" value="#00A3FF" />
    <div class="bar" style="margin-top:12px;">
      <div>
        <label style="margin-top:0;">Brightness (0–7)</label>
        <input id="brightness" type="range" min="0" max="7" value="7" />
      </div>
      <div class="pill">Level: <span id="bVal">7</span></div>
    </div>
    <div class="bar" style="margin-top:12px;">
      <button id="apply" class="btn">Apply</button>
      <div class="pill">Now: <span id="nowHex">#00A3FF</span></div>
    </div>
  </div>
  <div>
    <label>Quick swatches</label>
    <div id="swatches" class="grid" style="margin:8px 0 14px;"></div>
    <div id="preview" class="preview">#00A3FF</div>
  </div>
</div>
<div id="toast">Updated!</div>
<script>
const $=s=>document.querySelector(s);
const colorPicker=$("#colorPicker"), brightness=$("#brightness"), bVal=$("#bVal"), preview=$("#preview"), nowHex=$("#nowHex"), swatches=$("#swatches");
const toast=(m)=>{const t=$("#toast"); t.textContent=m||"Updated!"; t.classList.add("show"); setTimeout(()=>t.classList.remove("show"),1800);};
const clamp=(n,min,max)=>Math.max(min,Math.min(max,n));
function setPreview(hex,level){ preview.style.background=hex; preview.textContent=hex+"  •  b"+level; }

// Swatches (with light border for very dark colors so they stay visible)
const COLORS = ["#FF0000","#00FF00","#0000FF","#FFFF00","#FF00FF","#00FFFF","#FFFFFF","#FFA500","#FF1493","#00A3FF","#33FF99","#FF66CC","#AAFF00","#FF9933","#A0AEC0","#111111"];
function isDark(hex){
  const s = hex.replace("#",""); 
  const r = parseInt(s.slice(0,2),16), g = parseInt(s.slice(2,4),16), b = parseInt(s.slice(4,6),16);
  const L = 0.2126*r + 0.7152*g + 0.0722*b;
  return L < 80;
}
COLORS.forEach(c=>{
  const b=document.createElement("button");
  b.className="sw";
  b.style.background=c;
  b.style.borderColor = isDark(c) ? "#2a3340" : "transparent";
  b.title=c;
  b.onclick=()=>{
    colorPicker.value=c;
    const lvl=+brightness.value;
    setPreview(c,lvl);
    send(c,lvl);
  };
  swatches.appendChild(b);
});

function current(){ return {hex: colorPicker.value.toUpperCase(), level:+brightness.value}; }
function onChange(){ const {hex,level}=current(); setPreview(hex,level); nowHex.textContent=hex; }
colorPicker.addEventListener("input", onChange);
brightness.addEventListener("input", ()=>{ bVal.textContent=brightness.value; onChange(); });
$("#apply").addEventListener("click", ()=>{ const {hex,level}=current(); send(hex,level); });

async function send(hex, level){
  try{
    const u="/api/set?hex="+encodeURIComponent(hex)+"&b="+clamp(level,0,7);
    const r=await fetch(u,{method:"POST"});
    if(r.ok) toast("Updated!"); else toast("Error");
  }catch(e){ toast("Offline?"); }
}

// --- Live polling of /api/state ---
let lastTs = 0;
async function poll(){
  try{
    const r = await fetch("/api/state", {cache:"no-store"});
    if(!r.ok) return;
    const s = await r.json(); // {hex,b,ts}
    if (s && typeof s.ts === "number" && s.ts > lastTs){
      lastTs = s.ts;
      applyRemoteState(s.hex, s.b);
    }
  }catch(e){}
}
function applyRemoteState(hex, level){
  colorPicker.value = hex;
  brightness.value = String(level);
  bVal.textContent = String(level);
  setPreview(hex, level);
  nowHex.textContent = hex;
}
onChange();
setInterval(poll, 1000);
</script></body></html>
)HTML";

// ---- Helpers ----
bool isIp(String s)
{
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c != '.' && (c < '0' || c > '9'))
            return false;
    }
    return true;
}
String toStringIp(IPAddress ip) { return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3]; }

void hexToRgb(const String &hex, uint8_t &r, uint8_t &g, uint8_t &b)
{
    char buf[7];
    int j = 0;
    for (int i = 0; i < hex.length() && j < 6; i++)
    {
        char c = hex[i];
        if (c == '#')
            continue;
        if (isxdigit(c))
            buf[j++] = c;
    }
    while (j < 6)
        buf[j++] = '0';
    buf[6] = 0;
    unsigned long v = strtoul(buf, nullptr, 16);
    r = (v >> 16) & 0xFF;
    g = (v >> 8) & 0xFF;
    b = v & 0xFF;
}

// ---------- IR mapping (placeholders) ----------
struct NecMap
{
    const char *name;
    uint32_t code;
    const uint8_t r, g, b;
};
const NecMap PRESET_CODES[] = {
    {"Red", 0xF720DF, 255, 0, 0},
    {"Orange", 0xF710EF, 255, 132, 0},
    {"Orange-Yellow", 0xF730CF, 255, 187, 0},
    {"Yellow-Orange", 0xF708F7, 255, 234, 0},
    {"Yellow", 0xF728D7, 255, 255, 0},
    {"Green", 0xF7A05F, 0, 255, 68},
    {"Green-Cyan", 0xF7906F, 0, 255, 174},
    {"Cyan", 0xF7B04F, 0, 255, 234},
    {"Blue-Cyan", 0xF78877, 0, 238, 255},
    {"Light-Blue", 0xF7A857, 0, 213, 255},
    {"Blue", 0xF7609F, 0, 0, 255},
    {"Dusty-Blue", 0xF750AF, 0, 149, 255},
    {"Purple-Blue", 0xF7708F, 156, 125, 255},
    {"Purple", 0xF748B7, 128, 0, 255},
    {"Pink", 0xF76897, 255, 51, 170},
    {"White", 0xF7E01F, 255, 255, 255},
};
uint32_t mapHexToNec(const String &hex)
{
    uint8_t r, g, b;
    hexToRgb(hex, r, g, b);
    uint32_t best = PRESET_CODES[0].code;
    uint32_t bestD = 0xFFFFFFFF;
    for (auto &p : PRESET_CODES)
    {
        int dr = int(r) - int(p.r), dg = int(g) - int(p.g), db = int(b) - int(p.b);
        uint32_t d = uint32_t(dr * dr + dg * dg + db * db);
        if (d < bestD)
        {
            bestD = d;
            best = p.code;
        }
    }
    return best;
}

// --- Apply state to hardware + remember it (IR only) ---
void sendIrColor(const String &hex, int level)
{
    // Normalize input
    const String hexUp = hex.length() ? hex : String("#FFFFFF");
    const int lvl = constrain(level, 0, 7);

    // Throttle identical re-sends (prevents continuous blasting if UI spams same state)
    const unsigned long now = millis();
    if (hexUp == lastSentHex && lvl == lastSentLevel && (now - lastSendMs) < IR_MIN_GAP_MS)
    {
        // Update state for clients, skip IR
        currentHex = hexUp;
        currentLevel = lvl;
        currentTs = now;
        return;
    }
    lastSentHex = hexUp;
    lastSentLevel = lvl;
    lastSendMs = now;

    // Update in-memory state (for /api/state)
    currentHex = hexUp;
    currentLevel = lvl;
    currentTs = now;

    // Send exactly one NEC frame (a quick "tap")
    const uint16_t kNECBits = 32;
    const uint32_t nec = mapHexToNec(currentHex);
    irsend.sendNEC(nec, kNECBits); // single frame

    // Small gap so back-to-back different codes don't merge on some receivers
    delay(40);
}

// ---- HTTP handlers ----
void sendNoStore() { server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0"); }
void handleRoot()
{
    sendNoStore();
    server.send(200, "text/html", PAGE);
}

bool captivePortal()
{
    if (!isIp(server.hostHeader()))
    {
        server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()) + "/", true);
        server.send(302, "text/plain", "");
        return true;
    }
    return false;
}

void handleSet()
{
    String hex = server.hasArg("hex") ? server.arg("hex") : "#FFFFFF";
    int b = server.hasArg("b") ? server.arg("b").toInt() : 7;
    sendIrColor(hex, b);
    sendNoStore();
    server.send(200, "application/json",
                String("{\"ok\":true,\"hex\":\"") + currentHex + "\",\"b\":" + currentLevel + ",\"ts\":" + currentTs + "}");
}
void handleState()
{
    sendNoStore();
    server.send(200, "application/json",
                String("{\"hex\":\"") + currentHex + "\",\"b\":" + currentLevel + ",\"ts\":" + currentTs + "}");
}
void handleNotFound()
{
    if (captivePortal())
        return;
    handleRoot();
}

// ---- Setup / Loop ----
void setup()
{
    irsend.begin(); // init IR

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);
    dns.start(53, "*", apIP);

    server.on("/", handleRoot);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/state", HTTP_GET, handleState);
    server.on("/generate_204", handleRoot);        // Android
    server.on("/fwlink", handleRoot);              // Windows
    server.on("/hotspot-detect.html", handleRoot); // Apple
    server.onNotFound(handleNotFound);
    server.begin();
}

void loop()
{
    dns.processNextRequest();
    server.handleClient();
}
