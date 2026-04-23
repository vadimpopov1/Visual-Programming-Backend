#pragma once

#include "data.h"
#include <string>
#include <vector>

struct MapPoint {
    float lat;
    float lon;
    int rssi;
};

std::vector<MapPoint> db_load_points();

void parse_data(const std::string& data, Location* loc, CellSignalStrength* signal);
void db_add_data(Location* loc, CellSignalStrength* signal);
void run_server(Location* loc, CellSignalStrength* signal);