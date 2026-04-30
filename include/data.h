#pragma once

#include <atomic>
#include <string>
#include <vector>

struct Location {
    std::atomic<float> latitude;
    std::atomic<float> longitude;
    std::atomic<float> altitude;
    std::atomic<float> accuracy;
    std::atomic<long long> timestamp;
    std::string imei;
};

struct CellSignalStrength {
    std::string cellInfoList;
    std::atomic<int> level;
    std::atomic<int> cqi;
    std::atomic<int> rsrp;
    std::atomic<int> rsrq;
    std::atomic<int> rssi;
    std::atomic<int> rssnr;
    std::atomic<int> ssrsrp;
    std::atomic<int> ssrsrq;
    std::atomic<int> sssinr;
    std::atomic<int> timingadvancenr;
};

struct CellSignalStrengthData {
    std::vector<double> rsrp;
    std::vector<double> rsrq;
    std::vector<double> rssi;
    std::vector<double> timestamp;
};

extern CellSignalStrengthData CellData;