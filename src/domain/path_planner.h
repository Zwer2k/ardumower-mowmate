#pragma once
#ifdef ENABLE_MAP
#include <vector>
#include "mower_map.h"
#include "domain.h"

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
// ausgeführt; mowSpeed geht nur in die Zeitschätzung ein.
Polygon calculateWaypoints(ArduMower::Domain::Robot::MowerMap &map,
    ArduMower::Domain::Robot::MowSettings &settings,
    const ArduMower::Domain::Robot::State::State *state = nullptr,
    RouteReport *report = nullptr,
    float mowSpeed = 0.3f);

// Filtert eine bereits berechnete Route anhand der Runtime-Toggles.
Polygon filterRouteByToggles(const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &settings);

} // namespace PathPlanner
} // namespace Modem
} // namespace ArduMower
#endif
