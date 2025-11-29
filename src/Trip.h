#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class TripStatus {
    CANCELLED,
    PENDING,
    COMPLETE,
    IN_PROGRESS,
    UNKNOWN
};

struct Trip {
    String  id;
    String  bike_id;
    String  hub_id;
    String  customer_id;
    TripStatus trip_status;
    int64_t reservation_expiry;
    int64_t reservation_date;

    int64_t trip_start_date;  // 0 if null/absent
    int64_t trip_end_date;    // 0 if null/absent

    double  trip_end_long;    // NAN if null/absent
    double  trip_end_lat;     // NAN if null/absent

    String  trip_secret;      // "" if null/absent
    int     price;            // -1 if null/absent
    bool    isPaid;           // false if null/absent

    bool    hasTripStart;
    bool    hasTripEnd;
    bool    hasEndCoords;
    bool    hasPrice;
    bool    hasIsPaid;
};

inline TripStatus parseTripStatus(const char* s) {
    if (!s) return TripStatus::UNKNOWN;
    if (!strcmp(s, "CANCELLED"))   return TripStatus::CANCELLED;
    if (!strcmp(s, "PENDING"))     return TripStatus::PENDING;
    if (!strcmp(s, "COMPLETE"))    return TripStatus::COMPLETE;
    if (!strcmp(s, "IN_PROGRESS")) return TripStatus::IN_PROGRESS;
    return TripStatus::UNKNOWN;
}

inline bool parseTripFromJson(const String& json, Trip& out) {
    DynamicJsonDocument doc(1024);

    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print(F("JSON parse error: "));
        Serial.println(err.c_str());
        return false;
    }

    out.id          = doc["id"]        | "";
    out.bike_id     = doc["bike_id"]   | "";
    out.hub_id      = doc["hub_id"]    | "";
    out.customer_id = doc["customer_id"] | "";

    const char* statusStr = doc["trip_status"] | "";
    out.trip_status = parseTripStatus(statusStr);

    out.reservation_expiry = doc["reservation_expiry"] | 0LL;
    out.reservation_date   = doc["reservation_date"]   | 0LL;

    // Optional fields: null or missing → isNull()

    JsonVariant vStart = doc["trip_start_date"];
    if (vStart.isNull()) {
        out.trip_start_date = 0;
        out.hasTripStart = false;
    } else {
        out.trip_start_date = (int64_t)vStart.as<long long>();
        out.hasTripStart = true;
    }

    JsonVariant vEnd = doc["trip_end_date"];
    if (vEnd.isNull()) {
        out.trip_end_date = 0;
        out.hasTripEnd = false;
    } else {
        out.trip_end_date = (int64_t)vEnd.as<long long>();
        out.hasTripEnd = true;
    }

    JsonVariant vLong = doc["trip_end_long"];
    JsonVariant vLat  = doc["trip_end_lat"];
    if (vLong.isNull() || vLat.isNull()) {
        out.trip_end_long = NAN;
        out.trip_end_lat  = NAN;
        out.hasEndCoords  = false;
    } else {
        out.trip_end_long = vLong.as<double>();
        out.trip_end_lat  = vLat.as<double>();
        out.hasEndCoords  = true;
    }

    JsonVariant vSecret = doc["trip_secret"];
    out.trip_secret = vSecret.isNull()
        ? "" : String(vSecret.as<const char*>());

    JsonVariant vPrice = doc["price"];
    if (vPrice.isNull()) {
        out.price = -1;
        out.hasPrice = false;
    } else {
        out.price = vPrice.as<int>();
        out.hasPrice = true;
    }

    JsonVariant vPaid = doc["isPaid"];
    if (vPaid.isNull()) {
        out.isPaid = false;
        out.hasIsPaid = false;
    } else {
        out.isPaid = vPaid.as<bool>();
        out.hasIsPaid = true;
    }

    return true;
}
