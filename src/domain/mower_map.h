#pragma once
#include <vector>
#include <string>
#include <map>


#include <ArduinoJson.h>
#include <string>
#include <vector>
#include "log.h"



namespace ArduMower {
    namespace Domain {
        namespace Robot {
            enum class MapPointTag : uint8_t {
                NONE = 0,
                AREA = 1,
                BORDER = 3,
                EXCLUSION_BORDER = 4,
                CONNECTOR = 6,
                TRANSIT = 7,
            };

            inline const char* mapPointTagName(MapPointTag tag) {
                switch (tag) {
                    case MapPointTag::NONE: return "none";
                    case MapPointTag::AREA: return "area";
                    case MapPointTag::BORDER: return "border";
                    case MapPointTag::EXCLUSION_BORDER: return "exclusion_border";
                    case MapPointTag::CONNECTOR: return "connector";
                    case MapPointTag::TRANSIT: return "transit";
                }
                return "unknown";
            }

            struct MapPoint {
                double X;
                double Y;
                double delta = 0.0;
                String timestamp = "";
                int sol = 0;
                // true: Punkt ist eine automatisch berechnete Verbindung zwischen
                // aktiven Wegpunkt-Segmenten und wird bei jeder Änderung der
                // "Mow areas"-Schalter entfernt und neu berechnet.
                bool isConnector = false;
                // Interner Kategorie-Tag (uint8_t, 0-255). Wird nur an die UI
                // übertragen und dort für die korrekte Ein-/Ausblendung der
                // Mow-Area-Schalter verwendet. Kein Teil der Persistenz.
                uint8_t tag = 0;

                void marshal(JsonObject obj) const {
                    obj["X"] = X;
                    obj["Y"] = Y;
                }

                void marshalFull(JsonObject obj) const {
                    obj["X"] = X;
                    obj["Y"] = Y;
                    if (timestamp.length() > 0) obj["timestamp"] = timestamp;
                    if (delta != 0.0) obj["delta"] = delta;
                    if (sol != 0) obj["sol"] = sol;
                    if (isConnector) obj["conn"] = 1;
                    if (tag != 0) obj["tag"] = tag;
                }
            };

            inline MapPoint unmarshalMapPoint(JsonObject obj) {
                MapPoint p{0, 0};
                if (obj["X"].is<JsonVariant>()) p.X = obj["X"];
                else if (obj["x"].is<JsonVariant>()) p.X = obj["x"];
                if (obj["Y"].is<JsonVariant>()) p.Y = obj["Y"];
                else if (obj["y"].is<JsonVariant>()) p.Y = obj["y"];
                if (obj["delta"].is<JsonVariant>()) p.delta = obj["delta"];
                if (obj["timestamp"].is<JsonVariant>()) p.timestamp = obj["timestamp"].as<String>();
                if (obj["sol"].is<JsonVariant>()) p.sol = obj["sol"];
                if (obj["conn"].is<JsonVariant>()) p.isConnector = obj["conn"].as<int>() != 0;
                if (obj["tag"].is<JsonVariant>()) p.tag = obj["tag"].as<uint8_t>();
                return p;
            }


            struct MowerMap {
                uint32_t timestamp;
                String dateTime = "";
                String source = "";

                std::vector<MapPoint> perimeter;
                std::vector<std::vector<MapPoint>> exclusions;
                std::vector<MapPoint> waypoints;
                std::vector<MapPoint> dockpoints;
                // Separate planning line used by the modem path planner. It is
                // never sent to Sunray because the AT+W protocol has no type
                // for a Search-Wire geometry.
                std::vector<MapPoint> searchWire;

                // Kartenausrichtung in Grad (0 = Norden/oben), nicht Teil des Geometrie-Hashes
                double rotation = 0.0;

