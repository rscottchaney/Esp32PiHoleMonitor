#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClient.h> 
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration
const char* ssid     = "No19";
const char* password = "mostpeoplechoosetousegoodsecurity";
const char* piholeIP   = "192.168.1.206";       // Your Pi-hole's local IP Address
const char* appPassword   = "R8EzbEzNyZfvyLI7wZ7AE6ukV/G2RBWLacUTpmqGkVc="; // Paste token here (leave empty if password disabled)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String sessionID = ""; // Variable to hold the active dynamic SID
unsigned long lastTime = 0;
unsigned long delayInterval = 10000; // Refresh statistics every 10 seconds

// Draws a 4-bar signal graph in the upper-right corner of the screen
void drawWiFiSignal(int x, int y) {
  int32_t rssi = WiFi.RSSI();
  int numBars = 0;
  
  if (rssi > -60) numBars = 4;
  else if (rssi > -70) numBars = 3;
  else if (rssi > -80) numBars = 2;
  else if (rssi > -90) numBars = 1;
  
  // Draw 4 incremental vertical bars
  // fillRect(x, y, width, height, color)
  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2; // Bars get taller (2px, 4px, 6px, 8px)
    int barX = x + (i * 3);      // Space bars 3 pixels apart
    int barY = y + (8 - barHeight); // Align bars to the bottom edge
    
    if (i < numBars) {
      display.fillRect(barX, barY, 2, barHeight, SSD1306_WHITE); // Filled bar
    } else {
      display.drawRect(barX, barY, 2, barHeight, SSD1306_WHITE); // Empty outline bar
    }
  }
}

bool loginToPihole() {
  NetworkClient client;
  HTTPClient http;
  String loginUrl = "http://" + String(piholeIP) + "/api/auth";
  http.begin(client, loginUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  
  String payload = "{\"password\":\"" + String(appPassword) + "\"}";
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error && doc["session"]["valid"] == true) {
      sessionID = doc["session"]["sid"].as<String>();
      http.end();
      return true;
    }
  }
  http.end();
  return false;
}

void fetchPiholeStats() {
  if (sessionID == "") {
    if (!loginToPihole()) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Auth Attempt Failed");
      display.display();
      return;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    NetworkClient client; 
    HTTPClient http;
    String url = "http://" + String(piholeIP) + "/api/stats/summary";
    http.begin(client, url);
    http.addHeader("sid", sessionID);
    http.addHeader("Accept", "application/json");

    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        long dns_queries = doc["queries"]["total"];
        long ads_blocked = doc["queries"]["blocked"];
        float ads_percentage = doc["queries"]["percent_blocked"];
        
        display.clearDisplay();
        
        // --- 3-PIXEL DOWNWARD SHIFT MODIFICATIONS ---
        
        // 1. Shifted header text down to Y=3 (was Y=0)
        display.setTextSize(1);
        display.setCursor(0, 3);
        display.print("               ");
        
        // 2. Shifted Wi-Fi graph widget down to Y=3 (was Y=0)
        drawWiFiSignal(115, 3);
        
        // 3. Shifted horizontal divider line down to Y=13 (was Y=10)
        display.drawFastHLine(0, 13, 128, SSD1306_WHITE); 
        
        // 4. Shifted data readout text down to Y=18 (was Y=15)
        display.setCursor(0, 18);
        display.printf("Queries: %ld\n", dns_queries);
        display.printf("Blocked: %ld\n", ads_blocked);
        
        // 5. Shifted secondary divider line down to Y=38 (was Y=36)
        display.drawFastHLine(0, 38, 128, SSD1306_WHITE); 
        
        // 6. Shifted large percentage text down to Y=46 (was Y=44)
        display.setCursor(0, 46);
        display.setTextSize(2);
        display.printf("%0.1f%%\n", ads_percentage);
        
        // 7. Shifted "Blocked" text label down to Y=51 (was Y=49)
        display.setTextSize(1);
        display.setCursor(84, 51);
        display.print("Blocked");
        
        display.display();
      }
    } 
    else if (httpResponseCode == 401) {
      sessionID = ""; 
    } 
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Connecting Wi-Fi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  fetchPiholeStats();
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= delayInterval) {
    fetchPiholeStats();
    lastTime = currentTime;
  }
}