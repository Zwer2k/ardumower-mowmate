#include "pathplanner.h"
#include "clipper_adapter.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <queue>

#define _LOG_ "PathPlanner::"

// Default no-op logging for the standalone library. When compiled inside the
// firmware, this macro can be overridden by the build system to route logs to
// the actual logging framework.
#ifndef PP_LOG
#define PP_LOG(level, fmt, ...) do { (void)(level); (void)(fmt); } while(0)
#endif

namespace ArduMower {
namespace Modem {
namespace PathPlannerCore {

static const double _PI = 3.14159265358979323846;
static const double DEG2RAD = _PI / 180.0;

static const uint8_t RouteTagArea = 1;
static const uint8_t RouteTagBorder = 3;
static const uint8_t RouteTagConnector = 6;

// Tunable path-planner parameters.  These are expressed as factors of the
// mowing width (settings.width) so they can later be exposed in the UI.
namespace Tune {

// Distance of mowing lines from exclusion borders when the exclusion border
// is not mowed.  1.0 means the line center is one mower width away from the
// exclusion, leaving an unmowed strip of about half a width at the exclusion.
static const double exclusionMarginFactor = 1.0;

// Spacing between successive ring lines as a factor of the mowing width.
// A value below 1.0 makes the rings overlap slightly, which guarantees full
// coverage even when rings are smoothed or clipped by exclusions.
static const double ringSpacingFactor = 0.9;

// Progressive ring smoothing: inner rings gradually forget perimeter details.
// The closing radius for ring n is
//     r = min(n * width * smoothingGrowth, width * smoothingMax)
// Ring 0 (outermost) is never smoothed so the configured border distance is
// kept exactly.
// Lower smoothingGrowth prevents inner rings from shifting too much, which
// would create uneven spacing between adjacent rings.
static const double smoothingGrowth = 0.12;
static const double smoothingMax = 0.5;

} // namespace Tune

double distance(const Point &a, const Point &b) {
    double dx = a.X - b.X;
    double dy = a.Y - b.Y;
    return std::sqrt(dx * dx + dy * dy);
}

Point lerp(const Point &a, const Point &b, double t) {
    return Point{a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t};
}

double crossProduct(const Point &a, const Point &b, const Point &c) {
    return (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
}

bool pointInPolygon(const Point &p, const Polygon &poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].Y > p.Y) != (poly[j].Y > p.Y) &&
            p.X < (poly[j].X - poly[i].X) * (p.Y - poly[i].Y) / (poly[j].Y - poly[i].Y) + poly[i].X)
            inside = !inside;
    }
    return inside;
}

Point rotatePoint(const Point &p, double angleDeg) {
    double rad = angleDeg * DEG2RAD;
    double c = std::cos(rad);
    double s = std::sin(rad);
    return Point{p.X * c - p.Y * s, p.X * s + p.Y * c};
}

Polygon rotatePolygon(const Polygon &poly, double angleDeg) {
    Polygon result;
    result.reserve(poly.size());
    for (const auto &p : poly) result.push_back(rotatePoint(p, angleDeg));
    return result;
}

std::vector<Polygon> rotatePolygons(const std::vector<Polygon> &polys, double angleDeg) {
    std::vector<Polygon> result;
    result.reserve(polys.size());
    for (const auto &p : polys) result.push_back(rotatePolygon(p, angleDeg));
    return result;
}

void boundingBox(const Polygon &poly, double &minX, double &minY, double &maxX, double &maxY) {
    minX = minY = std::numeric_limits<double>::max();
    maxX = maxY = -std::numeric_limits<double>::max();
    for (const auto &p : poly) {
        if (p.X < minX) minX = p.X;
        if (p.X > maxX) maxX = p.X;
        if (p.Y < minY) minY = p.Y;
        if (p.Y > maxY) maxY = p.Y;
    }
}

double polygonArea(const Polygon &poly) {
    if (poly.size() < 3) return 0.0;
    double area = 0.0;
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        area += poly[i].X * poly[j].Y;
        area -= poly[j].X * poly[i].Y;
    }
    return area / 2.0;
}

bool isClockwise(const Polygon &poly) { return polygonArea(poly) < 0; }

size_t nearestPointIndex(const Point &p, const Polygon &poly) {
    if (poly.empty()) return 0;
    size_t best = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < poly.size(); i++) {
        double d = distance(p, poly[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

Polygon reversePolygon(const Polygon &poly) {
    Polygon rev = poly;
    std::reverse(rev.begin(), rev.end());
    return rev;
}

std::vector<Intersection> intersectRayWithPolygon(double y, const Polygon &poly) {
    std::vector<Intersection> result;
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        double y1 = poly[i].Y;
        double y2 = poly[j].Y;
        if (std::abs(y2 - y1) < 1e-10) continue;
        if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
            double t = (y - y1) / (y2 - y1);
            double x = poly[i].X + t * (poly[j].X - poly[i].X);
            result.push_back({x, (int)i});
        }
    }
    std::sort(result.begin(), result.end(),
        [](const Intersection &a, const Intersection &b) { return a.x < b.x; });
    return result;
}

std::vector<Polygon> offsetPolygonInward(const Polygon &poly, double dist) {
    if (dist <= 0.001 || poly.size() < 3) return {poly};
    auto result = clipOffset(poly, -dist);
    if (result.empty()) return {};
    return result;
}

// Check if a point lies on (or very close to) any polygon edge.
static bool pointOnBoundary(const Point &p, const Polygon &poly, double tolSq = 1e-6) {
    if (poly.size() < 2) return false;
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        double dx = poly[j].X - poly[i].X, dy = poly[j].Y - poly[i].Y;
        double len2 = dx*dx + dy*dy;
        if (len2 < 1e-10) continue;
        double t = std::max(0.0, std::min(1.0,
            ((p.X - poly[i].X)*dx + (p.Y - poly[i].Y)*dy) / len2));
        Point proj = {poly[i].X + t*dx, poly[i].Y + t*dy};
        double d2 = (p.X - proj.X)*(p.X - proj.X) + (p.Y - proj.Y)*(p.Y - proj.Y);
        if (d2 < tolSq) return true;
    }
    return false;
}

static bool segmentsIntersect(const Point &a1, const Point &a2, const Point &b1, const Point &b2) {
    double o1 = crossProduct(a1, a2, b1);
    double o2 = crossProduct(a1, a2, b2);
    double o3 = crossProduct(b1, b2, a1);
    double o4 = crossProduct(b1, b2, a2);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    // Check collinear cases
    auto onSegment = [](const Point &p, const Point &q, const Point &r) {
        return q.X <= std::max(p.X, r.X) + 1e-9 && q.X >= std::min(p.X, r.X) - 1e-9 &&
               q.Y <= std::max(p.Y, r.Y) + 1e-9 && q.Y >= std::min(p.Y, r.Y) - 1e-9;
    };
    if (std::abs(o1) < 1e-9 && onSegment(a1, b1, a2)) return true;
    if (std::abs(o2) < 1e-9 && onSegment(a1, b2, a2)) return true;
    if (std::abs(o3) < 1e-9 && onSegment(b1, a1, b2)) return true;
    if (std::abs(o4) < 1e-9 && onSegment(b1, a2, b2)) return true;
    return false;
}