                // Mäh-Einstellungen, die dieser Karte zugeordnet sind.
                // Werden beim Import/Export und Speichern/Laden persistiert.
                int pattern = 0;          // 0=Lines, 1=Squares, 2=Rings
                float mowOfs = 0.3f;      // Spurbreite in m
                int patternAngle = 0;     // Musterwinkel in Grad
                float distanceToBorder = 0.0f;
                int borderLaps = 0;
                bool mowBorderCcw = false;
                // Runtime-Flags (können unterwegs ein-/ausgeschaltet werden)
                bool doMowArea = true;
                bool doMowExclusions = true; // true: Aussparungen umfahren (immer geometrisch); false: Nur Perimeter
                bool doMowPerimeter = true;  // true: Perimeter mitfahren
                bool doMowBorder = false;      // true: Randstreifen maehen (distanceToBorder+borderLaps)
                bool doMowExclusionBorder = false; // true: Randstreifen um Aussparungen

                // Flag, ob aktuell ein Lesevorgang läuft (z.B. für Map-Transfer)
                bool reading = false;

                // Setzt das reading-Flag (z.B. vor Map-Transfer)
                void beginRead() { reading = true; }
                // Hebt das reading-Flag wieder auf
                void endRead() { reading = false; }
                // Prüft, ob aktuell gelesen wird
                bool isReading() const { return reading; }

                // Kanonische Serialisierung nur der Geometrie (für Hash/Vergleich)
                void marshalGeometry(JsonObject obj) const {
                    JsonArray perim = obj["perimeter"].to<JsonArray>();
                    for (const auto& p : perimeter) p.marshal(perim.add<JsonObject>());
                    JsonArray excls = obj["exclusions"].to<JsonArray>();
                    for (const auto& ex : exclusions) {
                        JsonArray exArr = excls.add<JsonArray>();
                        for (const auto& p : ex) p.marshal(exArr.add<JsonObject>());
                    }
                    JsonArray docks = obj["dockpoints"].to<JsonArray>();
                    for (const auto& p : dockpoints) p.marshal(docks.add<JsonObject>());
                    JsonArray wire = obj["searchWire"].to<JsonArray>();
                    for (const auto& p : searchWire) p.marshal(wire.add<JsonObject>());
                    JsonArray wps = obj["waypoints"].to<JsonArray>();
                    for (const auto& p : waypoints) p.marshal(wps.add<JsonObject>());
                }

                void marshal(JsonObject obj) const {
                    marshalGeometry(obj);
                    Log(DBG, "marshal map perimeter %d, exclusions %d, waypoints %d, dockpoints %d", perimeter.size(), exclusions.size(), waypoints.size(), dockpoints.size());
                }

                void unmarshalGeometry(JsonObject obj) {
                    perimeter.clear();
                    JsonArray perim = obj["perimeter"];
                    if (perim) {
                        for (JsonObject p : perim) {
                            perimeter.push_back(unmarshalMapPoint(p));
                        }
                    }
                    exclusions.clear();
                    JsonArray excls = obj["exclusions"];
                    if (excls) {
                        for (JsonArray ex : excls) {
                            std::vector<MapPoint> e;
                            for (JsonObject p : ex) {
                                e.push_back(unmarshalMapPoint(p));
                            }
                            exclusions.push_back(e);
                        }
                    }
                    dockpoints.clear();
                    JsonArray docks = obj["dockpoints"];
                    if (docks) {
                        for (JsonObject p : docks) {
                            dockpoints.push_back(unmarshalMapPoint(p));
                        }
                    }
                    searchWire.clear();
                    JsonArray wire = obj["searchWire"];
                    if (wire) {
                        for (JsonObject p : wire) {
                            searchWire.push_back(unmarshalMapPoint(p));
                        }
                    }
                    waypoints.clear();
                    JsonArray wps = obj["waypoints"];
                    if (wps) {
                        for (JsonObject p : wps) {
                            waypoints.push_back(unmarshalMapPoint(p));
                        }
                    }
                }

                void unmarshal(JsonObject obj) {
                    unmarshalGeometry(obj["map"]);
                }

