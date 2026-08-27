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


// HC-SR04 Pin Definitions
const int trigPin = D2;
const int echoPin = D3;

// Proximity Settings
const int wakeDistanceCm = 50;        // Distance threshold to turn on screen (in centimeters)
const unsigned long screenTimeout = 10000; // Time to stay on after last detection (10 seconds)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String sessionID = ""; 
unsigned long lastNetworkCheck = 0;
unsigned long networkInterval = 10000; // Fetch stats every 10 seconds

unsigned long lastTriggerTime = 0;     // Tracks when a person was last seen
bool screenIsOn = true;

// Helper function to calculate distance using the HC-SR04
long readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Measure the bounce-back duration in microseconds
  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout limit
  
  // Calculate distance in centimeters (Speed of sound is ~343m/s)
  long distance = duration * 0.034 / 2;
  
  if (distance == 0) return 999; // Return a large distance if sensor times out
  return distance;
}

void drawWiFiSignal(int x, int y) {
  int32_t rssi = WiFi.RSSI();
  int numBars = 0;
  if (rssi > -60) numBars = 4;
  else if (rssi > -70) numBars = 3;
  else if (rssi > -80) numBars = 2;
  else if (rssi > -90) numBars = 1;
  
  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2;
    int barX = x + (i * 3);
    int barY = y + (8 - barHeight);
    if (i < numBars) {
      display.fillRect(barX, barY, 2, barHeight, SSD1306_WHITE);
    } else {
      display.drawRect(barX, barY, 2, barHeight, SSD1306_WHITE);
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
    if (!loginToPihole()) return;
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
        
        // Padded layout structure (Y+3 offset)
        display.setTextSize(1);
        display.setCursor(0, 3);
        display.print("PI-HOLE MONITOR");
        drawWiFiSignal(115, 3);
        display.drawFastHLine(0, 13, 128, SSD1306_WHITE); 
        
        display.setCursor(0, 18);
        display.printf("Queries: %ld\n", dns_queries);
        display.printf("Blocked: %ld\n", ads_blocked);
        display.drawFastHLine(0, 38, 128, SSD1306_WHITE); 
        
        display.setCursor(0, 46);
        display.setTextSize(2);
        display.printf("%0.1f%%\n", ads_percentage);
        
        display.setTextSize(1);
        display.setCursor(84, 51);
        display.print("Blocked");
        
        // Only refresh the glass image buffer if the screen should actively be visible
        if (screenIsOn) {
          display.display();
        }
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
  
  // Initialize HC-SR04 pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,3);
  display.println("Connecting Wi-Fi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  lastTriggerTime = millis(); // Initialize timer
  fetchPiholeStats();
}

void loop() {
  unsigned long currentTime = millis();
  
  // 1. Constantly check the proximity sensor (every 200ms)
  static unsigned long lastSensorCheck = 0;
  if (currentTime - lastSensorCheck >= 200) {
    lastSensorCheck = currentTime;
    long distance = readDistance();
    
    if (distance < wakeDistanceCm) {
      lastTriggerTime = currentTime; // Reset the idle countdown clock
      if (!screenIsOn) {
        screenIsOn = true;
        display.ssd1306_command(SSD1306_DISPLAYON); // Wake the physical OLED hardware panel
        fetchPiholeStats(); // Immediately draw fresh numbers
      }
    }
  }
  
  // 2. Manage the sleep countdown timer
  if (screenIsOn && (currentTime - lastTriggerTime >= screenTimeout)) {
    screenIsOn = false;
    display.ssd1306_command(SSD1306_DISPLAYOFF); // Put OLED glass panel to sleep mode
  }
  
  // 3. Keep updating the data silently in the background every 10 seconds
  if (currentTime - lastNetworkCheck >= networkInterval) {
    lastNetworkCheck = currentTime;
    fetchPiholeStats(); 
  }
}