#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>   // typeToString(), uint64ToString()

// ====== EDIT THESE ======
const char* WIFI_SSID = "JVan";
const char* WIFI_PASS = "3Pommern";
const char* API_KEY   = "d1A7c8e2F9b4k0qR6zL1";
// ========================

// Pins (ESP8266)
const uint16_t IR_SEND_PIN = D2;   // -> HW-489 SIG (TX)
const uint8_t  IR_RECV_PIN = D1;   // -> HW-490 OUT (RX)
const uint8_t  PIN_R = D5;         // RGB: Red
const uint8_t  PIN_G = D6;         // RGB: Green
const uint8_t  PIN_B = D7;         // RGB: Blue

ESP8266WebServer server(80);
IRsend irsend(IR_SEND_PIN);
IRrecv irrecv(IR_RECV_PIN);

// ---------- LED helpers ----------
void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  int pr = map(r, 0, 255, 0, 1023);
  int pg = map(g, 0, 255, 0, 1023);
  int pb = map(b, 0, 255, 0, 1023);

    analogWrite(PIN_R, pr);
    analogWrite(PIN_G, pg);
    analogWrite(PIN_B, pb);

}
void ledOff() { setRgb(0,0,0); }
void ledRed() { setRgb(255,0,0); }
void ledGrn() { setRgb(0,255,0); }
void ledBlu() { setRgb(0,0,255); }
void ledWht() { setRgb(255,255,255); }
void ledYel() { setRgb(255,210,0); }
void ledCyn() { setRgb(0,255,255); }
void ledMag() { setRgb(255,0,180); } // pink/magenta while connecting

// ---------- Security / CORS ----------
bool checkKey() { return server.hasArg("key") && server.arg("key") == API_KEY; }
void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
void handleOptions(){ sendCORS(); server.send(204); }

// ---------- IR receive storage (for /ir/last) ----------
static const uint16_t MAX_RAW = 300;
volatile bool has_last = false;
String    last_proto = "UNKNOWN";
uint64_t  last_code  = 0;
uint16_t  last_bits  = 0;
bool      last_repeat = false;
uint16_t  last_raw[MAX_RAW];
uint16_t  last_raw_len = 0;
const uint16_t RAW_TICK_US = 50;

// ---------- Optional: map common NEC codes -> colors ----------
struct IrColorMap {
  decode_type_t proto;
  uint64_t code;
  const char* name;
  uint8_t r, g, b;
};

// Example NEC 24/21-key remote codes (may differ on your remote).
// Replace codes once you learn yours from Serial or /ir/last.
static const IrColorMap COLOR_MAP[] = {
  { NEC, 0x00FF9867, "RED",    255,   0,   0 },
  { NEC, 0x00FF38C7, "GREEN",    0, 255,   0 },
  { NEC, 0x00FF18E7, "BLUE",     0,   0, 255 },
  { NEC, 0x00FF02FD, "WHITE",  255, 255, 255 },
  // add more once learned:
  // { NEC, 0xXXXXXXXX, "YELLOW", 255, 220, 0 },
  // { NEC, 0xXXXXXXXX, "CYAN",     0, 255, 255 },
  // { NEC, 0xXXXXXXXX, "MAGENTA", 255,   0, 180 },
};
static const uint8_t COLOR_MAP_LEN = sizeof(COLOR_MAP)/sizeof(COLOR_MAP[0]);

bool applyMappedColor(decode_type_t proto, uint64_t code) {
  for (uint8_t i = 0; i < COLOR_MAP_LEN; i++) {
    if (COLOR_MAP[i].proto == proto && COLOR_MAP[i].code == code) {
      setRgb(COLOR_MAP[i].r, COLOR_MAP[i].g, COLOR_MAP[i].b);
      Serial.print("IR->LED: ");
      Serial.print(COLOR_MAP[i].name);
      Serial.print(" (0x"); Serial.print(uint64ToString(code, 16)); Serial.println(")");
      return true;
    }
  }
  return false;
}

// Capture & log any new IR decode. Also try to mirror LED color if mapped.
void captureIfAvailable() {
  decode_results results;
  if (irrecv.decode(&results)) {
    last_proto  = typeToString(results.decode_type);
    last_bits   = results.bits;
    last_repeat = results.repeat;
    last_code   = results.value;

    last_raw_len = min<uint16_t>(results.rawlen, MAX_RAW);
    for (uint16_t i = 0; i < last_raw_len; i++) last_raw[i] = results.rawbuf[i];
    has_last = true;

    // --- Serial log ---
    Serial.print("IR RX: proto=");
    Serial.print(last_proto);
    Serial.print(" code=0x");
    Serial.print(uint64ToString(last_code, 16));
    Serial.print(" bits=");
    Serial.print(last_bits);
    if (last_repeat) Serial.print(" (repeat)");
    Serial.print(" rawlen=");
    Serial.println(last_raw_len);

    // --- LED feedback ---
    if (!last_repeat) { // ignore repeats for color changes
      if (!applyMappedColor(results.decode_type, results.value)) {
        // Unknown code: blink yellow quickly as feedback
        ledYel(); delay(80); ledOff();
      }
    }

    irrecv.resume();
  }
}

