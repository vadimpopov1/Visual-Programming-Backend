#include "server.h"
#include "map.h"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"
#include "implot.h"

OsmTileTexture::~OsmTileTexture()
{
    if (tex_id != 0)
        glDeleteTextures(1, &tex_id);
}

void TileTexture_Load(OsmTileTexture& tile, const std::vector<std::byte>& data)
{
    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);

    stbi_uc* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(data.data()),
        static_cast<int>(data.size()),
        &w, &h, &channels, STBI_rgb_alpha);

    if (pixels == nullptr)
        return;

    std::lock_guard<std::mutex> guard(tile.mtx);
    tile.w = w;
    tile.h = h;
    tile.image.assign(
        reinterpret_cast<std::byte*>(pixels),
        reinterpret_cast<std::byte*>(pixels) + w * h * 4);
    tile.loaded  = true;
    tile.pending = false;

    stbi_image_free(pixels);
}

void TileTexture_Upload(OsmTileTexture& tile)
{
    std::lock_guard<std::mutex> guard(tile.mtx);
    if (!tile.loaded)
        return;

    if (tile.tex_id == 0)
        glGenTextures(1, &tile.tex_id);

    glBindTexture(GL_TEXTURE_2D, tile.tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 tile.w, tile.h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE,
                 tile.image.data());

    tile.loaded = false;
}

static size_t WriteCallback(void* src, size_t size, size_t count, void* dst)
{
    auto& buf = *static_cast<std::vector<std::byte>*>(dst);
    auto* ptr = static_cast<std::byte*>(src);
    buf.insert(buf.end(), ptr, ptr + size * count);
    return size * count;
}

static void ProcessJob(const OsmTileFetcher::Job& job)
{
    namespace fs = std::filesystem;

    const fs::path disk_cache = fs::path("cache") / std::to_string(job.tile.zoom) / std::to_string(job.tile.x) / (std::to_string(job.tile.y) + ".png");

    std::vector<std::byte> buf;

    if (std::ifstream file{disk_cache, std::ios::binary | std::ios::ate}; file.is_open()) {
        buf.resize(file.tellg());
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buf.data()), buf.size());
        job.on_done(job.tile, buf);
        return;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
        return;

    const std::string url = "https://tile.openstreetmap.org/" + std::to_string(job.tile.zoom) + "/" + std::to_string(job.tile.x)    + "/" + std::to_string(job.tile.y)    + ".png";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "VS-BACKEND/1.0");
    curl_easy_setopt(curl, CURLOPT_REFERER, "https://localhost/");

    const bool success = curl_easy_perform(curl) == CURLE_OK && !buf.empty();
    curl_easy_cleanup(curl);

    if (!success)
        return;

    std::error_code ec;
    std::filesystem::create_directories(disk_cache.parent_path(), ec);
    if (std::ofstream out{disk_cache, std::ios::binary})
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size());

    job.on_done(job.tile, buf);
}

static void WorkerLoop(OsmTileFetcher* fetcher)
{
    while (true) {
        OsmTileFetcher::Job job;
        {
            std::unique_lock<std::mutex> lock(fetcher->mtx);
            fetcher->signal.wait(lock, [fetcher] {
                return fetcher->stopping || !fetcher->jobs.empty();
            });
            if (fetcher->stopping)
                return;
            job = std::move(fetcher->jobs.front());
            fetcher->jobs.pop();
        }
        ProcessJob(job);
    }
}

void Fetcher_Init(OsmTileFetcher& fetcher, size_t thread_count)
{
    for (size_t i = 0; i < thread_count; ++i)
        fetcher.workers.emplace_back(WorkerLoop, &fetcher);
}

void Fetcher_Shutdown(OsmTileFetcher& fetcher)
{
    {
        std::unique_lock<std::mutex> lock(fetcher.mtx);
        fetcher.stopping = true;
    }
    fetcher.signal.notify_all();
    for (auto& w : fetcher.workers)
        if (w.joinable()) w.join();
    fetcher.workers.clear();
}

void Fetcher_Clear(OsmTileFetcher& fetcher)
{
    std::unique_lock<std::mutex> lock(fetcher.mtx);
    fetcher.jobs = {};
}

void Fetcher_Submit(OsmTileFetcher& fetcher, OsmTileCoord tile, TileCallback cb)
{
    {
        std::unique_lock<std::mutex> lock(fetcher.mtx);
        fetcher.jobs.push({tile, cb});
    }
    fetcher.signal.notify_one();
}

static double LatToMercatorY(double lat)
{
    const double rad = lat * M_PI / 180.0;
    return std::log(std::tan(rad) + 1.0 / std::cos(rad)) * (180.0 / M_PI);
}

