// AuthConnection.h -- the game client's TCP connection to an authoritative server (protocol 3.0 framing).
//
// Kept apart from the game client's own source so the socket headers (Winsock on Windows) never meet the MFC and SDL headers
// there. Non-blocking: Pump() reads whatever has arrived and returns the whole frames; Send() queues and writes what it can.
// Not available in the browser build (it would use a WebSocket; see docs/authoritative-race.md).
#ifndef AUTH_CONNECTION_H
#define AUTH_CONNECTION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AuthFrame
{
    std::uint8_t type = 0;
    std::vector<std::uint8_t> payload;
};

class AuthConnection
{
public:
    AuthConnection();
    ~AuthConnection();

    bool Connect(const std::string& host, int port, std::string& error);
    void Close();
    bool IsOpen() const;
    // Queues a frame (type + payload) and writes as much as the socket takes. False once the connection is lost.
    bool Send(std::uint8_t type, const std::vector<std::uint8_t>& payload);
    // Reads what has arrived and appends whole frames to `out`. False once the connection is lost or sent a bad frame.
    bool Pump(std::vector<AuthFrame>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

#endif
