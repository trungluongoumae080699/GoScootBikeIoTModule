#pragma once
#include <Arduino.h>

// -----------------------------------------
// Alert Type
// -----------------------------------------
enum class AlertType : uint8_t
{
    CRASH = 0,
    LOW_BATTERY = 1,
    BOUNDARY_CROSS = 2
};

inline const char *alertTypeToString(AlertType t)
{
    switch (t)
    {
    case AlertType::CRASH:
        return "crash";
    case AlertType::LOW_BATTERY:
        return "low_battery";
    case AlertType::BOUNDARY_CROSS:
        return "boundary_cross";
    }
    return "";
}

// -----------------------------------------
// Alert Structure
// -----------------------------------------
struct Alert
{
    String id;
    String bike_id;
    String content;
    AlertType type;
    float longitude;
    float latitude;
    int64_t time;
};


inline int encodeAlert(const Alert &a, uint8_t *buffer)
{
    int offset = 0;

    // ----- id -----
    uint8_t idLen = (uint8_t)min((size_t)255, a.id.length());
    buffer[offset++] = idLen;
    memcpy(buffer + offset, a.id.c_str(), idLen);
    offset += idLen;

    // ----- bike_id -----
    uint8_t bikeLen = (uint8_t)min((size_t)255, a.bike_id.length());
    buffer[offset++] = bikeLen;
    memcpy(buffer + offset, a.bike_id.c_str(), bikeLen);
    offset += bikeLen;

    // ----- content -----
    uint8_t contentLen = (uint8_t)min((size_t)255, a.content.length());
    buffer[offset++] = contentLen;
    memcpy(buffer + offset, a.content.c_str(), contentLen);
    offset += contentLen;

    // ----- type (1 byte enum) -----
    buffer[offset++] = (uint8_t)a.type;

    // ----- longitude -----
    writeFloat32LE(buffer, a.longitude, offset);

    // ----- latitude -----
    writeFloat32LE(buffer, a.latitude, offset);

    // ----- time (int64 LE) -----
    writeInt64LE(buffer, a.time, offset);

    return offset;
}