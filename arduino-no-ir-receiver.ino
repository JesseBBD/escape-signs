#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ====== EDIT THESE ======
const char* WIFI_SSID = "JVan";
const char* WIFI_PASS = "3Pommern";
const char* API_KEY   = "d1A7c8e2F9b4k0qR6zL1";
// ========================

// ----- LED wiring mode -----
// true  = resistor-only preview (LED + -> 3.3V), pins SINK current (active LOW)
// false = transistor drivers   (LED + -> 5V),   pins drive bases (active HIGH)
#define LED_ACTIVE_LOW false

// Pins
const uint16_t IR_SEND_PIN = D2;   // HW-489 SIG
const uint8_t  PIN_R = D5;         // preview Red
const uint8_t  PIN_G = D6;         // preview Green
const uint8_t  PIN_B = D7;         // preview Blue

ESP8266WebServer server(80);
IRsend irsend(IR_SEND_PIN);

// ---------- LED helpers ----------
void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  int pr = map(r, 0, 255, 0, 1023);
  int pg = map(g, 0, 255, 0, 1023);
  int pb = map(b, 0, 255, 0, 1023);
  if (LED_ACTIVE_LOW) {
    analogWrite(PIN_R, 1023 - pr);
    analogWrite(PIN_G, 1023 - pg);
    analogWrite(PIN_B, 1023 - pb);
  } else {
    analogWrite(PIN_R, pr);
    analogWrite(PIN_G, pg);
    analogWrite(PIN_B, pb);
  }
}
void ledOff(){ setRgb(0,0,0); }
void ledRed(){ setRgb(255,0,0); }
void ledGrn(){ setRgb(0,255,0); }
void ledBlu(){ setRgb(0,0,255); }
void ledWht(){ setRgb(255,255,255); }
void ledMag(){ setRgb(255,0,180); } // pink/magenta
void ledYel(){ setRgb(255,210,0); }

// ---------- Security / CORS ----------
bool checkKey() { return server.hasArg("key") && server.arg("key") == API_KEY; }
void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
void handleOptions(){ sendCORS(); server.send(204); }

// ---------- IR map (your codes) ----------
struct Btn {
  const char* name;   // slug used in API
  uint32_t    code;   // NEC 32-bit
  bool        preview;// update RGB preview?
  uint8_t     r,g,b;  // preview color (ignored if preview=false)
};

static const Btn BTN_MAP[] = {
  {"bright_up",   0xF700FF, false, 0,0,0},
  {"bright_down", 0xF7807F, false, 0,0,0},

  {"off",  0xF740BF, true,  0,0,0},
  {"on",   0xF7C03F, true,  255,255,255},

  {"red1",   0xF720DF, true, 255,0,0},
  {"red2",   0xF710EF, true, 255,0,0},
  {"red3",   0xF730CF, true, 255,0,0},
  {"red4",   0xF708F7, true, 255,0,0},
  {"red5",   0xF728D7, true, 255,0,0},

  {"green1", 0xF7A05F, true, 0,255,0},
  {"green2", 0xF7906F, true, 0,255,0},
  {"green3", 0xF7B04F, true, 0,255,0},
  {"green4", 0xF78877, true, 0,255,0},
  {"green5", 0xF7A857, true, 0,255,0},

  {"blue1",  0xF7609F, true, 0,0,255},
  {"blue2",  0xF750AF, true, 0,0,255},
  {"blue3",  0xF7708F, true, 0,0,255},
  {"blue4",  0xF748B7, true, 0,0,255},
  {"blue5",  0xF76897, true, 0,0,255},

  {"white",  0xF7E01F, true, 255,255,255},

  {"flash",  0xF7D02F, false, 0,0,0},
  {"strobe", 0xF7F00F, false, 0,0,0},
  {"fade",   0xF7C837, false, 0,0,0},
  {"smooth", 0xF7E817, false, 0,0,0},
};
static const uint8_t BTN_COUNT = sizeof(BTN_MAP)/sizeof(BTN_MAP[0]);

// case-insensitive compare (no custom types in signature)
bool equalsIgnoreCaseStr(const String& a, const char* b) {
  String bb(b);
  return a.equalsIgnoreCase(bb);
}

// Return index in BTN_MAP, or -1 if not found
int16_t findBtnIndex(const String& name) {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (equalsIgnoreCaseStr(name, BTN_MAP[i].name)) return i;
  }
  return -1;
}

// ---------- HTTP Handlers ----------
void handleHealth() {
  sendCORS();
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /color?r=&g=&b=&key=
void handleColor() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("r") || !server.hasArg("g") || !server.hasArg("b")) {
    server.send(400, "application/json", "{\"error\":\"missing r/g/b\"}"); return;
  }
  int r = constrain(server.arg("r").toInt(), 0, 255);
  int g = constrain(server.arg("g").toInt(), 0, 255);
  int b = constrain(server.arg("b").toInt(), 0, 255);

  Serial.printf("HTTP /color -> r=%d g=%d b=%d\n", r,g,b);
  setRgb(r,g,b);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /btn?name=<slug>&key=...
