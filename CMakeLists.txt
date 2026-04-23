cmake_minimum_required(VERSION 3.14)
project(backend_notes)
set(CMAKE_CXX_STANDARD 17)
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

if(APPLE)
    find_path(GLEW_INCLUDE_DIR GL/glew.h
        PATHS /usr/local/include /opt/homebrew/include /opt/local/include)
    find_library(GLEW_LIBRARY NAMES GLEW glew
        PATHS /usr/local/lib /opt/homebrew/lib /opt/local/lib)
    if(GLEW_INCLUDE_DIR AND GLEW_LIBRARY)
        set(GLEW_FOUND TRUE)
        set(GLEW_INCLUDE_DIRS ${GLEW_INCLUDE_DIR})
        set(GLEW_LIBRARIES ${GLEW_LIBRARY})
        message(STATUS "Found GLEW: ${GLEW_LIBRARY}")
    else()
        message(FATAL_ERROR "GLEW not found on macOS. Install with: brew install glew")
    endif()
else()
    find_package(GLEW REQUIRED)
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)

set(ZMQ_INCLUDE_DIR "/opt/homebrew/include")
set(ZMQ_LIBRARY "/opt/homebrew/lib/libzmq.dylib")
if(EXISTS ${ZMQ_INCLUDE_DIR}/zmq.hpp AND EXISTS ${ZMQ_LIBRARY})
    set(ZMQ_FOUND TRUE)
    set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
    set(ZMQ_LIBRARIES ${ZMQ_LIBRARY})
    message(STATUS "Found ZeroMQ: ${ZMQ_LIBRARY}")
else()
    message(FATAL_ERROR "ZeroMQ not found. Install with: brew install zmq cppzmq")
endif()

set(PQXX_INCLUDE_DIR "/opt/homebrew/include")
set(PQXX_LIBRARY "/opt/homebrew/lib/libpqxx.dylib")
if(EXISTS ${PQXX_INCLUDE_DIR}/pqxx/pqxx)
    set(PQXX_FOUND TRUE)
    set(PQXX_INCLUDE_DIRS ${PQXX_INCLUDE_DIR})
    set(PQXX_LIBRARIES ${PQXX_LIBRARY})
    message(STATUS "Found libpqxx: ${PQXX_LIBRARY}")
else()
    message(FATAL_ERROR "libpqxx not found. Install with: brew install libpqxx")
endif()

find_package(CURL REQUIRED)

set(STB_INCLUDE_DIR ${THIRD_PARTY_DIR}/stb)
if(NOT EXISTS ${STB_INCLUDE_DIR}/stb_image.h)
    message(WARNING "stb_image.h not found at ${STB_INCLUDE_DIR}/stb_image.h Download it from https://github.com/nothings/stb")
endif()

include_directories(${INCLUDE_DIR})
include_directories(
    ${GLEW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${STB_INCLUDE_DIR}
)

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${THIRD_PARTY_DIR}/imgui)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)
target_compile_options(implot PRIVATE -fPIC)

add_executable(main
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/server.cpp
    ${SRC_DIR}/gui.cpp
    ${SRC_DIR}/map.cpp
)

if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    target_link_libraries(main PRIVATE
        imgui implot
        ${SDL2_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${ZMQ_LIBRARIES}
        ${PQXX_LIBRARIES}
        ${CURL_LIBRARIES}
        pthread dl
    )
    target_link_options(main PRIVATE -framework OpenGL)
else()
    target_link_libraries(main PRIVATE
        imgui implot
        ${SDL2_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ZMQ_LIBRARIES}
        ${PQXX_LIBRARIES}
        ${CURL_LIBRARIES}
        pthread dl
    )
endif()

target_include_directories(main PRIVATE
    ${GLEW_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${STB_INCLUDE_DIR}
)