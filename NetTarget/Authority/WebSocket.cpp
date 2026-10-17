#include "WebSocket.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace WebSocket
{
namespace
{
std::uint32_t Rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}
}

void Sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20])
{
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<std::uint8_t> msg(data, data + size);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(msg[chunk + 4 * i]) << 24) | (static_cast<std::uint32_t>(msg[chunk + 4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + 4 * i + 2]) << 8) | static_cast<std::uint32_t>(msg[chunk + 4 * i + 3]);
        for (int i = 16; i < 80; ++i) w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const std::uint32_t t = Rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
}

std::string Base64(const std::uint8_t* data, std::size_t size)
{
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) | (i + 1 < size ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += i + 1 < size ? kTable[(v >> 6) & 63] : '=';
        out += i + 2 < size ? kTable[v & 63] : '=';
    }
    return out;
}

std::string AcceptKey(const std::string& clientKey)
{
    const std::string joined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::uint8_t digest[20];
    Sha1(reinterpret_cast<const std::uint8_t*>(joined.data()), joined.size(), digest);
    return Base64(digest, 20);
}

std::vector<std::uint8_t> EncodeFrame(std::uint8_t opcode, const std::uint8_t* payload, std::size_t size)
{
    std::vector<std::uint8_t> f;
    f.push_back(static_cast<std::uint8_t>(0x80 | (opcode & 0x0F)));
    if (size < 126) f.push_back(static_cast<std::uint8_t>(size));
    else if (size < 65536) { f.push_back(126); f.push_back(static_cast<std::uint8_t>(size >> 8)); f.push_back(static_cast<std::uint8_t>(size)); }
    else { f.push_back(127); for (int i = 7; i >= 0; --i) f.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(size) >> (8 * i))); }
    f.insert(f.end(), payload, payload + size);
    return f;
}

bool Server::Refuse(std::vector<std::uint8_t>& reply, const char* status)
{
    const std::string r = std::string("HTTP/1.1 ") + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    reply.insert(reply.end(), r.begin(), r.end());
    mClosing = true;
    return false;
}

bool Server::Fail(std::vector<std::uint8_t>& reply, std::uint16_t code)
{
    if (!mClosing) {
        const std::uint8_t payload[2] = {static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
        const std::vector<std::uint8_t> f = EncodeFrame(8, payload, 2);
        reply.insert(reply.end(), f.begin(), f.end());
    }
    mClosing = true;
    return false;
}

bool Server::Handshake(std::vector<std::uint8_t>& reply)
{
    static const char kEnd[] = "\r\n\r\n";
    const auto end = std::search(mBuffer.begin(), mBuffer.end(), kEnd, kEnd + 4);
    if (end == mBuffer.end()) return mBuffer.size() <= 4096 ? true : Refuse(reply, "431 Request Header Fields Too Large");
    const std::string head(mBuffer.begin(), end);
    mBuffer.erase(mBuffer.begin(), end + 4);
    std::size_t lineEnd = head.find("\r\n");
    const std::string request = head.substr(0, lineEnd);
    if (request != "GET " + mConfig.path + " HTTP/1.1") return Refuse(reply, request.compare(0, 4, "GET ") == 0 ? "404 Not Found" : "405 Method Not Allowed");
    std::string upgrade, connection, key, version, origin;
    int headers = 0;
    while (lineEnd != std::string::npos) {
        const std::size_t start = lineEnd + 2;
        lineEnd = head.find("\r\n", start);
        const std::string line = head.substr(start, lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
        if (line.empty()) continue;
        if (++headers > 40) return Refuse(reply, "431 Request Header Fields Too Large");
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) return Refuse(reply, "400 Bad Request");
        const std::string name = Lower(line.substr(0, colon)), value = Trim(line.substr(colon + 1));
        if (name == "upgrade") upgrade = Lower(value);
        else if (name == "connection") connection = Lower(value);
        else if (name == "sec-websocket-key") key = value;
        else if (name == "sec-websocket-version") version = value;
        else if (name == "origin") origin = value;
    }
    if (upgrade != "websocket" || connection.find("upgrade") == std::string::npos || key.size() != 24 || version != "13") return Refuse(reply, "400 Bad Request");
    if (std::find(mConfig.origins.begin(), mConfig.origins.end(), origin) == mConfig.origins.end()) return Refuse(reply, "403 Forbidden");
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + AcceptKey(key) + "\r\n\r\n";
    reply.insert(reply.end(), response.begin(), response.end());
    mUpgraded = true;
    return true;
}

bool Server::Frames(std::vector<std::uint8_t>& protocolBytes, std::vector<std::uint8_t>& reply)
{
    for (;;) {
        if (mBuffer.size() < 2) return true;
        const std::uint8_t b0 = mBuffer[0], b1 = mBuffer[1];
        const bool fin = (b0 & 0x80) != 0;
        const std::uint8_t opcode = b0 & 0x0F;
        if ((b0 & 0x70) != 0) return Fail(reply, 1002);       // no extensions are negotiated, so reserved bits must be clear
        if ((b1 & 0x80) == 0) return Fail(reply, 1002);       // client frames are always masked
        std::uint64_t length = b1 & 0x7F;
        std::size_t header = 2;
        if (length == 126) { if (mBuffer.size() < 4) return true; length = (static_cast<std::uint64_t>(mBuffer[2]) << 8) | mBuffer[3]; header = 4; }
        else if (length == 127) {
            if (mBuffer.size() < 10) return true;
            length = 0;
            for (int i = 0; i < 8; ++i) length = (length << 8) | mBuffer[2 + i];
            header = 10;
        }
        const bool control = (opcode & 0x08) != 0;
        if (control && (length > 125 || !fin)) return Fail(reply, 1002);
        if (length > mConfig.maxMessage) return Fail(reply, 1009);
        if (mBuffer.size() < header + 4 + length) return true;
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
        const std::uint8_t* mask = &mBuffer[header];
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = mBuffer[header + 4 + i] ^ mask[i % 4];
        mBuffer.erase(mBuffer.begin(), mBuffer.begin() + static_cast<std::ptrdiff_t>(header + 4 + length));
        if (opcode == 8) { return Fail(reply, 1000); }
        if (opcode == 9) { const std::vector<std::uint8_t> pong = EncodeFrame(10, payload.data(), payload.size()); reply.insert(reply.end(), pong.begin(), pong.end()); continue; }
        if (opcode == 10) continue;
        if (opcode == 1) return Fail(reply, 1003);            // text is not part of the protocol
        if (opcode == 2 || opcode == 0) {
            if (opcode == 2) { if (mInMessage) return Fail(reply, 1002); mInMessage = true; mMessage.clear(); }
            else if (!mInMessage) return Fail(reply, 1002);
            mMessage.insert(mMessage.end(), payload.begin(), payload.end());
            if (mMessage.size() > mConfig.maxMessage) return Fail(reply, 1009);
            if (fin) { protocolBytes.insert(protocolBytes.end(), mMessage.begin(), mMessage.end()); mMessage.clear(); mInMessage = false; }
            continue;
        }
        return Fail(reply, 1002);                             // reserved opcodes
    }
}

bool Server::Feed(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& protocolBytes, std::vector<std::uint8_t>& reply)
{
    if (mClosing) return false;
    mBuffer.insert(mBuffer.end(), data, data + size);
    if (!mUpgraded) {
        if (!Handshake(reply)) return false;
        if (!mUpgraded) return true;
    }
    return Frames(protocolBytes, reply);
}
}
