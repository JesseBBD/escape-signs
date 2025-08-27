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

// Pins (ESP8266)
const uint16_t IR_SEND_PIN = D2;   // -> HW-489 SIG
const uint8_t  PIN_R = D5;         // -> NPN base via 1k, collector to HW-478 R
const uint8_t  PIN_G = D6;         // -> NPN base via 1k, collector to HW-478 G
const uint8_t  PIN_B = D7;         // -> NPN base via 1k, collector to HW-478 B

ESP8266WebServer server(80);
IRsend irsend(IR_SEND_PIN);

void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
void handleOptions(){ sendCORS(); server.send(204); }

bool checkKey() {
  return server.hasArg("key") && server.arg("key") == API_KEY;
}

void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  int pr = map(r, 0, 255, 0, 1023);
  int pg = map(g, 0, 255, 0, 1023);
  int pb = map(b, 0, 255, 0, 1023);

  analogWrite(D5, pr); // Red
  analogWrite(D6, pg); // Green
  analogWrite(D7, pb); // Blue
}


// --- routes ---
void handleHealth() {
  sendCORS();
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /color?r=0-255&g=0-255&b=0-255&key=...
void handleColor() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("r") || !server.hasArg("g") || !server.hasArg("b")) {
    server.send(400, "application/json", "{\"error\":\"missing r/g/b\"}"); return;
  }
  int r = constrain(server.arg("r").toInt(), 0, 255);
  int g = constrain(server.arg("g").toInt(), 0, 255);
  int b = constrain(server.arg("b").toInt(), 0, 255);
  setRgb(r,g,b);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /ir?proto=NEC&code=0x00FF02FD&bits=32&key=...
void handleIr() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("proto") || !server.hasArg("code") || !server.hasArg("bits")) {
    server.send(400, "application/json", "{\"error\":\"missing proto/code/bits\"}"); return;
  }
  String proto = server.arg("proto");
  uint32_t code = strtoul(server.arg("code").c_str(), nullptr, 0);
  uint16_t bits = (uint16_t) server.arg("bits").toInt();

  irsend.begin(); // sets 38kHz default carrier
  bool sent = false;
  if (proto == "NEC")       { irsend.sendNEC(code, bits); sent = true; }
  else if (proto == "SONY") { irsend.sendSony(code, bits); sent = true; }
  else if (proto == "RC5")  { irsend.sendRC5(code, bits); sent = true; }
  else if (proto == "RC6")  { irsend.sendRC6(code, bits); sent = true; }
  else if (proto == "SAMSUNG"){ irsend.sendSAMSUNG(code, bits); sent = true; }

  if (!sent) { server.send(400, "application/json", "{\"error\":\"unsupported proto\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /irraw?kHz=38&durations=9000,4500,560,560,...&key=...
void handleIrRaw() {
  sendCORS();
  if (!checkKey()) { server.send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
  if (!server.hasArg("durations")) { server.send(400, "application/json", "{\"error\":\"missing durations\"}"); return; }

  long khzLong = server.hasArg("kHz") ? server.arg("kHz").toInt() : 38;
  if (khzLong < 1) khzLong = 1;
  int khz = (int)khzLong;

  String csv = server.arg("durations");
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

void connectWifiVerbose() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to "); Serial.print(WIFI_SSID); Serial.print(" ...");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) { delay(250); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("Connected! IP: "); Serial.println(WiFi.localIP()); }
  else { Serial.println("Failed to connect within 20s."); WiFi.printDiag(Serial); }
}

void setup() {
  Serial.begin(115200); delay(200);

  // PWM pins
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  analogWriteRange(1023);
  setRgb(255,0,0);

  connectWifiVerbose();
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("ir-d1")) Serial.println("mDNS: http://ir-d1.local");
  }

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/health", HTTP_OPTIONS, handleOptions);
  server.on("/color",  HTTP_POST,   handleColor);
  server.on("/color",  HTTP_OPTIONS,handleOptions);
  server.on("/ir",     HTTP_POST,   handleIr);
  server.on("/ir",     HTTP_OPTIONS,handleOptions);
  server.on("/irraw",  HTTP_POST,   handleIrRaw);
  server.on("/irraw",  HTTP_OPTIONS,handleOptions);
  server.onNotFound([](){ sendCORS(); server.send(404, "application/json", "{\"error\":\"not found\"}"); });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
