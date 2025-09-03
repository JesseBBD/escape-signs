/*
  ESP32 • RTDB Poller + IR (Leader -> Current) — printf-free
  - Stable polling (no stream)
  - Follows /tally/leader, falls back to /current
  - Mirrors chosen color back into /current on color change
  - Auto-detects DB base: "/escape-signs/rooms" OR "/rooms"
*/

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <limits.h>

// ---- IR (TX only) ----
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ---- Firebase (RTDB) ----
#define MBFS_FLASH_FS SPIFFS
#include <SPIFFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ================== CONFIG ==================
#define WIFI_SSID "GUESSWHO"
#define WIFI_PASS "8fhZfvXCk"
#define API_KEY "AIzaSyA9L8ggCCiuZB0AEplEKlc7XYhzSOnwpRk"
#define DATABASE_URL "https://escape-signs-default-rtdb.europe-west1.firebasedatabase.app"
#define USER_EMAIL "jesse@bbd.co.za"
#define USER_PASS "This Is Stupid!123"
String ROOM = "ESC1";

#define MIRROR_TO_CURRENT 1

// Poll cadence
static const uint32_t POLL_MS_OK = 2500;
static const uint32_t POLL_MS_BACKOFF = 6000;

// IR GPIO
const uint16_t IR_PIN = 17; // try 4 if needed
IRsend irsend(IR_PIN);

// Firebase globals
FirebaseData fb;
FirebaseAuth auth;
FirebaseConfig config;

// State
String lastHex = "";
int lastBrightness = -1;
uint32_t lastPoll = 0;
bool backoff = false;
static const uint8_t MAX_FAILS_SOFT = 6;
static const uint8_t MAX_FAILS_HARD = 18;
uint8_t consecFails = 0;

// DB base auto-detect
String ACTIVE_BASE = "/escape-signs/rooms";
const char *CANDIDATE_BASES[] = {"/escape-signs/rooms", "/rooms"};

// ================== UTILS ==================
String normalizeHex(String hex)
{
    hex.trim();
    hex.replace("#", "");
    hex.toUpperCase();
    if (hex.length() != 6)
        return "";
    return "#" + hex;
}

void hexToRGB(const String &h, int &r, int &g, int &b)
{
    String s = h;
    s.replace("#", "");
    r = strtol(s.substring(0, 2).c_str(), nullptr, 16);
    g = strtol(s.substring(2, 4).c_str(), nullptr, 16);
    b = strtol(s.substring(4, 6).c_str(), nullptr, 16);
}

void ensureTimeSync()
{
    time_t now = time(nullptr);
    if (now < 8 * 3600)
    {
        Serial.print("[Time] Re-syncing");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        uint32_t t0 = millis();
        while (now < 8 * 3600 && millis() - t0 < 15000)
        {
            delay(300);
            Serial.print(".");
            now = time(nullptr);
        }
        Serial.print(" done (epoch=");
        Serial.print((long)now);
        Serial.println(")");
    }
}

void softRecover()
{
    Serial.println("[RECOVER] Soft recovery (re-NTP + Firebase.begin)");
    ensureTimeSync();
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
}

// ================== IR mapping ==================
void sendColorIR(const String &hex)
{
    struct Map
    {
        const char *hex;
        uint32_t nec;
    };
    static const Map table[] = {
        {"#FF0000", 0xF720DF}, // red
        {"#FF8400", 0xF710EF}, // orange
        {"#FFEA00", 0xF730CF}, // yellow
        {"#00FF44", 0xF7A05F}, // green
        {"#00FFAE", 0xF7906F}, // cyan
        {"#00D5FF", 0xF7A857}, // light blue
        {"#0000FF", 0xF7609F}, // dark blue
        {"#9C7DFF", 0xF7708F}, // purple
        {"#FF33AA", 0xF76897}, // pink
        {"#FFFFFF", 0xF7E01F}, // white
    };

    int R, G, B;
    hexToRGB(hex, R, G, B);
    int bestIdx = -1, bestDist = INT_MAX;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        int r, g, b;
        hexToRGB(table[i].hex, r, g, b);
        int d = (R - r) * (R - r) + (G - g) * (G - g) + (B - b) * (B - b);
        if (d < bestDist)
        {
            bestDist = d;
            bestIdx = (int)i;
        }
        if (d == 0)
            break;
    }

    if (bestIdx >= 0)
    {
        Serial.print("[IR] color ");
        Serial.print(hex);
        Serial.print(" -> map ");
        Serial.print(table[bestIdx].hex);
        Serial.print(" code 0x");
        Serial.print((unsigned long)table[bestIdx].nec, HEX);
        Serial.print(" (dist=");
        Serial.print(bestDist);
        Serial.println(")");
        irsend.sendNEC(table[bestIdx].nec, 32);
        delay(90);
        irsend.sendNEC(table[bestIdx].nec, 32);
        delay(90);
    }
    else
    {
        Serial.print("[IR] No mapping for ");
        Serial.println(hex);
    }
}

