#pragma once

#include <Arduino.h>
#include <stdint.h>

struct Telemetry
{
  String id;
  String bikeId;
  float last_gps_long;
  float last_gps_lat;
  float longitude;
  float latitude;
  int32_t battery;
  int64_t time;
  int64_t last_gps_contact_time;
  bool batteryIsLow;
  bool isToppled;
  bool isCrashed;
  bool isOutOfBound;
  UsageState usageState;
};

// ---- helpers for little endian writes ----
inline void writeInt32LE(uint8_t *buf, int32_t value, int &offset)
{
  uint32_t v = static_cast<uint32_t>(value);
  buf[offset++] = (uint8_t)(v & 0xFF);
  buf[offset++] = (uint8_t)((v >> 8) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 16) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 24) & 0xFF);
}

inline void writeInt64LE(uint8_t *buf, int64_t value, int &offset)
{
  uint64_t v = static_cast<uint64_t>(value);
  buf[offset++] = (uint8_t)(v & 0xFF);
  buf[offset++] = (uint8_t)((v >> 8) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 16) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 24) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 32) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 40) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 48) & 0xFF);
  buf[offset++] = (uint8_t)((v >> 56) & 0xFF);
}

inline void writeFloat32LE(uint8_t *buf, float value, int &offset)
{
  uint32_t raw = 0;
  memcpy(&raw, &value, sizeof(float));
  buf[offset++] = (uint8_t)(raw & 0xFF);
  buf[offset++] = (uint8_t)((raw >> 8) & 0xFF);
  buf[offset++] = (uint8_t)((raw >> 16) & 0xFF);
  buf[offset++] = (uint8_t)((raw >> 24) & 0xFF);
}

inline uint8_t boolToUint8(bool v) { return v ? 1 : 0; }

// Return number of bytes written
inline int encodeTelemetry(const Telemetry &t, uint8_t *buffer)
{
  int offset = 0;

  // 1) ID length (1 byte) + ID bytes
  uint8_t idLen = (uint8_t)min((size_t)255, (size_t)t.id.length());
  buffer[offset++] = idLen;
  memcpy(buffer + offset, t.id.c_str(), idLen);
  offset += idLen;

  // 2) BikeId length (1 byte) + BikeId bytes
  uint8_t bikeLen = (uint8_t)min((size_t)255, (size_t)t.bikeId.length());
  buffer[offset++] = bikeLen;
  memcpy(buffer + offset, t.bikeId.c_str(), bikeLen);
  offset += bikeLen;

  // 3) BatteryStatus (int32, LE)
  writeInt32LE(buffer, t.battery, offset);

  // 4) Current longitude (float32, LE)
  writeFloat32LE(buffer, t.longitude, offset);

  // 5) Current latitude (float32, LE)
  writeFloat32LE(buffer, t.latitude, offset);

  // 6) Current time (int64, LE)
  writeInt64LE(buffer, t.time, offset);

  // 7) Last GPS longitude (float32, LE)
  writeFloat32LE(buffer, t.last_gps_long, offset);

  // 8) Last GPS latitude (float32, LE)
  writeFloat32LE(buffer, t.last_gps_lat, offset);

  // 9) Last GPS contact time (int64, LE)
  writeInt64LE(buffer, t.last_gps_contact_time, offset);

  // 10) BatteryIsLow (1 byte)
  buffer[offset++] = boolToUint8(t.batteryIsLow);

  // 11) IsToppled (1 byte)
  buffer[offset++] = boolToUint8(t.isToppled);

  // 12) IsCrashed (1 byte)
  buffer[offset++] = boolToUint8(t.isCrashed);

  // 13) IsOutOfBound (1 byte)
  buffer[offset++] = boolToUint8(t.isOutOfBound);

  // 14) UsageStatus (1 byte)
  buffer[offset++] = static_cast<uint8_t>(t.usageState);

  return offset;
}
