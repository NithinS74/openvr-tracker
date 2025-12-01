#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "common.h"
#include "wifi_secrets.h"

// --- TRACKER CONFIGURATION ---
const uint8_t DEVICE_ID = 1;

// --- HARDWARE CONFIGURATION ---
// I2C Wiring for standard ESP32: SDA -> GPIO 21, SCL -> GPIO 22
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire);

WiFiUDP udp;
unsigned long lastTime = 0;
const int INTERVAL_MS = 10; // ~100Hz

void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial monitor time to catch up

    Serial.println("\n\n--- VR Tracker Sender Starting (ESP32) ---");
    Serial.printf("Tracker ID: %d\n", DEVICE_ID);

    // 1. Initialize I2C Explicitly
    // This ensures GPIO 21 and 22 are actually used.
    Wire.begin(21, 22); 
    
    Serial.print("Initializing BNO055... ");
    if (!bno.begin()) {
        Serial.println("FAILED!");
        Serial.println("Check wiring: SDA->21, SCL->22, VCC->3.3V, GND->GND");
        while (1); // Halt
    }
    Serial.println("OK!");
    delay(100);

    // 2. Crystal Selection (THE FIX)
    // CRITICAL: Set to FALSE for generic/Chinese BNO055 modules. 
    // Set to TRUE only for official Adafruit boards. 
    // If this is wrong, you get (0,0,0,0) data.
    bno.setExtCrystalUse(false); 

    // 3. Connect to Wi-Fi
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected!");
    Serial.print("ESP IP: ");
    Serial.println(WiFi.localIP());

    // 4. Setup UDP
    udp.begin(TARGET_PORT);
    Serial.printf("Targeting PC: %s:%d\n", PC_IP, TARGET_PORT);
}

void loop() {
    // Non-blocking loop
    if (millis() - lastTime >= INTERVAL_MS) {
        lastTime = millis();
        sendIMUData();
    }
}

void sendIMUData() {
    // 1. Read Quaternion
    imu::Quaternion quat = bno.getQuat();
    
    // 2. Read Linear Acceleration
    imu::Vector<3> linAcc = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);

    // DEBUG: Print to Serial to verify sensor is working locally
    // If these are zero, your sensor is not calibrating or wiring is loose.
    // Serial.printf("Q: %.2f %.2f %.2f %.2f\n", quat.w(), quat.x(), quat.y(), quat.z());

    // 3. Pack Data into the mandatory struct
    IMUPacket pkt;

    // Ensure types match common.h (float)
    pkt.w = (float)quat.w();
    pkt.x = (float)quat.x();
    pkt.y = (float)quat.y();
    pkt.z = (float)quat.z();

    pkt.ax = (float)linAcc.x();
    pkt.ay = (float)linAcc.y();
    pkt.az = (float)linAcc.z();

    pkt.tracker_id = DEVICE_ID;

    // 4. Send over UDP
    // If you strictly want BROADCAST (to all PCs), use:
    // udp.beginPacket(IPAddress(255,255,255,255), TARGET_PORT);
    // Otherwise, use the PC_IP from secrets:
    udp.beginPacket(PC_IP, TARGET_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
}