#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <functional>
#include <vector>
#include "mower_map.h"

namespace ArduMower {
    namespace Modem {
        namespace PathPlanner { class TransitCache; }

        struct MapMeta {
            String id;            // Stabiler Slot-Identifier (bei alten Karten = Hash, bei neuen eindeutig)
            String name;          // Anzeigename
            double area = 0.0;    // Fläche in m²
            String hash;          // Hash der aktuellen Kartengeometrie
            int crc = 0;          // Sunray-kompatibler CRC (Feld 16), aus computeMapCrc()
            double rotation = 0.0; // Kartenausrichtung in Grad
            uint32_t timestamp = 0;
            String file;          // SPIFFS-Dateiname, z.B. /maps/map_0.json
            // Subkarte (Bereich): ID der übergeordneten Karte; leer bei Hauptkarten.
            // Ihre Datei enthält nur Auswahl, Mäh-Einstellungen und Route, die
            // Geometrie erbt sie von der Hauptkarte.
            String parentId;
            // Kennung "veraltet": Hauptkarte geomKey = geometryKey(), Subkarte
            // selKey = Schlüssel der eigenen Auswahl, routeKey = planKey() bei
            // der Berechnung ihrer Route (siehe MowerMap::planKey()).
            uint32_t geomKey = 0;
            uint32_t selKey = 0;
            uint32_t routeKey = 0;
            // Winkel zufällig/dynamisch (MowerMap::angleMode != 0): keine
            // gespeicherte Route, sie entsteht je Auftrag. lastAngle ist der
            // zuletzt zum Mäher geschickte Winkel, -1 = noch keiner.
            uint8_t angleMode = 0;
            int16_t lastAngle = -1;

            void marshal(JsonObject obj) const;
            void unmarshal(JsonObject obj);
        };

        class MapManager {
        public:
            bool begin();

            // Speichert die übergebene Karte unter dem angegebenen Namen.
            // Ist currentId nicht leer, wird die Karte mit dieser ID überschrieben (Update).
            // Ansonsten wird eine neue Karte erstellt (oder eine Karte mit gleichem Hash aktualisiert).
            // Rückgabe: ID oder leerer String bei Fehler.
            // progress meldet 0..100 %; den Großteil der Zeit kostet das Schreiben.
            // parentId: neue Subkarte unter dieser Karte anlegen. Eine bestehende
            // Subkarte (currentId) bleibt Subkarte. Bei Subkarten wird nur
            // ihr eigener Teil geschrieben (Auswahl, Einstellungen, Route).
            String save(const ArduMower::Domain::Robot::MowerMap &map, const String &name, const String &currentId = "", double rotation = 0.0,
                        const std::function<void(int percent)> &progress = {}, const String &parentId = "");

            // Bei Subkarten: die wirksame Karte, also die Geometrie der
            // Hauptkarte plus Auswahl, Einstellungen und Route der Subkarte.
            bool load(const String &id, ArduMower::Domain::Robot::MowerMap &out);
            bool loadActive(ArduMower::Domain::Robot::MowerMap &out);

            bool rename(const String &id, const String &name);
            // Löscht auch alle Unterbereiche der Karte.
            bool remove(const String &id);

            bool isArea(const String &id) const;
            // Wie MowerMap::planKey() der geladenen Karte, nur aus dem Index;
            // 0, wenn die Hauptkarte noch keinen Geometrie-Schlüssel hat.
            uint32_t planKey(const String &id) const;
            // Subkarte, deren Route nicht zur aktuellen Geometrie/Auswahl passt
            bool isOutdated(const String &id) const;
            // Route je Auftrag neu (zufälliger/dynamischer Winkel)
            bool hasDynamicAngle(const String &id) const;
            // Zuletzt zum Mäher geschickter Winkel, -1 = keiner
            int lastAngle(const String &id) const;
            bool setLastAngle(const String &id, int angle);
            String parentOf(const String &id) const;
            // Alle Unterbereiche (auch Sub-Sub-Karten) von id
            std::vector<String> descendants(const String &id) const;
            // Eigener Teil einer Subkarte (Auswahl, Einstellungen) ohne Route
            bool readAreaOwn(const String &id, JsonObject out);

            const std::vector<MapMeta>& list() const { return _index.maps; }

            String findByHash(const String &hash) const;
            bool setActive(const String &id);
            String activeId() const { return _index.activeId; }

            static String computeHash(const ArduMower::Domain::Robot::MowerMap &map);
            // CRC der Karte, wie sie zum Mäher geht: Die Wegpunkte werden mit den
            // "Mow areas"-Schaltern der Karte gefiltert. uploadPoints erhält die
            // Anzahl der übertragenen Punkte.
            // transits (optional): Verbindungswege über ausgeblendete Abschnitte
            // cache (optional): bereits berechnete Verbindungswege dieser Karte
            static int computeCrc(const ArduMower::Domain::Robot::MowerMap &map, int *uploadPoints = nullptr,
                                  std::vector<std::vector<ArduMower::Domain::Robot::MapPoint>> *transits = nullptr,
                                  PathPlanner::TransitCache *cache = nullptr);
            static double computeArea(const ArduMower::Domain::Robot::MowerMap &map);

            // Gibt den in SPIFFS gespeicherten CRC zurück (0 wenn nicht gefunden)
            int getCrc(const String &id) const;
            // Korrigiert den gespeicherten CRC (z. B. von Karten, deren CRC noch
            // ohne die "Mow areas"-Schalter berechnet wurde)
            bool updateCrc(const String &id, int crc);
            static bool isMapValid(const ArduMower::Domain::Robot::MowerMap &map);

            String generateDefaultName() const;

        private:
            struct Index {
                String activeId;
                uint32_t nextFileId = 0;
                std::vector<MapMeta> maps;
            };

            Index _index;
            bool _initialized = false;

            static constexpr const char *MAPS_DIR = "/maps";
            static constexpr const char *INDEX_FILE = "/maps/index.json";

            bool loadIndex();
            bool loadChain(const String &id, ArduMower::Domain::Robot::MowerMap &out, int depth);
            bool saveIndex();
            String allocateFileName();
            MapMeta* findMeta(const String &id);
            const MapMeta* findMeta(const String &id) const;
            static double polygonArea(const std::vector<ArduMower::Domain::Robot::MapPoint> &poly);
        };

    } // namespace Modem
} // namespace ArduMower
