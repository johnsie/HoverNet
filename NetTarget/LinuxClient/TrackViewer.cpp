#include "../GraphicsSDL2/SDL2Graphics.h"
#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr int kWidth = 1024;
constexpr int kHeight = 768;
constexpr int kPadding = 32;

#ifndef HOVERNET_SOURCE_DIR
#define HOVERNET_SOURCE_DIR "."
#endif

std::string SourcePath(const char* relativePath)
{
    return std::string(HOVERNET_SOURCE_DIR) + "/" + relativePath;
}

struct Bounds
{
    MR_Int32 minX = std::numeric_limits<MR_Int32>::max();
    MR_Int32 maxX = std::numeric_limits<MR_Int32>::min();
    MR_Int32 minY = std::numeric_limits<MR_Int32>::max();
    MR_Int32 maxY = std::numeric_limits<MR_Int32>::min();
};

void Expand(Bounds& bounds, const MR_2DCoordinate& point)
{
    bounds.minX = std::min(bounds.minX, point.mX);
    bounds.maxX = std::max(bounds.maxX, point.mX);
    bounds.minY = std::min(bounds.minY, point.mY);
    bounds.maxY = std::max(bounds.maxY, point.mY);
}

Bounds GetBounds(const MR_Level& level)
{
    Bounds bounds;
    for (int room = 0; room < level.GetRoomCount(); ++room) {
        for (int vertex = 0; vertex < level.GetRoomVertexCount(room); ++vertex) {
            Expand(bounds, level.GetRoomVertex(room, vertex));
        }
    }
    return bounds;
}

void SetPixel(std::vector<MR_UInt8>& buffer, int x, int y, MR_UInt8 color)
{
    if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
        buffer[y * kWidth + x] = color;
    }
}

void DrawLine(std::vector<MR_UInt8>& buffer, int x0, int y0, int x1, int y1, MR_UInt8 color)
{
    const int deltaX = std::abs(x1 - x0);
    const int stepX = x0 < x1 ? 1 : -1;
    const int deltaY = -std::abs(y1 - y0);
    const int stepY = y0 < y1 ? 1 : -1;
    int error = deltaX + deltaY;

    while (true) {
        SetPixel(buffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int twiceError = 2 * error;
        if (twiceError >= deltaY) {
            error += deltaY;
            x0 += stepX;
        }
        if (twiceError <= deltaX) {
            error += deltaX;
            y0 += stepY;
        }
    }
}

class Mapper
{
public:
    explicit Mapper(const Bounds& bounds)
    {
        const double mapWidth = std::max<MR_Int32>(1, bounds.maxX - bounds.minX);
        const double mapHeight = std::max<MR_Int32>(1, bounds.maxY - bounds.minY);
        scale = std::min((kWidth - 2.0 * kPadding) / mapWidth, (kHeight - 2.0 * kPadding) / mapHeight);
        offsetX = (kWidth - scale * mapWidth) / 2.0 - scale * bounds.minX;
        offsetY = (kHeight - scale * mapHeight) / 2.0 + scale * bounds.maxY;
    }

    void Project(const MR_2DCoordinate& point, int& x, int& y) const
    {
        x = static_cast<int>(offsetX + scale * point.mX);
        y = static_cast<int>(offsetY - scale * point.mY);
    }

private:
    double scale;
    double offsetX;
    double offsetY;
};

void DrawSection(std::vector<MR_UInt8>& buffer, const MR_Level& level, int section, bool feature,
                 const Mapper& mapper, MR_UInt8 color)
{
    const int vertexCount = feature ? level.GetFeatureVertexCount(section) : level.GetRoomVertexCount(section);
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const MR_2DCoordinate& first = feature ? level.GetFeatureVertex(section, vertex) : level.GetRoomVertex(section, vertex);
        const MR_2DCoordinate& second = feature
            ? level.GetFeatureVertex(section, (vertex + 1) % vertexCount)
            : level.GetRoomVertex(section, (vertex + 1) % vertexCount);
        int x0;
        int y0;
        int x1;
        int y1;
        mapper.Project(first, x0, y0);
        mapper.Project(second, x1, y1);
        DrawLine(buffer, x0, y0, x1, y1, color);
    }
}

int ParseFrameCount(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--frames") == 0) {
        return std::max(1, std::atoi(argv[2]));
    }
    return -1;
}
}

int main(int argc, char** argv)
{
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_RecordFile* track = new MR_RecordFile;
    const std::string trackPath = SourcePath("NetTarget/Tracks/ClassicH.trk");
    if (!track->OpenForRead(trackPath.c_str())) {
        std::fprintf(stderr, "Could not open ClassicH.trk\n");
        delete track;
        return 1;
    }

    MR_GameSession session(FALSE);
    if (!session.LoadNew("ClassicH", track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load ClassicH.trk\n");
        return 1;
    }

    const MR_Level& level = *session.GetCurrentLevel();
    const Bounds bounds = GetBounds(level);
    if (bounds.minX > bounds.maxX || bounds.minY > bounds.maxY) {
        std::fprintf(stderr, "ClassicH.trk contains no renderable room geometry\n");
        return 1;
    }

    SDL2GraphicsBackend graphics;
    if (!graphics.Initialize(nullptr, kWidth, kHeight)) {
        return 1;
    }

    std::array<MR_UInt8, 256 * 3> palette{};
    palette[10 * 3] = 48;
    palette[10 * 3 + 1] = 170;
    palette[10 * 3 + 2] = 220;
    palette[11 * 3] = 255;
    palette[11 * 3 + 1] = 184;
    palette[11 * 3 + 2] = 70;
    palette[12 * 3] = 255;
    palette[12 * 3 + 1] = 255;
    palette[12 * 3 + 2] = 255;
    graphics.SetPalette(palette.data(), static_cast<int>(palette.size()));

    const Mapper mapper(bounds);
    std::vector<MR_UInt8> framebuffer(kWidth * kHeight, 0);
    for (int room = 0; room < level.GetRoomCount(); ++room) {
        DrawSection(framebuffer, level, room, false, mapper, 10);
    }
    for (int feature = 0; feature < level.GetFeatureCount(0); ++feature) {
        DrawSection(framebuffer, level, feature, true, mapper, 11);
    }
    for (int player = 0; player < level.GetPlayerCount(); ++player) {
        const MR_3DCoordinate& position = level.GetStartingPos(player);
        const MR_2DCoordinate point = {position.mX, position.mY};
        int x;
        int y;
        mapper.Project(point, x, y);
        DrawLine(framebuffer, x - 5, y, x + 5, y, 12);
        DrawLine(framebuffer, x, y - 5, x, y + 5, 12);
    }

    const int mapPixels = static_cast<int>(std::count_if(framebuffer.begin(), framebuffer.end(),
        [](MR_UInt8 pixel) { return pixel != 0; }));
    if (mapPixels == 0) {
        std::fprintf(stderr, "ClassicH.trk produced an empty map\n");
        return 1;
    }
    std::printf("ClassicH map: rooms=%d players=%d pixels=%d\n",
        level.GetRoomCount(), level.GetPlayerCount(), mapPixels);

    const int frameLimit = ParseFrameCount(argc, argv);
    int framesRendered = 0;
    bool running = true;
    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            running = event.type != SDL_QUIT &&
                !(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
        }
        graphics.Present(framebuffer.data(), kWidth, kHeight);
        ++framesRendered;
        SDL_Delay(16);
    }

    return 0;
}