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
    // Schwelle der Routenvereinfachung in Metern. Wirkt als Verschmelzungsradius
    // für doppelte Punkte und (halbiert) als erlaubte Abweichung beim Entfernen
    // nahezu kollinearer Punkte. Größere Werte ergeben weniger Wegpunkte.
    float simplifyEpsilon;

    Settings()
        : timestamp(0), pattern(0), width(0.3f), angle(0),
          distanceToBorder(0), borderLaps(0),
          mowArea(true), mowExclusionBorder(true), mowBorderCcw(false),
          simplifyEpsilon(0.02f) {}
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

// Kürzeste Wege durch den freien Raum: innerhalb des Containers (Perimeter)
// und außerhalb aller Hindernisse (Exclusions). Knoten sind die konkaven Ecken
// des Containers und die konvexen Ecken der Hindernisse, jeweils um CLEARANCE
// in den freien Raum versetzt; Kanten sind alle geraden Verbindungen, die den
// freien Raum nicht verlassen (Sichtbarkeitsgraph). Die Sichtbarkeit zwischen
// Knoten wird erst bei Bedarf berechnet und über mehrere Anfragen gecacht,
// daher eine Instanz pro Berechnung wiederverwenden.
class FreeSpaceRouter {
public:
    static constexpr double CLEARANCE = 0.03;   // Abstand der Knoten zum Rand (m)
    static constexpr double BOUNDARY_TOL = 0.002; // Punkte auf dem Rand gelten als frei (m)

    FreeSpaceRouter(const std::vector<Point> &container,
        const std::vector<std::vector<Point>> &obstacles,
        const std::vector<std::vector<Point>> &preferredRoutes = {});

    bool pointFree(const Point &p) const;
    bool segmentFree(const Point &a, const Point &b) const;
    // Weg von from nach to (beide enthalten). Ist die direkte Linie frei, ist das
    // {from, to}; findet sich kein Weg, ebenfalls {from, to}.
    std::vector<Point> route(const Point &from, const Point &to) const;
    // Wie route(), meldet aber, ob ein freier Weg gefunden wurde. quick nutzt
    // nur die schnelle Suche; scheitert sie, ist trotzdem ein Weg möglich.
    bool findRoute(const Point &from, const Point &to, std::vector<Point> &path,
        bool quick = false) const;

private:
    struct Box { double minX, minY, maxX, maxY; };
    std::vector<Point> container_;
    std::vector<std::vector<Point>> obstacles_;
    Box containerBox_;
    std::vector<Box> obstacleBoxes_;
    std::vector<Point> nodes_;
    // Nachbarecken der Polygonecke, aus der ein Knoten stammt (für die
    // Tangentenregel); hasCorner_ = 0 für Knoten ohne Ecke (Suchdraht).
    std::vector<Point> cornerPrev_, cornerNext_;
    std::vector<char> hasCorner_;
    // nodes_[0, primaryCount_) sind die versetzten Ecken; danach folgen die
    // Original-Ecken auf dem Rand. Sie werden nur genutzt, wenn die erste Suche
    // scheitert (enge Durchgänge, in denen versetzte Ecken im Nachbarhindernis
    // landen).
    size_t primaryCount_ = 0;
    std::vector<std::vector<size_t>> preferred_;   // bevorzugte Kanten (Suchdraht)
    mutable std::vector<signed char> visibility_;  // -1 unbekannt, 0/1

    bool nodeVisible(size_t a, size_t b) const;
    bool tangentAt(size_t node, const Point &other) const;
    void addNode(const Point &p, const Point *prev, const Point *next);
    bool search(const Point &from, const Point &to, size_t nodeCount, bool tangentOnly,
        std::vector<Point> &path) const;
    bool crossesProperly(const Point &a, const Point &b, const std::vector<Point> &poly) const;
};

// Verbindungen innerhalb eines Mähmusters: zuerst innerhalb der Mähfläche
// (hält den Randabstand ein), sonst innerhalb des ganzen Perimeters.
struct ConnectorRouting {
    const FreeSpaceRouter *perimeter = nullptr;
    std::vector<const FreeSpaceRouter *> areas;
    std::vector<Point> route(const Point &from, const Point &to) const;
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
    const std::vector<Polygon> &holes, double width, const Point &startNear,
    const ConnectorRouting *routing = nullptr);
// router (optional) verbindet die einzelnen Runden kollisionsfrei; ohne router
// werden sie direkt aneinandergehängt.
Polygon addBorderLaps(const Polygon &perimeter, const std::vector<Polygon> &holes,
    int laps, bool ccw, const Point &startNear, double width,
    const FreeSpaceRouter *router = nullptr);

void sortSolutionPolygonsByDistance(std::vector<Polygon> &solution, const Point &startPt);
void connectPolysUsingPathFinding(Polygon &waypoints, const std::vector<Polygon> &polys,
    const Polygon &perimeter, const std::vector<Polygon> &areasToMow,
    const std::vector<Polygon> &holes = {}, bool ringsMode = false,
    const std::vector<Polygon> &preferredRoutes = {},
    const ConnectorRouting *routing = nullptr);
std::vector<Polygon> clipSegmentsAgainstHoles(const std::vector<Polygon> &segments,
    const std::vector<Polygon> &holes);
// Sichere Verbindung zweier Punkte innerhalb des Perimeters und außerhalb der
// Exclusion-Löcher (kürzester Weg, siehe FreeSpaceRouter). Wird für die
// Neuberechnung von Verbindungslinien bei Toggle-Wechseln benötigt.
Polygon walkBoundaryWithHoles(const Point &from, const Point &to,
    const Polygon &outerBoundary, const std::vector<Polygon> &holes,
    const std::vector<Polygon> &preferredRoutes = {});

Polygon calculateWaypoints(Map &map, Settings &settings, const State *state = nullptr);

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
