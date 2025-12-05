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
};

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
    String id; // nếu isValid == false, lý do tại sao
};