static Point nearestPointOnPolygon(const Point &p, const Polygon &poly) {
    if (poly.size() < 3) return p;
    Point best = poly[0];
    double bestDist = distance(p, best);
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        double dx = poly[j].X - poly[i].X, dy = poly[j].Y - poly[i].Y;
        double len2 = dx*dx + dy*dy;
        if (len2 < 1e-10) continue;
        double t = std::max(0.0, std::min(1.0, ((p.X - poly[i].X)*dx + (p.Y - poly[i].Y)*dy) / len2));
        Point proj = {poly[i].X + t*dx, poly[i].Y + t*dy};
        double d = distance(p, proj);
        if (d < bestDist) { bestDist = d; best = proj; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// FreeSpaceRouter
// ---------------------------------------------------------------------------

static double signedArea(const Polygon &poly) { return polygonArea(poly); }

FreeSpaceRouter::FreeSpaceRouter(const Polygon &container,
    const std::vector<Polygon> &obstacles,
    const std::vector<Polygon> &preferredRoutes)
    : container_(container)
{
    auto boxOf = [](const Polygon &poly) {
        Box b{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
              -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
        for (const auto &p : poly) {
            b.minX = std::min(b.minX, p.X); b.maxX = std::max(b.maxX, p.X);
            b.minY = std::min(b.minY, p.Y); b.maxY = std::max(b.maxY, p.Y);
        }
        return b;
    };
    containerBox_ = boxOf(container_);
    for (const auto &o : obstacles) {
        if (o.size() < 3) continue;
        obstacles_.push_back(o);
        obstacleBoxes_.push_back(boxOf(o));
    }

    // Knoten: Ecken, an denen ein kürzester Weg abknicken kann. Beim Container
    // sind das die nach innen gerichteten (konkaven) Ecken, bei Hindernissen die
    // nach außen gerichteten (konvexen). Hindernisecken werden entlang der
    // Winkelhalbierenden um CLEARANCE in den freien Raum geschoben, damit der
    // Mäher nicht auf der Exclusion-Kante fährt. Containerecken bleiben auf dem
    // Perimeter: Verbindungen laufen dort wie bisher auf dem Rand und nicht in
    // dem Streifen, den "distance to border" frei halten soll.
    auto addCorners = [&](const Polygon &poly, bool isObstacle) {
        const size_t n = poly.size();
        if (n < 3) return;
        const double orient = signedArea(poly) >= 0 ? 1.0 : -1.0;
        for (size_t i = 0; i < n; i++) {
            const Point &u = poly[(i + n - 1) % n];
            const Point &v = poly[i];
            const Point &w = poly[(i + 1) % n];
            if (distance(u, v) < 1e-6 || distance(v, w) < 1e-6) continue;
            const double turn = crossProduct(u, v, w) * orient; // >0: konvex
            const bool convex = turn > 1e-12;
            const bool reflex = turn < -1e-12;
            if (isObstacle ? !convex : !reflex) continue;
            if (!isObstacle) {
                if (pointFree(v)) addNode(v, &u, &w);
                continue;
            }
            double ax = u.X - v.X, ay = u.Y - v.Y;
            double bx = w.X - v.X, by = w.Y - v.Y;
            const double la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
            ax /= la; ay /= la; bx /= lb; by /= lb;
            double dx = ax + bx, dy = ay + by;
            const double ld = std::sqrt(dx * dx + dy * dy);
            if (ld < 1e-9) continue;
            dx /= ld; dy /= ld;
            // (dx,dy) zeigt in den Innenwinkel der Ecke. Bei konvexen
            // Hindernisecken und konkaven Containerecken liegt der freie Raum
            // auf der Gegenseite. Der Abstand wird so skaliert, dass die Kanten
            // selbst CLEARANCE entfernt bleiben (begrenzt bei spitzen Winkeln).
            const double sinHalf = std::abs(ax * dy - ay * dx);
            const double d = CLEARANCE / std::max(sinHalf, 0.25);
            Point node{v.X - dx * d, v.Y - dy * d};
            if (pointFree(node)) addNode(node, &u, &w);
        }
    };
    addCorners(container_, false);
    for (const auto &o : obstacles_) addCorners(o, true);

    // Suchdraht: Punkte als zusätzliche Knoten, Kanten entlang des Drahts
    // werden mit halbem Gewicht bevorzugt.
    for (const auto &routePoly : preferredRoutes) {
        size_t prevIdx = std::numeric_limits<size_t>::max();
        for (const auto &p : routePoly) {
            if (!pointFree(p)) { prevIdx = std::numeric_limits<size_t>::max(); continue; }
            addNode(p, nullptr, nullptr);
            const size_t idx = nodes_.size() - 1;
            if (prevIdx != std::numeric_limits<size_t>::max()) {
                if (preferred_.size() < nodes_.size()) preferred_.resize(nodes_.size());
                preferred_[prevIdx].push_back(idx);
                preferred_[idx].push_back(prevIdx);
            }
            prevIdx = idx;
        }
    }
    primaryCount_ = nodes_.size();
    // Rückfallebene: alle Original-Ecken, die auf dem freien Rand liegen.
    auto addRawCorners = [&](const Polygon &poly) {
        const size_t n = poly.size();
        for (size_t i = 0; i < n; i++)
            if (pointFree(poly[i])) addNode(poly[i], &poly[(i + n - 1) % n], &poly[(i + 1) % n]);
    };
    addRawCorners(container_);
    for (const auto &o : obstacles_) addRawCorners(o);
    preferred_.resize(nodes_.size());
    visibility_.assign(nodes_.size() * nodes_.size(), -1);
}

void FreeSpaceRouter::addNode(const Point &p, const Point *prev, const Point *next) {
    nodes_.push_back(p);
    cornerPrev_.push_back(prev ? *prev : p);
    cornerNext_.push_back(next ? *next : p);
    hasCorner_.push_back(prev && next ? 1 : 0);
}

// Tangentenregel: Ein kürzester Weg knickt an einer Ecke nur ab, wenn die
// Linie zur Ecke das Polygon dort nur berührt, beide Nachbarecken also auf
// derselben Seite der Linie liegen. Kanten, die in das Polygon hineinschneiden
// würden, werden ohne teure Freiheitsprüfung verworfen.
bool FreeSpaceRouter::tangentAt(size_t node, const Point &other) const {
    if (!hasCorner_[node]) return true;
    const Point &p = nodes_[node];
    // Sinus des Winkels zwischen Linie und Nachbarkante; fast kollineare
    // Nachbarn (|sin| < 1e-3) gelten als Berührung, sonst kippt das Vorzeichen
    // bei Startpunkten auf der Verlängerung einer Kante durch Rundung.
    auto side = [&](const Point &q) {
        const double norm = distance(other, p) * distance(q, p);
        if (norm < 1e-12) return 0.0;
        const double s = crossProduct(other, p, q) / norm;
        return std::abs(s) < 1e-3 ? 0.0 : s;
    };
    return side(cornerPrev_[node]) * side(cornerNext_[node]) >= 0.0;
}

bool FreeSpaceRouter::pointFree(const Point &p) const {
    const double tolSq = BOUNDARY_TOL * BOUNDARY_TOL;
    if (!pointInPolygon(p, container_) && !pointOnBoundary(p, container_, tolSq)) return false;
    for (size_t i = 0; i < obstacles_.size(); i++) {
        const Box &b = obstacleBoxes_[i];
        if (p.X < b.minX || p.X > b.maxX || p.Y < b.minY || p.Y > b.maxY) continue;
        if (pointInPolygon(p, obstacles_[i]) && !pointOnBoundary(p, obstacles_[i], tolSq))
            return false;
    }
    return true;
}

bool FreeSpaceRouter::crossesProperly(const Point &a, const Point &b, const Polygon &poly) const {
    // Echte Kreuzung: Endpunkte jeweils strikt auf verschiedenen Seiten.
    // Berühren einer Ecke oder Laufen auf einer Kante zählt nicht.
    const double eps = 1e-9;
    for (size_t i = 0; i < poly.size(); i++) {
        const Point &c = poly[i];
        const Point &d = poly[(i + 1) % poly.size()];
        const double o1 = crossProduct(a, b, c), o2 = crossProduct(a, b, d);
        if (!((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps))) continue;
        const double o3 = crossProduct(c, d, a), o4 = crossProduct(c, d, b);
        if ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps)) return true;
    }
    return false;
}

// Parameter t (0..1 entlang a->b) aller Berührungen und Schnitte der Strecke
// mit den Kanten von poly, inklusive Durchgang durch Ecken und kollinearer
// Überlappung. Zwischen zwei aufeinanderfolgenden Parametern liegt die Strecke
// vollständig innerhalb oder außerhalb von poly.
static void collectTouchParams(const Point &a, const Point &b, const Polygon &poly,
    std::vector<double> &ts)
{
    const double rx = b.X - a.X, ry = b.Y - a.Y;
    const double len2 = rx * rx + ry * ry;
    if (len2 < 1e-18) return;
    const double eps = 1e-9;
    for (size_t i = 0; i < poly.size(); i++) {
        const Point &c = poly[i];
        const Point &d = poly[(i + 1) % poly.size()];
        const double sx = d.X - c.X, sy = d.Y - c.Y;
        const double qx = c.X - a.X, qy = c.Y - a.Y;
        const double denom = rx * sy - ry * sx;
        if (std::abs(denom) < 1e-12) {
            // Parallel: nur bei kollinearer Lage relevant.
            if (std::abs(qx * ry - qy * rx) > 1e-9 * std::sqrt(len2)) continue;
            for (const Point *e : {&c, &d}) {
                const double t = ((e->X - a.X) * rx + (e->Y - a.Y) * ry) / len2;
                if (t > -eps && t < 1 + eps) ts.push_back(std::min(1.0, std::max(0.0, t)));
            }
            continue;
        }
        const double t = (qx * sy - qy * sx) / denom;
        const double u = (qx * ry - qy * rx) / denom;
        if (t > -eps && t < 1 + eps && u > -eps && u < 1 + eps)
            ts.push_back(std::min(1.0, std::max(0.0, t)));
    }
}

bool FreeSpaceRouter::segmentFree(const Point &a, const Point &b) const {
    const double minX = std::min(a.X, b.X), maxX = std::max(a.X, b.X);
    const double minY = std::min(a.Y, b.Y), maxY = std::max(a.Y, b.Y);
    if (crossesProperly(a, b, container_)) return false;
    std::vector<size_t> near;
    for (size_t i = 0; i < obstacles_.size(); i++) {
        const Box &o = obstacleBoxes_[i];
        if (maxX < o.minX || minX > o.maxX || maxY < o.minY || minY > o.maxY) continue;
        if (crossesProperly(a, b, obstacles_[i])) return false;
        near.push_back(i);
    }
    // Ohne echte Kreuzung kann die Strecke den freien Raum nur dort verlassen,
    // wo sie eine Ecke oder Kante berührt (z. B. Sehne durch zwei Ecken eines
    // Hindernisses). Zwischen je zwei Berührungen ist der Zustand konstant, also
    // genügt dort ein Test in der Mitte.
    std::vector<double> ts{0.0, 1.0};
    collectTouchParams(a, b, container_, ts);
    for (size_t i : near) collectTouchParams(a, b, obstacles_[i], ts);
    std::sort(ts.begin(), ts.end());
    const double tolSq = BOUNDARY_TOL * BOUNDARY_TOL;
    for (size_t k = 0; k + 1 < ts.size(); k++) {
        if (ts[k + 1] - ts[k] < 1e-9) continue;
        const Point p = lerp(a, b, (ts[k] + ts[k + 1]) / 2);
        if (!pointInPolygon(p, container_) && !pointOnBoundary(p, container_, tolSq)) return false;
        for (size_t i : near)
            if (pointInPolygon(p, obstacles_[i]) && !pointOnBoundary(p, obstacles_[i], tolSq))
                return false;
    }
    return true;
}

bool FreeSpaceRouter::nodeVisible(size_t a, size_t b) const {
    const size_t n = nodes_.size();
    signed char &v = visibility_[a * n + b];
    if (v < 0) {
        v = segmentFree(nodes_[a], nodes_[b]) ? 1 : 0;
        visibility_[b * n + a] = v;
    }
    return v == 1;
}

bool FreeSpaceRouter::findRoute(const Point &from, const Point &to, Polygon &path, bool quick) const {
    if (segmentFree(from, to)) { path = {from, to}; return true; }
    // Schnell zuerst (versetzte Ecken, nur Tangenten). Knickpunkte, die keine
    // Polygonecke sind (sich berührende Exclusions), braucht die volle Suche.
    if (search(from, to, primaryCount_, true, path)) return true;
    if (quick) return false;
    if (search(from, to, primaryCount_, false, path)) return true;
    return nodes_.size() > primaryCount_ && search(from, to, nodes_.size(), false, path);
}

Polygon FreeSpaceRouter::route(const Point &from, const Point &to) const {
    Polygon path;
    if (findRoute(from, to, path)) return path;
    PP_LOG(0, "%sFreeSpaceRouter: no path (%.2f,%.2f)->(%.2f,%.2f)", _LOG_,
        from.X, from.Y, to.X, to.Y);
    return {from, to};
}

Polygon ConnectorRouting::route(const Point &from, const Point &to) const {
    Polygon path;
    for (const FreeSpaceRouter *area : areas) {
        if (!area->pointFree(from) || !area->pointFree(to)) continue;
        // Nur die schnelle Suche: Ist die Mähfläche zwischen den Punkten
        // unterbrochen, würde die vollständige Suche erst nach allen Kanten
        // aufgeben. Der Perimeter-Router findet den Weg dann direkt.
        if (area->findRoute(from, to, path, true)) return path;
    }
    if (perimeter) return perimeter->route(from, to);
    return {from, to};
}

bool FreeSpaceRouter::search(const Point &from, const Point &to, size_t nodeCount,
    bool tangentOnly, Polygon &path) const {
    // A* über den Sichtbarkeitsgraphen der ersten nodeCount Knoten;
    // Index n = from, n + 1 = to.
    const size_t n = nodeCount;
    const size_t src = n, dst = n + 1;
    auto pos = [&](size_t i) -> const Point & {
        return i == src ? from : (i == dst ? to : nodes_[i]);
    };
    std::vector<double> g(n + 2, std::numeric_limits<double>::max());
    std::vector<int> prev(n + 2, -1);
    std::vector<char> closed(n + 2, 0);
    std::vector<signed char> toVisible(n, -1);
    using Item = std::pair<double, size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[src] = 0.0;
    open.push({distance(from, to), src});

    while (!open.empty()) {
        const size_t u = open.top().second;
        open.pop();
        if (closed[u]) continue;
        closed[u] = 1;
        if (u == dst) break;
        const Point &pu = pos(u);

        auto relax = [&](size_t v, double w) {
            const double cand = g[u] + w;
            if (cand < g[v] - 1e-12) {
                g[v] = cand;
                prev[v] = (int)u;
                open.push({cand + distance(pos(v), to), v});
            }
        };

        if (u != src) {
            signed char &tv = toVisible[u];
            if (tv < 0) tv = segmentFree(pu, to) ? 1 : 0;
            if (tv == 1) relax(dst, distance(pu, to));
        }
        for (size_t v = 0; v < n; v++) {
            if (closed[v] || v == u) continue;
            // Tangentenregel nur zwischen zwei Eckknoten: Start- und Zielpunkte
            // liegen oft direkt auf einer Kante neben der Ecke, dort ist die
            // Regel zu streng.
            if (tangentOnly && u != src &&
                (!tangentAt(v, pu) || !tangentAt(u, nodes_[v]))) continue;
            const bool visible = u == src ? segmentFree(from, nodes_[v]) : nodeVisible(u, v);
            if (!visible) continue;
            double w = distance(pu, nodes_[v]);
            if (u != src && std::find(preferred_[u].begin(), preferred_[u].end(), v) != preferred_[u].end())
                w *= 0.5;
            relax(v, w);
        }
    }

    if (prev[dst] < 0) return false;
    path.clear();
    for (int u = (int)dst; u >= 0; u = prev[u]) path.push_back(pos((size_t)u));
    std::reverse(path.begin(), path.end());
    return true;
}

Polygon walkBoundaryWithHoles(const Point &from, const Point &to,
    const Polygon &outerBoundary,
    const std::vector<Polygon> &holes,
    const std::vector<Polygon> &preferredRoutes)
{
    FreeSpaceRouter router(outerBoundary, holes, preferredRoutes);
    return router.route(from, to);
}

static Point projectToBoundary(const Point &p, const Polygon &poly) {
    if (pointInPolygon(p, poly)) return p;
    Point best = p;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        double dx = poly[j].X - poly[i].X;
        double dy = poly[j].Y - poly[i].Y;
        double len2 = dx*dx + dy*dy;
        if (len2 < 0.001) continue;
        double t = std::max(0.0, std::min(1.0, ((p.X-poly[i].X)*dx + (p.Y-poly[i].Y)*dy) / len2));
        Point proj = {poly[i].X + t*dx, poly[i].Y + t*dy};
        double d = distance(p, proj);
        if (d < bestDist) { bestDist = d; best = proj; }
    }
    return best;
}

