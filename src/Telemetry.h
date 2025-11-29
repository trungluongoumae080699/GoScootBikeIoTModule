#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>
#include <stdint.h>

struct Telemetry {
  String id;
  String bikeId;
  float longitude;
  float latitude;
  int32_t battery;
  int64_t time;
};

void writeUint16BE(uint8_t *buf, uint16_t value, int &index) {
  buf[index++] = (value >> 8) & 0xFF;
  buf[index++] = (value) & 0xFF;
}

void writeInt32BE(uint8_t *buf, int32_t value, int &index) {
  buf[index++] = (value >> 24) & 0xFF;
  buf[index++] = (value >> 16) & 0xFF;
  buf[index++] = (value >> 8) & 0xFF;
  buf[index++] = (value) & 0xFF;
}

void writeInt64BE(uint8_t *buf, int64_t value, int &index) {
  for (int i = 7; i >= 0; i--) {
    buf[index++] = (value >> (8 * i)) & 0xFF;
  }
}

void writeFloatBE(uint8_t *buf, float value, int &index) {
  uint32_t raw = *((uint32_t*)&value);  // reinterpret bits
  writeInt32BE(buf, raw, index);
}

int encodeTelemetry(const Telemetry &t, uint8_t *outBuf) {
  int index = 0;

  // ----- Encode id -----
  uint16_t idLen = t.id.length();
  writeUint16BE(outBuf, idLen, index);
  memcpy(outBuf + index, t.id.c_str(), idLen);
  index += idLen;

  // ----- Encode bikeId -----
  uint16_t bikeIdLen = t.bikeId.length();
  writeUint16BE(outBuf, bikeIdLen, index);
  memcpy(outBuf + index, t.bikeId.c_str(), bikeIdLen);
  index += bikeIdLen;

  // ----- Encode float longitude -----
  writeFloatBE(outBuf, t.longitude, index);

  // ----- Encode float latitude -----
  writeFloatBE(outBuf, t.latitude, index);

  // ----- Encode int32 battery -----
  writeInt32BE(outBuf, t.battery, index);

  // ----- Encode int64 time -----
  writeInt64BE(outBuf, t.time, index);

  return index; // number of bytes encoded
}



#endif