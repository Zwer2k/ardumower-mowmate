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
        {9.36, -7.51}, {9.23, -7.43}, {9.13, -7.33}, {7.07, -5.31},
        {6.99, -5.04}, {7.02, -4.83}, {7.05, -4.73}, {7.02, -4.6},
        {5.69, -3.16}, {5.53, -3.25}, {5.51, -3.45}, {6.28, -4.66},
        {4.76, -6.13}, {3.25, -7.59}, {2.97, -7.75}, {3.33, -8},
        {3.74, -8.139997}, {3.99, -8.32}, {4.1, -8.45}, {4.15, -8.59},
        {4.15, -8.79}, {4.19, -8.929999}, {4.75, -9.429999}, {4.92, -9.469999},
        {5.02, -9.54}, {5.08, -9.65}, {5.11, -9.83}, {5.06, -10.01},
        {4.82, -10.21}, {4.65, -10.31}, {4.84, -10.32}, {5.18, -10.14},
        {5.42, -10}, {5.77, -9.63}, {5.73, -9.389999}, {6.23, -8.78},
        {6.5, -8.84}, {6.63, -8.809999}, {7.05, -8.46}, {7.26, -8.28},
        {7.41, -8.26}, {7.88, -8.04}, {9.19, -9.42}, {10.33, -8.34},
        {10.16, -8.33}, {10.02, -8.29}, {9.88, -8.17}, {9.71, -8.01},
        {9.57, -7.65}, {9.57, -7.48}, {9.36, -7.51}
    };
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
    user.width = 0.13f;
    user.angle = 0;
    user.distanceToBorder = 1;
    user.borderLaps = 1;
    user.mowArea = true;
    user.mowExclusionBorder = true;
    user.mowBorderCcw = false;
    run("user2_settings", user);

    // Wie es wäre, wenn mowArea=false (nur border)
    Settings borderOnly = user;
    borderOnly.mowArea = false;
    run("user2_border_only", borderOnly);

    // Ohne alles (nur Startpunkt?)
    Settings none = user;
    none.mowArea = false;
    none.distanceToBorder = 0;
    none.borderLaps = 0;
    none.mowExclusionBorder = false;
    run("user2_none", none);

    return 0;
}
