// Prüft eine berechnete Route gegen die Original-Geometrie der Karte:
// Kein Segment darf den Perimeter verlassen oder durch eine Exclusion führen.
// Abgetastet wird alle 1 cm; Punkte innerhalb von tol am Rand zählen nicht
// (Randrunden und Verbindungen dürfen exakt auf der Grenze laufen).
#pragma once

#include <pathplanner.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace route_check {

using namespace ArduMower::Modem::PathPlannerCore;

struct Violation {
    size_t segment;      // Index des Startpunkts in der Route
    bool outside;        // true: außerhalb des Perimeters, false: in Exclusion
    int exclusion;       // Index der Exclusion (oder -1)
    double depth;        // größte Eindringtiefe in m
    uint8_t tagFrom, tagTo;
    double length;
};

inline double distToSegment(const Point &p, const Point &a, const Point &b) {
    double dx = b.X - a.X, dy = b.Y - a.Y;
    double l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((p.X - a.X) * dx + (p.Y - a.Y) * dy) / l2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    double x = a.X + t * dx - p.X, y = a.Y + t * dy - p.Y;
    return std::sqrt(x * x + y * y);
}

inline double distToBoundary(const Point &p, const Polygon &poly) {
    double best = 1e18;
    for (size_t i = 0; i < poly.size(); i++)
        best = std::min(best, distToSegment(p, poly[i], poly[(i + 1) % poly.size()]));
    return best;
}

inline std::vector<Violation> check(const Map &map, const Polygon &route, double tol = 0.02) {
    std::vector<Violation> out;
    for (size_t i = 0; i + 1 < route.size(); i++) {
        const Point &a = route[i], &b = route[i + 1];
        double len = distance(a, b);
        int n = std::max(1, (int)std::ceil(len / 0.01));
        Violation v{i, false, -1, 0.0, a.tag, b.tag, len};
        for (int k = 0; k <= n; k++) {
            double t = (double)k / n;
            Point p{a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t};
            if (!pointInPolygon(p, map.perimeter)) {
                double d = distToBoundary(p, map.perimeter);
                if (d > tol && d > v.depth) { v.depth = d; v.outside = true; v.exclusion = -1; }
            }
            for (size_t e = 0; e < map.exclusions.size(); e++) {
                if (!pointInPolygon(p, map.exclusions[e])) continue;
                double d = distToBoundary(p, map.exclusions[e]);
                if (d > tol && d > v.depth) { v.depth = d; v.outside = false; v.exclusion = (int)e; }
            }
        }
        if (v.depth > 0) out.push_back(v);
    }
    return out;
}

inline void writeSvg(const std::string &file, const Map &map, const Polygon &route,
                     const std::vector<Violation> &violations) {
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto &p : map.perimeter) {
        minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
        minY = std::min(minY, p.Y); maxY = std::max(maxY, p.Y);
    }
    std::ofstream o(file);
    auto pts = [](const Polygon &poly) {
        std::string s;
        char buf[64];
        for (const auto &p : poly) { std::snprintf(buf, sizeof(buf), "%.3f,%.3f ", p.X, -p.Y); s += buf; }
        return s;
    };
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='1200' height='900' viewBox='"
      << minX - 1 << " " << -maxY - 1 << " " << maxX - minX + 2 << " " << maxY - minY + 2 << "'>\n"
      << "<rect x='-1000' y='-1000' width='2000' height='2000' fill='white'/>\n"
      << "<polygon points='" << pts(map.perimeter) << "' fill='#eef7ee' stroke='black' stroke-width='0.05'/>\n";
    for (const auto &e : map.exclusions)
        o << "<polygon points='" << pts(e) << "' fill='#f8d0d0' stroke='red' stroke-width='0.04'/>\n";
    o << "<polyline points='" << pts(route) << "' fill='none' stroke='#3366cc' stroke-width='0.02'/>\n";
    for (const auto &v : violations)
        o << "<polyline points='" << pts({route[v.segment], route[v.segment + 1]})
          << "' fill='none' stroke='" << (v.outside ? "orange" : "magenta") << "' stroke-width='0.12'/>\n";
    o << "</svg>\n";
}

} // namespace route_check
