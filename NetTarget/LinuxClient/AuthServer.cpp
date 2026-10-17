// HoverNetAuthServer -- a dedicated server for authoritative races over TCP (protocol 3.0 framing, no encryption yet).
//
//   HoverNetAuthServer --track ClassicH --players 8 [--laps 3] [--port 9610] [--bind 127.0.0.1] [--once]
//                      [--limit-ticks N] [--start-delay-ms 3000] [--max-connections 64] [--replay-dir DIR]
//   HoverNetAuthServer --lobby [--tracks "ClassicH,The River"] [--port 9610]     many races: players host and join through the lobby
//
// It hosts one race at a time (RaceHost) and starts a fresh one when the race ends. It binds to the loopback address unless
// told otherwise: exposing it is a deliberate act, and until the encrypted session exists it should sit behind TLS
// termination or on a trusted network. One thread, non-blocking sockets, no allocation per tick beyond the host's own.
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AuthSocket.h"
#include "SimHarness.h"
#include "CrashReporter.h"
#include "../Authority/SecureChannel.h"
#include "../Authority/WebSocket.h"
#include "../Authority/LobbyServer.h"
#include "../Authority/Moderation.h"
#include "../Authority/RaceHost.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
struct Peer
{
    AuthSocket socket = kInvalidSocket;
    int connection = 0;
    std::vector<std::uint8_t> pending;   // bytes not yet accepted by the socket
    std::string address;
    // Transport security: undecided until the first two bytes arrive, then plain or secure for the life of the connection.
    bool decided = false;
    std::vector<std::uint8_t> firstBytes;
    std::unique_ptr<Secure::Server> secure;
    std::unique_ptr<WebSocket::Server> ws;   // a browser: the connection began with an HTTP upgrade request
};

// Whether connections may/must be encrypted, and the server's key.
struct Security
{
    bool enabled = false;                 // an identity is loaded: secure sessions are accepted
    bool require = false;                 // plain connections are refused
    std::uint8_t secret[32] = {0};
    bool wsEnabled = false;               // browsers may connect over WebSocket (behind a web server that terminates TLS)
    WebSocket::Config ws;
};

// Milliseconds since 1970, but advancing with the steady clock: the epoch is read once at start-up, so audit records have real
// dates and the race clock never jumps if the system clock is corrected.
std::int64_t NowMs()
{
    static const auto steadyStart = std::chrono::steady_clock::now();
    static const std::int64_t epochStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return epochStart + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - steadyStart).count();
}

bool FlushPeer(Peer& p)
{
    while (!p.pending.empty()) {
        const int sent = static_cast<int>(send(p.socket, reinterpret_cast<const char*>(p.pending.data()), static_cast<int>(p.pending.size()), 0));
        if (sent > 0) { p.pending.erase(p.pending.begin(), p.pending.begin() + sent); continue; }
        if (sent < 0 && AuthNet::WouldBlock()) return true;
        return false;
    }
    return true;
}
}


