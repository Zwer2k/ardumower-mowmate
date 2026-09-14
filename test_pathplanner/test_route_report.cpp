// Tests for the route quality check (lib/pathplanner/route_report.cpp).
//
// Plain main() with a failure counter, same pattern as the other tests here.
// Build: make test_route_report && ./test_route_report

#include <route_report.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ArduMower::Modem::PathPlannerCore;

static int failures = 0;

static void expect(bool ok, const std::string &what) {
    if (ok) return;
    std::printf("  FAIL: %s\n", what.c_str());
    failures++;
}

static void expectNear(double actual, double expected, double tol, const std::string &what) {
    if (std::fabs(actual - expected) <= tol) return;
    std::printf("  FAIL: %s (expected %.4f, got %.4f)\n", what.c_str(), expected, actual);
    failures++;
}

static Polygon makeRoute(const std::vector<std::pair<double, double>> &pts) {
    Polygon route;
    route.reserve(pts.size());
    for (const auto &p : pts) route.push_back(Point{p.first, p.second});
    return route;
}

static size_t countKind(const RouteReport &r, FindingKind kind) {
    size_t n = 0;
    for (const auto &f : r.findings) {
        if (f.kind == kind) n++;
    }
    return n;
}

// ---------------------------------------------------------------------------

static void testStraightLine() {
    std::printf("straight line\n");
    RouteCheckParams p;  // radius 0.30, speed 0.30
    const auto route = makeRoute({{0, 0}, {1, 0}, {2, 0}, {3, 0}});
    const RouteReport r = analyzeRoute(route, p);

    expectNear(r.stats.totalLength, 3.0, 1e-9, "total length");
    expect(r.stats.pointCount == 4, "point count");
    expect(r.stats.rotationCount == 0, "no rotations on a straight line");
    expect(r.stats.trackedCorners == 0, "no tracked corners on a straight line");
    expect(r.stats.findingsTotal == 0, "no findings on a straight line");
    // 3 m at 0.3 m/s, no braking and no rotation.
    expectNear(r.stats.estimatedSeconds, 10.0, 1e-6, "time estimate");
}

static void testWideCornerIsFine() {
    std::printf("right angle with long legs\n");
    RouteCheckParams p;
    const auto route = makeRoute({{0, 0}, {1, 0}, {1, 1}});
    const RouteReport r = analyzeRoute(route, p);

    // tangent = 0.30 * tan(45 deg) = 0.30 m, both legs are 1 m.
    expect(r.stats.trackedCorners == 1, "one tracked corner");
    expect(r.stats.rotationCount == 0, "90 deg is steered, not rotated");
    expect(r.stats.findingsTotal == 0, "long legs leave room for the corner");
}

static void testTightCornerIsReported() {
    std::printf("right angle with short legs\n");
    RouteCheckParams p;
    const auto route = makeRoute({{0, 0}, {0.2, 0}, {0.2, 0.2}});
    const RouteReport r = analyzeRoute(route, p);

    expect(r.stats.trackedCorners == 1, "one tracked corner");
    expect(r.stats.findingsTotal == 1, "exactly one finding");
    expect(countKind(r, FindingKind::TightCorner) == 1, "finding is a tight corner");
    if (!r.findings.empty()) {
        const RouteFinding &f = r.findings[0];
        expect(f.index == 1, "finding points at the corner waypoint");
        expect(f.severity == FindingSeverity::Error, "tight corner is an error");
        expectNear(f.angleDeg, 90.0, 1e-6, "turn angle");
        // needs 0.30 m run-in, has 0.20 m
        expectNear(f.shortfall, 0.10, 1e-6, "missing run-in");
    }
}

