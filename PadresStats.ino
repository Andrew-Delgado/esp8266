#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

// Settings
const unsigned long POLL_RATE = 120000;
const unsigned long SCROLL_DELAY = 45;

// Data
String wins;
String losses;
String winPct;
String divRank;
String leagueRank;
String gamesBack;
String streak;

// Display buffer
String displayLines[8];

unsigned long lastFetchTime = 0;

// scrolling state
int scrollY = SCREEN_HEIGHT;

// -------------------------
// helper
// -------------------------
String extractValue(String line) {
  int colon = line.indexOf(':');
  if (colon == -1) return "";

  String val = line.substring(colon + 1);
  val.replace("\"", "");
  val.replace("}", "");
  val.trim();
  return val;
}

// -------------------------
// fetch data (FIXED STREAM VERSION)
// -------------------------
void fetchPadresData() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  const char* url =
    "https://statsapi.mlb.com/api/v1/standings?leagueId=104&season=2026"
    "&teamId=135&fields=records,teamRecords,team,name,wins,losses,"
    "winningPercentage,divisionRank,leagueRank,gamesBack,streak,streakCode";

  Serial.println("\n[HTTP] Beginning request...");

  if (!http.begin(client, url)) {
    Serial.println("[HTTP] Failed to begin request.");
    return;
  }

  Serial.println("[HTTP] Sending GET...");
  int httpCode = http.GET();
  Serial.printf("[HTTP] Response code: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP] Bad response, aborting. Code: %d\n", httpCode);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();

  // ---------------------------
  // PHASE 1: stream until we see "name":"Padres"
  // ---------------------------
  String probe = "";
  bool foundPadres = false;
  const String target = "\"name\":\"Padres\"";

  Serial.println("[Stream] Scanning for Padres team object...");

  while (stream->available()) {
    char c = stream->read();
    probe += c;

    if (probe.length() > (unsigned)target.length() + 5) {
      probe.remove(0, 1);
    }

    if (probe.indexOf(target) > -1) {
      foundPadres = true;
      Serial.println("[Stream] Found Padres team object");
      break;
    }
  }

  if (!foundPadres) {
    Serial.println("[Error] Padres team object not found in stream.");
    http.end();
    return;
  }

  // ---------------------------
  // PHASE 2: collect until we have "winningPercentage" and its value
  // ---------------------------
// PHASE 2: collect until we have winningPercentage and its value
  String chunk = target;

  while (stream->available()) {
    chunk += (char)stream->read();

    // Once we have "winningPercentage" plus enough chars to hold its value, stop
    int wpPos = chunk.indexOf("\"winningPercentage\"");
    if (wpPos > -1 && chunk.length() > (unsigned)(wpPos + 35)) {
      Serial.println("[Stream] Captured full Padres record");
      break;
    }

    if (chunk.length() > 4000) {
      Serial.println("[Error] Chunk grew too large, bailing.");
      http.end();
      return;
    }
  }

  // ---------------------------
  // PARSE FIELDS FROM CHUNK
  // wins/losses/winPct use lastIndexOf — their real values come AFTER all the nested records
  // divisionRank/leagueRank/gamesBack/streakCode use indexOf — they appear before the records block
  // ---------------------------
  auto extractFirst = [&](String key) -> String {
    int start = chunk.indexOf("\"" + key + "\"");
    if (start == -1) {
      Serial.printf("[Parse] Key not found: %s\n", key.c_str());
      return "";
    }
    int colon = chunk.indexOf(":", start);
    int end   = chunk.indexOf(",", colon);
    String val = chunk.substring(colon + 1, end);
    val.replace("\"", "");
    val.replace("}", "");
    val.trim();
    Serial.printf("[Parse] %s = '%s'\n", key.c_str(), val.c_str());
    return val;
  };

  auto extractLast = [&](String key) -> String {
    int start = chunk.lastIndexOf("\"" + key + "\"");
    if (start == -1) {
      Serial.printf("[Parse] Key not found: %s\n", key.c_str());
      return "";
    }
    int colon = chunk.indexOf(":", start);
    int end   = chunk.indexOf(",", colon);
    if (end == -1) end = chunk.indexOf("}", colon);
    String val = chunk.substring(colon + 1, end);
    val.replace("\"", "");
    val.replace("}", "");
    val.trim();
    Serial.printf("[Parse] %s = '%s'\n", key.c_str(), val.c_str());
    return val;
  };

  // wins/losses use lastIndexOf — they repeat inside nested records
  // winningPercentage only appears once, so extractFirst is fine
  wins       = extractLast("wins");
  losses     = extractLast("losses");
  winPct     = extractFirst("winningPercentage");  // <-- changed from extractLast
  divRank    = extractFirst("divisionRank");
  leagueRank = extractFirst("leagueRank");
  gamesBack  = extractFirst("gamesBack");

  int scPos = chunk.indexOf("streakCode");
  if (scPos == -1) {
    Serial.println("[Parse] streakCode not found in chunk.");
    streak = "";
  } else {
    int a = chunk.indexOf("\"", scPos + 12);
    int b = chunk.indexOf("\"", a + 1);
    streak = (a != -1 && b != -1) ? chunk.substring(a + 1, b) : "";
    Serial.printf("[Parse] streakCode = '%s'\n", streak.c_str());
  }

  Serial.println("\n[Data] Final values:");
  Serial.println("  Wins:      " + wins);
  Serial.println("  Losses:    " + losses);
  Serial.println("  Win%:      " + winPct);
  Serial.println("  DivRank:   " + divRank);
  Serial.println("  LeagueRnk: " + leagueRank);
  Serial.println("  GB:        " + gamesBack);
  Serial.println("  Streak:    " + streak);

  displayLines[0] = "Team: Padres";
  displayLines[1] = "Wins: "    + wins;
  displayLines[2] = "Losses: "  + losses;
  displayLines[3] = "Win%: "    + winPct;
  displayLines[4] = "DivRank: " + divRank;
  displayLines[5] = "LgRank: "  + leagueRank;
  displayLines[6] = "GB: "      + gamesBack;
  displayLines[7] = "Streak: "  + streak;

  Serial.println("[Data] displayLines updated. Padres fetch complete.");
}

// -------------------------
// SCROLL DISPLAY (RESTORED)
// -------------------------
void drawScroll() {

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const int LINE_HEIGHT = 8;
  const int TOTAL_HEIGHT = 8 * LINE_HEIGHT;

  for (int i = 0; i < 8; i++) {

    int y = scrollY + (i * LINE_HEIGHT);

    if (y > -LINE_HEIGHT && y < SCREEN_HEIGHT) {
      display.setCursor(0, y);
      display.print(displayLines[i]);
    }
  }

  display.display();
}

// -------------------------
// SETUP
// -------------------------
void setup() {

  Serial.begin(115200);

  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (true)
      ;
  }

  display.setRotation(2);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Fetching...");
  display.display();

  fetchPadresData();

  lastFetchTime = millis();
}

// -------------------------
// LOOP
// -------------------------
void loop() {

  unsigned long now = millis();

  // refresh data
  if (now - lastFetchTime > POLL_RATE) {
    lastFetchTime = now;
    fetchPadresData();
  }

  // scroll
  drawScroll();

  scrollY--;

  if (scrollY < -64) {
    scrollY = SCREEN_HEIGHT;
  }

  delay(SCROLL_DELAY);
}
