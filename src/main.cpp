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

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#define PORT 5555

struct Location {
    std::atomic<float> latitude;
    std::atomic<float> longitude;
    std::atomic<float> altitude;
    std::atomic<float> accuracy;
    std::atomic<long long> timestamp;
    std::string imei;
};

void parse_received_data(const std::string& data, Location* loc) {
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
}

void run_gui(Location* loc) {
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
        ImGui::Begin("Location Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Latitude: %.6f°", loc->latitude.load());
        ImGui::Text("Longitude: %.6f°", loc->longitude.load());
        ImGui::Text("Altitude: %.6f", loc->altitude.load());
        ImGui::Text("Accuracy: %.6f", loc->accuracy.load());
        ImGui::Text("Timestamp %lld", loc->timestamp.load());
        ImGui::Text("IMEI: %s", loc->imei.c_str());
        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / io.Framerate, io.Framerate);

        ImGui::Spacing();
        // Временная заглушка графика для тестов, в будушем будет переделана в рабочую версию
        static float xs1[1001], ys1[1001];
        for (int i = 0; i < 1001; ++i) {
            xs1[i] = i * 0.001f;
            ys1[i] = 0.5f + 0.5f * sinf(50 * (xs1[i] + (float)ImGui::GetTime() / 10));
        }
        static double xs2[20], ys2[20];
        for (int i = 0; i < 20; ++i) {
            xs2[i] = i * 1/19.0f;
            ys2[i] = xs2[i] * xs2[i];
        }
        if (ImPlot::BeginPlot("Line Plots")) {
            ImPlot::SetupAxes("x","y");
            ImPlot::PlotLine("f(x)", xs1, ys1, 1001);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
            ImPlot::PlotLine("g(x)", xs2, ys2, 20,ImPlotLineFlags_Segments);
            ImPlot::EndPlot();
        }

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

void run_server(Location* loc) {
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
        
        parse_received_data(received_data, loc);

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

    locationInfo.latitude.store(0.0f);
    locationInfo.longitude.store(0.0f);
    locationInfo.altitude.store(0.0f);
    locationInfo.accuracy.store(0.0f);
    locationInfo.timestamp.store(0);
    locationInfo.imei = "None";

    std::thread server_thread(run_server, &locationInfo);
    
    run_gui(&locationInfo); // Нельзя создать отдельный потом для GUI тк в MacOS поток для графики должен быть в главном.

    server_thread.join();

    return 0;
}