void handleBtn() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("name")) { server.send(400, "application/json", "{\"error\":\"missing name\"}"); return; }

  String name = server.arg("name");
  int16_t idx = findBtnIndex(name);
  if (idx < 0) {
    server.send(404, "application/json", "{\"error\":\"unknown button\"}");
    return;
  }
  const Btn& btn = BTN_MAP[idx];

  Serial.printf("HTTP /btn -> %s  code=0x%08lX\n", btn.name, (unsigned long)btn.code);

  // Send IR (NEC, 32 bits)
  irsend.sendNEC(btn.code, 32);

  // Preview
  if (btn.preview) {
    setRgb(btn.r, btn.g, btn.b);
  } else {
    ledYel(); delay(70); ledOff();
  }

  String json = String("{\"ok\":true,\"name\":\"") + btn.name + "\"}";
  server.send(200, "application/json", json);
}

// POST /power?state=on|off&key=...
void handlePower() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("state")) { server.send(400, "application/json", "{\"error\":\"missing state\"}"); return; }

  String s = server.arg("state");
  int16_t idx = -1;
  if (s.equalsIgnoreCase("on"))  idx = findBtnIndex("on");
  if (s.equalsIgnoreCase("off")) idx = findBtnIndex("off");

  if (idx < 0) { server.send(400, "application/json", "{\"error\":\"state must be on/off\"}"); return; }

  const Btn& b = BTN_MAP[idx];
  Serial.printf("HTTP /power -> %s  code=0x%08lX\n", b.name, (unsigned long)b.code);
  irsend.sendNEC(b.code, 32);
  if (s.equalsIgnoreCase("on")) ledWht(); else ledOff();

  String json = String("{\"ok\":true,\"state\":\"") + s + "\"}";
  server.send(200, "application/json", json);
}

// ---------- Wi-Fi with status colours ----------
void connectWifiVerbose() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to "); Serial.print(WIFI_SSID); Serial.print(" ...");

  uint32_t start = millis();
  uint32_t lastBlink = 0;
  bool on = false;

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    if (millis() - lastBlink >= 300) {
      lastBlink = millis();
      on = !on;
      if (on) ledMag(); else ledOff(); // blink pink while connecting
      Serial.print(".");
    }
    delay(25);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP: "); Serial.println(WiFi.localIP());
    ledGrn(); // success
  } else {
    Serial.println("Failed to connect within 20s.");
    WiFi.printDiag(Serial);
    ledRed(); // failure
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  analogWriteRange(1023);
  ledOff();

  irsend.begin(); // init IR transmitter once

  connectWifiVerbose();
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("ir-d1")) Serial.println("mDNS: http://ir-d1.local");
  }

  // Routes
  server.on("/health",  HTTP_GET,     handleHealth);
  server.on("/health",  HTTP_OPTIONS, handleOptions);

  server.on("/color",   HTTP_POST,    handleColor);
  server.on("/color",   HTTP_OPTIONS, handleOptions);

  server.on("/btn",     HTTP_POST,    handleBtn);
  server.on("/btn",     HTTP_OPTIONS, handleOptions);

  server.on("/power",   HTTP_POST,    handlePower);
  server.on("/power",   HTTP_OPTIONS, handleOptions);

  server.onNotFound([](){
    sendCORS();
    server.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server.on("/nec", HTTP_POST, handleNecCmd);
server.on("/nec", HTTP_OPTIONS, handleOptions);

  server.begin();
  Serial.println("HTTP server started");
}

// --- handler: POST /nec?cmd=0xNN&key=... ---
void handleNecCmd() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("cmd")) { server.send(400, "application/json", "{\"error\":\"missing cmd\"}"); return; }

  String s = server.arg("cmd");
  // accept formats like "0xA4" or "164"
  uint8_t cmd = (uint8_t) strtoul(s.c_str(), nullptr, 0);
  uint8_t inv = (uint8_t) (~cmd);

  // Build full 32-bit extended NEC: 0x00F7 cmd inv
  uint32_t code = (0x00F7UL << 16) | ((uint32_t)cmd << 8) | (uint32_t)inv;

  Serial.printf("HTTP /nec -> cmd=0x%02X inv=0x%02X full=0x%08lX\n", cmd, inv, (unsigned long)code);
  irsend.sendNEC(code, 32);

  // brief yellow blink so you see activity
  ledYel(); delay(70); ledOff();

  char buf[64];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"cmd\":\"0x%02X\",\"full\":\"0x%08lX\"}", cmd, (unsigned long)code);
  server.send(200, "application/json", buf);
}


void loop() {
  server.handleClient();
}
