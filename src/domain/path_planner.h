#pragma once
#ifdef ENABLE_MAP
#include <functional>
#include <vector>
#include "mower_map.h"
#include "domain.h"
#include <map>
#include <array>
#include <memory>
#include "pathplanner.h"

namespace ArduMower {
namespace Modem {
namespace PathPlanner {

using Point = ArduMower::Domain::Robot::MapPoint;
using Polygon = std::vector<Point>;

// Ein einzelner Befund der Routenprüfung. Die Zahlenwerte sind bewusst
// kompakt gehalten, weil die Liste als JSON zum Browser geht.
struct RouteFinding {
    uint8_t severity = 0;  // 0=info, 1=warning, 2=error
    uint8_t kind = 0;      // 0=tightCorner, 1=tangentOverlap, 2=degenerateSegment
    uint32_t index = 0;    // betroffener Wegpunkt
    uint32_t index2 = 0;   // zweiter Wegpunkt bei Paarbefunden, sonst == index
    float angleDeg = 0.0f;
    float shortfall = 0.0f;  // fehlende bzw. überlappende Meter
};

// Kennzahlen und Befunde einer berechneten Route. Rein informativ, die Route
// selbst wird davon nicht verändert.
struct RouteReport {
    bool valid = false;
    uint32_t pointCount = 0;
    float totalLength = 0.0f;       // m
    uint32_t rotationCount = 0;     // Drehungen auf der Stelle
    uint32_t trackedCorners = 0;    // vom Regler durchfahrene Ecken
    float estimatedSeconds = 0.0f;  // grobe Schätzung
    uint32_t findingsTotal = 0;     // kann größer als findings.size() sein
    float turnRadius = 0.0f;        // verwendeter Prüfradius
    // Wegpunkte je Kategorie
    uint32_t areaCount = 0;
    uint32_t borderCount = 0;
    uint32_t exclusionBorderCount = 0;
    uint32_t transitCount = 0;
    uint32_t connectorCount = 0;
    std::vector<RouteFinding> findings;
};

// Berechnet die Route. Ist report gesetzt, wird zusätzlich die Routenprüfung
// ausgeführt; mowSpeed geht nur in die Zeitschätzung ein. progress erhält den
// Fortschritt der Planung in Prozent (nur bei Änderung).
Polygon calculateWaypoints(ArduMower::Domain::Robot::MowerMap &map,
    ArduMower::Domain::Robot::MowSettings &settings,
    const ArduMower::Domain::Robot::State::State *state = nullptr,
    RouteReport *report = nullptr,
    float mowSpeed = 0.3f,
    const std::function<void(int percent)> &progress = {});

// Zwischenspeicher der Verbindungswege einer Karte: (Start, Ziel) -> Weg um die
// Exclusions herum. Der ESP32-S3 rechnet double nur in Software; ein Weg kostet
// dort Zehntelsekunden, alle Lücken einer Karte zusammen Sekunden. Leeren,
// sobald sich Perimeter, Exclusions oder Wegpunkte ändern.
class TransitCache {
public:
    void clear() { paths_.clear(); router_.reset(); }
    size_t size() const { return paths_.size(); }
    const std::vector<PathPlannerCore::Point> *find(const Point &a, const Point &b) const;
    void put(const Point &a, const Point &b, std::vector<PathPlannerCore::Point> path);
    // Wegfinder für Perimeter und Exclusions der Karte, einmal aufgebaut
    PathPlannerCore::FreeSpaceRouter &router(const ArduMower::Domain::Robot::MowerMap &map);
private:
    using Key = std::array<int32_t, 4>;
    static Key key(const Point &a, const Point &b);
    std::map<Key, std::vector<PathPlannerCore::Point>> paths_;
    std::unique_ptr<PathPlannerCore::FreeSpaceRouter> router_;
};

// Lücken (Ende eines Abschnitts, Anfang des nächsten), die das Filtern mit
// diesen Schaltern überbrücken muss, ohne die Wege zu berechnen.
std::vector<std::pair<Point, Point>> routeGaps(const Polygon &route,
    const ArduMower::Domain::Robot::MowSettings &settings);

// Rechnet höchstens einen fehlenden Verbindungsweg voraus, für eine der acht
// Kombinationen der Mow-areas-Schalter (ab combo). false: alle sind im Cache.
bool prefillTransitCache(TransitCache &cache, const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &base, int &combo);

// Filtert eine bereits berechnete Route anhand der Runtime-Toggles.
// transits (optional) erhält die eingefügten Verbindungswege über
// ausgeblendete Abschnitte, jeweils vom Ende eines Abschnitts bis zum Anfang
// des nächsten, damit die Karten sie anzeigen können.
Polygon filterRouteByToggles(const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &settings,
    std::vector<Polygon> *transits = nullptr, TransitCache *cache = nullptr);

} // namespace PathPlanner
} // namespace Modem
} // namespace ArduMower
#endif
