#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

// --- NETWORK CONFIGURATION ---
#define WIFI_SSID "Your_WiFi_Name"     // REPLACE THIS
#define WIFI_PASS "Your_WiFi_Password" // REPLACE THIS

// --- PC DRIVER CONFIGURATION ---
// Run 'ipconfig' on Windows to find your IPv4 Address
const char *PC_IP = "192.168.1.X"; // REPLACE THIS
const int TARGET_PORT = 8080;      // Must match driver port

#endif
