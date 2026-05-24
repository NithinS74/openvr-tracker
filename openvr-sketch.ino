#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include "TCA9548.h"
#include <WiFi.h>
#include <WiFiUdp.h>

#include "common.h"
#include "wifi_secrets.h"

// --- HARDWARE CONFIGURATION ---
TCA9548 myMux(0x70);
const uint8_t MAX_TRACKERS = 5;
Adafruit_BNO055 bno[MAX_TRACKERS] = {
    Adafruit_BNO055(55, 0x28, &Wire),
    Adafruit_BNO055(55, 0x28, &Wire),
    Adafruit_BNO055(55, 0x28, &Wire),
    Adafruit_BNO055(55, 0x28, &Wire),
    Adafruit_BNO055(55, 0x28, &Wire)
};

// Tracking active sensors
uint8_t active_tracker_count = 0;
uint8_t active_tracker_ids[8] = {0}; // Sized to 8 to match HandshakeReqPacket array size

// --- NETWORKING STATE ---
enum TrackerState {
    STATE_DISCONNECTED,
    STATE_HANDSHAKE,
    STATE_STREAMING
};
TrackerState currentState = STATE_DISCONNECTED;

WiFiUDP udp;
IPAddress pc_ip; // Dynamically acquired from PC Driver ACK
bool is_ip_known = false;

// Timing Variables
unsigned long lastDataTime = 0;
unsigned long lastHandshakeTime = 0;
unsigned long lastHeartbeatTime = 0;
const int INTERVAL_MS = 10; // ~100Hz
const int HANDSHAKE_INTERVAL_MS = 1000; // 1Hz Broadcast
const int HEARTBEAT_TIMEOUT_MS = 5000;  // 5 seconds connection timeout

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n--- VR Multi-Tracker Sender Starting (ESP32) ---");

    Wire.begin(21, 22); 

    // Initialize Multiplexer
    if (myMux.begin() == false) {
        Serial.println("PCA9548a Mux not detected. Check wiring (SDA->21, SCL->22).");
        while(1);
    }
    Serial.println("PCA9548a Mux initialized.");

    // Hardware Discovery
    for (uint8_t i = 0; i < MAX_TRACKERS; i++) {
        myMux.selectChannel(i);
        delay(10); // Settle time
        
        Serial.printf("Checking channel %d... ", i);
        if (bno[i].begin()) {
            Serial.println("OK");
            bno[i].setExtCrystalUse(false);
            active_tracker_ids[active_tracker_count] = i;
            active_tracker_count++;
        } else {
            Serial.println("FAILED");
        }
    }
    
    Serial.printf("Hardware Scan Complete. Found %d active trackers.\n", active_tracker_count);

    // Connect to Wi-Fi
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

    // We must bind the local port to receive UDP packets (ACKs and Heartbeats)
    udp.begin(TARGET_PORT);
    
    // Begin Handshake Protocol
    currentState = STATE_HANDSHAKE;
}

void loop() {
    // 1. Process Incoming Commands from Driver
    processIncomingPackets();

    // 2. Handle State Logic
    if (currentState == STATE_HANDSHAKE) {
        if (millis() - lastHandshakeTime >= HANDSHAKE_INTERVAL_MS) {
            lastHandshakeTime = millis();
            sendHandshake();
        }
    } else if (currentState == STATE_STREAMING) {
        // Check Heartbeat Timeout
        if (millis() - lastHeartbeatTime > HEARTBEAT_TIMEOUT_MS) {
            Serial.println("Heartbeat lost. Reverting to Handshake state.");
            currentState = STATE_HANDSHAKE;
            is_ip_known = false;
        } else if (millis() - lastDataTime >= INTERVAL_MS) {
            lastDataTime = millis();
            sendIMUData();
        }
    }
}

void processIncomingPackets() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        // Read the packet header byte
        uint8_t packetType = udp.read();
        
        if (packetType == PACKET_HANDSHAKE_ACK && currentState == STATE_HANDSHAKE) {
            // Packet is 2 bytes total. Second byte is status.
            uint8_t status = udp.read();
            if (status == 0) { // OK
                pc_ip = udp.remoteIP();
                is_ip_known = true;
                currentState = STATE_STREAMING;
                lastHeartbeatTime = millis(); // Reset heartbeat timer
                Serial.print("Handshake ACK received. Locked onto PC IP: ");
                Serial.println(pc_ip);
            }
        } else if (packetType == PACKET_HEARTBEAT && currentState == STATE_STREAMING) {
            lastHeartbeatTime = millis(); // Refresh timeout
        } else if (packetType == PACKET_HANDSHAKE_REQ && currentState == STATE_STREAMING) {
            // "Reset Command" received from the driver (driver rebooted)
            Serial.println("Driver requested a reset. Reverting to Handshake state.");
            currentState = STATE_HANDSHAKE;
            is_ip_known = false;
        }
        
        // Discard any remaining bytes in this packet
        udp.flush();
    }
}

void sendHandshake() {
    HandshakeReqPacket req;
    req.packet_type = PACKET_HANDSHAKE_REQ;
    req.active_tracker_count = active_tracker_count;
    // Fill the array (defaulting unused slots to 0 or 0xFF)
    for (uint8_t i = 0; i < 8; i++) {
        if (i < active_tracker_count) {
            req.active_tracker_ids[i] = active_tracker_ids[i];
        } else {
            req.active_tracker_ids[i] = 0xFF; // Padding
        }
    }
    
    // Broadcast to the subnet to auto-discover the PC
    IPAddress broadcastIp = IPAddress(255, 255, 255, 255);
    udp.beginPacket(broadcastIp, TARGET_PORT);
    udp.write((uint8_t*)&req, sizeof(HandshakeReqPacket));
    udp.endPacket();
    
    Serial.println("Broadcasted Handshake Request...");
}

void sendIMUData() {
    if (!is_ip_known) return;

    for (uint8_t i = 0; i < active_tracker_count; i++) {
        uint8_t id = active_tracker_ids[i];
        
        myMux.selectChannel(id); // Rapidly select the I2C channel for this sensor
        
        imu::Quaternion quat = bno[id].getQuat();
        imu::Vector<3> linAcc = bno[id].getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);

        IMUPacket pkt;
        pkt.packet_type = PACKET_IMU_DATA;
        pkt.imu_id = id;
        pkt.w = (float)quat.w();
        pkt.x = (float)quat.x();
        pkt.y = (float)quat.y();
        pkt.z = (float)quat.z();
        pkt.ax = (float)linAcc.x();
        pkt.ay = (float)linAcc.y();
        pkt.az = (float)linAcc.z();

        udp.beginPacket(pc_ip, TARGET_PORT);
        udp.write((uint8_t*)&pkt, sizeof(IMUPacket));
        udp.endPacket();
    }
}