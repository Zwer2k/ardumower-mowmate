#include <pathplanner.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <limits>
#include <cstdlib>

using namespace ArduMower::Modem::PathPlannerCore;

static Polygon makeSquare(double cx, double cy, double size) {
    double h = size / 2.0;
    return {
        {cx - h, cy - h},
        {cx + h, cy - h},
        {cx + h, cy + h},
        {cx - h, cy + h},
        {cx - h, cy - h},
    };
}

static Map makeTestMap() {
    Map map;
    map.perimeter = makeSquare(0, 0, 10);
    map.exclusions.push_back(makeSquare(2, 2, 2));
    return map;
}

static void makeOutDir() {
    static bool created = false;
    if (!created) {
        if (std::system("mkdir -p out") != 0) {
            std::cerr << "Failed to create out directory" << std::endl;
        }
        created = true;
    }
}

static std::string safeFileName(const std::string &name) {
    std::string s = name;
    for (char &c : s) {
        if (c == ' ' || c == '/' || c == '+' || c == '=') c = '_';
    }
    return s;
}

static void polygonBounds(const Polygon &poly, double &minX, double &minY, double &maxX, double &maxY) {
    minX = minY = std::numeric_limits<double>::max();
    maxX = maxY = -std::numeric_limits<double>::max();
    for (const auto &p : poly) {
        if (p.X < minX) minX = p.X;
        if (p.X > maxX) maxX = p.X;
        if (p.Y < minY) minY = p.Y;
        if (p.Y > maxY) maxY = p.Y;
    }
}

static std::string pointsToPath(const Polygon &poly) {
    std::ostringstream oss;
    for (const auto &p : poly) {
        if (!oss.str().empty()) oss << " ";
        oss << p.X << "," << p.Y;
    }
    return oss.str();
}

static void writeSvg(const std::string &name, const Map &map, const Polygon &waypoints) {
    makeOutDir();
    std::string filename = "out/" + safeFileName(name) + ".svg";
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "[" << name << "] failed to open " << filename << std::endl;
        return;
    }

    double minX, minY, maxX, maxY;
    polygonBounds(map.perimeter, minX, minY, maxX, maxY);
    for (const auto &excl : map.exclusions) {
        double eminX, eminY, emaxX, emaxY;
        polygonBounds(excl, eminX, eminY, emaxX, emaxY);
        minX = std::min(minX, eminX);
        minY = std::min(minY, eminY);
        maxX = std::max(maxX, emaxX);
        maxY = std::max(maxY, emaxY);
    }
    double padding = std::max((maxX - minX), (maxY - minY)) * 0.05 + 0.5;
    minX -= padding; minY -= padding; maxX += padding; maxY += padding;
    double width = maxX - minX;
    double height = maxY - minY;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"800\" "
        << "viewBox=\"" << minX << " " << minY << " " << width << " " << height << "\">\n";
    out << "<rect x=\"" << minX << "\" y=\"" << minY << "\" width=\"" << width
        << "\" height=\"" << height << "\" fill=\"white\"/>\n";

    out << "<polygon points=\"" << pointsToPath(map.perimeter) << "\" "
        << "fill=\"none\" stroke=\"black\" stroke-width=\"0.08\"/>\n";

    for (const auto &excl : map.exclusions) {
        out << "<polygon points=\"" << pointsToPath(excl) << "\" "
            << "fill=\"rgba(255,0,0,0.2)\" stroke=\"red\" stroke-width=\"0.06\"/>\n";
    }

    if (waypoints.size() >= 2) {
        out << "<polyline points=\"" << pointsToPath(waypoints) << "\" "
            << "fill=\"none\" stroke=\"blue\" stroke-width=\"0.04\"/>\n";
    }

    if (!waypoints.empty()) {
        out << "<circle cx=\"" << waypoints[0].X << "\" cy=\"" << waypoints[0].Y
            << "\" r=\"0.15\" fill=\"green\"/>\n";
    }

    out << "</svg>\n";
    std::cout << "[" << name << "] wrote " << filename << std::endl;
}

static void printResult(const std::string &name, const Settings &s, size_t n) {
    std::cout << std::left << std::setw(40) << name
              << " area=" << s.mowArea
              << " border=" << s.distanceToBorder << "x" << s.borderLaps
              << " exclBorder=" << s.mowExclusionBorder
              << " pattern=" << s.pattern
              << " -> " << n << " waypoints" << std::endl;
}

static int runTest(const std::string &name, Settings s) {
    Map map = makeTestMap();
    Polygon route = calculateWaypoints(map, s, nullptr);
    printResult(name, s, route.size());
    writeSvg(name, map, route);
    if (route.empty()) {
        std::cerr << "FAIL: " << name << " produced no waypoints" << std::endl;
        return 1;
    }
    return 0;
}