static Point projectToAreaBoundary(const Point &p, const std::vector<Polygon> &areas) {
    Point best = p;
    double bestDist = std::numeric_limits<double>::max();
    bool insideAny = false;
    for (const auto &area : areas) {
        if (area.size() < 3) continue;
        if (pointInPolygon(p, area)) { insideAny = true; break; }
        Point proj = projectToBoundary(p, area);
        double d = distance(p, proj);
        if (d < bestDist) { bestDist = d; best = proj; }
    }
    if (insideAny) return p;
    return best;
}

static Polygon pruneOutside(const Polygon &waypoints, const std::vector<Polygon> &areas) {
    if (areas.empty()) return waypoints;
    Polygon result;
    result.reserve(waypoints.size());
    for (const auto &wp : waypoints) {
        Point snapped = projectToAreaBoundary(wp, areas);
        snapped.tag = wp.tag;
        // A connector may end exactly at the first point of a Border lap.
        // Keep that coincident category transition: dropping the Border point
        // opens an otherwise closed lap because its closing point remains.
        if (result.empty() || distance(result.back(), snapped) > 0.01 ||
            result.back().tag != snapped.tag)
            result.push_back(snapped);
    }
    return result;
}

static Polygon pruneOutside(const Polygon &waypoints, const Polygon &area) {
    return pruneOutside(waypoints, std::vector<Polygon>{area});
}

