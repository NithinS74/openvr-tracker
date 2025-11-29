#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "common.h"
#include "wifi_secrets.h"

// --- TRACKER CONFIGURATION ---
// Set this to 0 for Waist, 1 for Left Foot, 2 for Right Foot, etc.
const uint8_t DEVICE_ID = 1; 

// --- HARDWARE CONFIGURATION ---
// I2C Wiring for NodeMCU / Wemos D1 Mini:
// SDA -> GPIO 4 (D2)
// SCL -> GPIO 5 (D1)
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28);

// UDP Object
WiFiUDP udp;

// Loop Timing
unsigned long lastTime = 0;
const int INTERVAL_MS = 10; // 10ms = ~100Hz (Good for VR)

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n--- VR Tracker Sender Starting ---");
  Serial.printf("Tracker ID: %d\n", DEVICE_ID);

  // 1. Initialize BNO055
  Serial.print("Initializing BNO055... ");
  if (!bno.begin()) {
    Serial.println("FAILED!");
    Serial.println("Check wiring: SDA->D2, SCL->D1, VCC->3.3V, GND->GND");
    while (1); // Stop here
  }
  Serial.println("OK!");
  
  // Use external crystal for better accuracy
  bno.setExtCrystalUse(true);

  // 2. Connect to Wi-Fi
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

  // 3. Setup UDP
  udp.begin(TARGET_PORT);
  Serial.printf("Targeting PC: %s:%d\n", PC_IP, TARGET_PORT);
}

void loop() {
  if (millis() - lastTime >= INTERVAL_MS) {
    lastTime = millis();
    sendIMUData();
  }
}

void sendIMUData() {
  // 1. Read Quaternion (Fused)
  imu::Quaternion quat = bno.getQuat();

  // 2. Read Linear Acceleration (Gravity Removed)
  imu::Vector<3> linAcc = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);

  // 3. Pack Data
  IMUPacket pkt;
  
  // Cast double (BNO lib) to float (Our Packet)
  pkt.w = (float)quat.w();
  pkt.x = (float)quat.x();
  pkt.y = (float)quat.y();
  pkt.z = (float)quat.z();
  
  pkt.ax = (float)linAcc.x();
  pkt.ay = (float)linAcc.y();
  pkt.az = (float)linAcc.z();

  // Assign the Tracker ID defined at the top
  pkt.tracker_id = DEVICE_ID;

  // 4. Send over UDP
  udp.beginPacket(PC_IP, TARGET_PORT);
  udp.write((uint8_t*)&pkt, sizeof(pkt));
  udp.endPacket();
}
