/**
 * @file hub_main.cpp
 * @brief Central Gateway Firmware for Agricultural WSN Ground-Truthing
 * @description Manages ESP-NOW reception, SD-card persistence (now with Humidity), 
 * OLED telemetry, and scheduled Supabase sync.
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_private/wifi.h"
#include "secrets.h"

// ==============================================================================
// CONFIGURATION
// ==============================================================================
const char* WIFI_SSID = SECRET_SSID;
const char* WIFI_PASS = SECRET_PASS;
String supabase_url   = SECRET_SUPABASE_URL;
String supabase_key   = SECRET_SUPABASE_KEY;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SD_CS_PIN 5
#define BOOT_BUTTON 0
#define RSSI_WINDOW 10

// Timing (ms)
const unsigned long LOG_INTERVAL  = 1800000;  // 30 Minutes
const unsigned long SYNC_COOLDOWN = 10800000; // 3 Hours
const unsigned long SYNC_MAX_TIME = 120000;   // 2 Minutes Failsafe

// ==============================================================================
// STRUCTURES & GLOBALS
// ==============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;

typedef struct struct_message {
    int id;
    float moisture;
    float temp;
    float humidity;
    int battPct;         
    unsigned int readingId; 
} struct_message;

struct_message nodeData[6];           
bool nodeHasReported[6] = {false};    

int rssiHistory[6][RSSI_WINDOW]; 
int rssiIndex[6] = {0};
bool sdStatus = false;
int latestRawRSSI = -100;

unsigned long lastRecvTime = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastLogTime = 0;
unsigned long lastSyncTime = 0;
unsigned long lastPulseTime = 0; 
bool isSyncing = false;

// ==============================================================================
// RADIO FUNCTIONS
// ==============================================================================
float getSmoothedRSSI() {
    long sum = 0;
    int count = 0;
    for (int n = 1; n <= 5; n++) {
        for (int i = 0; i < RSSI_WINDOW; i++) {
            if (rssiHistory[n][i] < 0 && rssiHistory[n][i] > -98) { 
                sum += rssiHistory[n][i];
                count++;
            }
        }
    }
    return (count == 0) ? 0 : (float)sum / count; 
}

void promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t*)buf;
    if (type == WIFI_PKT_DATA || type == WIFI_PKT_MGMT) {
        latestRawRSSI = p->rx_ctrl.rssi;
    }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if (isSyncing) return; 
    
    struct_message temp;
    memcpy(&temp, incomingData, sizeof(temp));
    
    if (temp.id >= 1 && temp.id <= 5) {
        nodeData[temp.id] = temp;
        nodeData[temp.id].readingId = millis(); 
        nodeHasReported[temp.id] = true; 
        
        lastRecvTime = millis();
        lastPulseTime = millis(); 
        
        rssiHistory[temp.id][rssiIndex[temp.id]] = latestRawRSSI;
        rssiIndex[temp.id] = (rssiIndex[temp.id] + 1) % RSSI_WINDOW;
    }
}

void startESPNOW() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100); 
    
    WiFi.mode(WIFI_STA);
    esp_wifi_stop(); 
    esp_wifi_deinit();
    
    wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
    my_config.ampdu_tx_enable = false; 
    esp_wifi_init(&my_config); 
    esp_wifi_start();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
    esp_wifi_set_promiscuous_rx_cb(&promiscuous_rx_cb);
    
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    esp_wifi_internal_set_fix_rate(WIFI_IF_STA, true, WIFI_PHY_RATE_1M_L);

    esp_now_deinit(); 
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
        Serial.println("ESP-NOW Ready");
    } else {
        ESP.restart();
    }
}

// ==============================================================================
// CLOUD SYNC
// ==============================================================================
void syncToSupabase() {
    isSyncing = true;
    unsigned long syncStart = millis();
    
    display.clearDisplay();
    display.setCursor(15, 25);
    display.print("SYNCING CLOUD...");
    display.display();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED && (millis() - syncStart < 30000)) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String endpoint = supabase_url + "/rest/v1/sensor_data";

        for (int i = 1; i <= 5; i++) {
            if (nodeData[i].id != 0) { 
                http.begin(endpoint);
                http.addHeader("apikey", supabase_key);
                http.addHeader("Authorization", "Bearer " + supabase_key);
                http.addHeader("Content-Type", "application/json");

                StaticJsonDocument<256> doc;
                doc["node_id"]     = nodeData[i].id;
                doc["moisture"]    = nodeData[i].moisture;
                doc["temp"]        = nodeData[i].temp;
                doc["humidity"]    = nodeData[i].humidity;
                doc["battery_pct"] = nodeData[i].battPct;
                
                String jsonStr;
                serializeJson(doc, jsonStr);
                http.POST(jsonStr);
                http.end();
            }
        }
        lastSyncTime = millis(); 
    }

    startESPNOW();
    isSyncing = false;
}

// ==============================================================================
// SD LOGGING
// ==============================================================================
void processAndLog() {
    if (millis() - lastLogTime >= LOG_INTERVAL) {
        File dataFile = SD.open("/data.csv", FILE_APPEND);
        if (dataFile) {
            sdStatus = true;
            DateTime now = rtc.now();
            dataFile.printf("%04d/%02d/%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
            
            for (int i = 1; i <= 5; i++) {
                if (nodeHasReported[i]) {
                    dataFile.printf(",M:%.1f,T:%.1f,H:%.1f,B:%d", 
                                    nodeData[i].moisture, nodeData[i].temp, 
                                    nodeData[i].humidity, nodeData[i].battPct);
                    nodeHasReported[i] = false; 
                } else {
                    dataFile.printf(",NA,NA,NA,NA"); 
                }
            }
            dataFile.println();
            dataFile.close();
        } else {
            sdStatus = false;
            SD.begin(SD_CS_PIN);
        }
        lastLogTime = millis();
    }
}

void handleSDWipe() {
    unsigned long startTime = millis();
    const unsigned long holdDuration = 3000; // 3 seconds
    
    while (digitalRead(BOOT_BUTTON) == LOW) {
        unsigned long elapsed = millis() - startTime;
        
        // Calculate progress for the bar (0 to 128 pixels wide)
        int progress = map(elapsed, 0, holdDuration, 0, 128);
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(20, 15);
        display.print("HOLD TO WIPE SD");
        
        // Draw the Progress Bar Frame
        display.drawRect(0, 35, 128, 10, WHITE);
        // Fill the Progress Bar
        display.fillRect(0, 35, progress, 10, WHITE);
        
        display.display();
        
        if (elapsed >= holdDuration) {
            // Success Animation
            for (int i = 0; i < 4; i++) {
                display.clearDisplay();
                display.setCursor(30, 25);
                display.print("CLEANING");
                for (int j = 0; j <= i; j++) display.print(".");
                display.display();
                delay(200);
            }
            
            SD.remove("/data.csv");
            Serial.println("SD Card Wiped.");
            ESP.restart(); // Restart to re-initialize everything
        }
    }
}

// ==============================================================================
// MAIN LOOP
// ==============================================================================
void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    
    for (int n = 0; n < 6; n++) {
        for (int i = 0; i < RSSI_WINDOW; i++) rssiHistory[n][i] = -100;
    }

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextColor(WHITE);
    rtc.begin();
    
    if (SD.begin(SD_CS_PIN)) {
        sdStatus = true;
        File file = SD.open("/data.csv", FILE_APPEND);
        if (file && file.size() == 0) {
            file.println("Timestamp,N1_Data,N2_Data,N3_Data,N4_Data,N5_Data");
        }
        file.close();
    }

    startESPNOW();
}

void loop() {
    if (digitalRead(BOOT_BUTTON) == LOW) handleSDWipe();
    if (!isSyncing) processAndLog();

    if (!isSyncing) {
        bool timeToSync = (millis() - lastSyncTime >= SYNC_COOLDOWN);
        unsigned long timeSinceLastPacket = millis() - lastRecvTime;
        bool isSafeWindow = (timeSinceLastPacket > 300000) && (timeSinceLastPacket < 600000);

        if (timeToSync && isSafeWindow && lastRecvTime != 0) {
            syncToSupabase();
        }
    }

    if (!isSyncing && (millis() - lastScreenUpdate > 1000)) {
        lastScreenUpdate = millis();
        DateTime now = rtc.now();
        display.clearDisplay();
        display.setCursor(0,0);
        display.printf("%02d:%02d:%02d SD:%s", now.hour(), now.minute(), now.second(), sdStatus ? "OK" : "!!");
        display.drawLine(0, 9, 128, 9, WHITE);

        for (int i = 1; i <= 5; i++) {
            int col = (i <= 3) ? 0 : 68;
            int row = 13 + ((i-1) % 3) * 13; 
            display.setCursor(col, row);
            unsigned long age = (millis() - nodeData[i].readingId) / 1000;
            if (age < 3600 && nodeData[i].id != 0) {
                display.printf("N%d:%d%% %dm", i, nodeData[i].battPct, (int)(age/60));
            } else {
                display.printf("N%d: --", i);
            }
        }
        
        display.drawLine(0, 52, 128, 52, WHITE); 
        display.setCursor(0, 55); 
        float avgRSSI = getSmoothedRSSI();
        if (millis() - lastRecvTime > 60000 || avgRSSI == 0) {
            display.print("Searching nodes...");
        } else {
            display.printf("Signal: %.1f dBm", avgRSSI);
            if (millis() - lastPulseTime < 500) display.fillCircle(124, 58, 1, WHITE); 
        }
        display.display();
    }
}