void sendBrightnessIR(int level)
{
    level = constrain(level, 0, 7);
    const uint32_t IR_MIN = 0xF7807F; // min brightness
    const uint32_t IR_UP = 0xF700FF;  // BRIGHT+
    Serial.print("[IR] brightness level=");
    Serial.println(level);
    irsend.sendNEC(IR_MIN, 32);
    delay(90);
    for (int i = 0; i < level; i++)
    {
        irsend.sendNEC(IR_UP, 32);
        delay(90);
    }
}

void applyLightState(const String &hex, int brightness)
{
    Serial.print("[STATE] hex=");
    Serial.print(hex);
    Serial.print("  brightness=");
    Serial.println(brightness);
    sendColorIR(hex);
    if (brightness >= 0)
        sendBrightnessIR(brightness);
}

// ================== DB helpers ==================
String roomBasePath() { return String(ACTIVE_BASE) + "/" + ROOM; }

bool getStringPath(const String &path, String &out)
{
    if (Firebase.RTDB.getString(&fb, path.c_str()))
    {
        out = fb.stringData();
        return true;
    }
    Serial.print("[DB] getString(");
    Serial.print(path);
    Serial.print(") failed: ");
    Serial.println(fb.errorReason());
    return false;
}

bool getIntPath(const String &path, int &out)
{
    if (Firebase.RTDB.getInt(&fb, path.c_str()))
    {
        out = fb.intData();
        return true;
    }
    Serial.print("[DB] getInt(");
    Serial.print(path);
    Serial.print(") failed: ");
    Serial.println(fb.errorReason());
    return false;
}

bool getLeaderHexFromCounts(const String &roomBase, String &out)
{
    return getStringPath(roomBase + "/tally/leader", out);
}

bool getPreferredColorHex(const String &roomBase, String &out)
{
    // Prefer leader; fallback to current
    if (getLeaderHexFromCounts(roomBase, out))
        return true;
    return getStringPath(roomBase + "/current", out);
}

void mirrorCurrent(const String &roomBase, const String &normHex)
{
#if MIRROR_TO_CURRENT
    if (!Firebase.RTDB.setString(&fb, (roomBase + "/current").c_str(), normHex.c_str()))
    {
        Serial.print("[DB] mirror /current failed: ");
        Serial.println(fb.errorReason());
    }
    else
    {
        Serial.print("[DB] mirrored leader -> current: ");
        Serial.println(normHex);
    }
#endif
}

// ================== WiFi / Firebase ==================
void connectWiFi()
{
    Serial.print("\n[WiFi] Connecting to ");
    Serial.print(WIFI_SSID);
    Serial.println(" ...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.onEvent([](WiFiEvent_t e)
                 {
    Serial.print("[WiFi] Event: "); Serial.println((int)e); });

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        if (millis() - t0 > 30000)
        {
            Serial.println("\n[WiFi] Failed. Rebooting...");
            ESP.restart();
        }
    }
    Serial.print("\n[WiFi] Connected. IP: ");
    Serial.println(WiFi.localIP());
    ensureTimeSync();
}

