// ===== Escape Sign • AP Captive Portal + IR (ESP8266) =====
// - SoftAP you connect to from your phone (captive portal)
// - Swatches + Effects + "Custom" (cycles all swatches every 5s)
// - Secured JSON API (/btn, /power, /custom) with API key
// - IR send via NEC 32-bit
// Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266)

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ====== EDIT THESE ======
const char *AP_SSID = "ESCAPE-SIGN";
const char *AP_PASS = "#BBD2025"; // must be 8–63 chars
const char *API_KEY = "d1A7c8e2F9b4k0qR6zL1";
// ========================

// === Optional LED preview (kept OFF to match "remove RGB") ===
#define ENABLE_PREVIEW false
#if ENABLE_PREVIEW
const uint8_t PIN_R = D5, PIN_G = D6, PIN_B = D7;
#define LED_ACTIVE_LOW false
#endif

// ---- IR ----
const uint16_t IR_SEND_PIN = D2; // HW-489 SIG -> transistor -> IR LED
IRsend irsend(IR_SEND_PIN);

// ---- AP + Web ----
IPAddress apIP(192, 168, 4, 1);
DNSServer dns;
ESP8266WebServer server(80);

// ---------- IR map (your codes) ----------
struct Btn
{
    const char *name; // slug used in API/UI
    uint32_t code;    // NEC 32-bit
    bool preview;
    uint8_t r, g, b;
};

static const Btn BTN_MAP[] = {
    {"bright_up", 0xF700FF, false, 0, 0, 0},
    {"bright_down", 0xF7807F, false, 0, 0, 0},

    {"off", 0xF740BF, true, 0, 0, 0},
    {"on", 0xF7C03F, true, 255, 255, 255},

    {"red1", 0xF720DF, true, 255, 0, 0},
    {"red2", 0xF710EF, true, 255, 132, 0},
    {"red3", 0xF730CF, true, 255, 187, 0},
    {"red4", 0xF708F7, true, 255, 234, 0},
    {"red5", 0xF728D7, true, 255, 255, 0},

    {"green1", 0xF7A05F, true, 0, 255, 68},
    {"green2", 0xF7906F, true, 0, 255, 174},
    {"green3", 0xF7B04F, true, 0, 255, 234},
    {"green4", 0xF78877, true, 0, 238, 255},
    {"green5", 0xF7A857, true, 0, 213, 255},

    {"blue1", 0xF7609F, true, 0, 0, 255},
    {"blue2", 0xF750AF, true, 0, 149, 255},
    {"blue3", 0xF7708F, true, 156, 125, 255},
    {"blue4", 0xF748B7, true, 128, 0, 255},
    {"blue5", 0xF76897, true, 255, 51, 255},

    {"white", 0xF7E01F, true, 255, 255, 255},

    {"flash", 0xF7D02F, false, 0, 0, 0},
    {"strobe", 0xF7F00F, false, 0, 0, 0},
    {"fade", 0xF7C837, false, 0, 0, 0},
    {"smooth", 0xF7E817, false, 0, 0, 0},
};
static const uint8_t BTN_COUNT = sizeof(BTN_MAP) / sizeof(BTN_MAP[0]);

// ---------- (Optional) LED preview helpers ----------
#if ENABLE_PREVIEW
void setRgb(uint8_t r, uint8_t g, uint8_t b)
{
    int pr = map(r, 0, 255, 0, 1023);
    int pg = map(g, 0, 255, 0, 1023);
    int pb = map(b, 0, 255, 0, 1023);
    if (LED_ACTIVE_LOW)
    {
        analogWrite(PIN_R, 1023 - pr);
        analogWrite(PIN_G, 1023 - pg);
        analogWrite(PIN_B, 1023 - pb);
    }
    else
    {
        analogWrite(PIN_R, pr);
        analogWrite(PIN_G, pg);
        analogWrite(PIN_B, pb);
    }
}
void ledOff() { setRgb(0, 0, 0); }
void ledYel() { setRgb(255, 210, 0); }
void ledWht() { setRgb(255, 255, 255); }
#else
inline void ledOff() {}
inline void ledYel() {}
inline void ledWht() {}
#endif