// One pass over the sockets for any server core (RaceHost or LobbyServer): accept, read, advance time, write, reap.
template <class Core>
void PumpSockets(Core& core, AuthSocket listener, std::vector<std::unique_ptr<Peer>>& peers, int maxConnections, Moderation* moderation, const Security& security)
{
    // Accept.
        for (;;) {
            sockaddr_in from;
            socklen_t len = sizeof(from);
            AuthSocket s = accept(listener, reinterpret_cast<sockaddr*>(&from), &len);
            if (s == kInvalidSocket) break;
            char text[64] = "?";
            inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
            if (moderation != nullptr && moderation->IsBanned(text)) { AuthNet::CloseSocket(s); continue; }   // a banned address gets nothing
            int sameAddress = 0;
            for (const auto& p : peers) if (p->address == text) ++sameAddress;
            const bool viaLocalProxy = security.wsEnabled && std::strcmp(text, "127.0.0.1") == 0;   // every browser arrives from the web server in front
            if (static_cast<int>(peers.size()) >= maxConnections || (sameAddress >= 16 && !viaLocalProxy)) { AuthNet::CloseSocket(s); continue; }   // a cap per address and overall
            AuthNet::SetNonBlocking(s);
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            std::unique_ptr<Peer> p(new Peer);
            p->socket = s;
            p->address = text;
            p->connection = core.Connect(NowMs());
            peers.push_back(std::move(p));
        }
        // Read.
        std::uint8_t buffer[4096];
        for (auto& p : peers) {
            if (p->socket == kInvalidSocket) continue;
            for (int rounds = 0; rounds < 8; ++rounds) {
                const int n = static_cast<int>(recv(p->socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0));
                if (n > 0) {
                    const std::uint8_t* bytes = buffer;
                    std::size_t count = static_cast<std::size_t>(n);
                    std::vector<std::uint8_t> joined;
                    if (!p->decided) {
                        // A secure handshake starts "HNS1"; a protocol frame cannot start with "HN" (that length is above the maximum).
                        p->firstBytes.insert(p->firstBytes.end(), bytes, bytes + count);
                        if (p->firstBytes.size() < 2) continue;
                        p->decided = true;
                        joined.swap(p->firstBytes);
                        bytes = joined.data();
                        count = joined.size();
                        if (security.wsEnabled && bytes[0] == 'G' && bytes[1] == 'E') {   // "GE": no protocol frame can start so (that length is above the maximum)
                            p->ws.reset(new WebSocket::Server(security.ws));
                        }
                        else if (security.enabled && Secure::Server::LooksLikeHandshake(bytes, count) && bytes[0] == 'H' && bytes[1] == 'N') {
                            p->secure.reset(new Secure::Server(security.secret));
                        }
                        else if (security.require) {
                            core.Disconnect(p->connection);   // plain connections are not accepted here
                            break;
                        }
                    }
                    if (p->ws) {
                        std::vector<std::uint8_t> plain, reply;
                        const bool open = p->ws->Feed(bytes, count, plain, reply);
                        p->pending.insert(p->pending.end(), reply.begin(), reply.end());
                        if (!plain.empty()) core.Receive(p->connection, plain.data(), plain.size(), NowMs());
                        if (!open) { core.Disconnect(p->connection); break; }
                    }
                    else if (p->secure) {
                        std::vector<std::uint8_t> plain, reply;
                        if (!p->secure->Feed(bytes, count, plain, reply)) { core.Disconnect(p->connection); break; }
                        p->pending.insert(p->pending.end(), reply.begin(), reply.end());
                        if (!plain.empty()) core.Receive(p->connection, plain.data(), plain.size(), NowMs());
                    }
                    else {
                        core.Receive(p->connection, bytes, count, NowMs());
                    }
                    continue;
                }
                if (n == 0 || !AuthNet::WouldBlock()) core.Disconnect(p->connection);
                break;
            }
        }
        core.Advance(NowMs());
        // Write and reap.
        for (auto& p : peers) {
            if (p->socket == kInvalidSocket) continue;
            const std::vector<std::uint8_t> out = core.TakeOutput(p->connection);
            if (p->ws) {
                if (p->ws->Ready() && !out.empty()) { const std::vector<std::uint8_t> framed = WebSocket::EncodeFrame(2, out.data(), out.size()); p->pending.insert(p->pending.end(), framed.begin(), framed.end()); }
            }
            else if (p->secure && p->secure->Ready()) {
                if (!out.empty()) { const std::vector<std::uint8_t> sealed = p->secure->Seal(out.data(), out.size()); p->pending.insert(p->pending.end(), sealed.begin(), sealed.end()); }
            }
            else if (!p->secure) {
                p->pending.insert(p->pending.end(), out.begin(), out.end());
            }
            const bool ok = FlushPeer(*p);
            if (!ok || (core.IsClosed(p->connection) && p->pending.empty())) { AuthNet::CloseSocket(p->socket); p->socket = kInvalidSocket; }
            else if (p->pending.size() > (1u << 20)) { core.Disconnect(p->connection); }     // a client that does not read is dropped
        }
        peers.erase(std::remove_if(peers.begin(), peers.end(), [](const std::unique_ptr<Peer>& p) { return p->socket == kInvalidSocket; }), peers.end());

}

// The moderators' console: a text protocol on its own loopback-only port (see Moderation.h). Lines in, replies out.
struct AdminPeer
{
    AuthSocket socket = kInvalidSocket;
    AdminConsole::Session session;
    std::string inbox;
    std::string outbox;
};