// Remove near-duplicate points and redundant backtracking from a route.
// Backtracking happens when ring connectors visit a perimeter or hole edge
// that was already traversed earlier, forcing the mower to turn 180 degrees.
// A point is only dropped when the resulting shortcut stays inside the
// perimeter, so we never cut across concave corners or exclusion holes.
static Polygon simplifyRoute(const Polygon &route, const FreeSpaceRouter &router,
    double epsilon = 0.02) {
    if (route.size() < 3) return route;

    // A shortcut is only accepted when it stays inside the perimeter and
    // outside every exclusion.
    auto segmentInside = [&](const Point &from, const Point &to) {
        return router.segmentFree(from, to);
    };

    Polygon out;
    out.reserve(route.size());
    for (size_t i = 0; i < route.size(); i++) {
        // Skip near-duplicate consecutive points, unless the point is part of
        // a detour and the bypass to the next point would cut a corner.
        if (!out.empty() && distance(out.back(), route[i]) < epsilon &&
            out.back().tag != RouteTagBorder && route[i].tag != RouteTagBorder &&
            (i + 1 >= route.size() || segmentInside(out.back(), route[i + 1])))
            continue;
        // Skip a point when it lies almost on the straight line between its
        // neighbours (collinear within epsilon).  This removes intermediate
        // points on long straight segments without changing the path shape.
        if (out.size() >= 1 && i + 1 < route.size() &&
            out.back().tag != RouteTagBorder && route[i].tag != RouteTagBorder &&
            route[i + 1].tag != RouteTagBorder) {
            const Point &prev = out.back();
            const Point &next = route[i + 1];
            double cross = std::abs(crossProduct(prev, route[i], next));
            double len = distance(prev, next);
            if (len > 1e-9 && cross / len < epsilon * 0.5) {
                // The point is nearly on the segment prev->next; dropping it
                // does not change the travelled path.  Only accept the
                // shortcut when it stays inside the perimeter.
                if (segmentInside(prev, next))
                    continue;
            }
        }
        out.push_back(route[i]);
    }
    // Second pass: remove U-turns and partial backtracking that force the
    // mower to turn 180 degrees on the spot.
    // Two patterns are handled:
    //   1. A -> B -> A (exact return to a previous point)
    //   2. A -> B -> C where C lies on segment A-B (partial backtrack)
    Polygon cleaned;
    cleaned.reserve(out.size());
    for (size_t i = 0; i < out.size(); i++) {
        if (cleaned.size() >= 2) {
            const Point &a = cleaned[cleaned.size() - 2];
            const Point &b = cleaned[cleaned.size() - 1];

            // A closed Border lap intentionally returns to its first point.
            // It is not redundant backtracking and must retain every edge.
            if (a.tag == RouteTagBorder || b.tag == RouteTagBorder ||
                out[i].tag == RouteTagBorder) {
                cleaned.push_back(out[i]);
                continue;
            }

            // Pattern 1: exact return to A
            if (distance(a, out[i]) < epsilon && distance(b, out[i]) > epsilon) {
                // Only drop B when the shortcut from A to the current point
                // stays inside the perimeter.
                if (segmentInside(a, out[i])) {
                    cleaned.pop_back();
                    continue;
                }
            }

            // Pattern 2: partial backtrack where C lies on segment A-B.
            // The route went A -> B and then turned around, so C is between
            // A and B.  Dropping B and going directly from A to C removes the
            // backtracking without changing the covered path.
            double abx = b.X - a.X, aby = b.Y - a.Y;
            double acx = out[i].X - a.X, acy = out[i].Y - a.Y;
            double len2ab = abx * abx + aby * aby;
            if (len2ab > 1e-9) {
                double t = (acx * abx + acy * aby) / len2ab;
                if (t > 0.0 && t < 1.0) {
                    // Distance from C to line A-B
                    double projx = a.X + t * abx;
                    double projy = a.Y + t * aby;
                    double dx = out[i].X - projx;
                    double dy = out[i].Y - projy;
                    double distToLine = std::sqrt(dx * dx + dy * dy);
                    if (distToLine < epsilon) {
                        // C is on segment A-B.  Drop B and continue from A
                        // directly to C, but only when the shortcut stays
                        // inside the perimeter. C itself must stay: without
                        // it the next point would be joined to A unchecked.
                        if (segmentInside(a, out[i])) {
                            cleaned.pop_back();
                            cleaned.push_back(out[i]);
                            continue;
                        }
                    }
                }
            }
        }
        cleaned.push_back(out[i]);
    }
    return cleaned;
}

static bool segmentExitsPerimeter(const Point &from, const Point &to, const Polygon &perimeter);

// The double-corner (step notch) between offset rings is an inherent feature
// of the L-shaped connector: the connector follows the ring grid with two
// perpendicular edges of length == step.  Replacing these with a diagonal
// creates 45° corners that increase ring gaps, so the notch is kept as-is.

Polygon calculateRingsPattern(const Polygon &perimeter, const Polygon &areaToMow,
    const std::vector<Polygon> &holes, double width, const Point &startNear,
    const ConnectorRouting *routing, const Progress *progress)
{
    (void)perimeter;
    PP_LOG(0, "%scalculateRingsPattern width=%.3f holes=%d", _LOG_, width, (int)holes.size());
    if (areaToMow.size() < 3 || width < 0.001) return {};

    // Generate all parallel contour rings at once, each offset inward by
    // multiples of the (overlapping) line spacing.  This ensures the whole
    // area is covered uniformly instead of following a single shrinking path
    // that can leave large unmowed strips between widely separated rings.
    //
    // The spacing is Tune::ringSpacingFactor times the mowing width, so the
    // rings overlap slightly and no unmowed gaps appear when rings are
    // smoothed or clipped by exclusions.
    const double step = width * Tune::ringSpacingFactor;
    // Fortschritt: Ringe erzeugen (gemessen ~1/4 der Zeit), dann verbinden.
    // Die Zahl der Ringe ist vorab unbekannt; 2*Fläche/Umfang schätzt den
    // größten Versatz (exakt für Kreis und Quadrat).
    const Progress ringsProgress = progress ? progress->sub(0.0, 0.25) : Progress();
    const Progress connectProgress = progress ? progress->sub(0.25, 1.0) : Progress();
    double outline = 0.0;
    for (size_t i = 0; i < areaToMow.size(); i++)
        outline += distance(areaToMow[i], areaToMow[(i + 1) % areaToMow.size()]);
    const double estimatedLevels = outline > 0
        ? std::max(1.0, 2.0 * std::abs(polygonArea(areaToMow)) / outline / step) : 1.0;
    std::vector<Polygon> rings;
    double offset = 0.0;
    bool first = true;
    int ringLevel = 0;
    while (true) {
        std::vector<Polygon> offsets;
        if (first) {
            offsets = {areaToMow};
            first = false;
        } else {
            offsets = clipOffset(areaToMow, -offset);
        }
        if (offsets.empty()) break;
        bool anyValid = false;
        for (auto &ring : offsets) {
            if (ring.size() < 3) continue;
            if (std::abs(polygonArea(ring)) < 0.001) continue;

            // Progressive smoothing: inner rings gradually forget perimeter
            // details so long stretches become straight instead of copying
            // every indentation of the outer boundary.  Ring 0 is never
            // smoothed so the configured border distance is kept exactly.
            // A smoothed ring is only accepted when it does not come closer to
            // any exclusion hole than the unsmoothed ring, otherwise the
            // original ring is kept.
            if (ringLevel > 0) {
                double r = std::min(ringLevel * width * Tune::smoothingGrowth,
                                    width * Tune::smoothingMax);
                if (r > 0.001) {
                    auto closed = clipOffset(ring, r);
                    if (!closed.empty()) {
                        closed = clipOffset(closed[0], -r);
                        // Keep the largest resulting polygon so small
                        // artifacts from the closing operation are dropped.
                        Polygon best;
                        double bestArea = 0.0;
                        for (const auto &c : closed) {
                            double a = std::abs(polygonArea(c));
                            if (a > bestArea) { bestArea = a; best = c; }
                        }
                        if (best.size() >= 3) {
                            // Only accept the smoothed ring when it stays
                            // inside the original mowable area and no ring
                            // segment crosses an exclusion hole.  A segment
                            // check is necessary because the closing operation
                            // can pull a long straight edge across a hole even
                            // when all vertices are outside.
                            bool safe = true;
                            for (size_t i = 0; i + 1 < best.size() && safe; i++) {
                                for (const auto &hole : holes) {
                                    // Check the segment against every hole edge.
                                    for (size_t j = 0; j < hole.size() && safe; j++) {
                                        size_t k = (j + 1) % hole.size();
                                        if (segmentsIntersect(best[i], best[i + 1], hole[j], hole[k])) {
                                            safe = false;
                                        }
                                    }
                                    if (!safe) break;
                                    // Also test the segment midpoint: even
                                    // without an edge intersection the whole
                                    // segment could lie inside the hole.
                                    Point mid{(best[i].X + best[i + 1].X) / 2,
                                              (best[i].Y + best[i + 1].Y) / 2};
                                    if (pointInPolygon(mid, hole)) {
                                        safe = false;
                                        break;
                                    }
                                }
                            }
                            if (safe) {
                                auto clippedToArea = clipIntersect({best}, ring);
                                if (!clippedToArea.empty() &&
                                    std::abs(polygonArea(clippedToArea[0])) > 0.001) {
                                    ring = std::move(clippedToArea[0]);
                                }
                            }
                        }
                    }
                }
            }

            if (!holes.empty()) {
                auto clipped = clipDifference(ring, holes);
                for (auto &c : clipped) {
                    if (c.size() >= 3 && std::abs(polygonArea(c)) > 0.001) {
                        // Reject degenerate rings that only trace along hole
                        // boundaries.  When the remaining area collapses
                        // between holes, clipOffset can return a ring that
                        // follows the hole contour exactly; such a ring would
                        // drive the mower directly along the exclusion border.
                        bool onHoleBoundary = true;
                        const double boundaryTol = 0.02; // 2 cm
                        for (const auto &p : c) {
                            bool nearAny = false;
                            for (const auto &hole : holes) {
                                if (distance(p, nearestPointOnPolygon(p, hole)) < boundaryTol) {
                                    nearAny = true;
                                    break;
                                }
                            }
                            if (!nearAny) { onHoleBoundary = false; break; }
                        }
                        if (onHoleBoundary) continue;
                        rings.push_back(c);
                        anyValid = true;
                    }
                }
            } else {
                rings.push_back(ring);
                anyValid = true;
            }
        }
        if (!anyValid) break;
        offset += step;
        ringLevel++;
        ringsProgress.report(std::min(0.95, ringLevel / estimatedLevels));
    }

    if (rings.empty()) return {};

    // Sort rings by distance from startNear so the mower begins near its
    // current position and works inward.
    std::sort(rings.begin(), rings.end(), [&](const Polygon &a, const Polygon &b) {
        double da = distance(startNear, nearestPointOnPolygon(startNear, a));
        double db = distance(startNear, nearestPointOnPolygon(startNear, b));
        return da < db;
    });

    // Rotate each ring so it starts at the point nearest to the previous one,
    // then connect the rings using the same path-finding logic as the zigzag
    // pattern so connectors never cross exclusion holes or leave the perimeter.
    Point current = startNear;
    for (auto &ring : rings) {
        size_t best = nearestPointIndex(current, ring);
        Polygon rot;
        rot.reserve(ring.size());
        for (size_t i = 0; i < ring.size(); i++)
            rot.push_back(ring[(best + i) % ring.size()]);
        ring = std::move(rot);
        current = ring.back();
    }

    Polygon route;
    ringsProgress.report(1.0);
    connectPolysUsingPathFinding(route, rings, perimeter, {areaToMow}, holes, true, {}, routing,
        progress ? &connectProgress : nullptr);

    PP_LOG(0, "%scalculateRingsPattern done: %d points", _LOG_, route.size());
    return route;
}

