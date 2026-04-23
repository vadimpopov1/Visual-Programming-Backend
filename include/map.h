#pragma once

#include <GL/glew.h>
#include <functional>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <cstddef>

#include "server.h"

struct OsmTileCoord {
    int zoom, x, y;
};

using TileCallback = std::function<void(const OsmTileCoord&, const std::vector<std::byte>&)>;

struct OsmTileTexture {
    GLuint tex_id  = 0;
    bool pending = false;
    bool loaded  = false;
    int w = 0, h = 0;
    std::vector<std::byte> image;
    std::mutex mtx;

    ~OsmTileTexture();
};

struct OsmTileFetcher {
    struct Job {
        OsmTileCoord tile;
        TileCallback on_done;
    };

    std::vector<std::thread> workers;
    std::queue<Job> jobs;
    std::mutex mtx;
    std::condition_variable signal;
    bool stopping = false;
};

void TileTexture_Load(OsmTileTexture& tile, const std::vector<std::byte>& data);
void TileTexture_Upload(OsmTileTexture& tile);

void Fetcher_Init(OsmTileFetcher& fetcher, size_t thread_count);
void Fetcher_Shutdown(OsmTileFetcher& fetcher);
void Fetcher_Clear(OsmTileFetcher& fetcher);
void Fetcher_Submit(OsmTileFetcher& fetcher, OsmTileCoord tile, TileCallback cb);

void DrawMapTab();
void DrawMapPoints(const std::vector<MapPoint>& points, int zoom);