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

#define PORT 5555

struct Location {
    std::atomic<float> latitude;
    std::atomic<float> longitude;
    std::atomic<float> altitude;
    std::atomic<long long> timestamp;
};

void parse_received_data(const std::string& data, Location* loc) {
    size_t lat_pos = data.find("\"latitude\"");
    size_t lon_pos = data.find("\"longitude\"");
    size_t alt_pos = data.find("\"altitude\"");
    size_t ts_pos = data.find("\"timestamp\"");
    
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

    if (ts_pos != std::string::npos) {
        size_t colon_pos = data.find(":", ts_pos);
        size_t comma_pos = data.find(",", colon_pos);
        if (comma_pos == std::string::npos) comma_pos = data.find("}", colon_pos);
        std::string ts_str = data.substr(colon_pos + 1, comma_pos - colon_pos - 1);
        loc->timestamp.store(std::stoll(ts_str));
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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

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

        ImGui::Begin("Location Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Latitude: %.6f°", loc->latitude.load());
        ImGui::Text("Longitude: %.6f°", loc->longitude.load());
        ImGui::Text("Altitude: %.2f meters", loc->altitude.load());
        ImGui::Text("Timestamp %lld", loc->timestamp.load());
        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / io.Framerate, io.Framerate);
        ImGui::End();

        ImGui::Render();
        
        int display_w, display_h;
        SDL_GetWindowSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
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

        time_t now = time(0);
        struct tm* tm_info = localtime(&now);
        char time_str[30];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        std::stringstream json_entry;
        json_entry << "  {\n";
        json_entry << "    \"received_data\": " << received_data << ",\n";
        json_entry << "    \"server_time\": \"" << time_str << "\"\n";
        json_entry << "  }";

        std::string filename = "../database/locations.json";
        
        std::ifstream check_file(filename);
        bool file_exists = check_file.good();
        check_file.close();

        std::ofstream file(filename, std::ios::app);
        
        if (file.is_open()) {
            if (!file_exists) {
                file << "[\n";
                file << json_entry.str() << "\n";
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
            
            std::string response = "Successful sending";
            socket.send(zmq::buffer(response), zmq::send_flags::none);
        } else {
            std::string response = "Unexpected error";
            socket.send(zmq::buffer(response), zmq::send_flags::none);
        }
    }
}

int main() {
    Location locationInfo;

    locationInfo.latitude.store(0.0f);
    locationInfo.longitude.store(0.0f);
    locationInfo.altitude.store(0.0f);
    locationInfo.timestamp.store(0);

    std::thread server_thread(run_server, &locationInfo);
    
    run_gui(&locationInfo);

    server_thread.join();

    return 0;
}