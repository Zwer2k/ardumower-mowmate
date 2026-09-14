#ifdef ENABLE_MAP
#include "path_planner.h"
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

    return pm;
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
    ps.distanceToBorder = settings.distanceToBorder;
    ps.borderLaps = settings.borderLaps;
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

Polygon filterRouteByToggles(const Polygon &route,
    const ArduMower::Domain::Robot::MowerMap &map,
    const ArduMower::Domain::Robot::MowSettings &settings)
{
    if (route.empty()) return route;
    if (settings.doMowArea && settings.doMowBorder &&
        settings.doMowExclusions && settings.doMowExclusionBorder) {
        return route;
    }

    using Tag = ArduMower::Domain::Robot::MapPointTag;

    auto tagActive = [](uint8_t tag, const ArduMower::Domain::Robot::MowSettings &s) -> bool {
        switch (static_cast<Tag>(tag)) {
            case Tag::AREA: return s.doMowArea;
            case Tag::BORDER: return s.doMowBorder;
            case Tag::EXCLUSION_BORDER: return s.doMowExclusionBorder;
            case Tag::TRANSIT:
            case Tag::NONE: return true;
            // Mixed connectors join e.g. an Area route to a Border lap. They
            // are useful only when both categories remain enabled.
            case Tag::CONNECTOR: return s.doMowArea && s.doMowBorder;
        }
        return true;
    };

    std::vector<bool> active(route.size(), false);
    for (size_t i = 0; i < route.size(); i++) {
        active[i] = tagActive(route[i].tag, settings);
    }

    struct Segment {
        size_t begin;
        size_t end;
        uint8_t tag;
    };
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

    if (segments.empty()) return {};

    std::vector<PPC::Polygon> holes;
    for (const auto &ex : map.exclusions) {
        if (ex.size() >= 3) {
            PPC::Polygon h;
            for (const auto &p : ex) h.push_back({p.X, p.Y});
            holes.push_back(std::move(h));
        }
    }
    PPC::Polygon outerBoundary;
    for (const auto &p : map.perimeter) outerBoundary.push_back({p.X, p.Y});

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
            auto conn = PPC::walkBoundaryWithHoles(
                PPC::Point{lastPt.X, lastPt.Y},
                PPC::Point{nextPt.X, nextPt.Y},
                outerBoundary, holes);
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
    float mowSpeed)
{
    Log(INFO, "%scalculateWaypoints: width=%.2f angle=%d distToBorder=%.2f borderLaps=%d doMowArea=%d doMowBorder=%d doMowExclusionBorder=%d",
        _LOG_, settings.width, settings.angle, settings.distanceToBorder, settings.borderLaps,
        settings.doMowArea, settings.doMowBorder, settings.doMowExclusionBorder);
    // Berechne immer die vollständige Route; die Runtime-Toggles in settings
    // filtern anschließend, welche Bereiche tatsächlich gemäht werden.
    PPC::Map pm = toPlannerMap(map);
    PPC::Settings ps = toPlannerSettings(settings);
    const PPC::State pstate = toPlannerState(state);

    PPC::Polygon route = PPC::calculateWaypoints(pm, ps, state ? &pstate : nullptr);
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