std::vector<std::vector<Polygon>> computeBorderLapBoundaries(const Polygon &perimeter,
    const std::vector<Polygon> &holes, int laps, double width)
{
    std::vector<std::vector<Polygon>> result;
    if (laps <= 0 || perimeter.size() < 3) return result;

    Polygon currentBoundary = perimeter;
    for (int lap = 0; lap < laps; lap++) {
        // If we are supposed to mow exclusion borders, the lap boundary is the
        // outer perimeter with all exclusions cut out.  Offsetting this inward
        // then gives us separate polygons: one for the outer perimeter and one
        // for each exclusion, all at the correct lap distance.
        std::vector<Polygon> boundaries;
        if (!holes.empty()) {
            auto diff = clipDifference(currentBoundary, holes);
            for (const auto &p : diff) {
                if (p.size() >= 3) boundaries.push_back(p);
            }
        }
        if (boundaries.empty()) boundaries.push_back(currentBoundary);
        result.push_back(boundaries);

        // Prepare the next lap boundary by shrinking the current outer
        // perimeter inward.  Holes stay fixed so the inner lap polygons keep the
        // correct shape around the exclusions.
        if (lap + 1 < laps) {
            auto nextList = clipOffset(currentBoundary, -width);
            if (nextList.empty() || nextList[0].size() < 3) break;
            currentBoundary = nextList[0];
        }
    }
    return result;
}

Polygon addBorderLaps(const Polygon &perimeter, const std::vector<Polygon> &holes,
    int laps, bool ccw, const Point &startNear, double width,
    const FreeSpaceRouter *router)
{
    PP_LOG(0, "%saddBorderLaps laps=%d ccw=%d width=%.3f", _LOG_, laps, ccw, width);
    if (laps <= 0 || perimeter.size() < 3) return {};

    auto lapBoundaries = computeBorderLapBoundaries(perimeter, holes, laps, width);

    Polygon route;
    Point near = startNear;

    for (size_t lap = 0; lap < lapBoundaries.size(); lap++) {
        std::vector<Polygon> boundaries = lapBoundaries[lap];
        if (boundaries.empty()) break;

        // Sort boundaries so we start each lap from the closest polygon and
        // continue with the nearest remaining polygon.  This produces a
        // continuous-ish lap route that stays within the mowable area.
        sortSolutionPolygonsByDistance(boundaries, near);

        for (size_t b = 0; b < boundaries.size(); b++) {
            Polygon ring = boundaries[b];
            bool cw = isClockwise(ring);
            if (ccw == cw) std::reverse(ring.begin(), ring.end());

            size_t startIdx = nearestPointIndex(near, ring);
            Polygon ordered;
            for (size_t i = 0; i < ring.size(); i++) {
                size_t idx = (startIdx + i) % ring.size();
                if (ordered.empty() || distance(ordered.back(), ring[idx]) > 0.01)
                    ordered.push_back(ring[idx]);
            }
            if (!ordered.empty() && distance(ordered.back(), ordered[0]) > 0.01)
                ordered.push_back(ordered[0]);

            // Übergang von der vorherigen Runde: direkt nur, wenn die Linie im
            // freien Raum bleibt, sonst um Exclusions und Buchten herum.
            if (router && !route.empty() && !ordered.empty() &&
                distance(route.back(), ordered[0]) > 0.01) {
                Polygon conn = router->route(route.back(), ordered[0]);
                for (size_t k = 1; k + 1 < conn.size(); k++) {
                    if (distance(route.back(), conn[k]) <= 0.01) continue;
                    // Umweg innerhalb der Randrunden gehört zur Kategorie Rand
                    // (wie Umwege innerhalb eines Musters).
                    Point connector = conn[k];
                    connector.tag = RouteTagBorder;
                    route.push_back(connector);
                }
            }

            for (const auto &p : ordered) {
                if (route.empty() || distance(route.back(), p) > 0.01) {
                    Point lapPoint = p;
                    lapPoint.tag = RouteTagBorder;
                    route.push_back(lapPoint);
                }
            }

            if (!route.empty()) near = route.back();
        }
    }

    PP_LOG(0, "%saddBorderLaps done: %d points", _LOG_, route.size());
    return route;
}

void sortSolutionPolygonsByDistance(std::vector<Polygon> &solution, const Point &startPt) {
    std::vector<Polygon> sorted;
    sorted.reserve(solution.size());
    Point currentPos = startPt;

    while (!solution.empty()) {
        double minDist = 160000;
        size_t minPolyIdx = 0;
        bool reverse = false;

        for (size_t i = 0; i < solution.size(); i++) {
            size_t n = solution[i].size();
            if (n == 0) continue;

            double dFirst = distance(currentPos, solution[i][0]);
            double dLast = distance(currentPos, solution[i][n - 1]);
            if (dFirst < minDist) {
                minDist = dFirst; minPolyIdx = i; reverse = false;
            }
            if (dLast < minDist) {
                minDist = dLast; minPolyIdx = i; reverse = true;
            }
        }

        Polygon poly = solution[minPolyIdx];
        solution.erase(solution.begin() + minPolyIdx);

        if (reverse) {
            Polygon rev;
            rev.reserve(poly.size());
            for (int i = (int)poly.size() - 1; i >= 0; i--)
                rev.push_back(poly[i]);
            poly = rev;
        }

        sorted.push_back(poly);
        if (!poly.empty()) currentPos = poly.back();
    }

    solution = sorted;
}

