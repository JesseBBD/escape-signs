// ===== Escape Sign Captive Portal + Local RGB + IR Send (ESP8266) =====
// Board: LOLIN(WEMOS) D1 R2 & mini  (ESP8266)
// Needs: IRremoteESP8266 by markszabo (install via Library Manager)

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// ---- IR ----
#include <IRremoteESP8266.h>
#include <IRsend.h>
const uint16_t IR_PIN = D2; // HW-489 via transistor to D2 (GPIO4)
IRsend irsend(IR_PIN);

// ---- Wi-Fi / Captive Portal ----
const char *AP_SSID = "ESCAPE-SIGN";
const char *AP_PASS = "#BBD2025"; // or "" for open AP
IPAddress apIP(192, 168, 4, 1);
DNSServer dns;
ESP8266WebServer server(80);

// ---- Local RGB (common-cathode SMD via resistors) ----
const uint8_t PIN_R = D5; // GPIO14
const uint8_t PIN_G = D6; // GPIO12
const uint8_t PIN_B = D7; // GPIO13

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
.sw{width:40px;height:40px;border:none;border-radius:10px;cursor:pointer}
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
<header><h2>Escape Sign</h2><div class="pill"><span class="dot"></span>Connected to controller</div></header>
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
const COLORS=["#FF0000","#00FF00","#0000FF","#FFFF00","#FF00FF","#00FFFF","#FFFFFF","#FFA500","#FF1493","#00A3FF","#33FF99","#FF66CC","#AAFF00","#FF9933","#A0AEC0","#111111"];
COLORS.forEach(c=>{ const b=document.createElement("button"); b.className="sw"; b.style.background=c; b.title=c;
  b.onclick=()=>{ colorPicker.value=c; setPreview(c,+brightness.value); send(c,+brightness.value); }; swatches.appendChild(b); });
function current(){ return {hex: colorPicker.value.toUpperCase(), level:+brightness.value}; }
function onChange(){ const {hex,level}=current(); setPreview(hex,level); nowHex.textContent=hex; }
colorPicker.addEventListener("input", onChange);
brightness.addEventListener("input", ()=>{ bVal.textContent=brightness.value; onChange(); });
$("#apply").addEventListener("click", ()=>{ const {hex,level}=current(); send(hex,level); });
async function send(hex, level){
  try{ const u="/api/set?hex="+encodeURIComponent(hex)+"&b="+clamp(level,0,7);
    const r=await fetch(u,{method:"POST"}); if(r.ok) toast("Updated!"); else toast("Error");
  }catch(e){ toast("Offline?"); }
}
onChange();
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

uint8_t gamma8(uint8_t x)
{
  float xf = x / 255.0f;
  xf = powf(xf, 2.2f);
  return (uint8_t)(xf * 255.0f + 0.5f);
}
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
float levelToScale(int level)
{
  level = constrain(level, 0, 7);
  const float s[8] = {0.00f, 0.08f, 0.16f, 0.28f, 0.42f, 0.58f, 0.78f, 1.00f};
  return s[level];
}
void driveLocalRGB(const String &hex, int level)
{
  uint8_t r, g, b;
  hexToRgb(hex, r, g, b);
  float s = levelToScale(level);
  analogWrite(PIN_R, gamma8((uint8_t)(r * s)));
  analogWrite(PIN_G, gamma8((uint8_t)(g * s)));
  analogWrite(PIN_B, gamma8((uint8_t)(b * s)));
}

// ---------- IR mapping ----------
// Replace the NEC codes below with YOUR remote's learned codes.
// These placeholders compile, but may not match your controller.
struct NecMap
{
  const char *name;
  uint32_t code;
  const uint8_t r, g, b;
};
const NecMap PRESET_CODES[] = {
    {"RED", 0xF720DF, 255, 0, 0},
    {"GREEN", 0xF7A05F, 0, 255, 0},
    {"BLUE", 0xF7609F, 0, 0, 255},
    {"WHITE", 0xF7E01F, 255, 255, 255},
    {"ORANGE", 0xF730CF, 255, 165, 0},
    {"CYAN", 0xF7A857, 0, 255, 255},
    {"MAGENTA", 0xF76897, 255, 0, 255},
    // add the rest of your 24-key palette here...
};

// Find closest preset by Euclidean distance in RGB
uint32_t mapHexToNec(const String &hex)
{
  uint8_t r, g, b;
  hexToRgb(hex, r, g, b);
  uint32_t best = PRESET_CODES[0].code;
  uint32_t bestD = 0xFFFFFFFF;
  for (auto &p : PRESET_CODES)
  {
    int dr = int(r) - int(p.r);
    int dg = int(g) - int(p.g);
    int db = int(b) - int(p.b);
    uint32_t d = uint32_t(dr * dr + dg * dg + db * db);
    if (d < bestD)
    {
      bestD = d;
      best = p.code;
    }
  }
  return best;
}

// Some controllers support brightness UP/DOWN buttons.
// If yours does, you can send those here based on level changes.
// For now, we encode brightness into color choice only.
void sendIrColor(const String &hex, int level)
{
  // Always reflect locally for your SMD demo LED:
  driveLocalRGB(hex, level);

  // Send IR color command (NEC example).
  // Replace kNECBits=32 & irsend.sendNEC(...) with the right protocol for your remote.
  const uint16_t kNECBits = 32;
  uint32_t nec = mapHexToNec(hex);
  irsend.sendNEC(nec, kNECBits);

  // If your controller needs a small gap between commands:
  delay(50);
}

// ---- HTTP handlers ----
void handleRoot() { server.send(200, "text/html", PAGE); }
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
  b = constrain(b, 0, 7);
  sendIrColor(hex, b);
  server.send(200, "application/json", String("{\"ok\":true,\"hex\":\"") + hex + "\",\"b\":" + b + "}");
}
void handleNotFound()
{
  if (captivePortal())
    return;
  server.send(200, "text/html", PAGE);
}

// ---- Setup / Loop ----
void setup()
{
  // PWM pins
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  analogWriteRange(255);
  driveLocalRGB("#000000", 0);

  // IR: begin ONCE (fixes your compile/runtime error)
  irsend.begin(); // <-- returns void; no if-condition

  // Wi-Fi AP + captive DNS + HTTP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  dns.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/api/set", HTTP_POST, handleSet);
  server.on("/generate_204", handleRoot);        // Android
  server.on("/fwlink", handleRoot);              // Windows
  server.on("/hotspot-detect.html", handleRoot); // iOS/macOS
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop()
{
  dns.processNextRequest();
  server.handleClient();
}
