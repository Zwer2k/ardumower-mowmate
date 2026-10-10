// Randrunden um Exclusions sind getrennt von den Perimeterrunden: eigener Tag
// (ExclusionBorder = 4), eigene Rundenzahl (exclusionLaps) und eigener Abstand
// der Mähfläche (distanceToExclusions). Geprüft an einem 10 x 10 m Garten mit
// einer Exclusion in der Mitte, die den Perimeter nicht berührt.
#include <pathplanner.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "route_check.h"

using namespace ArduMower::Modem::PathPlannerCore;

static const uint8_t TAG_AREA = 1;
static const uint8_t TAG_BORDER = 3;
static const uint8_t TAG_EXCLUSION_BORDER = 4;

static Map makeMap() {
    Map map;
    map.perimeter = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    map.exclusions = {{{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
    return map;
}

// Abstand zur Exclusion, außerhalb positiv
static double distToExclusion(const Point &p, const Polygon &excl) {
    const double d = route_check::distToBoundary(p, excl);
    return pointInPolygon(p, excl) ? -d : d;
}

struct Counts { int area = 0, border = 0, exclusionBorder = 0; };

static Counts countTags(const Polygon &route) {
    Counts c;
    for (const auto &p : route) {
        if (p.tag == TAG_AREA) c.area++;
        else if (p.tag == TAG_BORDER) c.border++;
        else if (p.tag == TAG_EXCLUSION_BORDER) c.exclusionBorder++;
    }
    return c;
}

static int failures = 0;
static void expect(bool ok, const std::string &what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static Polygon run(const std::string &name, Settings s) {
    Map map = makeMap();
    Polygon route = calculateWaypoints(map, s, nullptr);
    auto violations = route_check::check(map, route);
    expect(violations.empty(), name + ": no segment leaves the perimeter or enters the exclusion (" +
        std::to_string(violations.size()) + " violations)");
    if (!violations.empty()) route_check::writeSvg("out/exclusion_laps_" + name + ".svg", map, route, violations);
    return route;
}

int main() {
    const Polygon excl = makeMap().exclusions[0];
    const double width = 0.2;

    Settings base;
    base.pattern = 0;
    base.width = (float)width;
    base.angle = 0;
    base.distanceToBorder = 1;
    base.borderLaps = 1;
    base.mowArea = true;
    base.mowExclusionBorder = true;

    {
        Settings s = base;
        s.exclusionLaps = 2;
        s.distanceToExclusions = 2;
        Polygon route = run("separate", s);
        Counts c = countTags(route);
        expect(c.border > 0, "perimeter laps are tagged Border");
        expect(c.exclusionBorder > 0, "exclusion laps are tagged ExclusionBorder");
        double minBorderDist = 1e9, minLap = 1e9, maxLap = -1e9, minArea = 1e9;
        for (const auto &p : route) {
            const double d = distToExclusion(p, excl);
            if (p.tag == TAG_BORDER) minBorderDist = std::min(minBorderDist, d);
            if (p.tag == TAG_EXCLUSION_BORDER) { minLap = std::min(minLap, d); maxLap = std::max(maxLap, d); }
            if (p.tag == TAG_AREA) minArea = std::min(minArea, d);
        }
        expect(minBorderDist > 3.0, "no Border point runs around the exclusion (min " + std::to_string(minBorderDist) + " m)");
        // Runde 0 auf dem Rand, Runde 1 eine Spurbreite außerhalb; an Ecken
        // liegt der versetzte Eckpunkt sqrt(2) Spurbreiten entfernt (Miter)
        expect(minLap > -0.01 && maxLap < width * std::sqrt(2.0) + 0.02,
            "exclusion laps lie 0 .. 1 width outside the exclusion, corners sqrt(2) (" + std::to_string(minLap) + " .. " +
            std::to_string(maxLap) + " m)");
        expect(maxLap > width - 0.05, "the second exclusion lap is one width further out");
        expect(minArea > 2 * width - 0.03,
            "area lines keep distanceToExclusions = 2 widths (min " + std::to_string(minArea) + " m)");
    }
    {
        Settings s = base;
        s.exclusionLaps = 0;
        s.distanceToExclusions = 0;
        Counts c = countTags(run("no-exclusion-laps", s));
        expect(c.border > 0 && c.exclusionBorder == 0, "exclusionLaps = 0: perimeter laps only");
    }
    {
        Settings s = base;
        s.borderLaps = 0;
        s.exclusionLaps = 1;
        Counts c = countTags(run("exclusion-laps-only", s));
        expect(c.border == 0 && c.exclusionBorder > 0, "borderLaps = 0: exclusion laps only");
    }
    {
        // Ohne Angabe wie früher: so viele Exclusion-Runden wie Randrunden
        Settings s = base;
        s.borderLaps = 2;
        Counts c = countTags(run("legacy-default", s));
        expect(c.border > 0 && c.exclusionBorder > 0, "exclusionLaps unset: follows borderLaps");
    }
    {
        Settings s = base;
        s.exclusionLaps = 2;
        s.mowExclusionBorder = false;
        Counts c = countTags(run("exclusion-border-off", s));
        expect(c.exclusionBorder == 0, "mowExclusionBorder = false: no exclusion laps");
    }

    printf("%s\n", failures == 0 ? "exclusion laps: all checks passed" : "exclusion laps: FAILED");
    return failures == 0 ? 0 : 1;
}
