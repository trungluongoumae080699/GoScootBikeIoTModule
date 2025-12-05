#pragma once
#include <Arduino.h>
#include <stdint.h>

struct CellInfo {
    int   mcc  = 0;
    int   mnc  = 0;
    long  lac  = 0;   // TAC/LAC
    long  cid  = 0;   // Cell ID

    // ----------------------------------------------------------
    // Parse CPSI line → Fill fields (mcc, mnc, lac, cid)
    // Example line:
    // +CPSI: LTE,Online,452-02,0x1817,156384564,155,...
    // ----------------------------------------------------------
    bool parseCpsiLine(const String &line) {
        int idx = line.indexOf("+CPSI:");
        if (idx < 0) return false;

        // Take the part after "+CPSI:"
        String payload = line.substring(idx + 6);
        payload.trim();

        int partIndex = 0;
        int start = 0;

        while (true) {
            int comma = payload.indexOf(',', start);
            String token;

            if (comma < 0) {
                token = payload.substring(start);
            } else {
                token = payload.substring(start, comma);
            }
            token.trim();

            // parts:
            // 0 -> LTE
            // 1 -> Online
            // 2 -> 452-02
            // 3 -> 0x1817    (TAC/LAC hex)
            // 4 -> 156384564 (CID)
            if (partIndex == 2) {
                int dash = token.indexOf('-');
                if (dash < 0) return false;
                mcc = token.substring(0, dash).toInt();
                mnc = token.substring(dash + 1).toInt();
            }
            else if (partIndex == 3) {
                // e.g., 0x1817 → convert
                lac = strtol(token.c_str(), nullptr, 0);
            }
            else if (partIndex == 4) {
                cid = token.toInt();
                break; // done
            }

            if (comma < 0) break;
            start = comma + 1;
            partIndex++;
        }

        return (mcc > 0 && lac > 0 && cid > 0);
    }

    // ----------------------------------------------------------
    // Build LocationAPI-compatible JSON
    // ----------------------------------------------------------
    String buildLocationApiJson() const {
        String json = "{";

        json += "\"token\":\"pk.934c1ddee8ca7d8db926995b255c9f26\",";
        json += "\"radio\":\"lte\",";
        json += "\"mcc\":" + String(mcc) + ",";
        json += "\"mnc\":" + String(mnc) + ",";

        json += "\"cells\":[{";
        json += "\"lac\":" + String(lac) + ",";
        json += "\"cid\":" + String(cid) + ",";
        json += "\"psc\":0";
        json += "}],";

        json += "\"address\":1";
        json += "}";

        return json;
    }
};