// Tests of the WebSocket adapter (NetTarget/Authority/WebSocket.*): the RFC 6455 test vectors, the upgrade rules (path, origin, key),
// masked frames, control frames, fragmentation, size limits, and random bytes.
//
//   HoverNetWebSocketTest
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../Authority/WebSocket.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

using Bytes = std::vector<std::uint8_t>;

Bytes Text(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string AsString(const Bytes& b) { return std::string(b.begin(), b.end()); }

WebSocket::Config MakeConfig()
{
    WebSocket::Config c;
    c.origins = {"https://hovernet.example"};
    c.path = "/auth/";
    c.maxMessage = 2000;
    return c;
}

std::string Upgrade(const std::string& origin = "https://hovernet.example", const std::string& path = "/auth/", const std::string& key = "dGhlIHNhbXBsZSBub25jZQ==")
{
    return "GET " + path + " HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: " + key +
           "\r\nSec-WebSocket-Version: 13\r\nOrigin: " + origin + "\r\n\r\n";
}

// A masked client frame.
Bytes Frame(std::uint8_t opcode, const Bytes& payload, bool fin = true, bool masked = true)
{
    Bytes f;
    f.push_back(static_cast<std::uint8_t>((fin ? 0x80 : 0) | opcode));
    const std::uint8_t maskBit = masked ? 0x80 : 0;
    if (payload.size() < 126) f.push_back(static_cast<std::uint8_t>(maskBit | payload.size()));
    else { f.push_back(static_cast<std::uint8_t>(maskBit | 126)); f.push_back(static_cast<std::uint8_t>(payload.size() >> 8)); f.push_back(static_cast<std::uint8_t>(payload.size())); }
    const std::uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    if (masked) f.insert(f.end(), mask, mask + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) f.push_back(masked ? payload[i] ^ mask[i % 4] : payload[i]);
    return f;
}

struct Session
{
    WebSocket::Server server{MakeConfig()};
    Bytes protocol, reply;
    bool open = true;
    bool Send(const Bytes& b) { open = server.Feed(b.data(), b.size(), protocol, reply); return open; }
    bool Send(const std::string& s) { return Send(Text(s)); }
    bool Upgraded() { return AsString(reply).find("101 Switching Protocols") != std::string::npos; }
};
}