static void testUturnCountsAsRotation() {
    std::printf("180 degree reversal\n");
    RouteCheckParams p;
    const auto route = makeRoute({{0, 0}, {1, 0}, {0, 0}});
    const RouteReport r = analyzeRoute(route, p);

    expect(r.stats.rotationCount == 1, "one rotation on the spot");
    expect(r.stats.trackedCorners == 0, "a reversal is not steered through");
    expect(r.stats.findingsTotal == 0, "a row-end reversal is normal, not a finding");
    // 2 m travel plus (180-120)/29 s rotating plus the creep after standstill.
    const double expected = 2.0 / 0.3 + 60.0 / 29.0 + 3.0 * (1.0 - 0.1 / 0.3);
    expectNear(r.stats.estimatedSeconds, expected, 1e-6, "time estimate includes the turn");
}

static void testAdjacentCornersOverlap() {
    std::printf("two corners sharing a short segment\n");
    RouteCheckParams p;
    // Each corner on its own has 0.4 m of room and needs 0.3 m, but together
    // they need 0.6 m on the 0.4 m segment between them.
    const auto route = makeRoute({{0, 0}, {1, 0}, {1, 0.4}, {2, 0.4}});
    const RouteReport r = analyzeRoute(route, p);

    expect(r.stats.trackedCorners == 2, "two tracked corners");
    expect(countKind(r, FindingKind::TightCorner) == 0, "neither corner is tight on its own");
    expect(countKind(r, FindingKind::TangentOverlap) == 1, "one overlap");
    for (const auto &f : r.findings) {
        if (f.kind != FindingKind::TangentOverlap) continue;
        expect(f.index == 1 && f.index2 == 2, "overlap names both corners");
        expect(f.severity == FindingSeverity::Warning, "overlap is a warning");
        expectNear(f.shortfall, 0.2, 1e-6, "overlap amount");
    }
}

static void testDegenerateSegment() {
    std::printf("zero length segment\n");
    RouteCheckParams p;
    const auto route = makeRoute({{0, 0}, {1, 0}, {1, 0}, {2, 0}});
    const RouteReport r = analyzeRoute(route, p);

    expect(countKind(r, FindingKind::DegenerateSegment) >= 1, "degenerate segment reported");
}

static void testFindingsAreCapped() {
    std::printf("more problems than the cap allows\n");
    RouteCheckParams p;
    p.maxFindings = 5;

    // Saw tooth with 0.1 m steps: every corner is a tight 90 degree turn.
    std::vector<std::pair<double, double>> pts;
    for (int k = 0; k <= 20; k++) {
        pts.push_back({k * 0.1, (k % 2) ? 0.1 : 0.0});
    }
    const RouteReport r = analyzeRoute(makeRoute(pts), p);

    expect(r.findings.size() == 5, "stored findings are capped");
    expect(r.stats.findingsTotal >= 19, "total keeps counting past the cap");
    expect(r.stats.findingsTotal > r.findings.size(), "total exceeds the stored list");
}

static void testRadiusDoesNotChangeStats() {
    std::printf("radius only affects findings\n");
    const auto route = makeRoute({{0, 0}, {1, 0}, {1, 1}});

    RouteCheckParams small;
    small.minTurnRadius = 0.30;
    RouteCheckParams large;
    large.minTurnRadius = 3.00;

    const RouteReport a = analyzeRoute(route, small);
    const RouteReport b = analyzeRoute(route, large);

    expectNear(b.stats.totalLength, a.stats.totalLength, 1e-9, "length is independent of radius");
    expect(b.stats.pointCount == a.stats.pointCount, "point count is independent of radius");
    expect(b.stats.trackedCorners == a.stats.trackedCorners, "corner count is independent");
    expect(a.stats.findingsTotal == 0, "small radius fits");
    expect(b.stats.findingsTotal > 0, "large radius does not fit");
}

int main() {
    testStraightLine();
    testWideCornerIsFine();
    testTightCornerIsReported();
    testUturnCountsAsRotation();
    testAdjacentCornersOverlap();
    testDegenerateSegment();
    testFindingsAreCapped();
    testRadiusDoesNotChangeStats();

    if (failures == 0) {
        std::printf("route report: all checks passed\n");
        return 0;
    }
    std::printf("route report: %d check(s) failed\n", failures);
    return 1;
}
