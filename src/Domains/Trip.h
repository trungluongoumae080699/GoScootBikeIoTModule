#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct Trip
{
    String id;
    String customer_id;
    String bike_id;
    int64_t reservation_expiry;
    String trip_secret;
    float current_lng = 0;
    float current_lat = 0;
};

bool parseTripJson(const String &json, Trip &outTrip)
{
    // Ensure 64-bit support is enabled in your build:
    // #define ARDUINOJSON_USE_LONG_LONG 1
    // before including <ArduinoJson.h>

    StaticJsonDocument<512> doc;

    DeserializationError err = deserializeJson(doc, json);
    if (err)
    {
        Serial.print(F("❌ JSON parse error: "));
        Serial.println(err.c_str());
        return false;
    }

    if (!doc.is<JsonObject>())
    {
        Serial.println(F("❌ JSON is not an object"));
        return false;
    }

    JsonObject obj = doc.as<JsonObject>();

    // ---- Required keys ----
    const char *requiredKeys[] = {
        "id",
        "bike_id",
        "customer_id",
        "reservation_expiry",
        "trip_secret"};

    for (const char *key : requiredKeys)
    {
        if (!obj.containsKey(key))
        {
            Serial.print(F("❌ Missing key: "));
            Serial.println(key);
            return false;
        }
        if (obj[key].isNull())
        {
            Serial.print(F("❌ Null value for key: "));
            Serial.println(key);
            return false;
        }
    }

    // ---- Type checks ----
    if (!obj["id"].is<const char *>())
    {
        Serial.println(F("❌ 'id' is not a string"));
        return false;
    }
    if (!obj["bike_id"].is<const char *>())
    {
        Serial.println(F("❌ 'bike_id' is not a string"));
        return false;
    }
    if (!obj["customer_id"].is<const char *>())
    {
        Serial.println(F("❌ 'customer_id' is not a string"));
        return false;
    }
    if (!obj["trip_secret"].is<const char *>())
    {
        Serial.println(F("❌ 'trip_secret' is not a string"));
        return false;
    }
    if (!obj["reservation_expiry"].is<long long>())
    {
        Serial.println(F("❌ 'reservation_expiry' is not an integer"));
        return false;
    }

    long long expiry = obj["reservation_expiry"].as<long long>();
    if (expiry < 0 || expiry > 9999999999999LL)
    {
        Serial.println(F("❌ 'reservation_expiry' out of expected range"));
        return false;
    }

    // ---- All good → fill Trip struct ----
    outTrip.id = String(obj["id"].as<const char *>());
    outTrip.bike_id = String(obj["bike_id"].as<const char *>());
    outTrip.customer_id = String(obj["customer_id"].as<const char *>());
    outTrip.trip_secret = String(obj["trip_secret"].as<const char *>());
    outTrip.reservation_expiry = static_cast<int64_t>(expiry);

    Serial.println(F("✅ Trip JSON parsed successfully"));
    return true;
}

// ---- Trip encoder (Arduino) ----
inline int encodeTrip(const Trip &t, uint8_t *buffer)
{
    int offset = 0;

    // 1) id (len + bytes)
    uint8_t idLen = (uint8_t)min((size_t)255, t.id.length());
    buffer[offset++] = idLen;
    memcpy(buffer + offset, t.id.c_str(), idLen);
    offset += idLen;

    // 2) customer_id (len + bytes)
    uint8_t custLen = (uint8_t)min((size_t)255, t.customer_id.length());
    buffer[offset++] = custLen;
    memcpy(buffer + offset, t.customer_id.c_str(), custLen);
    offset += custLen;

    // 3) bike_id (len + bytes)
    uint8_t bikeLen = (uint8_t)min((size_t)255, t.bike_id.length());
    buffer[offset++] = bikeLen;
    memcpy(buffer + offset, t.bike_id.c_str(), bikeLen);
    offset += bikeLen;

    // 4) reservation_expiry (int64 LE)
    writeInt64LE(buffer, t.reservation_expiry, offset);

    // 5) trip_secret (len + bytes)
    uint8_t secretLen = (uint8_t)min((size_t)255, t.trip_secret.length());
    buffer[offset++] = secretLen;
    memcpy(buffer + offset, t.trip_secret.c_str(), secretLen);
    offset += secretLen;

    // 6) current_lng (float32 LE)
    writeFloat32LE(buffer, t.current_lng, offset);

    // 7) current_lat (float32 LE)
    writeFloat32LE(buffer, t.current_lat, offset);

    return offset; // total bytes written
}

