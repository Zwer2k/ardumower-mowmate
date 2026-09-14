#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#include "pathplanner.h"

namespace ArduMower {
namespace Modem {
namespace PathPlannerCore {

// ---------------------------------------------------------------------------
// Route quality check.
//
// Runs on a finished route and reports how well the mower's line tracker can
// actually follow it, plus a few statistics. It never changes the route.
//
// Why a minimum turn radius at all? A differential drive can turn on the spot,
// so it has no kinematic turn radius. Sunray's line tracker, however, only
// rotates on the spot above a large heading error; below that it steers
// through the corner with the Stanley controller, whose angular rate is
// capped. Speed divided by that cap is an effective minimum radius.
//
// All default thresholds mirror Sunray so the report matches what the mower
// will really do:
//   - rotate on the spot from 120 deg heading error   LineTracker.cpp:77
//   - rotation speed 29 deg/s                          LineTracker.cpp:91
//   - slow to 0.1 m/s within 0.5 m of a target, but only above 0.2 m/s set
//     speed and only when the next point is not straight ahead
//                                                      LineTracker.cpp:127-131
//   - "straight ahead" means below 20 deg              map.cpp:921
//   - 3 s of slow travel after every standstill        robot.cpp:1131
// ---------------------------------------------------------------------------

struct RouteCheckParams {
    // Set by the user. Rule of thumb: mow speed divided by the tracker's
    // maximum angular rate (STANLEY_MAX_ANGULAR_SPEED in Sunray).
    double minTurnRadius = 0.30;          // m
    double mowSpeed = 0.30;               // m/s, for the time estimate

    // Sunray mirror, see comment above. Only change together with Sunray.
    double rotateThresholdDeg = 120.0;
    double straightToleranceDeg = 20.0;
    double rotationSpeedDegPerSec = 29.0;
    double slowSpeed = 0.10;              // m/s
    double slowApproachDistance = 0.50;   // m
    double slowAfterStopSeconds = 3.0;    // s
    double slowSpeedThreshold = 0.20;     // approach braking only above this set speed

    // Segments shorter than this count as degenerate.
    double degenerateLength = 0.005;      // m

    // Upper bound on the reported findings so the payload stays small. The
    // total is still counted in RouteStats::findingsTotal.
    size_t maxFindings = 50;
};

enum class FindingKind : uint8_t {
    TightCorner = 0,        // corner needs more straight run-in/run-out than it has
    TangentOverlap = 1,     // two adjacent corners do not fit on their shared segment
    DegenerateSegment = 2,  // zero-length segment
};

enum class FindingSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
};

struct RouteFinding {
    FindingSeverity severity = FindingSeverity::Info;
    FindingKind kind = FindingKind::TightCorner;
    uint32_t index = 0;      // waypoint the finding refers to
    uint32_t index2 = 0;     // second waypoint for pair findings, else == index
    double angleDeg = 0.0;   // turn angle at index
    double shortfall = 0.0;  // missing metres, or overlap in metres
};

struct RouteStats {
    uint32_t pointCount = 0;
    double totalLength = 0.0;       // m
    uint32_t rotationCount = 0;     // corners at or above rotateThresholdDeg
    uint32_t trackedCorners = 0;    // corners steered through by the controller
    double estimatedSeconds = 0.0;  // rough estimate, see analyzeRoute()
    uint32_t findingsTotal = 0;     // may exceed findings.size()
};

struct RouteReport {
    RouteStats stats;
    std::vector<RouteFinding> findings;
};

// Analyses a finished route. Pure function, does not modify the route.
RouteReport analyzeRoute(const Polygon &route, const RouteCheckParams &params);

// Turn angle at b between the segments a->b and b->c, in degrees.
// 0 means straight ahead, 180 means a complete reversal. Returns 0 if either
// segment is degenerate.
double turnAngleDeg(const Point &a, const Point &b, const Point &c);

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
