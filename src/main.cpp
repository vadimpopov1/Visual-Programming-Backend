#include <iostream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_metal.h"
#if TARGET_OS_OSX
#include "imgui_impl_osx.h"

struct location
{
    float latitude;
    float longitude;
    float altitude;
};

void run_gui(location *loc) {

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow(
        "Backend start", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    bool is_running = true;
    while (is_running) {

    }

}

int main(){
    static location locationInfo;

    std::thread gui_thread(run_gui, &locationInfo);

    gui_thread.join();

    return 0;
}