// ---------- HTTP routes ----------
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

  Serial.print("HTTP /color -> r="); Serial.print(r);
  Serial.print(" g="); Serial.print(g);
  Serial.print(" b="); Serial.println(b);

  setRgb(r,g,b);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /ir?proto=NEC&code=0x..&bits=32&key=
void handleIr() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("proto") || !server.hasArg("code") || !server.hasArg("bits")) {
    server.send(400, "application/json", "{\"error\":\"missing proto/code/bits\"}"); return;
  }
  String proto = server.arg("proto");
  uint32_t code = strtoul(server.arg("code").c_str(), nullptr, 0);
  uint16_t bits = (uint16_t) server.arg("bits").toInt();

  Serial.print("HTTP /ir -> proto="); Serial.print(proto);
  Serial.print(" code=0x"); Serial.print(String(code, 16));
  Serial.print(" bits="); Serial.println(bits);

  irsend.begin(); // 38 kHz default
  bool sent = false;
  if (proto == "NEC")        { irsend.sendNEC(code, bits); sent = true; }
  else if (proto == "SONY")  { irsend.sendSony(code, bits); sent = true; }
  else if (proto == "RC5")   { irsend.sendRC5(code, bits);  sent = true; }
  else if (proto == "RC6")   { irsend.sendRC6(code, bits);  sent = true; }
  else if (proto == "SAMSUNG"){ irsend.sendSAMSUNG(code, bits); sent = true; }

  if (!sent) { server.send(400, "application/json", "{\"error\":\"unsupported proto\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /irraw?kHz=38&durations=...&key=
void handleIrRaw() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("durations")) { server.send(400, "application/json", "{\"error\":\"missing durations\"}"); return; }

  long khzLong = server.hasArg("kHz") ? server.arg("kHz").toInt() : 38;
  if (khzLong < 1) khzLong = 1;
  int khz = (int)khzLong;

  String csv = server.arg("durations");
  Serial.print("HTTP /irraw -> kHz="); Serial.print(khz); Serial.print(" durations(len)="); Serial.println(csv.length());

  int count = 1;
  for (unsigned int i=0; i<csv.length(); i++) if (csv[i] == ',') count++;
  uint16_t *dur = new uint16_t[count];

  char* s = strdup(csv.c_str());
  int idx = 0;
  for (char* tok = strtok(s, ","); tok && idx < count; tok = strtok(nullptr, ",")) {
    dur[idx++] = (uint16_t) atoi(tok);
  }
  free(s);

  irsend.begin();
  irsend.sendRaw(dur, idx, khz); // microseconds, carrier kHz
  delete[] dur;

  server.send(200, "application/json", "{\"ok\":true}");
}

// GET /ir/last
void handleIrLast() {
  sendCORS();
  String json = "{\"ok\":";
  json += (has_last ? "true" : "false");
  if (has_last) {
    json += ",\"proto\":\"" + last_proto + "\"";
    json += ",\"code\":\"0x" + uint64ToString(last_code, 16) + "\"";
    json += ",\"bits\":" + String(last_bits);
    json += ",\"repeat\":" + String(last_repeat ? "true" : "false");
    json += ",\"tick_us\":" + String(RAW_TICK_US);
    json += ",\"raw_ticks\":[";
    for (uint16_t i = 0; i < last_raw_len; i++) {
      if (i) json += ",";
      json += String(last_raw[i]);
    }
    json += "]";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// POST /ir/clear?key=
void handleIrClear() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  has_last = false;
  last_proto = "UNKNOWN"; last_code = 0; last_bits = 0; last_repeat = false; last_raw_len = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---- Wi-Fi with status colours ----
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
      if (on) ledMag(); else ledOff();  // blink pink while connecting
      Serial.print(".");
    }
    delay(25);
    captureIfAvailable();
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

  irrecv.enableIRIn(); // start receiver

  connectWifiVerbose();
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("ir-d1")) Serial.println("mDNS: http://ir-d1.local");
  }

  // Routes
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/health", HTTP_OPTIONS, handleOptions);

  server.on("/color",  HTTP_POST,   handleColor);
  server.on("/color",  HTTP_OPTIONS,handleOptions);

  server.on("/ir",     HTTP_POST,   handleIr);
  server.on("/ir",     HTTP_OPTIONS,handleOptions);

  server.on("/irraw",  HTTP_POST,   handleIrRaw);
  server.on("/irraw",  HTTP_OPTIONS,handleOptions);

  server.on("/ir/last",  HTTP_GET,  handleIrLast);
  server.on("/ir/clear", HTTP_POST, handleIrClear);
  server.on("/ir/clear", HTTP_OPTIONS, handleOptions);

  server.onNotFound([](){ sendCORS(); server.send(404, "application/json", "{\"error\":\"not found\"}"); });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  captureIfAvailable();
}