void PumpAdmin(AdminConsole& console, AuthSocket listener, std::vector<std::unique_ptr<AdminPeer>>& peers)
{
    for (;;) {
        sockaddr_in from;
        socklen_t len = sizeof(from);
        AuthSocket s = accept(listener, reinterpret_cast<sockaddr*>(&from), &len);
        if (s == kInvalidSocket) break;
        if (peers.size() >= 8) { AuthNet::CloseSocket(s); continue; }
        AuthNet::SetNonBlocking(s);
        char text[64] = "?";
        inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
        std::unique_ptr<AdminPeer> p(new AdminPeer);
        p->socket = s;
        p->session = console.Open(text);
        p->outbox = console.Greeting(p->session);
        peers.push_back(std::move(p));
    }
    char buffer[1024];
    for (auto& p : peers) {
        const int n = static_cast<int>(recv(p->socket, buffer, sizeof(buffer), 0));
        if (n > 0) p->inbox.append(buffer, static_cast<std::size_t>(n));
        else if (n == 0 || !AuthNet::WouldBlock()) p->session.closed = true;
        if (p->inbox.size() > 4096) { p->session.closed = true; p->inbox.clear(); }
        std::size_t nl;
        while (!p->session.closed && (nl = p->inbox.find('\n')) != std::string::npos) {
            const std::string line = p->inbox.substr(0, nl);
            p->inbox.erase(0, nl + 1);
            p->outbox += console.Handle(p->session, line, NowMs());
        }
        while (!p->outbox.empty()) {
            const int sent = static_cast<int>(send(p->socket, p->outbox.data(), static_cast<int>(p->outbox.size()), 0));
            if (sent > 0) { p->outbox.erase(0, static_cast<std::size_t>(sent)); continue; }
            if (sent < 0 && AuthNet::WouldBlock()) break;
            p->session.closed = true;
            break;
        }
        if (p->session.closed && p->outbox.empty()) { AuthNet::CloseSocket(p->socket); p->socket = kInvalidSocket; }
    }
    peers.erase(std::remove_if(peers.begin(), peers.end(), [](const std::unique_ptr<AdminPeer>& p) { return p->socket == kInvalidSocket; }), peers.end());
}

// A tiny HTTP responder on a loopback port for monitoring: GET /health and GET /metrics (Prometheus text format).
struct StatusPeer
{
    AuthSocket socket = kInvalidSocket;
    std::string request;
};

std::string Metric(const char* name, const char* help, const char* type, unsigned long long value)
{
    char line[256];
    std::snprintf(line, sizeof(line), "# HELP %s %s\n# TYPE %s %s\n%s %llu\n", name, help, name, type, name, value);
    return line;
}

// The result table beside each official replay (rank, player, laps, finish and best lap): what a leaderboard reads.
void WriteResults(const std::string& replayPath, const std::vector<Authority::PlayerResult>& results)
{
    const std::string path = replayPath.substr(0, replayPath.size() - 4) + ".results.tsv";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) { std::fprintf(stderr, "cannot write the results to %s\n", path.c_str()); return; }
    std::fprintf(f, "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n");
    for (const auto& r : results)
        std::fprintf(f, "%d\t%d\t%d\t%d\t%lld\t%lld\n", r.rank, r.player, r.laps, r.finished ? 1 : 0, static_cast<long long>(r.finishTimeMs), static_cast<long long>(r.bestLapMs));
    std::fclose(f);
}

// One JSON object per line for operators' tooling (--event-log FILE): server start, races hosted and finished with their results,
// drain. No player names or addresses are written, so the file needs no special retention.
FILE* g_eventLog = nullptr;
void LogEvent(const char* event, const std::string& fields = std::string())
{
    if (g_eventLog == nullptr) return;
    std::fprintf(g_eventLog, "{\"time_ms\":%lld,\"event\":\"%s\"%s%s}\n", static_cast<long long>(NowMs()), event, fields.empty() ? "" : ",", fields.c_str());
    std::fflush(g_eventLog);
}

