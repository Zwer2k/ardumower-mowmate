#include <pathplanner.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <limits>
#include <cstdlib>

using namespace ArduMower::Modem::PathPlannerCore;

static std::string pointsToPath(const Polygon &poly) {
    std::ostringstream oss;
    for (const auto &p : poly) {
        if (!oss.str().empty()) oss << " ";
        oss << p.X << "," << p.Y;
    }
    return oss.str();
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

static void writeSvg(const std::string &name, const Map &map, const Polygon &waypoints) {
    if (std::system("mkdir -p out") != 0) return;
    std::string filename = "out/" + name + ".svg";
    std::ofstream out(filename);
    if (!out) return;

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
    for (size_t i = 0; i < waypoints.size(); i++) {
        out << "<text x=\"" << waypoints[i].X << "\" y=\"" << waypoints[i].Y
            << "\" font-size=\"0.25\" fill=\"black\">" << i << "</text>\n";
    }
    out << "</svg>\n";
    std::cout << "wrote " << filename << std::endl;
}

static Map makeMap() {
    Map map;
    map.perimeter = {
        {4.82, -10.21}, {4.65, -10.31}, {4.84, -10.32}, {5.18, -10.14},
        {5.41, -9.99}, {5.79, -9.62}, {5.71, -9.38}, {6.22, -8.77},
        {6.52, -8.86}, {7.05, -8.46}, {7.26, -8.28}, {7.41, -8.26},
        {7.88, -8.04}, {9.19, -9.429999}, {10.35, -8.349998}, {10.04, -8.309999},
        {9.88, -8.17}, {9.71, -8.01}, {9.58, -7.72}, {9.52, -7.45},
        {9.28, -7.45}, {9.139999, -7.31}, {7.08, -5.34}, {6.99, -5.04},
        {7.03, -4.88}, {7.200765133, -3.921196222}, {5.73, -3.17}, {5.51, -3.45},
        {6.28, -4.66}, {3.03, -7.8}, {3.4, -8.02}, {3.73, -8.179996}, {4, -8.33},
        {4.16, -8.53}, {4.26, -8.7}, {4.26, -8.98}, {4.75, -9.429999},
        {4.92, -9.5}, {5.02, -9.69}, {5.06, -9.88}, {5.03, -10.01}, {4.82, -10.21}
    };
    map.exclusions.push_back({
        {4.719904, -7.044161}, {5.843588352, -7.190095425},
        {5.653875351, -6.21234417}, {4.719904, -7.044161}
    });
    return map;
}

static void run(const std::string &name, Settings s) {
    Map map = makeMap();
    Polygon route = calculateWaypoints(map, s, nullptr);
    std::cout << name << ": " << route.size() << " waypoints" << std::endl;
    writeSvg(name, map, route);
}

int main() {
    Settings user;
    user.width = 0.23f;
    user.angle = 0;
    user.distanceToBorder = 1;
    user.borderLaps = 1;
    user.mowArea = true;
    user.mowBorderCcw = false;
    user.mowExclusionBorder = true;
    run("user_settings", user);

    // Ohne Border-Offset, nur Area
    Settings areaOnly = user;
    areaOnly.distanceToBorder = 0;
    areaOnly.borderLaps = 0;
    run("user_area_only", areaOnly);

    // Border offset kleiner
    Settings smallBorder = user;
    smallBorder.distanceToBorder = 1;
    smallBorder.borderLaps = 0;
    run("user_small_border_offset", smallBorder);

    // Border laps ohne offset
    Settings lapsOnly = user;
    lapsOnly.distanceToBorder = 0;
    lapsOnly.borderLaps = 1;
    run("user_border_laps_only", lapsOnly);

    return 0;
}
