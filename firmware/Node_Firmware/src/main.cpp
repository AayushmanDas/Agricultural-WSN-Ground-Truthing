/**
 * @file node_main.cpp
 * @brief Remote Sensor Node Firmware for Agricultural WSN Ground-Truthing
 * @description Reads capacitance soil moisture and BME280 data, calculates battery capacity,
 * transmits via ESP-NOW MAC protocol, and enters Deep Sleep to conserve energy.
 * @version 1.0.0
 */

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "secrets.h"

// ==============================================================================
// CONFIGURATION & CALIBRATION
// ==============================================================================
#define NODE_ID 5 // IDENTIFIER: Change for each physical node

// Hub MAC Address format -> {0x25, 0x3A, 0x42, 0xCE, 0xB7, 0xFF}
uint8_t hubAddress[] = HUB_MAC_ADDRESS;

// --- Sleep Configuration ---
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  1800         // Sleep duration: 900 seconds = 15 minutes

// --- Pin Definitions ---
const int SOIL_PIN = 34; 
const int BATT_PIN = 35; 

// ==============================================================================
// GLOBAL OBJECTS & MEMORY
// ==============================================================================
Adafruit_BME280 bme; 
esp_now_peer_info_t peerInfo;

extern "C" esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool enable, wifi_phy_rate_t rate);

typedef struct struct_message {
    int id;
    float moisture;
    float temp;
    float humidity;
    int battPct;         
    unsigned int readingId;
} struct_message;

struct_message myData;

// RTC_DATA_ATTR ensures this variable survives Deep Sleep resets
RTC_DATA_ATTR unsigned int packetCount = 0; 

// Calibration variables loaded at boot
float battMultiplier;
int airVal;
int waterVal;

// ==============================================================================
// CALIBRATION DATABASE
// ==============================================================================
void loadCalibration() {
    switch(NODE_ID) {
        case 1:
            battMultiplier = 6.17; airVal = 3160; waterVal = 980; break;
        case 2:
            battMultiplier = 5.96; airVal = 3290; waterVal = 1040; break;
        case 3:
            battMultiplier = 6.25; airVal = 3190; waterVal = 1007; break;
        case 4:
            battMultiplier = 5.76; airVal = 2970; waterVal = 1010; break;
        case 5:
            battMultiplier = 6.13; airVal = 2830; waterVal = 940; break;
        default:
            battMultiplier = 6.0;  airVal = 3000; waterVal = 1000; break;
    }
}

// ==============================================================================
// MAIN LIFECYCLE
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(500); // Brief stabilization delay

    loadCalibration(); 

    if (!bme.begin(0x77)) {
        Serial.println("[WARN] BME280 Initialization Failed!");
    } else {
      Serial.println("BME280 Initialized Sucessfully!");
    }

    // --- Radio Optimization & AMPDU Fix ---
    WiFi.mode(WIFI_STA);
    esp_wifi_stop(); 
    esp_wifi_deinit();
    
    wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
    my_config.ampdu_tx_enable = false; 
    my_config.ampdu_rx_enable = false;
    esp_wifi_init(&my_config);
    esp_wifi_start();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    esp_wifi_internal_set_fix_rate(WIFI_IF_STA, true, WIFI_PHY_RATE_1M_L);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] ESP-NOW Init Failed. Restarting...");
        ESP.restart();
    }

    memcpy(peerInfo.peer_addr, hubAddress, 6);
    peerInfo.channel = 1; 
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERR] Failed to register Hub Peer");
    }
}

void loop() {
    // --- 1. Read Soil Moisture ---
    long soilSum = 0;
    for (int i = 0; i < 10; i++) {
        soilSum += analogRead(SOIL_PIN);
        delay(5);
    }
    int rawSoil = soilSum / 10;
    float moistureVal = map(rawSoil, airVal, waterVal, 0, 100);
    myData.moisture = constrain(moistureVal, 0.0, 100.0);

    // --- 2. Read BME280 Atmospheric Data ---
    myData.temp = bme.readTemperature();
    myData.humidity = bme.readHumidity();
    if (isnan(myData.temp)) myData.temp = 0.0;
    if (isnan(myData.humidity)) myData.humidity = 0.0;

    // --- 3. Read Battery Telemetry ---
    long batSum = 0;
    for (int i = 0; i < 10; i++) { 
        batSum += analogRead(BATT_PIN);
        delay(2);
    }
    float rawBatAvg = batSum / 10.0;
    float voltage = (rawBatAvg * 3.31 / 4095.0) * battMultiplier;
    
    // Map Li-ion curve: 3.5V (0%) to 4.2V (100%)
    float pctFloat = (voltage - 3.5) * (100.0 / 0.7);
    myData.battPct = (int)constrain(pctFloat, 0.0, 100.0);

    // --- 4. Package & Transmit ---
    myData.id = NODE_ID;
    myData.readingId = packetCount++;

    esp_err_t result = esp_now_send(hubAddress, (uint8_t *) &myData, sizeof(myData));
    
    // --- 5. Debug Output ---
    Serial.println("\n-------------------------------------------------");
    Serial.printf("NODE %d TELEMETRY (Packet %d)\n", NODE_ID, myData.readingId);
    Serial.printf("SOIL: Raw [%d] | Air: %d | Water: %d | Moist: %.1f%%\n", rawSoil, airVal, waterVal, myData.moisture);
    Serial.printf("ATMOS: Temp: %.2f*C | Humidity: %.2f%%\n", myData.temp, myData.humidity);
    Serial.printf("BATT: Raw [%.1f] | Mult: %.2f | Volt: %.2fV | Cap: %d%%\n", rawBatAvg, battMultiplier, voltage, myData.battPct);
    Serial.printf("UPLINK STATUS: %s\n", (result == ESP_OK) ? "SUCCESS" : "FAIL");
    Serial.println("-------------------------------------------------");

    // --- 6. Enter Deep Sleep ---
    Serial.printf("Entering Deep Sleep for %d seconds...\n", TIME_TO_SLEEP);
    delay(100); // Allow serial buffer to flush before power cut
    
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}