static void addEdgeEvents(const Point &a, const Point &b,
    const Point &c, const Point &d, std::vector<double> &ts)
{
    double dx1 = b.X - a.X, dy1 = b.Y - a.Y;
    double dx2 = d.X - c.X, dy2 = d.Y - c.Y;
    double denom = dx1 * dy2 - dy1 * dx2;

    auto proj = [&](const Point &p) -> double {
        double len2 = dx1 * dx1 + dy1 * dy1;
        if (len2 < 1e-14) return 0.0;
        return ((p.X - a.X) * dx1 + (p.Y - a.Y) * dy1) / len2;
    };

    if (std::abs(denom) < 1e-12) {
        double cross = (c.X - a.X) * dy1 - (c.Y - a.Y) * dx1;
        if (std::abs(cross) > 1e-9) return; // parallel, not collinear
        double tC = proj(c);
        double tD = proj(d);
        double t1 = std::max(0.0, std::min(tC, tD));
        double t2 = std::min(1.0, std::max(tC, tD));
        if (t2 > t1 + 1e-9) {
            ts.push_back(t1);
            ts.push_back(t2);
        }
        return;
    }

    double t = ((c.X - a.X) * dy2 - (c.Y - a.Y) * dx2) / denom;
    double u = ((c.X - a.X) * dy1 - (c.Y - a.Y) * dx1) / denom;
    if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9) return;
    if (t >= 0.0 && t <= 1.0) ts.push_back(t);
}

std::vector<Polygon> clipSegmentsAgainstHoles(const std::vector<Polygon> &segments,
    const std::vector<Polygon> &holes)
{
    std::vector<Polygon> result;
    for (const auto &seg : segments) {
        if (seg.size() < 2) continue;
        for (size_t i = 0; i + 1 < seg.size(); i++) {
            const Point &a = seg[i];
            const Point &b = seg[i + 1];
            if (distance(a, b) < 1e-9) continue;

            std::vector<double> ts;
            for (const auto &hole : holes) {
                if (hole.size() < 3) continue;
                for (size_t j = 0; j < hole.size(); j++) {
                    size_t k = (j + 1) % hole.size();
                    addEdgeEvents(a, b, hole[j], hole[k], ts);
                }
                if (pointInPolygon(a, hole) || pointOnBoundary(a, hole, 1e-6)) ts.push_back(0.0);
                if (pointInPolygon(b, hole) || pointOnBoundary(b, hole, 1e-6)) ts.push_back(1.0);
            }

            if (ts.empty()) {
                Point mid = lerp(a, b, 0.5);
                bool inHole = false;
                for (const auto &hole : holes) {
                    if (pointInPolygon(mid, hole) || pointOnBoundary(mid, hole, 1e-6)) {
                        inHole = true; break;
                    }
                }
                if (!inHole) result.push_back({a, b});
                continue;
            }

            std::sort(ts.begin(), ts.end());
            std::vector<double> uniq;
            for (double t : ts) {
                if (uniq.empty() || t > uniq.back() + 1e-9) uniq.push_back(t);
            }

            std::vector<std::pair<double, double>> insideIntervals;
            for (size_t j = 0; j + 1 < uniq.size(); j++) {
                if (uniq[j + 1] - uniq[j] < 1e-9) continue;
                double tmid = (uniq[j] + uniq[j + 1]) * 0.5;
                Point pmid = lerp(a, b, tmid);
                bool inHole = false;
                for (const auto &hole : holes) {
                    if (pointInPolygon(pmid, hole) || pointOnBoundary(pmid, hole, 1e-6)) {
                        inHole = true; break;
                    }
                }
                if (inHole) insideIntervals.push_back({uniq[j], uniq[j + 1]});
            }

            std::sort(insideIntervals.begin(), insideIntervals.end());
            std::vector<std::pair<double, double>> merged;
            for (const auto &iv : insideIntervals) {
                if (merged.empty() || iv.first > merged.back().second + 1e-9) {
                    merged.push_back(iv);
                } else if (iv.second > merged.back().second) {
                    merged.back().second = iv.second;
                }
            }

            double last = 0.0;
            for (const auto &iv : merged) {
                if (iv.first > last + 1e-9)
                    result.push_back({lerp(a, b, last), lerp(a, b, iv.first)});
                last = std::max(last, iv.second);
            }
            if (last < 1.0 - 1e-9)
                result.push_back({lerp(a, b, last), b});
        }
    }
    return result;
}

static bool segmentExitsPerimeter(const Point &from, const Point &to, const Polygon &perimeter) {
    double segLen = distance(from, to);
    if (segLen < 1e-6) return false;
    int samples = std::max(5, (int)(segLen / 0.005));
    if (samples > 200) samples = 200;
    for (int k = 0; k <= samples; k++) {
        double t = (double)k / samples;
        Point p{from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t};
        if (!pointInPolygon(p, perimeter) && !pointOnBoundary(p, perimeter, 1e-6))
            return true;
    }
    return false;
}


static bool segmentStaysInAreas(const Point &from, const Point &to,
    const std::vector<Polygon> &areas)
{
    if (areas.empty()) return true;
    double segLen = distance(from, to);
    if (segLen < 1e-6) return true;
    int samples = std::max(5, (int)(segLen / 0.02));
    if (samples > 100) samples = 100;
    for (int k = 0; k <= samples; k++) {
        double t = (double)k / samples;
        Point p{from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t};
        bool insideAny = false;
        for (const auto &area : areas) {
            if (pointInPolygon(p, area) || pointOnBoundary(p, area, 1e-6)) {
                insideAny = true;
                break;
            }
        }
        if (!insideAny) return false;
    }
    return true;
}

static double pathTotalLength(const std::vector<Polygon> &paths) {
    double len = 0;
    for (const auto &p : paths)
        for (size_t i = 0; i + 1 < p.size(); i++)
            len += distance(p[i], p[i + 1]);
    return len;
}

void connectPolysUsingPathFinding(Polygon &waypoints, const std::vector<Polygon> &polys,
    const Polygon &perimeter, const std::vector<Polygon> &areasToMow,
    const std::vector<Polygon> &holes, bool ringsMode,
    const std::vector<Polygon> &preferredRoutes,
    const ConnectorRouting *routing, const Progress *progress) {
    waypoints.clear();

    // Track ring closing edges that must be inserted after all connectors
    // are computed.  This avoids shifting the connector start point from
    // poly[n-1] to poly[0], which would change the L-connector geometry.
    struct Closure { size_t after; Point pt; };
    std::vector<Closure> closures;

    for (size_t i = 0; i < polys.size(); i++) {
        if (progress) progress->report((double)i / polys.size());
        const auto &poly = polys[i];
        if (poly.empty()) continue;

        if (i > 0) {
            const Point &from = waypoints.back();
            const Point &to = poly[0];
            double d = distance(from, to);
            const uint8_t connectorTag = from.tag != 0 && from.tag == to.tag
                ? from.tag : RouteTagConnector;

            // Axis-aligned (L-shaped) connectors avoid 45° corners between
            // concentric square rings.  Clipper2's integer arithmetic handles
            // boundary precision correctly, unlike floating-point checks.
            // Only applied for rings; zigzag segments need direct connectors.
            bool usedLShape = false;
            if (ringsMode && d > 0.01 && std::abs(from.X - to.X) > 0.01 &&
                             std::abs(from.Y - to.Y) > 0.01) {
                const Point mid1 = {from.X, to.Y};
                const Point mid2 = {to.X, from.Y};
                const double pathLen1 = distance(from, mid1) + distance(mid1, to);
                const double pathLen2 = distance(from, mid2) + distance(mid2, to);
                const Point candidates[2] = {
                    pathLen1 <= pathLen2 ? mid1 : mid2,
                    pathLen1 <= pathLen2 ? mid2 : mid1
                };
                for (int ci = 0; ci < 2 && !usedLShape; ci++) {
                    const Point &mid = candidates[ci];
                    Polygon lpath = {from, mid, to};
                    double plen = distance(from, mid) + distance(mid, to);
                    auto clipped = clipIntersectOpen({lpath}, areasToMow);
                    if (!holes.empty()) {
                        auto diff = clipDifferenceOpen({lpath}, holes);
                        if (pathTotalLength(diff) < plen - 1e-3)
                            continue;
                    }
                    double clen = pathTotalLength(clipped);
                    if (clen > plen - 1e-3) {
                        Point connector = mid;
                        connector.tag = connectorTag;
                        waypoints.push_back(connector);
                        waypoints.push_back(to);
                        usedLShape = true;
                    }
                }
            }

            if (!usedLShape) {
                bool exitsPerimeter = d > 0.001 && segmentExitsPerimeter(from, to, perimeter);
                bool leavesMowArea = d > 0.001 && !segmentStaysInAreas(from, to, areasToMow);
                bool crossesHole = false;
                if (!holes.empty()) {
                    std::vector<Polygon> connSeg = clipSegmentsAgainstHoles({{from, to}}, holes);
                    if (connSeg.empty() || connSeg[0].size() < 2 ||
                        distance(connSeg[0][0], connSeg[0].back()) < d - 1e-6) {
                        crossesHole = true;
                    }
                }
                if (exitsPerimeter || leavesMowArea || crossesHole) {
                    Polygon conn = routing ? routing->route(from, to)
                        : walkBoundaryWithHoles(from, to, perimeter, holes, preferredRoutes);
                    for (size_t k = 0; k < conn.size(); k++) {
                        if (distance(waypoints.back(), conn[k]) <= 0.01) continue;
                        Point connector = conn[k];
                        if (distance(connector, to) > 0.01) connector.tag = connectorTag;
                        waypoints.push_back(connector);
                    }
                }
            }
        }

        for (size_t j = 0; j < poly.size(); j++)
            if (waypoints.empty() || distance(waypoints.back(), poly[j]) > 0.01)
                waypoints.push_back(poly[j]);

        // Defer closing-edge insertion for rings so the next ring's
        // connector still starts from poly[n-1] (the natural ring end).
        if (ringsMode && poly.size() >= 2 &&
            distance(waypoints.back(), poly[0]) > 0.01)
            closures.push_back({waypoints.size() - 1, poly[0]});
    }

    // Insert closing edges after all connectors are computed.
    // Process in reverse order so earlier indices remain valid.
    for (auto it = closures.rbegin(); it != closures.rend(); ++it)
        waypoints.insert(waypoints.begin() + it->after + 1, it->pt);
}

