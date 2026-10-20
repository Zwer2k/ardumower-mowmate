#ifdef ENABLE_MAP
#include "path_planner.h"
#include <memory>
#include "pathplanner.h"
#include "route_report.h"
#include "mower_map.h"
#include "domain.h"
#include "log.h"

#define _LOG_ "PathPlanner::"

namespace ArduMower {
namespace Modem {
namespace PathPlanner {

namespace PPC = ArduMower::Modem::PathPlannerCore;

static PPC::Point toPlannerPoint(const ArduMower::Domain::Robot::MapPoint &p) {
    return PPC::Point{p.X, p.Y};
}

static PPC::Point toPlannerPoint(const ArduMower::Domain::Robot::State::Point &p) {
    return PPC::Point{p.x, p.y};
}

static ArduMower::Domain::Robot::MapPoint toDomainPoint(const PPC::Point &p) {
    ArduMower::Domain::Robot::MapPoint point{p.X, p.Y};
    point.tag = p.tag;
    point.isConnector = p.tag == static_cast<uint8_t>(ArduMower::Domain::Robot::MapPointTag::CONNECTOR);
    return point;
}

static PPC::Map toPlannerMap(ArduMower::Domain::Robot::MowerMap &map) {
    PPC::Map pm;
    pm.timestamp = map.timestamp;
    pm.rotation = map.rotation;

    pm.perimeter.reserve(map.perimeter.size());
    for (const auto &p : map.perimeter) pm.perimeter.push_back(toPlannerPoint(p));

    pm.exclusions.reserve(map.exclusions.size());
    for (const auto &ex : map.exclusions) {
        std::vector<PPC::Point> poly;
        poly.reserve(ex.size());
        for (const auto &p : ex) poly.push_back(toPlannerPoint(p));
        pm.exclusions.push_back(std::move(poly));
    }

    pm.searchWire.reserve(map.searchWire.size());
    for (const auto &p : map.searchWire) pm.searchWire.push_back(toPlannerPoint(p));

    pm.dockpoints.reserve(map.dockpoints.size());
    for (const auto &p : map.dockpoints) pm.dockpoints.push_back(toPlannerPoint(p));

    // Teilbereich: nur die eigene Auswahl, begrenzt durch den Perimeter
    auto addSelection = [&pm](const std::vector<ArduMower::Domain::Robot::MapPoint> &sel) {
        if (sel.size() < 3) return;
        std::vector<PPC::Point> poly;
        poly.reserve(sel.size());
        for (const auto &p : sel) poly.push_back(toPlannerPoint(p));
        pm.selections.push_back(std::move(poly));
    };
    addSelection(map.selection);

    return pm;
}

// "border,area,exclusions" -> PPC::RouteBlock; leer oder ungültig: wie früher
// aus mowBorderCcw (aus: Fläche, Außengrenze, Exclusions; an: Außengrenze,
// Exclusions, Fläche).
static void toRouteOrder(const String &text, bool mowBorderCcw, uint8_t order[3]) {
    if (mowBorderCcw) {
        order[0] = PPC::RouteBlockBorder; order[1] = PPC::RouteBlockExclusionBorder; order[2] = PPC::RouteBlockArea;
    } else {
        order[0] = PPC::RouteBlockArea; order[1] = PPC::RouteBlockBorder; order[2] = PPC::RouteBlockExclusionBorder;
    }
    if (text.length() == 0) return;
    uint8_t parsed[3];
    int n = 0;
    int start = 0;
    while (start <= (int)text.length() && n < 4) {
        int comma = text.indexOf(',', start);
        String token = text.substring(start, comma < 0 ? text.length() : comma);
        token.trim();
        if (n >= 3) return;
        if (token == "area") parsed[n++] = PPC::RouteBlockArea;
        else if (token == "border") parsed[n++] = PPC::RouteBlockBorder;
        else if (token == "exclusions") parsed[n++] = PPC::RouteBlockExclusionBorder;
        else return;
        if (comma < 0) break;
        start = comma + 1;
    }
    if (n != 3) return;
    for (int i = 0; i < 3; i++) order[i] = parsed[i];  // Doppelte prüft der Planer
}

static PPC::Settings toPlannerSettings(const ArduMower::Domain::Robot::MowSettings &settings) {
    PPC::Settings ps;
    ps.timestamp = settings.timestamp;
    ps.pattern = settings.pattern;
    ps.width = settings.width;
    ps.angle = settings.angle;
    // Alle Weglinien-Typen werden immer berechnet; die Laufzeit-Toggles
    // (doMow*) entscheiden später, welche Bereiche tatsächlich gemäht werden.
    ps.mowArea = true;
    ps.mowExclusionBorder = true;
    ps.mowBorderCcw = settings.mowBorderCcw;
    toRouteOrder(settings.routeOrder, settings.mowBorderCcw, ps.routeOrder);
    ps.exclusionLapsWithArea = settings.exclusionLapsWithArea;
    ps.selectionEdgeLaps = settings.selectionEdgeLaps;
    ps.distanceToBorder = settings.distanceToBorder;
    ps.borderLaps = settings.borderLaps;
    ps.distanceToExclusions = (int)lroundf(settings.distanceToExclusions);
    ps.exclusionLaps = settings.exclusionLaps;
    ps.simplifyEpsilon = settings.simplifyEpsilon;
    // checkTurnRadius geht bewusst NICHT in die Planer-Settings: der Wert
    // dient nur der Routenprüfung und darf die Route nicht verändern.
    return ps;
}

static PPC::State toPlannerState(const ArduMower::Domain::Robot::State::State *state) {
    PPC::State ps;
    if (state != nullptr) {
        ps.job = state->job;
        ps.position.x = state->position.x;
        ps.position.y = state->position.y;
        ps.position.solution = state->position.solution;
    }
    return ps;
}

namespace {
using Tag = ArduMower::Domain::Robot::MapPointTag;

struct Segment {
    size_t begin;
    size_t end;
    uint8_t tag;
};

bool tagActive(uint8_t tag, const ArduMower::Domain::Robot::MowSettings &s) {
    switch (static_cast<Tag>(tag)) {
        case Tag::AREA: return s.doMowArea;
        case Tag::BORDER: return s.doMowBorder;
        case Tag::EXCLUSION_BORDER: return s.doMowExclusionBorder;
        case Tag::TRANSIT:
        case Tag::NONE: return true;
        // Mixed connectors: decided below from the categories they join.
        case Tag::CONNECTOR: return true;
    }
    return true;
}

bool allActive(const ArduMower::Domain::Robot::MowSettings &s) {
    return s.doMowArea && s.doMowBorder && s.doMowExclusions && s.doMowExclusionBorder;
}

// Zusammenhängende aktive Abschnitte der Route
std::vector<Segment> activeSegments(const Polygon &route, const ArduMower::Domain::Robot::MowSettings &settings) {
    std::vector<bool> active(route.size(), false);
    for (size_t i = 0; i < route.size(); i++) {
        active[i] = tagActive(route[i].tag, settings);
    }
    // A mixed connector joins two categories (e.g. Area and a Border or an
    // exclusion lap). It is useful only when both of them remain enabled, so
    // it follows the nearest real route point before and after it.
    auto isConnector = [](uint8_t tag) {
        return static_cast<Tag>(tag) == Tag::CONNECTOR || static_cast<Tag>(tag) == Tag::TRANSIT ||
               static_cast<Tag>(tag) == Tag::NONE;
    };
    for (size_t i = 0; i < route.size(); i++) {
        if (static_cast<Tag>(route[i].tag) != Tag::CONNECTOR) continue;
        bool before = true, after = true;
        for (size_t k = i; k-- > 0;) {
            if (!isConnector(route[k].tag)) { before = active[k]; break; }
        }
        for (size_t k = i + 1; k < route.size(); k++) {
            if (!isConnector(route[k].tag)) { after = active[k]; break; }
        }
        active[i] = before && after;
    }

    std::vector<Segment> segments;
    size_t segStart = 0;
    for (size_t i = 0; i <= route.size(); i++) {
        if (i == route.size() || !active[i]) {
            if (segStart < i) {
                uint8_t dominantTag = static_cast<uint8_t>(Tag::NONE);
                for (size_t k = segStart; k < i; k++) {
                    uint8_t t = route[k].tag;
                    if (t != 0 && t != static_cast<uint8_t>(Tag::CONNECTOR) &&
                        t != static_cast<uint8_t>(Tag::TRANSIT)) {
                        dominantTag = t;
                        break;
                    }
                }
                segments.push_back({segStart, i, dominantTag});
            }
            segStart = i + 1;
        }
    }
    return segments;
}

PPC::Polygon plannerOuter(const ArduMower::Domain::Robot::MowerMap &map) {
    PPC::Polygon outerBoundary;
    for (const auto &p : map.perimeter) outerBoundary.push_back({p.X, p.Y});
    return outerBoundary;
}

std::vector<PPC::Polygon> plannerHoles(const ArduMower::Domain::Robot::MowerMap &map) {
    std::vector<PPC::Polygon> holes;
    for (const auto &ex : map.exclusions) {
        if (ex.size() >= 3) {
            PPC::Polygon h;
            for (const auto &p : ex) h.push_back({p.X, p.Y});
            holes.push_back(std::move(h));
        }
    }
    return holes;
}
} // namespace

TransitCache::Key TransitCache::key(const Point &a, const Point &b) {
    // mm-genau: dieselben Wegpunkte ergeben denselben Schlüssel
    return {(int32_t)lround(a.X * 1000.0), (int32_t)lround(a.Y * 1000.0),
            (int32_t)lround(b.X * 1000.0), (int32_t)lround(b.Y * 1000.0)};
}

const std::vector<PPC::Point> *TransitCache::find(const Point &a, const Point &b) const {
    auto it = paths_.find(key(a, b));
    return it == paths_.end() ? nullptr : &it->second;
}

void TransitCache::put(const Point &a, const Point &b, std::vector<PPC::Point> path) {
    paths_[key(a, b)] = std::move(path);
}

PPC::FreeSpaceRouter &TransitCache::router(const ArduMower::Domain::Robot::MowerMap &map) {
    if (!router_) router_.reset(new PPC::FreeSpaceRouter(plannerOuter(map), plannerHoles(map)));
    return *router_;
}

std::vector<std::pair<Point, Point>> routeGaps(const Polygon &route,
    const ArduMower::Domain::Robot::MowSettings &settings)
{
    std::vector<std::pair<Point, Point>> gaps;
    if (route.empty() || allActive(settings)) return gaps;
    auto segments = activeSegments(route, settings);
    for (size_t s = 0; s + 1 < segments.size(); s++)
        gaps.push_back({route[segments[s].end - 1], route[segments[s + 1].begin]});
    return gaps;
}

bool prefillTransitCache(TransitCache &cache, const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &base, int &combo)
{
    for (; combo < 8; combo++) {
        ArduMower::Domain::Robot::MowSettings s = base;
        s.doMowArea = combo & 1;
        s.doMowBorder = combo & 2;
        s.doMowExclusionBorder = combo & 4;
        s.doMowExclusions = true;
        for (const auto &gap : routeGaps(route, s)) {
            if (cache.find(gap.first, gap.second)) continue;
            cache.put(gap.first, gap.second, cache.router(map).route(
                PPC::Point{gap.first.X, gap.first.Y}, PPC::Point{gap.second.X, gap.second.Y}));
            return true;  // höchstens ein Weg je Aufruf
        }
    }
    return false;
}

void JobBuilder::add(const ArduMower::Domain::Robot::MowerMap &area,
                     const ArduMower::Domain::Robot::MowSettings &settings) {
    PPC::JobPart part;
    const Polygon route = filterRouteByToggles(area.waypoints, area, settings);
    part.route.reserve(route.size());
    for (const auto &p : route) part.route.push_back(PPC::Point{p.X, p.Y, p.tag});
    auto addSelection = [&part](const std::vector<ArduMower::Domain::Robot::MapPoint> &sel) {
        if (sel.size() < 3) return;
        PPC::Polygon poly;
        for (const auto &p : sel) poly.push_back({p.X, p.Y});
        part.region.push_back(std::move(poly));
    };
    addSelection(area.selection);
    parts_.push_back(std::move(part));
}

Polygon JobBuilder::build(const ArduMower::Domain::Robot::MowerMap &map, bool overlapOnce,
                          std::vector<size_t> *partStarts) {
    PPC::FreeSpaceRouter router(plannerOuter(map), plannerHoles(map));
    const PPC::Polygon job = PPC::composeJob(parts_, router, overlapOnce,
        static_cast<uint8_t>(ArduMower::Domain::Robot::MapPointTag::TRANSIT), partStarts);
    parts_.clear();
    Polygon out;
    out.reserve(job.size());
    for (const auto &p : job) {
        ArduMower::Domain::Robot::MapPoint mp{p.X, p.Y};
        mp.tag = p.tag;
        mp.isConnector = p.tag == static_cast<uint8_t>(ArduMower::Domain::Robot::MapPointTag::TRANSIT);
        out.push_back(mp);
    }
    return out;
}

Polygon filterRouteByToggles(const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &settings,
    std::vector<Polygon> *transits, TransitCache *cache)
{
    if (route.empty()) return route;
    if (allActive(settings)) return route;
    std::vector<Segment> segments = activeSegments(route, settings);
    if (segments.empty()) return {};

    // Ein Wegfinder für alle Lücken (über den Cache, sonst lokal), und bereits
    // berechnete Wege aus dem Cache: Je Lücke einen Wegfinder aufzubauen
    // kostete auf dem ESP bei jedem Schalterwechsel Sekunden.
    TransitCache localCache;
    TransitCache &paths = cache ? *cache : localCache;

    Polygon result;
    for (size_t s = 0; s < segments.size(); s++) {
        const auto &seg = segments[s];
        for (size_t i = seg.begin; i < seg.end; i++) {
            const auto &p = route[i];
            if (result.empty() || result.back().X != p.X || result.back().Y != p.Y) {
                result.push_back(p);
            }
        }
        if (s + 1 < segments.size()) {
            const auto &next = segments[s + 1];
            bool needsConnector = tagActive(seg.tag, settings) || tagActive(next.tag, settings);
            if (!needsConnector) continue;

            const auto &lastPt = route[seg.end - 1];
            const auto &nextPt = route[next.begin];
            const std::vector<PPC::Point> *cached = paths.find(lastPt, nextPt);
            if (!cached) {
                paths.put(lastPt, nextPt, paths.router(map).route(
                    PPC::Point{lastPt.X, lastPt.Y}, PPC::Point{nextPt.X, nextPt.Y}));
                cached = paths.find(lastPt, nextPt);
            }
            const auto &conn = *cached;
            if (transits) {
                Polygon transit;
                transit.reserve(conn.size() + 2);
                transit.push_back(lastPt);
                for (const auto &c : conn) {
                    ArduMower::Domain::Robot::MapPoint tp{c.X, c.Y};
                    if (std::abs(tp.X - transit.back().X) > 1e-9 || std::abs(tp.Y - transit.back().Y) > 1e-9)
                        transit.push_back(tp);
                }
                if (std::abs(nextPt.X - transit.back().X) > 1e-9 || std::abs(nextPt.Y - transit.back().Y) > 1e-9)
                    transit.push_back(nextPt);
                transits->push_back(std::move(transit));
            }
            for (size_t k = 0; k < conn.size(); k++) {
                if (!result.empty() &&
                    std::abs(conn[k].X - result.back().X) < 1e-9 &&
                    std::abs(conn[k].Y - result.back().Y) < 1e-9) {
                    continue;
                }
                ArduMower::Domain::Robot::MapPoint cp;
                cp.X = conn[k].X;
                cp.Y = conn[k].Y;
                cp.isConnector = true;
                cp.tag = seg.tag != 0 && seg.tag == next.tag
                    ? seg.tag : static_cast<uint8_t>(Tag::CONNECTOR);
                result.push_back(cp);
            }
        }
    }
    return result;
}

Polygon calculateWaypoints(ArduMower::Domain::Robot::MowerMap &map,
    ArduMower::Domain::Robot::MowSettings &settings,
    const ArduMower::Domain::Robot::State::State *state,
    RouteReport *report,
    float mowSpeed,
    const std::function<void(int percent)> &progress)
{
    Log(INFO, "%scalculateWaypoints: width=%.2f angle=%d distToBorder=%.2f borderLaps=%d distToExclusions=%.0f exclusionLaps=%d doMowArea=%d doMowBorder=%d doMowExclusionBorder=%d",
        _LOG_, settings.width, settings.angle, settings.distanceToBorder, settings.borderLaps,
        settings.distanceToExclusions, settings.exclusionLaps,
        settings.doMowArea, settings.doMowBorder, settings.doMowExclusionBorder);
    // Berechne immer die vollständige Route; die Runtime-Toggles in settings
    // filtern anschließend, welche Bereiche tatsächlich gemäht werden.
    PPC::Map pm = toPlannerMap(map);
    PPC::Settings ps = toPlannerSettings(settings);
    const PPC::State pstate = toPlannerState(state);

    PPC::Polygon route = PPC::calculateWaypoints(pm, ps, state ? &pstate : nullptr, progress);
    Log(INFO, "%scalculateWaypoints: core produced %d waypoints", _LOG_, route.size());

    Polygon full;
    full.reserve(route.size());
    size_t areaCount = 0;
    size_t borderCount = 0;
    size_t exclusionBorderCount = 0;
    size_t transitCount = 0;
    size_t untaggedCount = 0;
    for (const auto &p : route) {
        auto mp = toDomainPoint(p);
        if (mp.tag == static_cast<uint8_t>(ArduMower::Domain::Robot::MapPointTag::NONE)) {
            mp.tag = static_cast<uint8_t>(ArduMower::Domain::Robot::MapPointTag::CONNECTOR);
            mp.isConnector = true;
        }
        switch (static_cast<ArduMower::Domain::Robot::MapPointTag>(mp.tag)) {
            case ArduMower::Domain::Robot::MapPointTag::AREA: ++areaCount; break;
            case ArduMower::Domain::Robot::MapPointTag::BORDER: ++borderCount; break;
            case ArduMower::Domain::Robot::MapPointTag::EXCLUSION_BORDER: ++exclusionBorderCount; break;
            case ArduMower::Domain::Robot::MapPointTag::TRANSIT: ++transitCount; break;
            case ArduMower::Domain::Robot::MapPointTag::CONNECTOR: ++untaggedCount; break;
            case ArduMower::Domain::Robot::MapPointTag::NONE: break;
        }
        full.push_back(mp);
    }
    Log(INFO, "%scalculateWaypoints: tags area=%d border=%d exclusionBorder=%d transit=%d neutral=%d", _LOG_,
        areaCount, borderCount, exclusionBorderCount, transitCount, untaggedCount);

    if (report != nullptr) {
        PPC::RouteCheckParams cp;
        cp.minTurnRadius = settings.checkTurnRadius;
        cp.mowSpeed = mowSpeed > 0.01f ? mowSpeed : 0.3f;
        const PPC::RouteReport rr = PPC::analyzeRoute(route, cp);

        report->valid = true;
        report->pointCount = rr.stats.pointCount;
        report->totalLength = static_cast<float>(rr.stats.totalLength);
        report->rotationCount = rr.stats.rotationCount;
        report->trackedCorners = rr.stats.trackedCorners;
        report->estimatedSeconds = static_cast<float>(rr.stats.estimatedSeconds);
        report->findingsTotal = rr.stats.findingsTotal;
        report->turnRadius = settings.checkTurnRadius;
        report->areaCount = areaCount;
        report->borderCount = borderCount;
        report->exclusionBorderCount = exclusionBorderCount;
        report->transitCount = transitCount;
        report->connectorCount = untaggedCount;
        report->findings.reserve(rr.findings.size());
        for (const auto &f : rr.findings) {
            RouteFinding out;
            out.severity = static_cast<uint8_t>(f.severity);
            out.kind = static_cast<uint8_t>(f.kind);
            out.index = f.index;
            out.index2 = f.index2;
            out.angleDeg = static_cast<float>(f.angleDeg);
            out.shortfall = static_cast<float>(f.shortfall);
            report->findings.push_back(out);
        }
        Log(INFO, "%scalculateWaypoints: check radius=%.2f length=%.1fm rotations=%d corners=%d findings=%d", _LOG_,
            report->turnRadius, report->totalLength, report->rotationCount,
            report->trackedCorners, report->findingsTotal);
    }

    // Die Route wird ungefiltert gespeichert; die Laufzeit-Toggles entscheiden
    // später bei Upload/Anzeige, welche Punkte tatsächlich gemäht werden.
    return full;
}

} // namespace PathPlanner
} // namespace Modem
} // namespace ArduMower
#endif