int main()
{
    // ---- test vectors ----------------------------------------------------------------------------------------------
    {
        std::uint8_t d[20];
        WebSocket::Sha1(reinterpret_cast<const std::uint8_t*>("abc"), 3, d);
        const std::uint8_t abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
        Check(std::memcmp(d, abc, 20) == 0, "SHA-1 of abc");
        // A message longer than one block (the 56-byte NIST vector).
        const std::string long56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        WebSocket::Sha1(reinterpret_cast<const std::uint8_t*>(long56.data()), long56.size(), d);
        const std::uint8_t v56[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae, 0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
        Check(std::memcmp(d, v56, 20) == 0, "SHA-1 of the 56-byte NIST message");
        Check(WebSocket::AcceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "the RFC 6455 accept key example");
        Check(WebSocket::Base64(reinterpret_cast<const std::uint8_t*>("fo"), 2) == "Zm8=", "base64 padding");
    }
    // ---- the upgrade -----------------------------------------------------------------------------------------------
    {
        Session s;
        Check(s.Send(Upgrade()) && s.Upgraded() && s.server.Ready(), "a good upgrade is accepted");
        Check(AsString(s.reply).find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos, "the response carries the accept key");
    }
    {   // Split across reads, one byte at a time.
        Session s;
        const std::string req = Upgrade();
        for (char c : req) s.Send(std::string(1, c));
        Check(s.Upgraded(), "an upgrade delivered a byte at a time is accepted");
    }
    {
        Session s; Check(!s.Send(Upgrade("https://evil.example")) && AsString(s.reply).find("403") != std::string::npos, "a foreign origin is refused with 403");
        Session n; Check(!n.Send(Upgrade("")) && AsString(n.reply).find("403") != std::string::npos, "a missing origin is refused");
        Session p; Check(!p.Send(Upgrade("https://hovernet.example", "/other/")) && AsString(p.reply).find("404") != std::string::npos, "another path is a 404");
        Session k; Check(!k.Send(Upgrade("https://hovernet.example", "/auth/", "short")) && AsString(k.reply).find("400") != std::string::npos, "a bad key is a 400");
        Session m; Check(!m.Send("POST /auth/ HTTP/1.1\r\n\r\n") && AsString(m.reply).find("405") != std::string::npos, "a POST is a 405");
        Session big; big.Send(std::string(5000, 'a')); Check(!big.open, "an endless header is cut off");
    }
    // ---- frames ----------------------------------------------------------------------------------------------------
    {
        Session s; s.Send(Upgrade());
        Check(s.Send(Frame(2, Text("hello"))) && AsString(s.protocol) == "hello", "a masked binary message is delivered");
        Check(s.Send(Frame(2, Bytes(300, 7))) && s.protocol.size() == 305, "a 16-bit length message is delivered");
        s.reply.clear();
        Check(s.Send(Frame(9, Text("pp"))) && s.reply == WebSocket::EncodeFrame(10, reinterpret_cast<const std::uint8_t*>("pp"), 2), "a ping is answered with a pong");
        s.protocol.clear();
        Check(s.Send(Frame(2, Text("ab"), false)) && s.Send(Frame(0, Text("cd"), false)) && s.protocol.empty() && s.Send(Frame(0, Text("ef"), true)) && AsString(s.protocol) == "abcdef", "a fragmented message is reassembled");
        Check(s.Send(Frame(2, Text("x"))) && s.Send(Frame(2, Text("y"))) && AsString(s.protocol) == "abcdefxy", "messages after a fragmented one still work");
        // Two frames in one read, and a frame cut in two.
        Bytes two = Frame(2, Text("1")); const Bytes second = Frame(2, Text("2")); two.insert(two.end(), second.begin(), second.end());
        s.protocol.clear();
        Check(s.Send(two) && AsString(s.protocol) == "12", "two frames in one read");
        const Bytes whole = Frame(2, Text("split")); s.protocol.clear();
        Check(s.Send(Bytes(whole.begin(), whole.begin() + 3)) && s.protocol.empty() && s.Send(Bytes(whole.begin() + 3, whole.end())) && AsString(s.protocol) == "split", "a frame cut in two");
    }
    {
        Session u; u.Send(Upgrade()); Check(!u.Send(Frame(2, Text("x"), true, false)), "an unmasked client frame closes the connection");
        Session t; t.Send(Upgrade()); Check(!t.Send(Frame(1, Text("text"))), "a text message closes the connection (1003)");
        Session o; o.Send(Upgrade()); Check(!o.Send(Frame(2, Bytes(2001, 1))), "a message over the limit closes the connection (1009)");
        Session g; g.Send(Upgrade()); Check(!g.Send(Frame(0, Text("orphan"))), "a continuation with nothing to continue closes it");
        Session f; f.Send(Upgrade()); f.Send(Frame(2, Text("a"), false)); Check(!f.Send(Frame(2, Text("b"))), "a new message inside a fragmented one closes it");
        Session r; r.Send(Upgrade()); Bytes bad = Frame(2, Text("x")); bad[0] |= 0x40; Check(!r.Send(bad), "a reserved bit closes the connection");
        Session x; x.Send(Upgrade()); Check(!x.Send(Frame(3, Text("x"))), "a reserved opcode closes the connection");
        Session c; c.Send(Upgrade()); c.reply.clear(); Check(!c.Send(Frame(8, Bytes{0x03, 0xE8})) && !c.reply.empty() && c.reply[0] == 0x88, "a close frame is answered with a close frame");
        Session pc; pc.Send(Upgrade()); Check(!pc.Send(Frame(9, Bytes(126, 0))), "an oversized ping closes the connection");
        Session fc; fc.Send(Upgrade()); Check(!fc.Send(Frame(9, Text("x"), false)), "a fragmented control frame closes the connection");
        Session after; after.Send(Upgrade()); after.Send(Frame(1, Text("x"))); Check(!after.Send(Frame(2, Text("y"))) && after.protocol.empty(), "nothing is delivered after a close");
    }
    // ---- random bytes, before and after the upgrade ----------------------------------------------------------------
    {
        std::uint64_t seed = 99;
        auto next = [&seed]() { seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27; return seed * 2685821657736338717ULL; };
        for (int round = 0; round < 3000; ++round) {
            Session s;
            if (round % 2) s.Send(Upgrade());
            Bytes junk(1 + next() % 200);
            for (auto& b : junk) b = static_cast<std::uint8_t>(next());
            if (round % 4 == 3) junk[0] = 0x82;   // a plausible first byte
            s.Send(junk);
        }
        Check(true, "3000 sessions of random bytes");
    }
    std::printf("websocket tests %s\n", gFailures == 0 ? "passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