bool validateTripJson(const String &json)
{
    // Nếu bạn dùng PlatformIO, nhớ đã bật:
    // build_flags = -D ARDUINOJSON_USE_LONG_LONG=1
    // Hoặc trong một header chung:
    // #define ARDUINOJSON_USE_LONG_LONG 1
    // trước khi include <ArduinoJson.h>

    StaticJsonDocument<512> doc;

    DeserializationError err = deserializeJson(doc, json);
    if (err)
    {
        Serial.print(F("❌ JSON parse error: "));
        Serial.println(err.c_str());
        return false;
    }

    if (!doc.is<JsonObject>())
    {
        Serial.println(F("❌ JSON is not an object"));
        return false;
    }

    JsonObject obj = doc.as<JsonObject>();

    // ------- Kiểm tra tồn tại & không null -------
    // thay vì containsKey (deprecated), ta check .isNull()
    const char *requiredKeys[] = {
        "id",
        "bike_id",
        "customer_id",
        "reservation_expiry",
        "trip_secret"};

    for (const char *key : requiredKeys)
    {
        if (!obj.containsKey(key))
        {
            Serial.print(F("❌ Missing key: "));
            Serial.println(key);
            return false;
        }
        if (obj[key].isNull())
        {
            Serial.print(F("❌ Null value for key: "));
            Serial.println(key);
            return false;
        }
    }

    // ------- TYPE CHECKING -------

    // id: string
    if (!obj["id"].is<const char *>())
    {
        Serial.println(F("❌ 'id' is not a string"));
        return false;
    }

    // bike_id: string
    if (!obj["bike_id"].is<const char *>())
    {
        Serial.println(F("❌ 'bike_id' is not a string"));
        return false;
    }

    // customer_id: string
    if (!obj["customer_id"].is<const char *>())
    {
        Serial.println(F("❌ 'customer_id' is not a string"));
        return false;
    }

    // trip_secret: string
    if (!obj["trip_secret"].is<const char *>())
    {
        Serial.println(F("❌ 'trip_secret' is not a string"));
        return false;
    }

    // reservation_expiry: bắt buộc là 64-bit integer
    if (!obj["reservation_expiry"].is<long long>())
    {
        Serial.println(F("❌ 'reservation_expiry' is not an integer"));
        return false;
    }

    long long expiry = obj["reservation_expiry"].as<long long>();
    if (expiry < 0 || expiry > 9999999999999LL)
    {
        Serial.println(F("❌ 'reservation_expiry' out of expected range"));
        return false;
    }

    Serial.println(F("✅ JSON structure is valid"));
    return true;
}

struct TripValidationResponse
{
    bool isValid;
};

bool decodeTripValidationResponse(const uint8_t *buf,
                                  size_t len,
                                  TripValidationResponse &out)
{
    if (!buf || len < 1)
    {
        Serial.println(F("[TRIP] Response too short (need at least 1 byte)"));
        return false;
    }

    uint8_t flag = buf[0];

    if (flag != 0 && flag != 1)
    {
        Serial.print(F("[TRIP] Warning: unexpected isValid value = "));
        Serial.println(flag);
        // Still treat non-zero as "true"
    }

    out.isValid = (flag != 0);
    return true;
}


struct TripTerminationPayload
{
    float end_lng;
    float end_lat;
};

// ---- Trip encoder (Arduino) ----
inline int encodeTripTerminationPayload(const TripTerminationPayload &t, uint8_t *buffer)
{
    int offset = 0;
    // 6) current_lng (float32 LE)
    writeFloat32LE(buffer, t.end_lng, offset);

    // 7) current_lat (float32 LE)
    writeFloat32LE(buffer, t.end_lat, offset);

    return offset; // total bytes written
}

struct TripTerminationResponse
{
    bool isValid;
};

bool decodeTripTerminationResponse(const uint8_t *buf,
                                  size_t len,
                                  TripTerminationResponse &out)
{
    if (!buf || len < 1)
    {
        Serial.println(F("[TRIP] Response too short (need at least 1 byte)"));
        return false;
    }

    uint8_t flag = buf[0];

    if (flag != 0 && flag != 1)
    {
        Serial.print(F("[TRIP] Warning: unexpected isValid value = "));
        Serial.println(flag);
        // Still treat non-zero as "true"
    }

    out.isValid = (flag != 0);
    return true;
}

int decodeTripStatusUpdate(const uint8_t *buf, size_t len)
{
    if (!buf || len < 1)
    {
        Serial.println(F("[TRIP] Payload too short"));
        return -1;   // ERROR
    }

    uint8_t status = buf[0];

    if (status > 2)
    {
        Serial.print(F("[TRIP] Invalid status = "));
        Serial.println(status);
        return -1;   // ERROR
    }

    return (int)status;
}