std::string ResultFields(const std::string& track, std::uint32_t ticks, std::uint64_t hash, const std::vector<Authority::PlayerResult>& results, const std::vector<Authority::RaceEvent>& events)
{
    char head[256];
    std::snprintf(head, sizeof(head), "\"track\":\"%s\",\"ticks\":%u,\"final_hash\":\"%016llx\",\"results\":[", track.c_str(), ticks, static_cast<unsigned long long>(hash));
    std::string out = head;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        char row[160];
        std::snprintf(row, sizeof(row), "%s{\"player\":%d,\"rank\":%d,\"laps\":%d,\"finished\":%s,\"finish_ms\":%lld,\"best_lap_ms\":%lld}", i ? "," : "", r.player, r.rank, r.laps,
                      r.finished ? "true" : "false", static_cast<long long>(r.finishTimeMs), static_cast<long long>(r.bestLapMs));
        out += row;
    }
    out += "],\"laps\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        char row[160];
        std::snprintf(row, sizeof(row), "%s{\"tick\":%u,\"player\":%d,\"lap\":%d,\"finished\":%s,\"lap_ms\":%lld,\"race_ms\":%lld}", i ? "," : "", e.tick, e.player, e.lap,
                      e.finished ? "true" : "false", static_cast<long long>(e.lapMs), static_cast<long long>(e.raceMs));
        out += row;
    }
    return out + "]";
}

// SIGTERM and SIGINT start a drain instead of killing races under way (a rolling restart): see docs/operators.md.
volatile std::sig_atomic_t g_drainRequested = 0;
void OnDrainSignal(int) { g_drainRequested = g_drainRequested == 0 ? 1 : 3; }   // 1 drain, 3 a second signal: leave now

template <class MetricsFn>
void PumpStatus(AuthSocket listener, std::vector<std::unique_ptr<StatusPeer>>& peers, MetricsFn metrics)
{
    for (;;) {
        AuthSocket s = accept(listener, nullptr, nullptr);
        if (s == kInvalidSocket) break;
        if (peers.size() >= 16) { AuthNet::CloseSocket(s); continue; }
        AuthNet::SetNonBlocking(s);
        std::unique_ptr<StatusPeer> p(new StatusPeer);
        p->socket = s;
        peers.push_back(std::move(p));
    }
    for (auto& p : peers) {
        char buffer[1024];
        const int n = static_cast<int>(recv(p->socket, buffer, sizeof(buffer), 0));
        if (n > 0) p->request.append(buffer, static_cast<std::size_t>(n));
        else if (n == 0 || !AuthNet::WouldBlock()) { AuthNet::CloseSocket(p->socket); p->socket = kInvalidSocket; continue; }
        if (p->request.find("\r\n\r\n") == std::string::npos && p->request.find("\n\n") == std::string::npos && p->request.size() < 4096) continue;
        std::string body, status = "200 OK";
        if (p->request.compare(0, 12, "GET /health ") == 0) {
            if (g_drainRequested != 0) { status = "503 Service Unavailable"; body = "draining\n"; }   // load balancers stop sending players
            else body = "ok\n";
        }
        else if (p->request.compare(0, 13, "GET /metrics ") == 0) body = metrics();
        else { status = "404 Not Found"; body = "not found\n"; }
        const std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        send(p->socket, response.data(), static_cast<int>(response.size()), 0);
        AuthNet::CloseSocket(p->socket);
        p->socket = kInvalidSocket;
    }
    peers.erase(std::remove_if(peers.begin(), peers.end(), [](const std::unique_ptr<StatusPeer>& p) { return p->socket == kInvalidSocket; }), peers.end());
}

std::string RenderMetrics(const RaceHost::Metrics& m, unsigned long long races, unsigned long long racesHosted, unsigned long long connections, std::int64_t uptimeMs)
{
    std::string out;
    out += Metric("hovernet_uptime_seconds", "Seconds since the server started.", "counter", static_cast<unsigned long long>(uptimeMs / 1000));
    out += Metric("hovernet_races_running", "Races on the server now.", "gauge", races);
    out += Metric("hovernet_races_hosted_total", "Races hosted since start.", "counter", racesHosted);
    out += Metric("hovernet_connections_open", "Open connections now.", "gauge", connections);
    out += Metric("hovernet_connections_accepted_total", "Connections accepted.", "counter", m.connectionsAccepted);
    out += Metric("hovernet_connections_rejected_total", "Connections refused with a message.", "counter", m.rejectedConnections);
    out += Metric("hovernet_bytes_received_total", "Bytes received.", "counter", m.bytesIn);
    out += Metric("hovernet_bytes_sent_total", "Bytes sent.", "counter", m.bytesOut);
    out += Metric("hovernet_ticks_simulated_total", "Race ticks simulated.", "counter", m.ticksSimulated);
    out += Metric("hovernet_inputs_accepted_total", "Player inputs accepted.", "counter", m.inputsAccepted);
    out += Metric("hovernet_inputs_late_total", "Player inputs that arrived after their tick.", "counter", m.inputsLate);
    out += Metric("hovernet_invalid_messages_total", "Invalid messages received.", "counter", m.invalidMessages);
    out += Metric("hovernet_snapshots_served_total", "Snapshots served to spectators and returning players.", "counter", m.snapshotsServed);
    return out;
}