                // Import/Export a Mower-compatible map JSON.
                // Mower format uses lower-case {x,y} arrays, a top-level
                // "perimeter" ring, "exclusions" array of rings, optional
                // "mowPoints"/"dockPoints"/"wayPoints" and meta fields.
                bool fromJson(JsonObject obj) {
                    perimeter.clear();
                    exclusions.clear();
                    dockpoints.clear();
                    waypoints.clear();

                    JsonArray perim = obj["perimeter"];
                    if (!perim) perim = obj["perimeter"];
                    if (perim) {
                        for (JsonObject p : perim) {
                            perimeter.push_back(unmarshalMapPoint(p));
                        }
                    }
                    if (perimeter.size() < 3) return false;
                    // Ensure closed ring
                    if (perimeter.size() > 0 &&
                        (perimeter.front().X != perimeter.back().X ||
                         perimeter.front().Y != perimeter.back().Y)) {
                        perimeter.push_back(perimeter.front());
                    }

                    JsonArray excls = obj["exclusions"];
                    if (excls) {
                        for (JsonArray ex : excls) {
                            std::vector<MapPoint> e;
                            for (JsonObject p : ex) {
                                e.push_back(unmarshalMapPoint(p));
                            }
                            if (e.size() >= 3) {
                                if (e.front().X != e.back().X || e.front().Y != e.back().Y) {
                                    e.push_back(e.front());
                                }
                                exclusions.push_back(e);
                            }
                        }
                    }

                    JsonArray docks = obj["dockPoints"];
                    if (!docks) docks = obj["dockpoints"];
                    if (docks) {
                        for (JsonObject p : docks) {
                            dockpoints.push_back(unmarshalMapPoint(p));
                        }
                    }

                    JsonArray wps = obj["wayPoints"];
                    if (!wps) wps = obj["waypoints"];
                    if (wps) {
                        for (JsonObject p : wps) {
                            waypoints.push_back(unmarshalMapPoint(p));
                        }
                    }

                    if (obj["rotation"].is<JsonVariant>()) {
                        rotation = obj["rotation"];
                    }
                    if (obj["dateTime"].is<JsonVariant>()) {
                        dateTime = obj["dateTime"].as<String>();
                    }
                    if (obj["source"].is<JsonVariant>()) {
                        source = obj["source"].as<String>();
                    }
                    // Mäh-Einstellungen aus Webapp-Export/Import
                    if (obj["patternAngle"].is<JsonVariant>()) patternAngle = obj["patternAngle"];
                    if (obj["mowOfs"].is<JsonVariant>()) mowOfs = obj["mowOfs"];
                    if (obj["patternRings"].is<JsonVariant>()) pattern = obj["patternRings"] ? 2 : 0;
                    // Legacy Webapp verwendet doMowArea/doPerimeterBorder/doExclusionsBorder
                    // als Berechnungs-Flags. Wir importieren sie als Runtime-Flags,
                    // damit alte Karten weiter funktionieren.
                    if (obj["doMowArea"].is<JsonVariant>()) {
                        bool v = obj["doMowArea"];
                        doMowArea = v;
                    }
                    if (obj["doMowExclusions"].is<JsonVariant>()) doMowExclusions = obj["doMowExclusions"];
                    if (obj["doMowPerimeter"].is<JsonVariant>()) doMowPerimeter = obj["doMowPerimeter"];
                    if (obj["doPerimeterBorder"].is<JsonVariant>()) {
                        bool v = obj["doPerimeterBorder"];
                        doMowBorder = v;
                        if (v && distanceToBorder <= 0.0f) distanceToBorder = 0.3f;
                        if (v && borderLaps <= 0) borderLaps = 1;
                    }
                    if (obj["doExclusionsBorder"].is<JsonVariant>()) {
                        bool v = obj["doExclusionsBorder"];
                        doMowExclusionBorder = v;
                    }
                    return true;
                }