// ---------- Security / CORS ----------
bool checkKey() { return server.hasArg("key") && server.arg("key") == API_KEY; }
void sendCORS()
{
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
void handleOptions()
{
    sendCORS();
    server.send(204);
}

// ---------- Utils ----------
bool equalsIgnoreCaseStr(const String &a, const char *b)
{
    String bb(b);
    return a.equalsIgnoreCase(bb);
}
int16_t findBtnIndex(const String &name)
{
    for (uint8_t i = 0; i < BTN_COUNT; i++)
        if (equalsIgnoreCaseStr(name, BTN_MAP[i].name))
            return i;
    return -1;
}
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
void sendNoStore() { server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0"); }

// ---------- Cycle engine (Custom effect) ----------
const uint32_t CYCLE_MS = 5000; // 5 seconds per swatch
bool cycleActive = false;
unsigned long cycleNextTs = 0;
uint8_t cycleIdx = 0;

// Swatch order: red1..5, green1..5, blue1..5
const char *SWATCH_NAMES[] = {
    "red1",
    "red2",
    "red3",
    "red4",
    "red5",
    "green1",
    "green2",
    "green3",
    "green4",
    "green5",
    "blue1",
    "blue2",
    "blue3",
    "blue4",
    "blue5",
};
const uint8_t SWATCH_COUNT = sizeof(SWATCH_NAMES) / sizeof(SWATCH_NAMES[0]);

void cycleStart()
{
    cycleActive = true;
    cycleIdx = 0;
    cycleNextTs = 0; // trigger immediately
}

void cycleStop()
{
    cycleActive = false;
}

void cycleTick()
{
    if (!cycleActive)
        return;
    unsigned long now = millis();
    if (now < cycleNextTs)
        return;

    // Find index of current swatch name in BTN_MAP and send IR
    const char *name = SWATCH_NAMES[cycleIdx];
    int16_t idx = findBtnIndex(String(name));
    if (idx >= 0)
    {
        const Btn &btn = BTN_MAP[idx];
        irsend.sendNEC(btn.code, 32);
    }

    // Schedule next step
    cycleIdx = (cycleIdx + 1) % SWATCH_COUNT;
    cycleNextTs = now + CYCLE_MS;
}

// ---------- UI page (AP mode) ----------
const char *PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Escape IR</title>
<style>
:root{--pad:16px;--radius:16px;--shadow:0 8px 24px rgba(0,0,0,.12);--bg:#0b0f14;--card:#121922;--border:#2a3340;--accent:#1a88ff}
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:var(--bg);color:#e8eef6}
header{padding:var(--pad);text-align:center}
.card{background:var(--card);margin:var(--pad);padding:var(--pad);border-radius:var(--radius);box-shadow:var(--shadow)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.grid{display:grid;gap:10px}
.grid.cols-5{grid-template-columns:repeat(5,1fr)}
.btn{padding:10px 12px;border-radius:12px;border:1px solid var(--border);background:#0f1620;color:#e8eef6;cursor:pointer}
.btn.acc{background:var(--accent);border:0;color:#fff;font-weight:600}
.sw{height:44px;border:none;border-radius:10px;cursor:pointer}
label{font-size:14px;opacity:.85;display:block;margin:8px 0 6px}
.pill{padding:6px 10px;background:#0f1620;border:1px solid var(--border);border-radius:999px;font-size:12px;opacity:.95}
#toast{visibility:hidden;min-width:160px;background:var(--accent);color:#fff;text-align:center;border-radius:999px;padding:10px 16px;position:fixed;left:50%;bottom:24px;transform:translateX(-50%)}
#toast.show{visibility:visible;animation:fadein .2s,fadeout .2s 1.6s}
@keyframes fadein{from{opacity:0;transform:translate(-50%,8px)}to{opacity:1;transform:translate(-50%,0)}}
@keyframes fadeout{from{opacity:1}to{opacity:0;visibility:hidden}}
</style></head><body>
<header><h2>Escape IR</h2><div class="pill">Connect to this Wi-Fi • Captive</div></header>

<div class="card">
  <div class="row">
    <button class="btn acc" data-power="on">Power ON</button>
    <button class="btn" data-power="off">Power OFF</button>
    <button class="btn" data-btn="bright_up">Bright +</button>
    <button class="btn" data-btn="bright_down">Bright −</button>
  </div>
</div>

<div class="card">
  <label>Swatches</label>
  <div id="sw" class="grid cols-5"></div>
</div>

<div class="card">
  <label>Effects</label>
  <div class="row">
    <button class="btn" data-btn="flash">Flash</button>
    <button class="btn" data-btn="strobe">Strobe</button>
    <button class="btn" data-btn="fade">Fade</button>
    <button class="btn" data-btn="smooth">Smooth</button>
    <button class="btn" id="custom">Custom</button>
  </div>
</div>

<div id="toast">Done</div>

<script>
const KEY = "%API_KEY%";
const toast=(m)=>{const t=document.getElementById("toast");t.textContent=m||"Done";t.classList.add("show");setTimeout(()=>t.classList.remove("show"),1600);};
const $=s=>document.querySelector(s);
const sw=$("#sw");

const SWATCHES = [
  // red1..5
  {name:"red1",  hex:"#FF0000"},
  {name:"red2",  hex:"#FF8400"},
  {name:"red3",  hex:"#FFBB00"},
  {name:"red4",  hex:"#FFEA00"},
  {name:"red5",  hex:"#FFFF00"},
  // green1..5
  {name:"green1",hex:"#00FF44"},
  {name:"green2",hex:"#00FFAE"},
  {name:"green3",hex:"#00FFEA"},
  {name:"green4",hex:"#00EEFF"},
  {name:"green5",hex:"#00D5FF"},
  // blue1..5
  {name:"blue1", hex:"#0000FF"},
  {name:"blue2", hex:"#0095FF"},
  {name:"blue3", hex:"#9C7DFF"},
  {name:"blue4", hex:"#8000FF"},
  {name:"blue5", hex:"#FF33FF"},
];

async function post(url){try{const r=await fetch(url,{method:"POST"});toast(r.ok?"OK":"Error");}catch(_){toast("Offline");}}

SWATCHES.forEach(s=>{const b=document.createElement("button");b.className="sw";b.style.background=s.hex;b.title=s.name;
  b.onclick=()=>post(`/btn?name=${encodeURIComponent(s.name)}&key=${encodeURIComponent(KEY)}`); sw.appendChild(b); });

document.querySelectorAll("[data-btn]").forEach(b=>b.addEventListener("click",()=>post(`/btn?name=${encodeURIComponent(b.dataset.btn)}&key=${encodeURIComponent(KEY)}`)));
document.querySelectorAll("[data-power]").forEach(b=>b.addEventListener("click",()=>post(`/power?state=${encodeURIComponent(b.dataset.power)}&key=${encodeURIComponent(KEY)}`)));

$("#custom").addEventListener("click", ()=> post(`/custom?key=${encodeURIComponent(KEY)}`));
</script>
</body></html>
)HTML";

// ---------- HTTP handlers ----------
void handleRoot()
{
    sendNoStore();
    String html(PAGE);
    html.replace("%API_KEY%", API_KEY); // embed key for on-page fetches
    server.send(200, "text/html", html);
}

void handleHealth()
{
    sendCORS();
    server.send(200, "application/json", "{\"ok\":true}");
}

void stopCycleOnManual() { cycleStop(); } // helper to stop custom mode on manual actions

void handleBtn()
{
    sendCORS();
    if (!checkKey())
    {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    if (!server.hasArg("name"))
    {
        server.send(400, "application/json", "{\"error\":\"missing name\"}");
        return;
    }
    stopCycleOnManual();

    int16_t idx = findBtnIndex(server.arg("name"));
    if (idx < 0)
    {
        server.send(404, "application/json", "{\"error\":\"unknown button\"}");
        return;
    }

    const Btn &btn = BTN_MAP[idx];
    irsend.sendNEC(btn.code, 32);

    server.send(200, "application/json", String("{\"ok\":true,\"name\":\"") + btn.name + "\"}");
}

void handlePower()
{
    sendCORS();
    if (!checkKey())
    {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    if (!server.hasArg("state"))
    {
        server.send(400, "application/json", "{\"error\":\"missing state\"}");
        return;
    }
    stopCycleOnManual();

    String s = server.arg("state");
    int16_t idx = s.equalsIgnoreCase("on") ? findBtnIndex("on") : s.equalsIgnoreCase("off") ? findBtnIndex("off")
                                                                                            : -1;
    if (idx < 0)
    {
        server.send(400, "application/json", "{\"error\":\"state must be on/off\"}");
        return;
    }

    const Btn &b = BTN_MAP[idx];
    irsend.sendNEC(b.code, 32);

    server.send(200, "application/json", String("{\"ok\":true,\"state\":\"") + s + "\"}");
}

// POST /custom?key=...  -> start cycling (5s each, loops)
void handleCustom()
{
    sendCORS();
    if (!checkKey())
    {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    cycleStart();
    server.send(200, "application/json", "{\"ok\":true,\"mode\":\"custom-cycle\"}");
}

// ---------- Captive portal helpers ----------
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
void handleNotFound()
{
    if (captivePortal())
        return;
    handleRoot();
}

// ---------- AP bring-up ----------
bool startApSafe(const char *ssid, const char *pass, int channel = 1, bool hidden = false, int maxConn = 4)
{
    size_t len = strlen(pass ? pass : "");
    bool open = (len == 0);
    if (!open && (len < 8 || len > 63))
    {
        open = true;
    }
    bool okCfg = WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    (void)okCfg;
    return open ? WiFi.softAP(ssid) : WiFi.softAP(ssid, pass, channel, hidden, maxConn);
}

void setup()
{
    Serial.begin(115200);
    delay(80);

#if ENABLE_PREVIEW
    pinMode(PIN_R, OUTPUT);
    pinMode(PIN_G, OUTPUT);
    pinMode(PIN_B, OUTPUT);
    analogWriteRange(1023);
    ledOff();
#endif

    irsend.begin();

    // Clean Wi-Fi state then AP
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(80);
    WiFi.mode(WIFI_OFF);
    delay(40);
    WiFi.mode(WIFI_AP);

    bool ok = startApSafe(AP_SSID, AP_PASS, 1, false, 6);
    Serial.printf("[AP] softAP: %s  SSID:%s  CH:1\n", ok ? "OK" : "FAIL", AP_SSID);
    Serial.printf("[AP] IP: %s\n", apIP.toString().c_str());

    // DNS for captive portal
    bool dnsOk = dns.start(53, "*", apIP);
    Serial.printf("[DNS] start: %s\n", dnsOk ? "OK" : "FAIL");

    // Routes
    server.on("/", HTTP_GET, handleRoot);

    server.on("/health", HTTP_GET, handleHealth);
    server.on("/health", HTTP_OPTIONS, handleOptions);

    server.on("/btn", HTTP_POST, handleBtn);
    server.on("/btn", HTTP_OPTIONS, handleOptions);

    server.on("/power", HTTP_POST, handlePower);
    server.on("/power", HTTP_OPTIONS, handleOptions);

    server.on("/custom", HTTP_POST, handleCustom);
    server.on("/custom", HTTP_OPTIONS, handleOptions);

    // OS captive URLs
    server.on("/generate_204", handleRoot);        // Android
    server.on("/fwlink", handleRoot);              // Windows
    server.on("/hotspot-detect.html", handleRoot); // Apple

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[HTTP] Server started");
}

void loop()
{
    dns.processNextRequest();
    server.handleClient();
    cycleTick(); // run the custom-cycle engine
}