static int runRouteTagTest(const std::string &name, Settings s,
    bool expectArea, bool expectBorder)
{
    Map map = makeTestMap();
    Polygon route = calculateWaypoints(map, s, nullptr);
    size_t areaPoints = 0;
    size_t borderPoints = 0;
    for (const auto &point : route) {
        if (point.tag == 1) ++areaPoints;
        if (point.tag == 3) ++borderPoints;
    }
    std::cout << name << " area-tagged=" << areaPoints
              << " border-tagged=" << borderPoints << std::endl;
    if ((expectArea && areaPoints == 0) || (!expectArea && areaPoints != 0) ||
        (expectBorder && borderPoints == 0) || (!expectBorder && borderPoints != 0)) {
        std::cerr << "FAIL: " << name << " has incorrect route tags" << std::endl;
        return 1;
    }
    return 0;
}

static int runBorderContinuityTest(const std::string &name, Settings s) {
    Map map = makeTestMap();
    Polygon route = calculateWaypoints(map, s, nullptr);
    for (size_t i = 1; i + 1 < route.size(); i++) {
        if (route[i - 1].tag == 3 && route[i + 1].tag == 3 && route[i].tag != 3) {
            std::cerr << "FAIL: " << name << " has an untagged gap inside a border lap" << std::endl;
            return 1;
        }
    }
    std::cout << name << " has no untagged gaps inside border laps" << std::endl;
    return 0;
}

static int runBorderLapClosureTest(const std::string &name, Settings s) {
    Map map = makeTestMap();
    map.exclusions.clear();
    Polygon route = calculateWaypoints(map, s, nullptr);
    size_t first = route.size();
    size_t last = route.size();
    for (size_t i = 0; i < route.size(); i++) {
        if (route[i].tag != 3) continue;
        if (first == route.size()) first = i;
        last = i;
    }
    if (first == route.size() || last <= first || distance(route[first], route[last]) > 0.01) {
        std::cerr << "FAIL: " << name << " does not end the border lap at its start point"
                  << " first=(" << route[first].X << "," << route[first].Y << ")"
                  << " last=(" << route[last].X << "," << route[last].Y << ")" << std::endl;
        std::cerr << "  border route:";
        for (size_t i = first; i <= last; i++) {
            if (route[i].tag == 3) std::cerr << " (" << route[i].X << "," << route[i].Y << ")";
        }
        std::cerr << std::endl;
        return 1;
    }
    std::cout << name << " closes the border lap" << std::endl;
    return 0;
}

int main() {
    int failures = 0;

    // Defaults: area only
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        failures += runTest("default area only", s);
    }

    // Area with border offset
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.distanceToBorder = 1;
        failures += runTest("area + border offset", s);
    }

    // Area with border laps CCW
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.borderLaps = 2;
        s.mowBorderCcw = true;
        failures += runTest("area + border laps CCW", s);
    }

    // Area with border laps CW
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.borderLaps = 2;
        s.mowBorderCcw = false;
        failures += runTest("area + border laps CW", s);
    }

    // Border only (no area)
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = false;
        s.borderLaps = 1;
        s.mowBorderCcw = true;
        failures += runTest("border only CCW", s);
    }

    // Exclusion border with area
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.mowExclusionBorder = true;
        failures += runTest("area + exclusion border", s);
    }

    // Squares pattern
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.pattern = 1;
        failures += runTest("squares pattern", s);
    }

    // Rings pattern
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = true;
        s.pattern = 2;
        failures += runTest("rings pattern", s);
    }

    // Route categories are assigned by their generating planner stage, not
    // by width-dependent distance heuristics. A single border lap must keep
    // exactly one border category for both narrow and wide cutting widths.
    for (double width : {0.12, 0.20}) {
        Settings s;
        s.width = width;
        s.pattern = 2;
        s.mowArea = true;
        s.borderLaps = 1;
        failures += runRouteTagTest("rings one border lap width=" + std::to_string(width), s, true, true);
        failures += runBorderContinuityTest("rings border continuity width=" + std::to_string(width), s);
        failures += runBorderLapClosureTest("rings border closure width=" + std::to_string(width), s);
    }

    // Lines must retain AREA tags so the Area toggle can hide/show them
    // independently of the configured perimeter border lap.
    {
        Settings s;
        s.width = 0.20;
        s.pattern = 0;
        s.mowArea = true;
        s.borderLaps = 1;
        failures += runRouteTagTest("lines area and border tags", s, true, true);
    }

    // No area, no border -> should be empty
    {
        Settings s;
        s.width = 0.3;
        s.mowArea = false;
        Map map = makeTestMap();
        Polygon route = calculateWaypoints(map, s, nullptr);
        printResult("no area no border (expect 0)", s, route.size());
        if (!route.empty()) {
            std::cerr << "FAIL: expected empty route when area and border are off" << std::endl;
            failures++;
        }
    }

    if (failures == 0) {
        std::cout << "\nAll settings-variant tests PASSED" << std::endl;
        return 0;
    } else {
        std::cerr << "\n" << failures << " settings-variant tests FAILED" << std::endl;
        return 1;
    }
}
