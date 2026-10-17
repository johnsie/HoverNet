// HoverNetNetProxy -- a TCP proxy that makes a good network bad, for testing (Stage 2's network-condition harness).
//
//   HoverNetNetProxy --listen-port P --target-port Q [--delay-ms D] [--jitter-ms J] [--kbps K] [--drop-after-bytes N]
//                    [--stall-ms S --stall-every-ms E] [--seed N]
//
// Everything a client sends reaches the target after D ms (plus up to J ms of jitter) and the same on the way back; bytes keep their
// order, as TCP promises, so a "bad" path shows up as lateness, bursts and stalls (what loss and reordering look like above TCP).
//   --kbps K                      limit each direction to K kilobits a second
//   --stall-ms S --stall-every-ms E   freeze both directions for S ms every E ms (a loss burst: nothing moves, then everything arrives)
//   --drop-after-bytes N          close a connection after N bytes have crossed it (a disconnect), once per connection
// It listens on the loopback address only and prints "listening on 127.0.0.1:PORT, ..." like the server.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "AuthSocket.h"

namespace
{
std::int64_t NowMs()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

struct Chunk { std::int64_t release; std::vector<char> bytes; };

struct Direction
{
    std::deque<Chunk> queue;
    std::int64_t earliest = 0;           // bandwidth: the next chunk may not leave before this
    std::uint64_t carried = 0;
};

struct Pipe
{
    AuthSocket client = kInvalidSocket, target = kInvalidSocket;
    Direction up, down;                  // client -> target, target -> client
    bool dropped = false;
};

struct Options
{
    int delay = 0, jitter = 0, kbps = 0, stall = 0, stallEvery = 0;
    long long dropAfter = 0;
    std::uint64_t seed = 1;
};

std::uint64_t gState = 1;
std::uint32_t Rand() { gState ^= gState >> 12; gState ^= gState << 25; gState ^= gState >> 27; return static_cast<std::uint32_t>((gState * 2685821657736338717ULL) >> 32); }

bool Stalled(const Options& o, std::int64_t now)
{
    return o.stall > 0 && o.stallEvery > 0 && (now % o.stallEvery) < o.stall;
}

void Read(AuthSocket from, Direction& dir, const Options& o, std::int64_t now, bool& closed)
{
    char buffer[4096];
    for (int i = 0; i < 8; ++i) {
        const int n = static_cast<int>(recv(from, buffer, sizeof(buffer), 0));
        if (n > 0) {
            std::int64_t release = now + o.delay + (o.jitter > 0 ? static_cast<int>(Rand() % (o.jitter + 1)) : 0);
            if (!dir.queue.empty()) release = std::max(release, dir.queue.back().release);   // keep the order
            if (o.kbps > 0) {
                dir.earliest = std::max(dir.earliest, now) + static_cast<std::int64_t>(n) * 8 / o.kbps;
                release = std::max(release, dir.earliest);
            }
            dir.queue.push_back({release, std::vector<char>(buffer, buffer + n)});
            continue;
        }
        if (n == 0 || !AuthNet::WouldBlock()) closed = true;
        break;
    }
}

void Write(AuthSocket to, Direction& dir, const Options& o, std::int64_t now, bool& closed)
{
    if (Stalled(o, now)) return;
    while (!dir.queue.empty() && dir.queue.front().release <= now) {
        Chunk& c = dir.queue.front();
        const int sent = static_cast<int>(send(to, c.bytes.data(), static_cast<int>(c.bytes.size()), 0));
        if (sent > 0) {
            dir.carried += static_cast<std::uint64_t>(sent);
            c.bytes.erase(c.bytes.begin(), c.bytes.begin() + sent);
            if (c.bytes.empty()) dir.queue.pop_front();
            continue;
        }
        if (sent < 0 && AuthNet::WouldBlock()) break;
        closed = true;
        break;
    }
}
}

int main(int argc, char** argv)
{
    int listenPort = 0, targetPort = 0;
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (a == "--listen-port") listenPort = std::atoi(value());
        else if (a == "--target-port") targetPort = std::atoi(value());
        else if (a == "--delay-ms") o.delay = std::atoi(value());
        else if (a == "--jitter-ms") o.jitter = std::atoi(value());
        else if (a == "--kbps") o.kbps = std::atoi(value());
        else if (a == "--stall-ms") o.stall = std::atoi(value());
        else if (a == "--stall-every-ms") o.stallEvery = std::atoi(value());
        else if (a == "--drop-after-bytes") o.dropAfter = std::atoll(value());
        else if (a == "--seed") gState = static_cast<std::uint64_t>(std::atoll(value())) * 2685821657736338717ULL + 1;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (targetPort <= 0) { std::fprintf(stderr, "usage: %s --listen-port P --target-port Q [options]\n", argv[0]); return 2; }
    if (!AuthNet::Startup()) return 1;
    int bound = 0;
    AuthSocket listener = AuthNet::Listen("127.0.0.1", listenPort, bound);
    if (listener == kInvalidSocket) { std::fprintf(stderr, "cannot listen\n"); return 1; }
    std::printf("listening on 127.0.0.1:%d, to port %d with %d ms delay, %d ms jitter\n", bound, targetPort, o.delay, o.jitter);
    std::fflush(stdout);

    std::vector<std::unique_ptr<Pipe>> pipes;
    for (;;) {
        for (;;) {
            AuthSocket c = accept(listener, nullptr, nullptr);
            if (c == kInvalidSocket) break;
            AuthSocket t = AuthNet::Connect("127.0.0.1", targetPort);
            if (t == kInvalidSocket) { AuthNet::CloseSocket(c); continue; }
            AuthNet::SetNonBlocking(c);
            std::unique_ptr<Pipe> p(new Pipe);
            p->client = c;
            p->target = t;
            pipes.push_back(std::move(p));
        }
        const std::int64_t now = NowMs();
        for (auto& p : pipes) {
            bool closed = false;
            Read(p->client, p->up, o, now, closed);
            Read(p->target, p->down, o, now, closed);
            Write(p->target, p->up, o, now, closed);
            Write(p->client, p->down, o, now, closed);
            if (o.dropAfter > 0 && !p->dropped && static_cast<long long>(p->up.carried + p->down.carried) >= o.dropAfter) { p->dropped = true; closed = true; }
            if (closed) {
                // Deliver what was already due, then hang up both ends.
                AuthNet::CloseSocket(p->client);
                AuthNet::CloseSocket(p->target);
                p->client = p->target = kInvalidSocket;
            }
        }
        pipes.erase(std::remove_if(pipes.begin(), pipes.end(), [](const std::unique_ptr<Pipe>& p) { return p->client == kInvalidSocket; }), pipes.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