MowableAreas computeMowableAreas(const Polygon &perimeter, const std::vector<Polygon> &exclusions,
    const Settings &settings)
{
    MowableAreas result;
    std::vector<Polygon> &areasToMow = result.areasToMow;
    std::vector<Polygon> &mowableRegion = result.mowableRegion;

    if (exclusions.empty()) {
      if (settings.distanceToBorder > 0 && settings.width > 0.001) {
        double offsetDist = settings.distanceToBorder * settings.width;
        areasToMow = offsetPolygonInward(perimeter, offsetDist);
      }
      mowableRegion = areasToMow.empty() ? std::vector<Polygon>{perimeter} : areasToMow;
    } else {
      // If the user does not want to mow the exclusion border, keep the
      // mowable area one full mowing width away from each exclusion so the
      // generated rings do not graze the exclusion boundary and so the
      // unmowed buffer around each exclusion is at most one mower width wide.
      std::vector<Polygon> effectiveExclusions = exclusions;
      if (!settings.mowExclusionBorder) {
        double exclusionMargin = settings.width * Tune::exclusionMarginFactor;
        if (exclusionMargin < 1e-3) exclusionMargin = 1e-3;
        std::vector<Polygon> expanded;
        for (const auto &excl : exclusions) {
          if (excl.size() < 3) continue;
          auto e = clipOffset(excl, exclusionMargin);
          for (const auto &p : e) {
            if (polygonArea(p) > 0.0) expanded.push_back(p);
            else expanded.push_back(reversePolygon(p));
          }
        }
        if (!expanded.empty()) effectiveExclusions = std::move(expanded);
      }
      mowableRegion = clipDifference(perimeter, effectiveExclusions);
      if (mowableRegion.empty()) {
        PP_LOG(0, "%sExclusions removed entire perimeter, nothing to mow", _LOG_);
        return result;
      }
      if (settings.distanceToBorder > 0 && settings.width > 0.001) {
        double offsetDist = settings.distanceToBorder * settings.width;
        for (const auto &area : mowableRegion) {
          if (polygonArea(area) < 0.0) continue; // skip holes
          auto offset = offsetPolygonInward(area, offsetDist);
          areasToMow.insert(areasToMow.end(), offset.begin(), offset.end());
        }
      } else {
        for (const auto &area : mowableRegion)
          if (polygonArea(area) > 0.0) areasToMow.push_back(area);
      }
    }
    if (areasToMow.empty()) areasToMow = {perimeter};
    for (auto it = areasToMow.begin(); it != areasToMow.end(); )
        if (it->size() < 3 || polygonArea(*it) < 0.0) it = areasToMow.erase(it); else ++it;

    // Collect exclusion holes for connector checks (negative-area polygons in mowableRegion).
    // Inflate holes by a tiny numeric epsilon so that ring vertices produced by
    // clipping do not land exactly on the hole boundary, which would be
    // flagged as "inside" by downstream point-in-polygon tests.
    std::vector<Polygon> holes;
    for (const auto &poly : mowableRegion)
        if (polygonArea(poly) < 0.0) holes.push_back(poly);
    {
        std::vector<Polygon> inflatedHoles;
        const double inflateBy = 0.015; // 15 mm safety buffer
        for (const auto &hole : holes) {
            auto expanded = clipOffset(reversePolygon(hole), inflateBy);
            for (auto &p : expanded) {
                if (polygonArea(p) < 0.0) inflatedHoles.push_back(p);
                else inflatedHoles.push_back(reversePolygon(p));
            }
        }
        holes = std::move(inflatedHoles);
    }
    result.holes = std::move(holes);
    return result;
}