static int LonToTileX(double lon, int zoom)
{
    return static_cast<int>(std::floor((0.5 + lon / 360.0) * (1 << zoom)));
}

static int MercYToTileY(double y, int zoom)
{
    return static_cast<int>(std::floor((0.5 - y / 360.0) * (1 << zoom)));
}

static double TileXToLon(int tx, int zoom)
{
    return (tx / static_cast<double>(1 << zoom) - 0.5) * 360.0;
}

static double TileYToMercY(int ty, int zoom)
{
    return (0.5 - ty / static_cast<double>(1 << zoom)) * 360.0;
}

void DrawMapTab()
{
    static bool first_run = true;
    if (first_run) {
        const double center_lon = 82.939999;
        const double center_lat = 54.981952;
        ImPlot::SetNextAxesLimits(
            center_lon - 0.1, center_lon + 0.1,
            LatToMercatorY(center_lat) - 0.1, LatToMercatorY(center_lat) + 0.1,
            ImPlotCond_Once);
        first_run = false;
    }

    if (!ImPlot::BeginPlot("##osm", ImVec2(-1, -1), ImPlotFlags_NoLegend | ImPlotFlags_Equal))
        return;

    static std::map<std::string, OsmTileTexture> tile_cache;
    static OsmTileFetcher fetcher;
    static bool fetcher_ready = false;
    static ImPlotRect last_view;
    static int last_zoom = -1;

    if (!fetcher_ready) {
        Fetcher_Init(fetcher, 8);
        fetcher_ready = true;
    }

    const ImPlotRect view = ImPlot::GetPlotLimits();
    const int zoom = std::clamp(
        static_cast<int>(-std::log(view.X.Max - view.X.Min) * 1.3 + 12), 4, 19);

    const bool view_changed =
        view.X.Min != last_view.X.Min || view.X.Max != last_view.X.Max ||
        view.Y.Min != last_view.Y.Min || view.Y.Max != last_view.Y.Max ||
        zoom != last_zoom;

    if (view_changed) {
        Fetcher_Clear(fetcher);
        for (auto& [key, tile] : tile_cache) {
            std::lock_guard<std::mutex> guard(tile.mtx);
            tile.pending = false;
        }
        last_view = view;
        last_zoom = zoom;
    }

    if (view.X.Max - view.X.Min >= 1200) {
        ImPlot::EndPlot();
        return;
    }

    const int wrap = 1 << zoom;
    for (int tx = LonToTileX(view.X.Min, zoom); tx <= LonToTileX(view.X.Max, zoom); tx++)
    {
        for (int ty = MercYToTileY(view.Y.Max, zoom); ty <= MercYToTileY(view.Y.Min, zoom); ty++)
        {
            const std::string key = std::to_string(zoom) + "_" + std::to_string(tx) + "_" + std::to_string(ty);
            OsmTileTexture& tile = tile_cache[key];

            if (tile.tex_id == 0 && !tile.pending) {
                tile.pending = true;
                const int rx = ((tx % wrap) + wrap) % wrap;
                const int ry = ((ty % wrap) + wrap) % wrap;
                Fetcher_Submit(fetcher, {zoom, rx, ry},
                    [&tile](const OsmTileCoord&, const std::vector<std::byte>& buf) {
                        TileTexture_Load(tile, buf);
                    });
            }

            TileTexture_Upload(tile);

            if (tile.tex_id != 0)
                ImPlot::PlotImage(("##t_" + key).c_str(),
                    static_cast<ImTextureID>(tile.tex_id),
                    {TileXToLon(tx, zoom), TileYToMercY(ty + 1, zoom)},
                    {TileXToLon(tx + 1, zoom), TileYToMercY(ty, zoom)},
                    {0, 0}, {1, 1}, {1, 1, 1, 1});
        }
    }

    static std::vector<MapPoint> map_points;
    static float last_load_time = 0.0f;
    const float now = ImGui::GetTime();
    if (now - last_load_time > 5.0f) {
        map_points = db_load_points();
        last_load_time = now;
    }
    DrawMapPoints(map_points, zoom);

    ImPlot::EndPlot();
}

static ImVec4 RssiToColor(int rssi)
{
    const float t = std::clamp((rssi - (-110.0f)) / (-65.0f - (-110.0f)), 0.0f, 1.0f);
    return ImVec4(1.0f - t, t, 0.0f, 0.6f);
}

void DrawMapPoints(const std::vector<MapPoint>& points, int zoom)
{
    for (const auto& p : points)
    {
        const double merc_y = LatToMercatorY(p.lat);
        const ImVec4 color = RssiToColor(p.rssi);

        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, color, 1.0f, color);
        double x = p.lon, y = merc_y;
        ImPlot::PlotScatter("##pt", &x, &y, 1);
    }
}