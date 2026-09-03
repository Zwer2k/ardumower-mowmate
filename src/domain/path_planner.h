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

Polygon calculateWaypoints(ArduMower::Domain::Robot::MowerMap &map,
    ArduMower::Domain::Robot::MowSettings &settings,
    const ArduMower::Domain::Robot::State::State *state = nullptr);

// Filtert eine bereits berechnete Route anhand der Runtime-Toggles.
Polygon filterRouteByToggles(const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &settings);

} // namespace PathPlanner
} // namespace Modem
} // namespace ArduMower
#endif
