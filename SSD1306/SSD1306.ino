#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

//////// wifi
const char* ssid     = "SSID";
const char* password = "PASSWORD";
HTTPClient http;
WiFiClient client;
///////

//////// display
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
////////

void setupWIFI(){
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

void printTextToDisplay(String text){
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.setRotation(2);
  display.println(text);
  display.display();
}

void setupDisplay(){
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  delay(2000);
  printTextToDisplay("initializing...");
}

void updateDisplay(String json){
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  const char* value = doc["message"];
  Serial.printf("The value is: %s\n", value);
  printTextToDisplay(value);
}


void getDataFromAPI(){
    const char * address = "http://192.168.1.86:8080/message";
    String payload = "";

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Starting HTTPS GET...");

      // Begin the HTTPS session
      if (http.begin(client, address)) {
        int code = http.GET();
        Serial.printf("Response code: %d\n", code);

        if (code == HTTP_CODE_OK) {
          payload = http.getString();
        } else {
          Serial.printf("GET failed: %s\n", http.errorToString(code).c_str());
        }
        http.end();  // Free resources
      } else {
        Serial.println("Unable to begin HTTPS session");
      }
    } else{
        Serial.println("Not connected to wifi");
    }

    updateDisplay(payload);
}

void setup() {
  Serial.begin(115200);

  setupWIFI();
  setupDisplay();
}

void loop() {
  getDataFromAPI();
  delay(500);
}