int main(int argc, char** argv)
{
    // --config FILE: one option per line, "name value" or "name" for a switch (the same names as the command line without the dashes);
    // '#' starts a comment. Command line options come after the file's and override it.
    std::vector<std::string> args(argv, argv + argc);
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] != "--config") continue;
        std::ifstream file(args[i + 1].c_str());
        if (!file) { std::fprintf(stderr, "cannot read the configuration file %s\n", args[i + 1].c_str()); return 2; }
        std::vector<std::string> fromFile;
        for (std::string line; std::getline(file, line);) {
            const std::size_t hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            std::istringstream words(line);
            std::string key, value;
            if (!(words >> key)) continue;
            std::getline(words, value);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '=')) value.erase(value.begin());
            while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) value.pop_back();
            fromFile.push_back("--" + key);
            if (!value.empty() && value != "true") fromFile.push_back(value);
        }
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        args.insert(args.begin() + 1, fromFile.begin(), fromFile.end());
        break;
    }
    std::vector<char*> argvStorage;
    for (std::string& a : args) argvStorage.push_back(&a[0]);
    argc = static_cast<int>(argvStorage.size());
    argv = argvStorage.data();

    HostConfig cfg;
    cfg.race.trackName = "ClassicH";
    cfg.race.players = 2;
    cfg.race.laps = 3;
    cfg.startDelayMs = 3000;
    int port = 9610, maxConnections = 64;
    std::string bind = "127.0.0.1";
    bool once = false;
    std::string replayDir;
    bool lobbyMode = false;
    std::string moderationDir;
    int adminPort = -1;
    int statusPort = -1;
    int drainTimeoutSeconds = 600;
    std::string eventLogPath;
    Security security;
    std::string crashDir;
    bool crashTest = false;
    std::string identityFile;
    bool requireEncryption = false;
    std::string trackList;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--track") cfg.race.trackName = value();
        else if (a == "--players") cfg.race.players = std::atoi(value());
        else if (a == "--laps") cfg.race.laps = std::atoi(value());
        else if (a == "--port") port = std::atoi(value());
        else if (a == "--bind") bind = value();
        else if (a == "--once") once = true;
        else if (a == "--limit-ticks") cfg.race.maxTicks = static_cast<std::uint32_t>(std::atol(value()));
        else if (a == "--start-delay-ms") cfg.startDelayMs = std::atoi(value());
        else if (a == "--max-connections") maxConnections = std::atoi(value());
        else if (a == "--replay-dir") replayDir = value();
        else if (a == "--lobby") lobbyMode = true;
        else if (a == "--moderation-dir") moderationDir = value();
        else if (a == "--admin-port") adminPort = std::atoi(value());
        else if (a == "--status-port") statusPort = std::atoi(value());
        else if (a == "--ws-origin") { std::stringstream list(value()); for (std::string origin; std::getline(list, origin, ',');) if (!origin.empty()) security.ws.origins.push_back(origin); security.wsEnabled = !security.ws.origins.empty(); }
        else if (a == "--ws-path") security.ws.path = value();
        else if (a == "--event-log") eventLogPath = value();
        else if (a == "--drain-timeout") drainTimeoutSeconds = std::atoi(value());
        else if (a == "--crash-dir") crashDir = value();
        else if (a == "--crash-test") crashTest = true;
        else if (a == "--identity-file") identityFile = value();
        else if (a == "--require-encryption") requireEncryption = true;
        else if (a == "--spectator-delay-ms") cfg.spectatorDelayMs = std::atoi(value());
        else if (a == "--tracks") trackList = value();
        else if (a == "--no-weapons") cfg.race.weapons = false;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    cfg.race.trackPath = SimHarness::TrackPath(cfg.race.trackName);

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    if (!AuthNet::Startup()) { std::fprintf(stderr, "network start-up failed\n"); return 1; }

    if (!identityFile.empty()) {
        std::string error;
        if (!Secure::LoadOrCreateIdentity(identityFile, security.secret, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        security.enabled = true;
        std::uint8_t publicKey[32];
        Crypto::X25519Public(publicKey, security.secret);
        std::printf("server key %s\n", Crypto::ToHex(publicKey, 32).c_str());
        std::fflush(stdout);
    }
    security.require = requireEncryption;
    if (requireEncryption && !security.enabled) { std::fprintf(stderr, "--require-encryption needs --identity-file\n"); return 2; }
    if (!eventLogPath.empty()) {
        g_eventLog = std::fopen(eventLogPath.c_str(), "ab");
        if (g_eventLog == nullptr) { std::fprintf(stderr, "cannot open the event log %s\n", eventLogPath.c_str()); return 1; }
    }
    std::signal(SIGTERM, OnDrainSignal);
    std::signal(SIGINT, OnDrainSignal);
    int boundPort = 0;
    AuthSocket listener = AuthNet::Listen(bind, port, boundPort);
    if (listener == kInvalidSocket) { std::fprintf(stderr, "cannot listen on %s:%d\n", bind.c_str(), port); return 1; }

    // Moderation (optional): an audit log on disk, the moderators' secrets, and the loopback console.
    std::unique_ptr<AuditLog> audit;
    std::unique_ptr<Moderation> moderation;
    std::unique_ptr<AdminConsole> console;
    AuthSocket adminListener = kInvalidSocket;
    std::vector<std::unique_ptr<AdminPeer>> adminPeers;
    if (!moderationDir.empty()) {
        audit.reset(new AuditLog(moderationDir + "/audit.log"));
        moderation.reset(new Moderation(*audit));
        std::map<std::string, std::string> secrets;
        std::ifstream in((moderationDir + "/moderators.txt").c_str());
        for (std::string line; std::getline(in, line);) {
            std::istringstream parts(line);
            std::string name, secret;
            if (parts >> name >> secret) secrets[name] = secret;
        }
        std::random_device seedDevice;
        console.reset(new AdminConsole(*moderation, secrets, (static_cast<std::uint64_t>(seedDevice()) << 32) ^ seedDevice()));
        cfg.moderation = moderation.get();
        if (adminPort >= 0) {
            int chosen = 0;
            adminListener = AuthNet::Listen("127.0.0.1", adminPort, chosen);
            if (adminListener == kInvalidSocket) { std::fprintf(stderr, "cannot open the admin console on port %d\n", adminPort); return 1; }
            std::printf("moderation console on 127.0.0.1:%d with %zu moderators; audit log %s/audit.log\n", chosen, secrets.size(), moderationDir.c_str());
            std::fflush(stdout);
        }
    }
    AuthSocket statusListener = kInvalidSocket;
    std::vector<std::unique_ptr<StatusPeer>> statusPeers;
    if (statusPort >= 0) {
        int chosen = 0;
        statusListener = AuthNet::Listen("127.0.0.1", statusPort, chosen);
        if (statusListener == kInvalidSocket) { std::fprintf(stderr, "cannot open the status port %d\n", statusPort); return 1; }
        std::printf("status on 127.0.0.1:%d (/health, /metrics)\n", chosen);
        std::fflush(stdout);
    }
    if (!crashDir.empty()) CrashReporter::Install(crashDir, HOVERNET_SERVER_VERSION);
    const std::int64_t serverStart = NowMs();
    std::int64_t lastPurge = 0;
    std::function<std::string()> metricsText = [&]() { return std::string(""); };
    auto housekeeping = [&]() {
        if (statusListener != kInvalidSocket) PumpStatus(statusListener, statusPeers, [&]() { return metricsText(); });
        if (crashTest && NowMs() - serverStart > 1500) { volatile int* bad = nullptr; *bad = 1; }
        if (adminListener != kInvalidSocket) PumpAdmin(*console, adminListener, adminPeers);
        if (moderation && NowMs() - lastPurge > 3600 * 1000) {   // hourly: chat context 24 hours, resolved reports 30 days, audit lines 180 days
            lastPurge = NowMs();
            moderation->Purge(NowMs(), 24LL * 3600 * 1000, 30LL * 24 * 3600 * 1000);
            audit->Purge(NowMs(), 180LL * 24 * 3600 * 1000);
        }
    };
    {   // Resume tokens must not be guessable: seed them from the operating system.
        std::random_device rd;
        cfg.tokenSeed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }
    if (lobbyMode) {
        LobbyConfig lobbyConfig;
        lobbyConfig.raceTemplate = cfg;
        std::stringstream names(trackList.empty() ? "ClassicH,Metro Spiral,Steeplechase,Switchback,The Alley2,The River,Tidal Causeway" : trackList);
        for (std::string name; std::getline(names, name, ',');) if (!name.empty()) lobbyConfig.tracks.push_back(name);
        lobbyConfig.maxConnections = maxConnections;
        LobbyServer lobby(lobbyConfig);
        lobby.SetRaceFinishedCallback([&](std::uint32_t id, const RaceHost& finished) {
            std::printf("race %u over after %u ticks, final hash %016llx\n", id, finished.Race().CurrentTick(), static_cast<unsigned long long>(finished.Race().StateHash()));
            for (const auto& r : finished.Race().Results()) std::printf("  player %d: rank %d, laps %d%s\n", r.player, r.rank, r.laps, r.finished ? ", finished" : "");
            LogEvent("race_finished", ResultFields(finished.Config().race.trackName, finished.Race().CurrentTick(), finished.Race().StateHash(), finished.Race().Results(), finished.Race().Events()));
            if (!replayDir.empty()) {
                char name[160];
                std::snprintf(name, sizeof(name), "/%s-%u-%lld.hnr", finished.Config().race.trackName.c_str(), id, static_cast<long long>(std::time(nullptr)));
                const std::string path = replayDir + name;
                const std::vector<std::uint8_t> bytes = finished.Race().ReplayBytes();
                FILE* f = std::fopen(path.c_str(), "wb");
                if (f != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size()) std::printf("replay saved to %s (%zu bytes)\n", path.c_str(), bytes.size());
                else std::fprintf(stderr, "cannot write the replay to %s\n", path.c_str());
                if (f != nullptr) std::fclose(f);
                WriteResults(path, finished.Race().Results());
            }
            std::fflush(stdout);
        });
        std::printf("listening on %s:%d, lobby with %zu tracks\n", bind.c_str(), boundPort, lobbyConfig.tracks.size());
        LogEvent("server_start", std::string("\"version\":\"") + HOVERNET_SERVER_VERSION + "\",\"mode\":\"lobby\"");
        std::fflush(stdout);
        metricsText = [&]() { return RenderMetrics(lobby.TotalMetrics(), static_cast<unsigned long long>(lobby.RaceCount()), lobby.RacesHosted(), static_cast<unsigned long long>(lobby.OpenConnections()), NowMs() - serverStart); };
        std::vector<std::unique_ptr<Peer>> lobbyPeers;
        std::int64_t drainStarted = -1, drainedAt = -1;
        for (;;) {
            PumpSockets(lobby, listener, lobbyPeers, maxConnections, moderation.get(), security);
            housekeeping();
            if (g_drainRequested != 0) {
                // Rolling restart: races under way finish (and their replays are saved), no race can be hosted or joined, and the
                // process leaves once none is running, or when the timeout ends. A second signal leaves at once.
                if (drainStarted < 0) {
                    drainStarted = NowMs();
                    lobby.SetDraining(true);
                    LogEvent("drain_start", "\"races_running\":" + std::to_string(lobby.RunningRaces()));
                    std::printf("draining: %d races running, leaving when they end (at most %d s)\n", lobby.RunningRaces(), drainTimeoutSeconds);
                    std::fflush(stdout);
                    g_drainRequested = 2;
                }
                if (lobby.RunningRaces() == 0 && drainedAt < 0) drainedAt = NowMs();
                if (g_drainRequested == 3 || (drainedAt >= 0 && NowMs() - drainedAt > 2500) || NowMs() - drainStarted > drainTimeoutSeconds * 1000LL) break;   // the pause lets the last results reach the players
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        LogEvent("drained", "\"races_still_running\":" + std::to_string(lobby.RunningRaces()));
        std::printf("drained after %lld ms, %d races were still running\n", static_cast<long long>(NowMs() - drainStarted), lobby.RunningRaces());
        std::fflush(stdout);
        for (auto& p : lobbyPeers) AuthNet::CloseSocket(p->socket);
        AuthNet::CloseSocket(listener);
        return 0;
    }

    std::unique_ptr<RaceHost> host(new RaceHost(cfg));
    std::string error;
    if (!host->Init(error)) { std::fprintf(stderr, "cannot host the race: %s\n", error.c_str()); return 1; }
    std::printf("listening on %s:%d, %s, %d players, %d laps\n", bind.c_str(), boundPort, cfg.race.trackName.c_str(), cfg.race.players, cfg.race.laps);
    LogEvent("server_start", std::string("\"version\":\"") + HOVERNET_SERVER_VERSION + "\",\"mode\":\"single\"");
    std::fflush(stdout);

    std::vector<std::unique_ptr<Peer>> peers;
    metricsText = [&]() { return RenderMetrics(host->GetMetrics(), 1, 1, static_cast<unsigned long long>(peers.size()), NowMs() - serverStart); };
    std::int64_t raceEndedAt = -1, drainStartedFixed = -1;
    for (;;) {
        PumpSockets(*host, listener, peers, maxConnections, moderation.get(), security);
        housekeeping();
        if (g_drainRequested != 0) {   // a race not yet started is simply abandoned; one under way finishes and is saved first
            if (drainStartedFixed < 0) { drainStartedFixed = NowMs(); std::printf("draining\n"); std::fflush(stdout); }
            if (g_drainRequested == 3 || !host->Started() || NowMs() - drainStartedFixed > drainTimeoutSeconds * 1000LL) break;
        }

        if (host->RaceFinished()) {
            if (raceEndedAt < 0) raceEndedAt = NowMs();
            bool flushed = true;
            for (const auto& p : peers) flushed = flushed && p->pending.empty();
            // Spectators are a delay behind: they get the last Ticks and the Result that long after the drivers do.
            const std::int64_t grace = 300 + (host->Spectators() > 0 ? cfg.spectatorDelayMs + 500 : 0);
            if (flushed && NowMs() - raceEndedAt > grace) {
                const std::vector<Authority::PlayerResult> results = host->Race().Results();
                std::printf("race over after %u ticks, final hash %016llx\n", host->Race().CurrentTick(), static_cast<unsigned long long>(host->Race().StateHash()));
                for (const auto& r : results) std::printf("  player %d: rank %d, laps %d%s\n", r.player, r.rank, r.laps, r.finished ? ", finished" : "");
                LogEvent("race_finished", ResultFields(cfg.race.trackName, host->Race().CurrentTick(), host->Race().StateHash(), results, host->Race().Events()));
                std::fflush(stdout);
                const RaceHost::Metrics& m = host->GetMetrics();
                std::printf("metrics: connections %llu accepted / %llu closed, %llu frames in (%llu bytes), %llu frames out (%llu bytes), %llu invalid, %llu ticks, inputs %llu accepted / %llu late / %llu too far, %llu snapshots\n",
                            (unsigned long long)m.connectionsAccepted, (unsigned long long)m.connectionsClosed, (unsigned long long)m.framesIn, (unsigned long long)m.bytesIn,
                            (unsigned long long)m.framesOut, (unsigned long long)m.bytesOut, (unsigned long long)m.invalidMessages, (unsigned long long)m.ticksSimulated,
                            (unsigned long long)m.inputsAccepted, (unsigned long long)m.inputsLate, (unsigned long long)m.inputsTooFar, (unsigned long long)m.snapshotsServed);
                std::fflush(stdout);
                if (!replayDir.empty()) {
                    // The official record of the race: a replay anyone can verify and watch.
                    char name[128];
                    std::snprintf(name, sizeof(name), "/%s-%lld.hnr", cfg.race.trackName.c_str(), static_cast<long long>(std::time(nullptr)));
                    const std::string path = replayDir + name;
                    const std::vector<std::uint8_t> bytes = host->Race().ReplayBytes();
                    FILE* f = std::fopen(path.c_str(), "wb");
                    if (f != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size()) std::printf("replay saved to %s (%zu bytes)\n", path.c_str(), bytes.size());
                    else std::fprintf(stderr, "cannot write the replay to %s\n", path.c_str());
                    if (f != nullptr) std::fclose(f);
                    WriteResults(path, results);
                    std::fflush(stdout);
                }
                if (once || g_drainRequested != 0) break;
                for (auto& p : peers) { host->Disconnect(p->connection); AuthNet::CloseSocket(p->socket); }
                peers.clear();
                host.reset(new RaceHost(cfg));
                if (!host->Init(error)) { std::fprintf(stderr, "cannot host the next race: %s\n", error.c_str()); return 1; }
                raceEndedAt = -1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (auto& p : peers) AuthNet::CloseSocket(p->socket);
    AuthNet::CloseSocket(listener);
    return 0;
}