void startFirebase()
{
    SPIFFS.begin(true);
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;
    config.timeout.serverResponse = 20000;
    fb.setBSSLBufferSize(4096, 1024);
    fb.setResponseSize(8192);

    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASS;

    Serial.println("[Firebase] Email/Password sign-in...");
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    uint32_t t0 = millis();
    while (auth.token.uid.length() == 0 && millis() - t0 < 15000)
    {
        delay(200);
    }

    Serial.print("[Auth] UID: ");
    // Option A (recommended, matches the rest of the sketch’s style)
    Serial.print("[Auth] UID: ");
    Serial.println(auth.token.uid.c_str());

    if (auth.token.uid.length() == 0)
    {
        Serial.println("[Firebase] Sign-in failed (no UID). Rebooting...");
        delay(2000);
        ESP.restart();
    }
    Serial.println("[Firebase] Ready.");

    // ---- Auto-detect base path ----
    size_t n = sizeof(CANDIDATE_BASES) / sizeof(CANDIDATE_BASES[0]);
    for (size_t i = 0; i < n; i++)
    {
        String base = CANDIDATE_BASES[i];
        String p1 = base + "/" + ROOM + "/tally/counts/leader";
        String p2 = base + "/" + ROOM + "/current";
        String tmp;
        if (Firebase.RTDB.getString(&fb, p1.c_str()))
        {
            ACTIVE_BASE = base;
            Serial.print("[DB] Base selected by leader: ");
            Serial.println(ACTIVE_BASE);
            return;
        }
        if (Firebase.RTDB.getString(&fb, p2.c_str()))
        {
            ACTIVE_BASE = base;
            Serial.print("[DB] Base selected by current: ");
            Serial.println(ACTIVE_BASE);
            return;
        }
        Serial.print("[DB] Probe ");
        Serial.print(base);
        Serial.print(" failed: ");
        Serial.println(fb.errorReason());
    }
    Serial.print("[DB] Using default base: ");
    Serial.println(ACTIVE_BASE);
}

// ================== Arduino ==================
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== ESP32 RTDB Poller + IR (Leader->Current) — printf-free ===");

    irsend.begin();
    connectWiFi();
    startFirebase();
}

void loop()
{
    uint32_t now = millis();
    uint32_t interval = backoff ? POLL_MS_BACKOFF : POLL_MS_OK;
    if (now - lastPoll < interval)
        return;
    lastPoll = now;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Lost connection. Reconnecting...");
        WiFi.reconnect();
        delay(800);
        if (WiFi.status() != WL_CONNECTED)
        {
            consecFails++;
            backoff = (consecFails >= 3);
            if (consecFails == MAX_FAILS_SOFT)
                softRecover();
            if (consecFails >= MAX_FAILS_HARD)
            {
                Serial.println("[RECOVER] Hard restart.");
                ESP.restart();
            }
            return;
        }
    }

    if (!Firebase.ready())
    {
        Serial.println("[Firebase] Not ready (token refresh?)");
        ensureTimeSync();
        consecFails++;
        backoff = (consecFails >= 3);
        if (consecFails == MAX_FAILS_SOFT)
            softRecover();
        if (consecFails >= MAX_FAILS_HARD)
        {
            Serial.println("[RECOVER] Hard restart.");
            ESP.restart();
        }
        return;
    }

    const String base = roomBasePath();
    String hex;
    int bri = -1;

    bool gotHex = getPreferredColorHex(base, hex);
    bool gotBri = getIntPath(base + "/brightness", bri);

    if (gotHex)
    {
        String norm = normalizeHex(hex);
        if (norm.length() == 7)
        {
            bool colorChanged = (norm != lastHex);
            bool changed = colorChanged || (bri != lastBrightness);
            if (changed)
            {
                applyLightState(norm, bri);
                if (colorChanged)
                    mirrorCurrent(base, norm);
                lastHex = norm;
                lastBrightness = bri;
            }
            consecFails = 0;
            backoff = false;
        }
        else
        {
            Serial.print("[DB] Invalid hex read: ");
            Serial.println(hex);
            consecFails++;
            backoff = (consecFails >= 3);
        }
    }
    else
    {
        Serial.println("[DB] No leader/current available yet.");
        consecFails++;
        backoff = (consecFails >= 3);
    }

    if (consecFails == MAX_FAILS_SOFT)
        softRecover();
    if (consecFails >= MAX_FAILS_HARD)
    {
        Serial.println("[RECOVER] Hard restart.");
        ESP.restart();
    }
}
