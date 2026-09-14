#include "route_report.h"

#include <cmath>
#include <algorithm>

namespace ArduMower {
namespace Modem {
namespace PathPlannerCore {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline double deg2rad(double deg) { return deg * kPi / 180.0; }

} // namespace

double turnAngleDeg(const Point &a, const Point &b, const Point &c) {
    const double v1x = b.X - a.X;
    const double v1y = b.Y - a.Y;
    const double v2x = c.X - b.X;
    const double v2y = c.Y - b.Y;

    const double l1 = std::sqrt(v1x * v1x + v1y * v1y);
    const double l2 = std::sqrt(v2x * v2x + v2y * v2y);
    if (l1 < 1e-12 || l2 < 1e-12) return 0.0;

    double dot = (v1x * v2x + v1y * v2y) / (l1 * l2);
    dot = std::max(-1.0, std::min(1.0, dot));
    return std::acos(dot) * 180.0 / kPi;
}

RouteReport analyzeRoute(const Polygon &route, const RouteCheckParams &params) {
    RouteReport report;
    report.stats.pointCount = static_cast<uint32_t>(route.size());
    if (route.size() < 2) return report;

    const double radius = params.minTurnRadius > 0.0 ? params.minTurnRadius : 0.0;
    const double speed = params.mowSpeed > 1e-6 ? params.mowSpeed : 1e-6;

    // Adding a finding is always counted, but only stored up to maxFindings so
    // the payload that later travels to the browser stays bounded.
    auto addFinding = [&](FindingSeverity severity, FindingKind kind, uint32_t index,
                          uint32_t index2, double angleDeg, double shortfall) {
        report.stats.findingsTotal++;
        if (report.findings.size() >= params.maxFindings) return;
        RouteFinding f;
        f.severity = severity;
        f.kind = kind;
        f.index = index;
        f.index2 = index2;
        f.angleDeg = angleDeg;
        f.shortfall = shortfall;
        report.findings.push_back(f);
    };

    for (size_t i = 0; i + 1 < route.size(); i++) {
        report.stats.totalLength += distance(route[i], route[i + 1]);
    }

    // Required straight run-in/run-out per corner, 0 where the corner is not
    // steered through (straight, rotation on the spot, or degenerate).
    std::vector<double> tangent(route.size(), 0.0);

    double extraSeconds = 0.0;

    for (size_t i = 1; i + 1 < route.size(); i++) {
        const Point &a = route[i - 1];
        const Point &b = route[i];
        const Point &c = route[i + 1];

        const double lenPrev = distance(a, b);
        const double lenNext = distance(b, c);

        if (lenPrev < params.degenerateLength || lenNext < params.degenerateLength) {
            addFinding(FindingSeverity::Error, FindingKind::DegenerateSegment,
                       static_cast<uint32_t>(i), static_cast<uint32_t>(i), 0.0, 0.0);
            continue;
        }

        const double angle = turnAngleDeg(a, b, c);

        if (angle < params.straightToleranceDeg) {
            // Straight enough: the tracker neither rotates nor brakes here.
            continue;
        }

        if (angle >= params.rotateThresholdDeg) {
            // Rotation on the spot. Normal at the end of a mowing row, so this
            // is not a finding. Sunray rotates only until the remaining error
            // is back below the threshold and steers through the rest.
            report.stats.rotationCount++;
            const double rotateDeg = angle - params.rotateThresholdDeg;
            if (params.rotationSpeedDegPerSec > 1e-6) {
                extraSeconds += rotateDeg / params.rotationSpeedDegPerSec;
            }
            // After every standstill the mower creeps for a few seconds. Only
            // the part that exceeds normal travel counts as extra.
            extraSeconds += params.slowAfterStopSeconds *
                            (1.0 - std::min(1.0, params.slowSpeed / speed));
            continue;
        }

        // Corner steered through by the controller.
        report.stats.trackedCorners++;

        if (params.mowSpeed > params.slowSpeedThreshold) {
            // Braking to slowSpeed over the last stretch before the waypoint.
            const double d = std::min(params.slowApproachDistance, lenPrev);
            if (params.slowSpeed > 1e-6) {
                extraSeconds += d * (1.0 / params.slowSpeed - 1.0 / speed);
            }
        }

        if (radius <= 0.0) continue;

        const double half = deg2rad(angle) / 2.0;
        const double t = radius * std::tan(half);
        tangent[i] = t;

        const double shorter = std::min(lenPrev, lenNext);
        if (t > shorter) {
            addFinding(FindingSeverity::Error, FindingKind::TightCorner,
                       static_cast<uint32_t>(i), static_cast<uint32_t>(i), angle, t - shorter);
        }
    }

    // Two adjacent corners share the segment between them. If both need more
    // run-out/run-in than the segment is long, the mower cannot settle between
    // them even though each corner on its own looks fine.
    for (size_t i = 1; i + 2 < route.size(); i++) {
        if (tangent[i] <= 0.0 || tangent[i + 1] <= 0.0) continue;
        const double segLen = distance(route[i], route[i + 1]);
        const double needed = tangent[i] + tangent[i + 1];
        if (needed > segLen) {
            addFinding(FindingSeverity::Warning, FindingKind::TangentOverlap,
                       static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), 0.0,
                       needed - segLen);
        }
    }

    report.stats.estimatedSeconds = report.stats.totalLength / speed + extraSeconds;
    return report;
}

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
