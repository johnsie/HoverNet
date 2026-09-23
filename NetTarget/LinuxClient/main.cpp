#include "../GraphicsSDL2/SDL2Graphics.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace
{
constexpr uint8_t kGameNameMessageType = 42;

bool SendServerHostedJoin(const char* host, const char* gameName)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = nullptr;
    if (getaddrinfo(host, "9600", &hints, &addresses) != 0) {
        return false;
    }

    int socketFd = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        socketFd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socketFd >= 0 && connect(socketFd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        if (socketFd >= 0) {
            close(socketFd);
            socketFd = -1;
        }
    }
    freeaddrinfo(addresses);
    if (socketFd < 0) {
        return false;
    }

    size_t nameLength = strlen(gameName);
    if (nameLength == 0 || nameLength > 255) {
        close(socketFd);
        return false;
    }

    std::vector<uint8_t> message(3 + nameLength);
    const uint16_t header = static_cast<uint16_t>(kGameNameMessageType << 10);
    message[0] = static_cast<uint8_t>(header & 0xFF);
    message[1] = static_cast<uint8_t>(header >> 8);
    message[2] = static_cast<uint8_t>(nameLength);
    memcpy(message.data() + 3, gameName, nameLength);

    ssize_t bytesSent = send(socketFd, message.data(), message.size(), 0);
    close(socketFd);
    return bytesSent == static_cast<ssize_t>(message.size());
}

int ParseFrameCount(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "--frames") == 0) {
        return std::max(1, std::atoi(argv[2]));
    }
    return -1;
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--server-check") == 0) {
        const bool connected = SendServerHostedJoin("192.168.10.181", "linux-bootstrap");
        fprintf(stdout, "RaceServer join %s\n", connected ? "succeeded" : "failed");
        return connected ? 0 : 1;
    }

    constexpr int width = 800;
    constexpr int height = 600;

    SDL2GraphicsBackend graphics;
    if (!graphics.Initialize(nullptr, width, height)) {
        return 1;
    }

    std::array<uint8_t, 256 * 3> palette{};
    for (int index = 0; index < 256; ++index) {
        palette[index * 3] = static_cast<uint8_t>(index);
        palette[index * 3 + 1] = static_cast<uint8_t>((index * 5) % 256);
        palette[index * 3 + 2] = static_cast<uint8_t>(255 - index);
    }
    graphics.SetPalette(palette.data(), static_cast<int>(palette.size()));

    std::vector<uint8_t> framebuffer(width * height);
    const int frameLimit = ParseFrameCount(argc, argv);
    int framesRendered = 0;
    bool running = true;
    uint8_t phase = 0;

    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                framebuffer[y * width + x] = static_cast<uint8_t>((x + y + phase) % 256);
            }
        }
        graphics.Present(framebuffer.data(), width, height);
        ++phase;
        ++framesRendered;
        SDL_Delay(16);
    }

    return 0;
}