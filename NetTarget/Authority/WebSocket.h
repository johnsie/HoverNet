// WebSocket.h -- the server side of RFC 6455 for browsers, with no sockets and no clock: bytes in, bytes out.
//
// The browser path for protocol 3.0 is not a second protocol. A browser opens a (TLS-terminated, by the web server in front) WebSocket
// and every binary message it sends is a run of ordinary protocol 3.0 frames; the server feeds those bytes to the same validated
// session layer as a TCP connection and wraps what it sends back in binary messages. This class is only the adapter: the HTTP upgrade
// (path, origin allow-list, key), frame parsing (masking required, no extensions, bounded sizes, control frames, fragmentation) and
// frame writing. Anything unexpected closes the connection with a close frame; nothing is ever interpreted as protocol data before
// the upgrade has succeeded.
#ifndef AUTHORITY_WEBSOCKET_H
#define AUTHORITY_WEBSOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace WebSocket
{
std::string Base64(const std::uint8_t* data, std::size_t size);
void Sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]);
// The Sec-WebSocket-Accept value for a client key (RFC 6455 section 4.2.2).
std::string AcceptKey(const std::string& clientKey);
// One unmasked server-to-client frame. opcode 2 is binary, 8 close, 10 pong.
std::vector<std::uint8_t> EncodeFrame(std::uint8_t opcode, const std::uint8_t* payload, std::size_t size);

struct Config
{
    std::vector<std::string> origins;     // allowed Origin header values; the upgrade is refused for any other (browsers always send one)
    std::string path = "/auth/";
    std::size_t maxMessage = 8192;        // bytes in one message (protocol frames are at most 1200)
};

class Server
{
public:
    explicit Server(const Config& config) : mConfig(config) {}
    // Feeds received bytes. Binary message payloads are appended to `protocolBytes`; bytes to send (the upgrade response, pongs, a
    // close frame) are appended to `reply`. Returns false once the connection must be closed (after sending `reply`).
    bool Feed(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& protocolBytes, std::vector<std::uint8_t>& reply);
    bool Ready() const { return mUpgraded && !mClosing; }   // true when binary messages may be sent

private:
    bool Handshake(std::vector<std::uint8_t>& reply);
    bool Frames(std::vector<std::uint8_t>& protocolBytes, std::vector<std::uint8_t>& reply);
    bool Fail(std::vector<std::uint8_t>& reply, std::uint16_t code);
    bool Refuse(std::vector<std::uint8_t>& reply, const char* status);

    Config mConfig;
    std::vector<std::uint8_t> mBuffer;
    std::vector<std::uint8_t> mMessage;   // a fragmented message being assembled
    bool mUpgraded = false, mClosing = false, mInMessage = false;
};
}

#endif
