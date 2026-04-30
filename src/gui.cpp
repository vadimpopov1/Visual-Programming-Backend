#include "gui.h"
#include "map.h"
#include "data.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#include <iostream>

void run_gui(Location* loc, CellSignalStrength* signal)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Location GUI",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);

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

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_WINDOWEVENT
                && event.window.event == SDL_WINDOWEVENT_CLOSE
                && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.7f));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::Begin("Main Dashboard", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::BeginTabBar("##MainTabs"))
        {
            if (ImGui::BeginTabItem("Main"))
            {
                const float left_panel_width = io.DisplaySize.x * 0.2f;
                ImGui::BeginChild("Left Panel", ImVec2(left_panel_width, 0), true);

                ImGui::Text("LOCATION INFO");
                ImGui::Separator();
                ImGui::Text("Latitude: %.6f", loc->latitude.load());
                ImGui::Text("Longitude: %.6f", loc->longitude.load());
                ImGui::Text("Altitude: %.6f", loc->altitude.load());
                ImGui::Text("Accuracy: %.6f", loc->accuracy.load());
                ImGui::Text("Timestamp: %lld", loc->timestamp.load());
                ImGui::Text("IMEI: %s", loc->imei.c_str());

                ImGui::Spacing(); ImGui::Spacing();
                ImGui::Text("CELL SIGNAL LTE");
                ImGui::Separator();
                ImGui::Text("Level: %d", signal->level.load());
                ImGui::Text("CQI: %d", signal->cqi.load());
                ImGui::Text("RSRP: %d", signal->rsrp.load());
                ImGui::Text("RSRQ: %d", signal->rsrq.load());
                ImGui::Text("RSSI: %d", signal->rssi.load());
                ImGui::Text("RSSNR: %d", signal->rssnr.load());

                ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator();
                ImGui::Text("Application average:\n%.3f ms/frame (%.1f FPS)",
                    1000.0f / io.Framerate, io.Framerate);
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("Right Panel", ImVec2(0, 0), true);
                ImGui::Text("SIGNAL GRAPH");
                ImGui::Separator();

                if (ImPlot::BeginPlot("Signal Strength", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("Time", "dBm", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 0, ImPlotCond_Always);
                    if (!CellData.timestamp.empty()) {
                        ImPlot::SetupAxisLimits(ImAxis_X1,
                            CellData.timestamp.front(), CellData.timestamp.back(), ImPlotCond_Always);
                        ImPlot::PlotLine("RSRP", CellData.timestamp.data(), CellData.rsrp.data(), (int)CellData.timestamp.size());
                        ImPlot::PlotLine("RSRQ", CellData.timestamp.data(), CellData.rsrq.data(), (int)CellData.timestamp.size());
                        ImPlot::PlotLine("RSSI", CellData.timestamp.data(), CellData.rssi.data(), (int)CellData.timestamp.size());
                    }
                    ImPlot::EndPlot();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Map")) {
                DrawMapTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
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