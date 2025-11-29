#pragma once

// Use #pragma pack to ensure the data is tightly packed without padding bytes.
// This is crucial for network communication between different architectures
// (ESP32 vs PC).
#pragma pack(push, 1)
struct IMUPacket {
  // Rotation: Quaternion data (Standard normalized quaternion)
  // Order: W, X, Y, Z is standard for many libraries, but ensure your ESP32
  // matches this.
  float w;
  float x;
  float y;
  float z;

  // Acceleration: Raw accelerometer data (Optional but recommended)
  // Useful for future features like gravity drift correction or basic velocity
  // estimation.
  float ax;
  float ay;
  float az;

  // Optional: Add a standard magic number or ID if you plan to support multiple
  // trackers later.
  uint8_t tracker_id;
};
#pragma pack(pop)
