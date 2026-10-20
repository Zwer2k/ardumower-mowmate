// Zusammensetzen eines Mähauftrags aus den Routen mehrerer Bereiche.
#include "pathplanner.h"

#include <algorithm>
#include <cmath>

namespace ArduMower {
namespace Modem {
namespace PathPlannerCore {

namespace {

// Reststücke darunter lohnen die Anfahrt nicht (m)
constexpr double MIN_PIECE_LENGTH = 0.05;

bool inRegion(const Point &p, const std::vector<Polygon> &region) {
    if (region.empty()) return true;  // ganze Karte
    for (const auto &poly : region)
        if (!pointInPolygon(p, poly)) return false;
    return true;
}

bool covered(const Point &p, const std::vector<const std::vector<Polygon> *> &earlier) {
    for (const auto *region : earlier)
        if (inRegion(p, *region)) return true;
    return false;
}

// Parameter t in (0, 1) der Schnittpunkte von a-b mit der Kante c-d
void edgeHits(const Point &a, const Point &b, const Point &c, const Point &d, std::vector<double> &ts) {
    const double rx = b.X - a.X, ry = b.Y - a.Y;
    const double sx = d.X - c.X, sy = d.Y - c.Y;
    const double den = rx * sy - ry * sx;
    if (std::fabs(den) < 1e-12) return;
    const double qx = c.X - a.X, qy = c.Y - a.Y;
    const double t = (qx * sy - qy * sx) / den;
    const double u = (qx * ry - qy * rx) / den;
    if (t > 1e-9 && t < 1.0 - 1e-9 && u >= -1e-9 && u <= 1.0 + 1e-9) ts.push_back(t);
}

Point lerp(const Point &a, const Point &b, double t, uint8_t tag) {
    return Point{a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, tag};
}

bool samePoint(const Point &a, const Point &b) {
    return std::fabs(a.X - b.X) < 1e-6 && std::fabs(a.Y - b.Y) < 1e-6;
}

double pieceLength(const Polygon &piece) {
    double len = 0;
    for (size_t i = 1; i < piece.size(); i++) len += distance(piece[i - 1], piece[i]);
    return len;
}

// Die Teile der Route außerhalb der früheren Bereiche, in Fahrtrichtung
std::vector<Polygon> uncoveredPieces(const Polygon &route,
    const std::vector<const std::vector<Polygon> *> &earlier)
{
    std::vector<Polygon> pieces;
    Polygon current;
    auto close = [&]() {
        if (current.size() >= 2 && pieceLength(current) >= MIN_PIECE_LENGTH) pieces.push_back(std::move(current));
        current.clear();
    };
    if (route.size() == 1) {
        if (!covered(route[0], earlier)) pieces.push_back(route);
        return pieces;
    }
    std::vector<double> ts;
    for (size_t i = 1; i < route.size(); i++) {
        const Point &a = route[i - 1];
        const Point &b = route[i];
        ts.clear();
        ts.push_back(0.0);
        for (const auto *region : earlier)
            for (const auto &poly : *region)
                for (size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++)
                    edgeHits(a, b, poly[j], poly[k], ts);
        ts.push_back(1.0);
        std::sort(ts.begin(), ts.end());
        for (size_t k = 1; k < ts.size(); k++) {
            const double t0 = ts[k - 1], t1 = ts[k];
            if (t1 - t0 < 1e-9) continue;
            if (covered(lerp(a, b, (t0 + t1) / 2, 0), earlier)) {
                close();
                continue;
            }
            const Point start = t0 == 0.0 ? a : lerp(a, b, t0, b.tag);
            const Point end = t1 == 1.0 ? b : lerp(a, b, t1, b.tag);
            if (current.empty() || !samePoint(current.back(), start)) {
                close();
                current.push_back(start);
            }
            current.push_back(end);
        }
    }
    close();
    return pieces;
}

} // namespace

Polygon composeJob(const std::vector<JobPart> &parts, const FreeSpaceRouter &router,
    bool overlapOnce, uint8_t transitTag, std::vector<size_t> *partStarts)
{
    Polygon out;
    if (partStarts) partStarts->clear();
    std::vector<const std::vector<Polygon> *> earlier;
    auto append = [&](const Polygon &piece) {
        if (piece.empty()) return;
        if (!out.empty() && !samePoint(out.back(), piece.front())) {
            const Polygon path = router.route(out.back(), piece.front());
            for (size_t k = 1; k + 1 < path.size(); k++) {
                Point p = path[k];
                p.tag = transitTag;
                out.push_back(p);
            }
        }
        for (const auto &p : piece)
            if (out.empty() || !samePoint(out.back(), p)) out.push_back(p);
    };
    for (const auto &part : parts) {
        if (partStarts) partStarts->push_back(out.size());
        if (overlapOnce && !earlier.empty()) {
            for (const auto &piece : uncoveredPieces(part.route, earlier)) append(piece);
        } else {
            append(part.route);
        }
        if (overlapOnce) earlier.push_back(&part.region);
    }
    return out;
}

} // namespace PathPlannerCore
} // namespace Modem
} // namespace ArduMower