                void toJson(JsonObject obj) const {
                    auto writePoint = [](JsonObject o, const MapPoint &p, bool includeTag = false) {
                        o["X"] = p.X;
                        o["Y"] = p.Y;
                        if (p.timestamp.length() > 0) o["timestamp"] = p.timestamp;
                        if (p.delta != 0.0) o["delta"] = p.delta;
                        if (p.sol != 0) o["sol"] = p.sol;
                        if (includeTag && p.tag != 0) o["tag"] = p.tag;
                    };
                    auto writeRing = [&](JsonArray arr, const std::vector<MapPoint> &ring) {
                        for (const auto &p : ring) writePoint(arr.add<JsonObject>(), p);
                    };
                    auto writeWaypoints = [&](JsonArray arr, const std::vector<MapPoint> &ring) {
                        for (const auto &p : ring) {
                            JsonObject point = arr.add<JsonObject>();
                            writePoint(point, p, true);
                            if (p.isConnector) point["conn"] = 1;
                        }
                    };

                    if (dateTime.length() > 0) obj["dateTime"] = dateTime;
                    if (source.length() > 0) obj["source"] = source;
                    obj["rotation"] = rotation;

                    obj["patternAngle"] = patternAngle;
                    obj["mowOfs"] = mowOfs;
                    obj["patternRings"] = (pattern == 2);
                    // Runtime-Flags (können unterwegs umgeschaltet werden)
                    obj["doMowExclusions"] = doMowExclusions;
                    obj["doMowPerimeter"] = doMowPerimeter;
                    obj["doMowArea"] = doMowArea;
                    obj["doPerimeterBorder"] = doMowBorder;
                    obj["doExclusionsBorder"] = doMowExclusionBorder;

                    JsonArray perim = obj["perimeter"].to<JsonArray>();
                    writeRing(perim, perimeter);

                    JsonArray excls = obj["exclusions"].to<JsonArray>();
                    for (const auto &ex : exclusions) {
                        JsonArray exArr = excls.add<JsonArray>();
                        writeRing(exArr, ex);
                    }

                    JsonArray docks = obj["dockPoints"].to<JsonArray>();
                    writeRing(docks, dockpoints);

                    JsonArray wire = obj["searchWire"].to<JsonArray>();
                    writeRing(wire, searchWire);

                    JsonArray wps = obj["wayPoints"].to<JsonArray>();
                    writeWaypoints(wps, waypoints);
                }

    // Sunray-kompatibler CRC: px = x*100 (cm), crc = Σ(trunc16(px) + trunc16(py))
    int computeMapCrc() const {
        return computeMapCrcDetail(nullptr, nullptr, nullptr, nullptr);
    }

    struct CrcDetail {
        int perimeter = 0;
        int exclusions = 0;
        int dockpoints = 0;
        int waypoints = 0;
    };

    int computeMapCrcDetail(int *outPerimeter, int *outExclusions, int *outDockpoints, int *outWaypoints) const {
        int crc = 0;
        int p = 0, e = 0, d = 0, w = 0;
        // Sunray-Konvertierung exakt replizieren: snprintf("%.2f") → strtof → *100 → short
        auto cm = [](double val) -> int16_t {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", val);
            float f = strtof(buf, nullptr);
            return static_cast<int16_t>(f * 100.0f);
        };
        for (const auto &pt : perimeter) {
            int v = cm(pt.X) + cm(pt.Y);
            p += v; crc += v;
        }
        for (const auto &ex : exclusions) {
            for (const auto &pt : ex) {
                int v = cm(pt.X) + cm(pt.Y);
                e += v; crc += v;
            }
        }
        for (const auto &pt : dockpoints) {
            int v = cm(pt.X) + cm(pt.Y);
            d += v; crc += v;
        }
        for (const auto &pt : waypoints) {
            int v = cm(pt.X) + cm(pt.Y);
            w += v; crc += v;
        }
        if (outPerimeter)   *outPerimeter = p;
        if (outExclusions)  *outExclusions = e;
        if (outDockpoints)  *outDockpoints = d;
        if (outWaypoints)   *outWaypoints = w;
        return crc;
    }
            };

        } // namespace Robot
    } // namespace Domain
} // namespace ArduMower