Polygon calculateWaypoints(Map &map, Settings &settings, const State *state,
    const ProgressCallback &progressCallback) {
    // Prozentspannen der Abschnitte (gemessen: das Muster braucht ~90 % der Zeit).
    int lastPercent = -1;
    const Progress progress(&progressCallback, &lastPercent);
    const Progress patternProgress = progress.sub(0.03, 0.85);
    const Progress borderProgress = progress.sub(0.85, 0.92);
    const Progress safetyProgress = progress.sub(0.92, 0.99);
    progress.report(0.0);

    PP_LOG(0, "%scalculateWaypoints pattern=%d width=%.3f angle=%d distToBorder=%d borderLaps=%d",
        _LOG_, settings.pattern, settings.width, settings.angle,
        settings.distanceToBorder, settings.borderLaps);

    Polygon perimeter = map.perimeter;
    if (perimeter.size() < 3) {
        PP_LOG(0, "%sPerimeter too small", _LOG_);
        return {};
    }

    Polygon route;
    Point startNear = perimeter[0];

    if (state != nullptr) {
        const int JOB_DOCK = 4;
        if (state->job == JOB_DOCK && !map.dockpoints.empty()) {
            const auto &dp = map.dockpoints.back();
            startNear = Point{dp.X, dp.Y};
            PP_LOG(0, "%sstartNear set to dock line end (%.3f, %.3f)", _LOG_, startNear.X, startNear.Y);
        } else if (state->position.solution > 0 &&
                   (state->position.x != 0.0 || state->position.y != 0.0)) {
            startNear = Point{state->position.x, state->position.y};
            PP_LOG(0, "%sstartNear set to GPS position (%.3f, %.3f)", _LOG_, startNear.X, startNear.Y);
        }
    }

    MowableAreas mow = computeMowableAreas(perimeter, map.exclusions, settings);
    if (mow.mowableRegion.empty()) {
        return {};
    }
    std::vector<Polygon> &areasToMow = mow.areasToMow;
    std::vector<Polygon> &mowableRegion = mow.mowableRegion;
    std::vector<Polygon> &holes = mow.holes;
    if (areasToMow.empty()) return {};
    const std::vector<Polygon> preferredRoutes = map.searchWire.size() >= 2
        ? std::vector<Polygon>{map.searchWire} : std::vector<Polygon>{};

    // Ein Router für alle Verbindungen dieser Berechnung: bleibt im Perimeter
    // und außerhalb der (originalen) Exclusions.
    std::vector<Polygon> exclusionObstacles;
    for (const auto &ex : map.exclusions)
        if (ex.size() >= 3) exclusionObstacles.push_back(ex);
    const FreeSpaceRouter router(perimeter, exclusionObstacles, preferredRoutes);
    // Verbindungen innerhalb des Mähmusters bleiben bevorzugt in der Mähfläche
    // (Randabstand) und außerhalb der aufgeweiteten Exclusion-Löcher.
    std::vector<FreeSpaceRouter> areaRouters;
    areaRouters.reserve(areasToMow.size());
    for (const auto &area : areasToMow) areaRouters.emplace_back(area, holes, preferredRoutes);
    ConnectorRouting routing;
    routing.perimeter = &router;
    for (const auto &r : areaRouters) routing.areas.push_back(&r);

    if (settings.borderLaps > 0 && settings.mowBorderCcw) {
        std::vector<Polygon> borderHoles;
        if (settings.mowExclusionBorder) {
            for (const auto &ex : map.exclusions) {
                if (ex.size() >= 3) borderHoles.push_back(ex);
            }
        }
        Polygon borderLaps = addBorderLaps(perimeter, borderHoles, settings.borderLaps, true, startNear,
            settings.width, &router);
        route = borderLaps;
        if (!route.empty()) startNear = route.back();
    }

    if (settings.mowArea) {
        std::vector<Polygon> allSegments;

        if (settings.pattern == 2) {
            // Fortschritt je Fläche nach Flächenanteil.
            double totalArea = 0.0, doneArea = 0.0;
            for (const auto &area : areasToMow) totalArea += std::abs(polygonArea(area));
            for (const auto &area : areasToMow) {
                const double share = totalArea > 0 ? std::abs(polygonArea(area)) / totalArea : 1.0;
                const Progress areaProgress = patternProgress.sub(doneArea, doneArea + share);
                doneArea += share;
                Polygon ar = calculateRingsPattern(perimeter, area, holes, settings.width, startNear, &routing,
                    &areaProgress);
                for (auto &point : ar) point.tag = RouteTagArea;
                if (!ar.empty()) allSegments.push_back(ar);
            }
        } else {
            int passes = (settings.pattern == 1) ? 2 : 1;
            for (int pass = 0; pass < passes; pass++) {
                // Je Durchgang: Schneiden (schnell), dann Verbinden.
                const Progress passProgress = patternProgress.sub((double)pass / passes,
                    (double)(pass + 1) / passes);
                const Progress passConnect = passProgress.sub(0.1, 1.0);
                passProgress.report(0.0);
                double angleDeg = settings.angle + (pass == 1 ? 90.0 : 0.0);

                auto rotatedAreas = rotatePolygons(areasToMow, -angleDeg);
                auto rotatedMowableRegion = rotatePolygons(mowableRegion, -angleDeg);

                // Identify the outer boundary of the mowable region.
                // Clipper2 Difference returns holes as separate negative-area polygons,
                // but we already collected (and inflated) them above for clipping.
                Polygon outerBoundary;
                for (const auto &poly : rotatedMowableRegion) {
                    if (polygonArea(poly) > 0.0) outerBoundary = poly;
                }
                if (outerBoundary.empty() && !rotatedMowableRegion.empty())
                    outerBoundary = rotatedMowableRegion[0];

                double rMinX, rMinY, rMaxX, rMaxY;
                boundingBox(rotatedAreas[0], rMinX, rMinY, rMaxX, rMaxY);
                for (size_t ai = 1; ai < rotatedAreas.size(); ai++) {
                    double ax, ay, bx, by;
                    boundingBox(rotatedAreas[ai], ax, ay, bx, by);
                    if (ax < rMinX) rMinX = ax;
                    if (ay < rMinY) rMinY = ay;
                    if (bx > rMaxX) rMaxX = bx;
                    if (by > rMaxY) rMaxY = by;
                }

                double margin = settings.width * 2;
                double xRange = std::max(std::abs(rMinX), std::abs(rMaxX)) + margin;
                double lastX = -xRange;
                Polygon zigzag;
                for (double y = rMinY - margin; y <= rMaxY + margin; y += settings.width) {
                    zigzag.push_back({lastX, y});
                    zigzag.push_back({-lastX, y});
                    lastX = -lastX;
                }

                // Rotate the already-inflated holes into the pass coordinate system.
                std::vector<Polygon> rotatedHoles = rotatePolygons(holes, -angleDeg);

                // Clip open zigzag against the mowable areas (already
                // exclusion-free). This is more robust than clipping against
                // the outer boundary and then subtracting holes separately,
                // because some long segments may pass through the hole if the
                // open-path intersection does not split them correctly.
                auto clipped = clipIntersectOpen({zigzag}, rotatedAreas);
                // Keep the hole subtraction as a safety net for any segments
                // that still cross an inflated hole.
                if (!rotatedHoles.empty()) {
                    clipped = clipSegmentsAgainstHoles(clipped, rotatedHoles);
                }
                auto passSegments = rotatePolygons(clipped, angleDeg);

                if (!passSegments.empty()) {
                    for (auto &segment : passSegments)
                        for (auto &point : segment) point.tag = RouteTagArea;
                    sortSolutionPolygonsByDistance(passSegments, startNear);
                    Polygon passRoute;
                    passProgress.report(0.1);
                    connectPolysUsingPathFinding(passRoute, passSegments, perimeter, areasToMow, holes, false,
                        preferredRoutes, &routing, &passConnect);
                    passRoute = pruneOutside(passRoute, areasToMow);
                    if (!passRoute.empty()) allSegments.push_back(passRoute);
                }
            }
        }

        if (!allSegments.empty()) {
            sortSolutionPolygonsByDistance(allSegments, startNear);
            Polygon pattern;
            connectPolysUsingPathFinding(pattern, allSegments, perimeter, areasToMow, holes, false,
                preferredRoutes, &routing);
            pattern = pruneOutside(pattern, areasToMow);

            if (!route.empty() && !pattern.empty()) {
                Polygon conn = router.route(route.back(), pattern[0]);
                for (size_t k = 0; k < conn.size(); k++) {
                    if (distance(route.back(), conn[k]) <= 0.01) continue;
                    Point connector = conn[k];
                    if (distance(connector, pattern[0]) > 0.01) connector.tag = RouteTagConnector;
                    route.push_back(connector);
                }
            }
            route.insert(route.end(), pattern.begin(), pattern.end());
            if (!route.empty()) startNear = route.back();
        }
    }

    patternProgress.report(1.0);
    if (settings.borderLaps > 0 && !settings.mowBorderCcw) {
        std::vector<Polygon> borderHoles;
        if (settings.mowExclusionBorder) {
            for (const auto &ex : map.exclusions) {
                if (ex.size() >= 3) borderHoles.push_back(ex);
            }
        }
        Polygon borderLaps = addBorderLaps(perimeter, borderHoles, settings.borderLaps, false, startNear,
            settings.width, &router);
        if (!route.empty() && !borderLaps.empty()) {
            Polygon conn = router.route(route.back(), borderLaps[0]);
            for (size_t k = 0; k < conn.size(); k++) {
                if (distance(route.back(), conn[k]) <= 0.01) continue;
                Point connector = conn[k];
                if (distance(connector, borderLaps[0]) > 0.01) connector.tag = RouteTagConnector;
                route.push_back(connector);
            }
        }
        route.insert(route.end(), borderLaps.begin(), borderLaps.end());
    }

    borderProgress.report(1.0);
    route = pruneOutside(route, perimeter);

    // Final safety pass: ensure no route segment leaves the perimeter or runs
    // through an exclusion. This can happen for connector segments produced by
    // different pattern stages (border laps, rings, zigzag) when the direct
    // line cuts across a concave part of the perimeter or across exclusions.
    Polygon safeRoute;
    for (size_t i = 0; i < route.size(); i++) {
        if ((i & 63) == 0) safetyProgress.report((double)i / route.size());
        if (i == 0) {
            safeRoute.push_back(route[i]);
            continue;
        }
        const Point &from = safeRoute.back();
        const Point &to = route[i];
        if (distance(from, to) < 0.001 && from.tag == to.tag) continue;

        if (!router.segmentFree(from, to)) {
            Polygon conn = router.route(from, to);
            for (const auto &p : conn) {
                if (safeRoute.empty() || distance(safeRoute.back(), p) > 0.01) {
                    Point detour = p;
                    // A safety detour inside one route category is part of
                    // that category. Only a transition between two different
                    // categories remains a mixed connector.
                    if (distance(detour, to) > 0.01) {
                        detour.tag = from.tag != 0 && from.tag == to.tag
                            ? from.tag : RouteTagConnector;
                    }
                    safeRoute.push_back(detour);
                }
            }
        }
        safeRoute.push_back(to);
    }
    route = std::move(safeRoute);

    // Remove redundant points and backtracking so perimeter edges are not
    // traversed multiple times and no 180-degree turns remain on the border.
    route = simplifyRoute(route, router, settings.simplifyEpsilon);

    progress.report(1.0);
    PP_LOG(0, "%scalculateWaypoints done: %d waypoints", _LOG_, route.size());
    return route;
}

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
