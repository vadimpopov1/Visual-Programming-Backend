#include <SDL2/SDL.h>
#include <GL/glew.h>

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <zmq.hpp>
#include <map>

#include <pqxx/pqxx>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#define PORT 5555

#define HOST "localhost"
#define DB_PORT "5432"
#define DB_NAME "location_db"
#define DB_USER "vadimpopov"              
#define DB_USER_PASSWORD "postgres" 

struct Location {
    std::atomic<float> latitude;
    std::atomic<float> longitude;
    std::atomic<float> altitude;
    std::atomic<float> accuracy;
    std::atomic<long long> timestamp;
    std::string imei;
};

struct CellSignalStrength {
    // lte
    std::atomic<int> level;
    std::atomic<int> cqi;
    std::atomic<int> rsrp;
    std::atomic<int> rsrq;
    std::atomic<int> rssi;
    std::atomic<int> rssnr;
    // nr
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

CellSignalStrengthData CellData = CellSignalStrengthData{};

void parse_received_data(const std::string& data, Location* loc, CellSignalStrength* signal) {
    size_t lat_pos = data.find("\"latitude\"");
    size_t lon_pos = data.find("\"longitude\"");
    size_t alt_pos = data.find("\"altitude\"");
    size_t acc_pos = data.find("\"accuracy\"");
    size_t ts_pos = data.find("\"timestamp\"");
    size_t imei_pos = data.find("\"imei\"");

    if (lat_pos != std::string::npos) {
        size_t colon_pos = data.find(":", lat_pos);
        size_t comma_pos = data.find(",", colon_pos);
        std::string lat_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->latitude.store(std::stof(lat_str));
    }
    
    if (lon_pos != std::string::npos) {
        size_t colon_pos = data.find(":", lon_pos);
        size_t comma_pos = data.find(",", colon_pos);
        std::string lon_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->longitude.store(std::stof(lon_str));
    }
    
    if (alt_pos != std::string::npos) {
        size_t colon_pos = data.find(":", alt_pos);
        size_t comma_pos = data.find(",", colon_pos);
        if (comma_pos == std::string::npos) comma_pos = data.find("}", colon_pos);
        std::string alt_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->altitude.store(std::stof(alt_str));
    }

    if (acc_pos != std::string::npos) {
        size_t colon_pos = data.find(":", acc_pos);
        size_t comma_pos = data.find(",", colon_pos);
        if (comma_pos == std::string::npos) comma_pos = data.find("}", colon_pos);
        std::string acc_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->accuracy.store(std::stof(acc_str));
    }

    if (ts_pos != std::string::npos) {
        size_t colon_pos = data.find(":", ts_pos);
        size_t comma_pos = data.find(",", colon_pos);
        if (comma_pos == std::string::npos) comma_pos = data.find("}", colon_pos);
        std::string ts_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->timestamp.store(std::stoll(ts_str));
    }

    if (imei_pos != std::string::npos) {
        size_t colon_pos = data.find(":", imei_pos);
        size_t comma_pos = data.find(",", colon_pos);
        if (comma_pos == std::string::npos) comma_pos = data.find("}", colon_pos);
        std::string imei_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        if (imei_str.size() >= 2 && imei_str.front() == '"' && imei_str.back() == '"') {
            imei_str = imei_str.substr(1, imei_str.size() - 2);
        }
        loc->imei = imei_str;
    }

    size_t cell_pos = data.find("\"cellInfoList\"");
    if (cell_pos == std::string::npos) return;

    size_t colon_pos = data.find(":", cell_pos);
    if (colon_pos == std::string::npos) return;

    size_t quote_start = data.find("\"", colon_pos + 1);
    if (quote_start == std::string::npos) return;
    size_t quote_end = data.find("\"", quote_start + 1);
    if (quote_end == std::string::npos) return;
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
        std::string num_str = cell_info_str.substr(num_start, num_end - num_start);
        try {
            return std::stoi(num_str);
        } catch (...) {
            return -1;
        }
    };

