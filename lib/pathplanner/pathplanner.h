#pragma once

#include <vector>
#include <cstdint>

namespace ArduMower {
namespace Modem {
namespace PathPlannerCore {

struct Point {
    double X;
    double Y;
    // Route category assigned by the planner stage. The modem maps these
    // values to its compact MapPointTag protocol (0=none, 1=area, 3=border).
    uint8_t tag = 0;
};

using Polygon = std::vector<Point>;

struct Map {
    uint32_t timestamp;
    std::vector<Point> perimeter;
    std::vector<std::vector<Point>> exclusions;
    std::vector<Point> searchWire;
    std::vector<Point> waypoints;
    std::vector<Point> dockpoints;
    double rotation = 0.0;
};

struct Settings {
    uint32_t timestamp;
    int pattern;
    float width;
    int angle;
    int distanceToBorder;
    int borderLaps;
    // Berechnung ist immer aktiv; die Laufzeit-Toggles (doMow*) filtern später.
    bool mowArea;
    bool mowExclusionBorder;
    bool mowBorderCcw;

    Settings()
        : timestamp(0), pattern(0), width(0.3f), angle(0),
          distanceToBorder(0), borderLaps(0),
          mowArea(true), mowExclusionBorder(true), mowBorderCcw(false) {}
};

struct Position {
    double x;
    double y;
    int solution;
};

struct State {
    int job;
    Position position;
};

// Core geometry helpers
double distance(const Point &a, const Point &b);
double crossProduct(const Point &a, const Point &b, const Point &c);
bool pointInPolygon(const Point &p, const Polygon &poly);
Point rotatePoint(const Point &p, double angleDeg);
Polygon rotatePolygon(const Polygon &poly, double angleDeg);
std::vector<Polygon> rotatePolygons(const std::vector<Polygon> &polys, double angleDeg);
void boundingBox(const Polygon &poly, double &minX, double &minY, double &maxX, double &maxY);
double polygonArea(const Polygon &poly);
bool isClockwise(const Polygon &poly);
size_t nearestPointIndex(const Point &p, const Polygon &poly);

struct Intersection {
    double x;
    int edgeIndex;
};
std::vector<Intersection> intersectRayWithPolygon(double y, const Polygon &poly);

std::vector<Polygon> offsetPolygonInward(const Polygon &poly, double distance);

// Berechnet die mähbare Region (Perimeter minus Exclusions) und die davon um
// distanceToBorder*width nach innen versetzte Mähfläche. Wird sowohl von
// calculateWaypoints als auch für die Tag-Klassifizierung im Firmware-Layer
// verwendet, damit beide Seiten exakt dieselbe Geometrie zugrunde legen.
struct MowableAreas {
    std::vector<Polygon> areasToMow;     // nach distanceToBorder versetzte Mähfläche(n)
    std::vector<Polygon> mowableRegion;  // Perimeter minus Exclusions (Loecher als negative Flaeche)
    std::vector<Polygon> holes;          // inflated Exclusion-Loecher (fuer Connector-Checks)
};
MowableAreas computeMowableAreas(const Polygon &perimeter, const std::vector<Polygon> &exclusions,
    const Settings &settings);

// Berechnet für jede Border-Lap-Runde (0..laps-1) die Boundary-Polygone, auf
// denen die Runde verläuft (Perimeter, bei jeder weiteren Runde um width nach
// innen versetzt; ggf. um Exclusion-Löcher reduziert). Wird von addBorderLaps
// und von der Tag-Klassifizierung im Firmware-Layer verwendet.
std::vector<std::vector<Polygon>> computeBorderLapBoundaries(const Polygon &perimeter,
    const std::vector<Polygon> &holes, int laps, double width);

Polygon calculateRingsPattern(const Polygon &perimeter, const Polygon &areaToMow,
    const std::vector<Polygon> &holes, double width, const Point &startNear);
Polygon addBorderLaps(const Polygon &perimeter, const std::vector<Polygon> &holes,
    int laps, bool ccw, const Point &startNear, double width);

void sortSolutionPolygonsByDistance(std::vector<Polygon> &solution, const Point &startPt);
void connectPolysUsingPathFinding(Polygon &waypoints, const std::vector<Polygon> &polys,
    const Polygon &perimeter, const std::vector<Polygon> &areasToMow,
    const std::vector<Polygon> &holes = {}, bool ringsMode = false,
    const std::vector<Polygon> &preferredRoutes = {});
std::vector<Polygon> clipSegmentsAgainstHoles(const std::vector<Polygon> &segments,
    const std::vector<Polygon> &holes);
// Sichere Verbindung zweier Punkte entlang der Perimetergrenze (unter
// Berücksichtigung von Exclusion-Löchern). Wird für die Neuberechnung von
// Verbindungslinien bei Toggle-Wechseln benötigt.
Polygon walkBoundaryWithHoles(const Point &from, const Point &to,
    const Polygon &outerBoundary, const std::vector<Polygon> &holes,
    const std::vector<Polygon> &preferredRoutes = {});

Polygon calculateWaypoints(Map &map, Settings &settings, const State *state = nullptr);

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
