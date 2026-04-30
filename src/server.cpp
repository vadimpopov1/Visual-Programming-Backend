#include "server.h"
#include "data.h"

#define ZMQ_PORT 5555

#define DB_HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "location_db"
#define DB_USER "vadimpopov"
#define DB_USER_PASSWORD "postgres"

#include <zmq.hpp>
#include <pqxx/pqxx>

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>

CellSignalStrengthData CellData;

void parse_data(const std::string& data, Location* loc, CellSignalStrength* signal)
{
    auto extract_float = [&](const std::string& key) -> std::optional<float> {
        size_t pos = data.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        size_t colon = data.find(":", pos);
        size_t comma = data.find(",", colon);
        if (comma == std::string::npos) comma = data.find("}", colon);
        return std::stof(data.substr(colon + 1, comma - colon - 1));
    };

    auto extract_llong = [&](const std::string& key) -> std::optional<long long> {
        size_t pos = data.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        size_t colon = data.find(":", pos);
        size_t comma = data.find(",", colon);
        if (comma == std::string::npos) comma = data.find("}", colon);
        return std::stoll(data.substr(colon + 1, comma - colon - 1));
    };

    auto extract_string = [&](const std::string& key) -> std::optional<std::string> {
        size_t pos = data.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        size_t colon = data.find(":", pos);
        size_t comma = data.find(",", colon);
        if (comma == std::string::npos) comma = data.find("}", colon);
        std::string val = data.substr(colon + 1, comma - colon - 1);
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        return val;
    };

    if (auto v = extract_float("latitude")) loc->latitude.store(*v);
    if (auto v = extract_float("longitude")) loc->longitude.store(*v);
    if (auto v = extract_float("altitude")) loc->altitude.store(*v);
    if (auto v = extract_float("accuracy")) loc->accuracy.store(*v);
    if (auto v = extract_llong("timestamp")) loc->timestamp.store(*v);
    if (auto v = extract_string("imei")) loc->imei = *v;

    size_t cell_pos = data.find("\"cellInfoList\"");
    if (cell_pos == std::string::npos) return;

    size_t colon_pos = data.find(":", cell_pos);  if (colon_pos == std::string::npos) return;
    size_t quote_start = data.find("\"", colon_pos + 1); if (quote_start == std::string::npos) return;
    size_t quote_end = data.find("\"", quote_start + 1); if (quote_end == std::string::npos) return;

    std::string cell_info_str = data.substr(quote_start + 1, quote_end - quote_start - 1);

    auto extract_int = [&](const std::string& key) -> int {
        size_t key_pos = cell_info_str.find(key);
        if (key_pos == std::string::npos) return -1;
        size_t eq_pos = cell_info_str.find("=", key_pos);
        if (eq_pos == std::string::npos) return -1;
        size_t num_start = cell_info_str.find_first_not_of(" \t", eq_pos + 1);
        if (num_start == std::string::npos) return -1;
        size_t num_end = cell_info_str.find_first_of(" \t,}]", num_start);
        if (num_end == std::string::npos) num_end = cell_info_str.length();
        try { return std::stoi(cell_info_str.substr(num_start, num_end - num_start)); }
        catch (...) { return -1; }
    };

    if (cell_info_str.find("CellInfoNr") != std::string::npos) {
        signal->ssrsrp.store(extract_int("ssRsrp"));
        signal->ssrsrq.store(extract_int("ssRsrq"));
        signal->sssinr.store(extract_int("ssSinr"));
    } else if (cell_info_str.find("CellInfoLte") != std::string::npos) {
        signal->level.store(extract_int("level"));
        signal->cqi.store(extract_int("cqi"));
        signal->rsrp.store(extract_int("rsrp"));
        signal->rsrq.store(extract_int("rsrq"));
        signal->rssi.store(extract_int("rssi"));
        signal->rssnr.store(extract_int("rssnr"));
    }

    signal->cellInfoList = cell_info_str;
}

void db_add_data(Location* loc, CellSignalStrength* signal)
{
    const char* conn_str = "host=" DB_HOST " port=" DB_PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD;
    pqxx::connection c(conn_str);
    pqxx::work tx(c);
    tx.exec_params(
        "INSERT INTO location (latitude, longitude, altitude, accuracy, timestamp, imei, cellinfolist) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        loc->latitude.load(), loc->longitude.load(),
        loc->altitude.load(), loc->accuracy.load(),
        loc->timestamp.load(), loc->imei.c_str(),
        signal->cellInfoList.c_str());
    tx.commit();
}

static int extract_rssi_from_cellinfo(const std::string& cellinfo)
{
    size_t pos = cellinfo.find("rssi=");
    if (pos == std::string::npos) return -110;
    pos += 5;
    while (pos < cellinfo.size() && std::isspace(cellinfo[pos])) pos++;
    bool negative = false;
    if (cellinfo[pos] == '-') { negative = true; pos++; }
    int value = 0;
    while (pos < cellinfo.size() && std::isdigit(cellinfo[pos])) {
        value = value * 10 + (cellinfo[pos] - '0');
        pos++;
    }
    return negative ? -value : value;
}

std::vector<MapPoint> db_load_points()
{
    const char* conn_str = "host=" DB_HOST " port=" DB_PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD;
    std::vector<MapPoint> points;
    try {
        pqxx::connection c(conn_str);
        pqxx::work tx(c);
        for (auto row : tx.exec("SELECT latitude, longitude, cellinfolist FROM location")) {
            MapPoint p;
            p.lat = row[0].as<float>();
            p.lon = row[1].as<float>();
            std::string cellinfo = row[2].as<std::string>();
            p.rssi = extract_rssi_from_cellinfo(cellinfo);
            points.push_back(p);
        }
    } catch (const std::exception& e) {
        std::cerr << "DB error: " << e.what() << std::endl;
    }
    return points;
}

static void append_json_entry(const std::string& filename, const std::string& entry)
{
    std::ifstream check(filename);
    const bool exists = check.good();
    check.close();

    if (!exists) {
        std::ofstream out(filename);
        out << "[\n" << entry << "\n\n]";
        return;
    }

    std::stringstream ss;
    ss << std::ifstream(filename).rdbuf();
    std::string content = ss.str();

    std::ofstream out(filename);
    if (content.size() >= 2 && content.substr(content.size() - 2) == "\n]") {
        content.erase(content.size() - 2);
        out << content;
        if (content.size() > 2) out << ",\n";
        out << entry << "\n]";
    } else {
        out << "[\n" << entry << "\n]";
    }
}

void run_server(Location* loc, CellSignalStrength* signal)
{
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind("tcp://*:" + std::to_string(ZMQ_PORT));

    std::filesystem::create_directories("../database");

    while (true) {
        zmq::message_t request;
        socket.recv(request, zmq::recv_flags::none);

        std::string received_data(static_cast<char*>(request.data()), request.size());
        parse_data(received_data, loc, signal);

        CellData.rsrp.push_back(static_cast<double>(signal->rsrp.load()));
        CellData.rsrq.push_back(static_cast<double>(signal->rsrq.load()));
        CellData.rssi.push_back(static_cast<double>(signal->rssi.load()));
        CellData.timestamp.push_back(static_cast<double>(loc->timestamp.load()));

        db_add_data(loc, signal);

        const std::string entry = "\t{\n\t\"received_data\": " + received_data + "\n\t}";
        const std::string filename = "../database/locations.json";

        append_json_entry(filename, entry);
        socket.send(zmq::buffer("Successful sending"), zmq::send_flags::none);
    }
}