    if (cell_info_str.find("CellInfoNr") != std::string::npos) {
        signal->ssrsrp.store(extract_int("ssRsrp"));
        signal->ssrsrq.store(extract_int("ssRsrq"));
        signal->sssinr.store(extract_int("ssSinr"));
    }
    else if (cell_info_str.find("CellInfoLte") != std::string::npos) {
        signal->level.store(extract_int("level"));
        signal->cqi.store(extract_int("cqi"));
        signal->rsrp.store(extract_int("rsrp"));
        signal->rsrq.store(extract_int("rsrq"));
        signal->rssi.store(extract_int("rssi"));
        signal->rssnr.store(extract_int("rssnr"));
    }
}
void run_gui(Location* loc, CellSignalStrength* signal) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Location GUI", 
                                          SDL_WINDOWPOS_CENTERED, 
                                          SDL_WINDOWPOS_CENTERED, 
                                          1280, 720, 
                                          window_flags);
    
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return;
    }
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot::StyleColorsDark();
    ImPlotStyle& plot_style = ImPlot::GetStyle();
    plot_style.PlotDefaultSize = ImVec2(800, 400);
    plot_style.Colors[ImPlotCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.7f);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowTitleAlign = ImVec2(-0.1f, 0.5f);
    style.WindowPadding = ImVec2(15, 15);
    style.ItemSpacing = ImVec2(5, 5);
    style.FramePadding = ImVec2(6, 4);
    // ImGui::StyleColorsDark(); 

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.7f));

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::Begin("Main Dashboard", nullptr, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        float left_panel_width = io.DisplaySize.x * 0.2f;
        ImGui::BeginChild("Left Panel", ImVec2(left_panel_width, 0), true);
        ImGui::Text("LOCATION INFO");
        ImGui::Separator();
        ImGui::Text("Latitude: %.6f", loc->latitude.load());
        ImGui::Text("Longitude: %.6f", loc->longitude.load());
        ImGui::Text("Altitude: %.6f", loc->altitude.load());
        ImGui::Text("Accuracy: %.6f", loc->accuracy.load());
        ImGui::Text("Timestamp: %lld", loc->timestamp.load());
        ImGui::Text("IMEI: %s", loc->imei.c_str());

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("CELL SIGNAL LTE");
        ImGui::Separator();
        ImGui::Text("Level: %d", signal->level.load());
        ImGui::Text("CQI: %d", signal->cqi.load());
        ImGui::Text("RSRP: %d", signal->rsrp.load());
        ImGui::Text("RSRQ: %d", signal->rsrq.load());
        ImGui::Text("RSSI: %d", signal->rssi.load());
        ImGui::Text("RSSNR: %d", signal->rssnr.load());

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Application average:\n%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("Right Panel", ImVec2(0, 0), true);
        ImGui::Text("SIGNAL GRAPH");
        ImGui::Separator();

        if (ImPlot::BeginPlot("Signal Strength", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time", "dBm", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            
            ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 0, ImPlotCond_Always);
            
            if (!CellData.timestamp.empty()) {
                ImPlot::SetupAxisLimits(ImAxis_X1, CellData.timestamp.front(), CellData.timestamp.back(), ImPlotCond_Always);
            }
            
            if (!CellData.timestamp.empty()) {
                ImPlot::PlotLine("RSRP", CellData.timestamp.data(), CellData.rsrp.data(), (int)CellData.timestamp.size());
                ImPlot::PlotLine("RSRQ", CellData.timestamp.data(), CellData.rsrq.data(), (int)CellData.timestamp.size());
                ImPlot::PlotLine("RSSI", CellData.timestamp.data(), CellData.rssi.data(), (int)CellData.timestamp.size());
            }
            
            ImPlot::EndPlot();
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::PopStyleColor();

        ImGui::Render();

        int display_w, display_h;
        SDL_GetWindowSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 0.7f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void db_add_data(Location *loc, CellSignalStrength *signal) {
    const char* info = "host=" HOST " port=" DB_PORT " dbname=" DB_NAME " user=" DB_USER " password=" DB_USER_PASSWORD;
    pqxx::connection c(info);

    pqxx::work tx(c);
    tx.exec_params("INSERT INTO location (latitude, longitude, altitude, accuracy, timestamp, imei, cellinfolist) VALUES ($1, $2, $3, $4, $5, $6, $7)",
    loc->latitude.load(), loc->longitude.load(), loc->altitude.load(), loc->accuracy.load(), loc->timestamp.load(), loc->imei.c_str(), "");
    tx.commit();
}

void run_server(Location* loc, CellSignalStrength* signal) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    
    std::string bind_address = "tcp://*:" + std::to_string(PORT);
    socket.bind(bind_address);

    struct stat st = {0};
    if (stat("../database", &st) == -1) {
        mkdir("../database", 0700);
    }

    while (true) {
        zmq::message_t request;
        
        auto result = socket.recv(request, zmq::recv_flags::none);
        
        std::string received_data(static_cast<char*>(request.data()), request.size());
        
        parse_received_data(received_data, loc, signal);

        CellData.rsrp.push_back(static_cast<double>(signal->rsrp.load()));
        CellData.rsrq.push_back(static_cast<double>(signal->rsrq.load()));
        CellData.rssi.push_back(static_cast<double>(signal->rssi.load()));
        CellData.timestamp.push_back(static_cast<double>(loc->timestamp.load()));

        db_add_data(loc, signal);

        std::stringstream json_entry;

        json_entry << "\t{\n\t\"received_data\": " << received_data << "\n\t}";
        
        std::string filename = "../database/locations.json";
        
        std::ifstream check_file(filename);
        bool file_exists = check_file.good();
        check_file.close();

        std::ofstream file(filename, std::ios::app);
        
        if (file.is_open()) {
            if (!file_exists) {
                file << "[\n";
                file << json_entry.str() << "\n";
                file << "\n]";
            } else {
                std::ifstream read_file(filename);
                std::stringstream content;
                content << read_file.rdbuf();
                read_file.close();
                
                std::string file_content = content.str();
                
                if (file_content.length() > 1 && 
                    file_content.substr(file_content.length() - 2) == "\n]") {
                    file_content = file_content.substr(0, file_content.length() - 2);
                    
                    std::ofstream write_file(filename);
                    write_file << file_content;
                    
                    if (file_content.length() > 2) {
                        write_file << ",\n";
                    }
                    write_file << json_entry.str() << "\n";
                    write_file << "]";
                    write_file.close();
                } else {
                    std::ofstream new_file(filename);
                    new_file << "[\n";
                    new_file << json_entry.str() << "\n";
                    new_file << "]";
                    new_file.close();
                }
            }
            file.close();
            
            socket.send(zmq::buffer("Successful sending"), zmq::send_flags::none);
        } else {
            socket.send(zmq::buffer("Unexpected error"), zmq::send_flags::none);
        }
    }
}

int main() {
    Location locationInfo;
    CellSignalStrength CellSignalStrengthInfo;

    // location stock
    locationInfo.latitude.store(0.0f);
    locationInfo.longitude.store(0.0f);
    locationInfo.altitude.store(0.0f);
    locationInfo.accuracy.store(0.0f);
    locationInfo.timestamp.store(0);
    locationInfo.imei = "None";

    // cellsignal stock
    CellSignalStrengthInfo.level.store(0);
    CellSignalStrengthInfo.cqi.store(0);
    CellSignalStrengthInfo.rsrp.store(0);
    CellSignalStrengthInfo.rsrq.store(0);
    CellSignalStrengthInfo.rssi.store(0);
    CellSignalStrengthInfo.rssnr.store(0);
    CellSignalStrengthInfo.ssrsrp.store(0);
    CellSignalStrengthInfo.ssrsrq.store(0);
    CellSignalStrengthInfo.sssinr.store(0);
    CellSignalStrengthInfo.timingadvancenr.store(0);

    std::thread server_thread(run_server, &locationInfo, &CellSignalStrengthInfo);
    
    run_gui(&locationInfo, &CellSignalStrengthInfo); // Нельзя создать отдельный потом для GUI тк в MacOS поток для графики должен быть в главном.

    server_thread.join();

    return 0;
}