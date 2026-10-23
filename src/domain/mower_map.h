#pragma once
#include <vector>
#include <string>
#include <map>
#include <cmath>


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
                // Subkarte (Bereich): Auswahlfläche, leer = ganze Karte.
                // Begrenzt nur durch Perimeter und Exclusions, auch bei
                // Unterbereichen (nicht durch die Auswahl des Elternbereichs).
                // Hauptkarten haben keine.
                std::vector<MapPoint> selection;
                // planKey() zum Zeitpunkt der Routenberechnung, 0 = unbekannt.
                // Weicht er vom aktuellen ab, ist die Route eines Bereichs veraltet.
                uint32_t routeKey = 0;

                // Kartenausrichtung in Grad (0 = Norden/oben), nicht Teil des Geometrie-Hashes
                double rotation = 0.0;

                // Mäh-Einstellungen, die dieser Karte zugeordnet sind.
                // Werden beim Import/Export und Speichern/Laden persistiert.
                int pattern = 0;          // 0=Lines, 1=Squares, 2=Rings
                float mowOfs = 0.3f;      // Spurbreite in m
                int patternAngle = 0;     // Musterwinkel in Grad
                // 0 fest, 1 zufällig, 2 dynamisch (siehe MowSettings::angleMode).
                // Nicht fest: Die Wegpunkte werden nicht gespeichert, die Route
                // entsteht für jeden Auftrag neu.
                int angleMode = 0;
                int angleStep = 30;
                // Nur im RAM: Winkel, mit dem die Wegpunkte bei zufälligem/
                // dynamischem Winkel berechnet wurden, sonst -1
                int16_t routeAngle = -1;
                float distanceToBorder = 0.0f;
                int borderLaps = 0;
                // Abstand der Mähfläche zu den Exclusions (Spurbreiten) und
                // Runden um die Exclusions; getrennt von distanceToBorder/borderLaps
                float distanceToExclusions = 0.0f;
                int exclusionLaps = 0;
                bool mowBorderCcw = false;
                // Reihenfolge der Abschnitte ("area", "border", "exclusions",
                // kommagetrennt); leer: wie früher aus mowBorderCcw
                String routeOrder = "";
                bool exclusionLapsWithArea = false;
                // Teilbereich: Randrunden auch entlang der Auswahlkante.
                bool selectionEdgeLaps = false;
                // Runtime-Flags (können unterwegs ein-/ausgeschaltet werden)
                bool doMowArea = true;
                bool doMowExclusions = true; // true: Aussparungen umfahren (immer geometrisch); false: Nur Perimeter
                bool doMowPerimeter = true;  // true: Perimeter mitfahren
                bool doMowBorder = false;      // true: Randstreifen maehen (distanceToBorder+borderLaps)
                bool doMowExclusionBorder = false; // true: Randstreifen um Aussparungen
                // Schwelle der Routenvereinfachung in m; verändert die Route.
                float simplifyEpsilon = 0.02f;
                // Mindestwendekreis in m; nur für den Prüfbericht, verändert die Route nicht.
                float checkTurnRadius = 0.30f;

                // Anzahl laufender Lesevorgänge (Map-Transfer zum Browser, Upload
                // zum Mäher). Ein Zähler statt eines Flags: Laufen beide, gab
                // früher der zuerst fertige die Sperre auch für den anderen frei.
                // Kopieren ergibt eine ungesperrte Karte (Snapshot), Zuweisen
                // behält die Sperre des Ziels.
                struct ReadLock {
                    uint8_t count = 0;
                    ReadLock() = default;
                    ReadLock(const ReadLock &) : count(0) {}
                    ReadLock &operator=(const ReadLock &) { return *this; }
                };
                ReadLock readLock;

                void beginRead() { if (readLock.count < 255) readLock.count++; }
                void endRead() { if (readLock.count > 0) readLock.count--; }
                bool isReading() const { return readLock.count > 0; }

                // Mäh-Einstellungen von o übernehmen. Subkarten erben die
                // Geometrie, haben aber eigene Einstellungen.
                void copySettingsFrom(const MowerMap &o) {
                    pattern = o.pattern;
                    mowOfs = o.mowOfs;
                    patternAngle = o.patternAngle;
                    angleMode = o.angleMode;
                    angleStep = o.angleStep;
                    distanceToBorder = o.distanceToBorder;
                    borderLaps = o.borderLaps;
                    distanceToExclusions = o.distanceToExclusions;
                    exclusionLaps = o.exclusionLaps;
                    mowBorderCcw = o.mowBorderCcw;
                    routeOrder = o.routeOrder;
                    exclusionLapsWithArea = o.exclusionLapsWithArea;
                    selectionEdgeLaps = o.selectionEdgeLaps;
                    doMowArea = o.doMowArea;
                    doMowExclusions = o.doMowExclusions;
                    doMowPerimeter = o.doMowPerimeter;
                    doMowBorder = o.doMowBorder;
                    doMowExclusionBorder = o.doMowExclusionBorder;
                    simplifyEpsilon = o.simplifyEpsilon;
                    checkTurnRadius = o.checkTurnRadius;
                }

                // Perimeter, Exclusions und Dockpunkte kompakt als [[x,y],...]
                // (Anzeige von Aufträgen und Vorschauen im Browser)
                void marshalOutline(JsonObject o) const {
                    auto ring = [](JsonArray arr, const std::vector<MapPoint> &pts) {
                        for (const auto &p : pts) {
                            JsonArray xy = arr.add<JsonArray>();
                            xy.add(roundf((float)p.X * 100.0f) / 100.0f);
                            xy.add(roundf((float)p.Y * 100.0f) / 100.0f);
                        }
                    };
                    ring(o["perimeter"].to<JsonArray>(), perimeter);
                    JsonArray ex = o["exclusions"].to<JsonArray>();
                    for (const auto &e : exclusions) ring(ex.add<JsonArray>(), e);
                    ring(o["dockpoints"].to<JsonArray>(), dockpoints);
                    o["rotation"] = rotation;
                }

                // Schlüssel für die Kennung "veraltet": Punkte auf cm gerundet,
                // damit Speichern und Laden ihn nicht verändern.
                static uint32_t keyMix(uint32_t k, uint32_t v) {
                    for (int i = 0; i < 4; i++) { k ^= (v >> (i * 8)) & 0xff; k *= 16777619u; }
                    return k;
                }
                static uint32_t pointsKey(uint32_t k, const std::vector<MapPoint> &pts) {
                    k = keyMix(k, (uint32_t)pts.size());
                    for (const auto &p : pts) {
                        k = keyMix(k, (uint32_t)(int32_t)lround(p.X * 100.0));
                        k = keyMix(k, (uint32_t)(int32_t)lround(p.Y * 100.0));
                    }
                    return k;
                }
                // Perimeter, Exclusions und Dockpunkte der Hauptkarte
                uint32_t geometryKey() const {
                    uint32_t k = pointsKey(2166136261u, perimeter);
                    k = keyMix(k, (uint32_t)exclusions.size());
                    for (const auto &ex : exclusions) k = pointsKey(k, ex);
                    k = pointsKey(k, dockpoints);
                    return k ? k : 1;
                }
                // Schlüssel einer Auswahl, 0 = keine (ganze Elternfläche)
                static uint32_t selectionKey(const std::vector<MapPoint> &sel) {
                    if (sel.size() < 3) return 0;
                    const uint32_t k = pointsKey(2166136261u, sel);
                    return k ? k : 1;
                }
                // Eine Auswahl in den Schlüssel einrechnen; leere Auswahl ändert nichts
                static uint32_t chainKey(uint32_t k, uint32_t selKey) {
                    if (!selKey) return k;
                    k = keyMix(k, selKey);
                    return k ? k : 1;
                }
                // Alles, wovon die Route eines Bereichs geometrisch abhängt
                uint32_t planKey() const {
                    uint32_t k = geometryKey();
                    return chainKey(k, selectionKey(selection));
                }

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
                // requirePerimeter = false: Datei einer Subkarte, die keine
                // eigene Geometrie enthält (nur Auswahl, Einstellungen, Route).
                bool fromJson(JsonObject obj, bool requirePerimeter = true) {
                    perimeter.clear();
                    exclusions.clear();
                    dockpoints.clear();
                    searchWire.clear();
                    selection.clear();
                    waypoints.clear();

                    JsonArray perim = obj["perimeter"];
                    if (!perim) perim = obj["perimeter"];
                    if (perim) {
                        for (JsonObject p : perim) {
                            perimeter.push_back(unmarshalMapPoint(p));
                        }
                    }
                    if (requirePerimeter && perimeter.size() < 3) return false;
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

                    JsonArray wire = obj["searchWire"];
                    if (wire) {
                        for (JsonObject p : wire) {
                            searchWire.push_back(unmarshalMapPoint(p));
                        }
                    }

                    JsonArray sel = obj["selection"];
                    if (sel) {
                        for (JsonObject p : sel) selection.push_back(unmarshalMapPoint(p));
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
                    angleMode = obj["angleMode"] | 0;
                    angleStep = obj["angleStep"] | 30;
                    if (obj["mowOfs"].is<JsonVariant>()) mowOfs = obj["mowOfs"];
                    if (obj["patternRings"].is<JsonVariant>()) pattern = obj["patternRings"] ? 2 : 0;
                    if (pattern == 2) angleMode = 0;  // Ringe haben keinen Winkel
                    // Randstreifen-Einstellungen. Sie wurden früher gar nicht
                    // serialisiert, wodurch sie bei jedem Speichern/Laden
                    // verloren gingen und der Legacy-Fallback unten sie
                    // überschrieb.
                    const bool hasDistanceToBorder = obj["distanceToBorder"].is<JsonVariant>();
                    const bool hasBorderLaps = obj["borderLaps"].is<JsonVariant>();
                    if (hasDistanceToBorder) distanceToBorder = obj["distanceToBorder"];
                    if (hasBorderLaps) borderLaps = obj["borderLaps"];
                    if (obj["mowBorderCcw"].is<JsonVariant>()) mowBorderCcw = obj["mowBorderCcw"];
                    routeOrder = obj["routeOrder"].is<const char *>() ? obj["routeOrder"].as<String>() : String("");
                    exclusionLapsWithArea = obj["exclusionLapsWithArea"] | false;
                    selectionEdgeLaps = obj["selectionEdgeLaps"] | false;
                    routeKey = obj["routeKey"] | 0u;
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
                        // Nur für alte Karten, die den Schalter ohne die
                        // zugehörigen Werte mitbringen. Der Fallback darf sich
                        // nicht am Wert orientieren, sonst überschreibt er ein
                        // bewusst gesetztes 0. distanceToBorder zählt
                        // Spurbreiten, daher ist 1 der kleinste sinnvolle Wert;
                        // das frühere 0.3 stammt aus der Zeit, als das Feld
                        // Meter bedeutete, und wurde im Planer zu 0 gekürzt.
                        if (v && !hasDistanceToBorder) distanceToBorder = 1.0f;
                        if (v && !hasBorderLaps) borderLaps = 1;
                    }
                    if (obj["doExclusionsBorder"].is<JsonVariant>()) {
                        bool v = obj["doExclusionsBorder"];
                        doMowExclusionBorder = v;
                    }
                    if (obj["distanceToExclusions"].is<JsonVariant>()) distanceToExclusions = obj["distanceToExclusions"];
                    // Ältere Karten kennen keine eigenen Exclusion-Runden: Dort
                    // lief um jede Exclusion so viele Runden wie um den Perimeter.
                    if (obj["exclusionLaps"].is<JsonVariant>()) exclusionLaps = obj["exclusionLaps"];
                    else exclusionLaps = borderLaps;
                    if (obj["simplifyEpsilon"].is<JsonVariant>()) simplifyEpsilon = obj["simplifyEpsilon"];
                    if (obj["checkTurnRadius"].is<JsonVariant>()) checkTurnRadius = obj["checkTurnRadius"];
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
                    if (angleMode != 0) obj["angleMode"] = angleMode;
                    if (angleStep != 30) obj["angleStep"] = angleStep;
                    obj["mowOfs"] = mowOfs;
                    obj["patternRings"] = (pattern == 2);
                    obj["distanceToBorder"] = distanceToBorder;
                    obj["borderLaps"] = borderLaps;
                    obj["distanceToExclusions"] = distanceToExclusions;
                    obj["exclusionLaps"] = exclusionLaps;
                    obj["mowBorderCcw"] = mowBorderCcw;
                    if (routeOrder.length() > 0) obj["routeOrder"] = routeOrder;
                    if (exclusionLapsWithArea) obj["exclusionLapsWithArea"] = true;
                    if (selectionEdgeLaps) obj["selectionEdgeLaps"] = true;
                    if (routeKey && angleMode == 0) obj["routeKey"] = routeKey;
                    // Runtime-Flags (können unterwegs umgeschaltet werden)
                    obj["doMowExclusions"] = doMowExclusions;
                    obj["doMowPerimeter"] = doMowPerimeter;
                    obj["doMowArea"] = doMowArea;
                    obj["doPerimeterBorder"] = doMowBorder;
                    obj["doExclusionsBorder"] = doMowExclusionBorder;
                    obj["simplifyEpsilon"] = simplifyEpsilon;
                    obj["checkTurnRadius"] = checkTurnRadius;

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

                    if (!selection.empty()) {
                        JsonArray sel = obj["selection"].to<JsonArray>();
                        writeRing(sel, selection);
                    }

                    JsonArray wps = obj["wayPoints"].to<JsonArray>();
                    // Zufälliger/dynamischer Winkel: Die Route gilt nur für
                    // einen Auftrag und wird nicht gespeichert
                    if (angleMode == 0) writeWaypoints(wps, waypoints);
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

    // Sunray-Konvertierung exakt replizieren: snprintf("%.2f") → strtof → *100 → short
    static int16_t crcCm(double val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", val);
        float f = strtof(buf, nullptr);
        return static_cast<int16_t>(f * 100.0f);
    }

    // CRC-Anteil einer Punktliste
    static int pointsCrc(const std::vector<MapPoint> &points) {
        int crc = 0;
        for (const auto &pt : points) crc += crcCm(pt.X) + crcCm(pt.Y);
        return crc;
    }

    int computeMapCrcDetail(int *outPerimeter, int *outExclusions, int *outDockpoints, int *outWaypoints) const {
        int crc = 0;
        int p = 0, e = 0, d = 0, w = 0;
        auto cm = crcCm;
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
