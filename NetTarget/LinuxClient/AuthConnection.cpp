#include "AuthConnection.h"

#ifdef HOVERNET_WEB
// The browser build: the same protocol 3.0 frames over a WebSocket (a wss:// address, TLS provided by the web server in front of the game
// server; see docs/operators.md). The browser's own WebSocket object does the networking; these few functions give it a queue the game
// loop reads from, because the browser delivers messages while the game loop yields to it between frames.
#include <emscripten.h>
#include <algorithm>
#include "../Authority/Protocol3.h"

EM_JS(int, hn_ws_open, (const char* url), {
    var text = UTF8ToString(url);
    var ws;
    try { ws = new WebSocket(text); } catch (e) { return -1; }
    ws.binaryType = 'arraybuffer';
    Module.hnWs = Module.hnWs || { next: 1, socks: {} };
    var id = Module.hnWs.next++;
    var rec = { ws: ws, queue: [], state: 0 };
    ws.onopen = function() { rec.state = 1; };
    ws.onclose = function() { rec.state = 2; };
    ws.onerror = function() { };
    ws.onmessage = function(m) { if (m.data instanceof ArrayBuffer) rec.queue.push(new Uint8Array(m.data)); };
    Module.hnWs.socks[id] = rec;
    return id;
});
EM_JS(int, hn_ws_state, (int id), {
    var r = Module.hnWs && Module.hnWs.socks[id];
    return r ? r.state : 2;
});
EM_JS(int, hn_ws_send, (int id, const unsigned char* data, int size), {
    var r = Module.hnWs && Module.hnWs.socks[id];
    if (!r || r.state !== 1) return 0;
    r.ws.send(HEAPU8.slice(data, data + size));   // a copy: the heap may be shared memory, which a WebSocket will not send
    return 1;
});
// Copies up to cap bytes of the oldest received message into the buffer; -1 when nothing has arrived.
EM_JS(int, hn_ws_recv, (int id, unsigned char* out, int cap), {
    var r = Module.hnWs && Module.hnWs.socks[id];
    if (!r || r.queue.length === 0) return -1;
    var m = r.queue[0];
    var n = Math.min(m.length, cap);
    HEAPU8.set(m.subarray(0, n), out);
    if (n < m.length) r.queue[0] = m.subarray(n); else r.queue.shift();
    return n;
});
EM_JS(void, hn_ws_close, (int id), {
    var r = Module.hnWs && Module.hnWs.socks[id];
    if (r) { try { r.ws.close(); } catch (e) { } delete Module.hnWs.socks[id]; }
});

struct AuthConnection::Impl
{
    int id = -1;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> pending;
    bool open = false;
};

AuthConnection::AuthConnection() : mImpl(new Impl) {}
AuthConnection::~AuthConnection() { Close(); }

bool AuthConnection::Connect(const std::string& host, int, std::string& error, const std::uint8_t* serverKey)
{
    Close();
    if (serverKey != nullptr) { error = "The browser does not use a server key: use a wss:// address, which the web server in front of the game server secures."; return false; }
    if (host.compare(0, 5, "wss:/") != 0 && host.compare(0, 4, "ws:/") != 0) { error = "Type the server as a wss:// address."; return false; }
    mImpl->id = hn_ws_open(host.c_str());
    if (mImpl->id < 0) { error = "The browser refused that address."; return false; }
    mImpl->parser = Protocol3::FrameParser();
    mImpl->pending.clear();
    mImpl->open = true;
    return true;
}

void AuthConnection::Close()
{
    if (mImpl->id >= 0) hn_ws_close(mImpl->id);
    mImpl->id = -1;
    mImpl->open = false;
}

bool AuthConnection::IsOpen() const { return mImpl->open; }

bool AuthConnection::Send(std::uint8_t type, const std::vector<std::uint8_t>& payload)
{
    if (!mImpl->open) return false;
    const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(type, payload);
    mImpl->pending.insert(mImpl->pending.end(), frame.begin(), frame.end());
    return Flush();
}

bool AuthConnection::Flush()
{
    if (!mImpl->open) return false;
    const int state = hn_ws_state(mImpl->id);
    if (state == 2) { mImpl->open = false; return false; }
    if (state == 1) {
        // One message per few frames, well under the server's limit for a message (8,192 bytes).
        while (!mImpl->pending.empty()) {
            const std::size_t n = std::min<std::size_t>(mImpl->pending.size(), 4096);
            if (!hn_ws_send(mImpl->id, mImpl->pending.data(), static_cast<int>(n))) break;
            mImpl->pending.erase(mImpl->pending.begin(), mImpl->pending.begin() + static_cast<std::ptrdiff_t>(n));
        }
    }
    if (mImpl->pending.size() > (1u << 20)) { mImpl->open = false; return false; }
    return true;
}

bool AuthConnection::Pump(std::vector<AuthFrame>& out)
{
    if (!mImpl->open) return false;
    std::uint8_t buffer[8192];
    for (int rounds = 0; rounds < 64; ++rounds) {
        const int n = hn_ws_recv(mImpl->id, buffer, sizeof(buffer));
        if (n < 0) break;
        mImpl->parser.Feed(buffer, static_cast<std::size_t>(n));
    }
    AuthFrame frame;
    while (mImpl->parser.Next(frame.type, frame.payload)) out.push_back(frame);
    if (mImpl->parser.Failed()) mImpl->open = false;
    Flush();
    // Messages that arrived before a close are delivered above; the loss is reported after them.
    if (mImpl->open && hn_ws_state(mImpl->id) == 2 && hn_ws_recv(mImpl->id, buffer, 0) < 0) mImpl->open = false;
    return mImpl->open;
}
#else
#include "AuthSocket.h"
#include "../Authority/Protocol3.h"
#include "../Authority/SecureChannel.h"

struct AuthConnection::Impl
{
    AuthSocket socket = kInvalidSocket;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> pending;
    bool open = false;
    std::unique_ptr<Secure::Client> secure;
    std::vector<std::uint8_t> waiting;     // plaintext queued until the secure session is ready
};

AuthConnection::AuthConnection() : mImpl(new Impl) {}
AuthConnection::~AuthConnection() { Close(); }

bool AuthConnection::Connect(const std::string& host, int port, std::string& error, const std::uint8_t* serverKey)
{
    Close();
    if (!AuthNet::Startup()) { error = "network start-up failed"; return false; }
    mImpl->socket = AuthNet::Connect(host, port);
    if (mImpl->socket == kInvalidSocket) { error = "cannot connect to " + host + ":" + std::to_string(port); return false; }
    mImpl->parser = Protocol3::FrameParser();
    mImpl->pending.clear();
    mImpl->waiting.clear();
    mImpl->secure.reset();
    mImpl->open = true;
    if (serverKey != nullptr) {
        mImpl->secure.reset(new Secure::Client(serverKey));
        const std::vector<std::uint8_t> hello = mImpl->secure->Hello();
        mImpl->pending.insert(mImpl->pending.end(), hello.begin(), hello.end());
        Flush();
    }
    return true;
}

void AuthConnection::Close()
{
    if (mImpl->socket != kInvalidSocket) AuthNet::CloseSocket(mImpl->socket);
    mImpl->socket = kInvalidSocket;
    mImpl->open = false;
}

bool AuthConnection::IsOpen() const { return mImpl->open; }


bool AuthConnection::Send(std::uint8_t type, const std::vector<std::uint8_t>& payload)
{
    if (!mImpl->open) return false;
    const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(type, payload);
    if (mImpl->secure) {
        // Nothing can be sealed before the server's reply has given us the keys; queue it until then.
        mImpl->waiting.insert(mImpl->waiting.end(), frame.begin(), frame.end());
        if (mImpl->secure->Ready() && !mImpl->waiting.empty()) {
            const std::vector<std::uint8_t> sealed = mImpl->secure->Seal(mImpl->waiting.data(), mImpl->waiting.size());
            mImpl->pending.insert(mImpl->pending.end(), sealed.begin(), sealed.end());
            mImpl->waiting.clear();
        }
    }
    else {
        mImpl->pending.insert(mImpl->pending.end(), frame.begin(), frame.end());
    }
    return Flush();
}

bool AuthConnection::Flush()
{
    if (!mImpl->open) return false;
    while (!mImpl->pending.empty()) {
        const int sent = static_cast<int>(send(mImpl->socket, reinterpret_cast<const char*>(mImpl->pending.data()), static_cast<int>(mImpl->pending.size()), 0));
        if (sent > 0) { mImpl->pending.erase(mImpl->pending.begin(), mImpl->pending.begin() + sent); continue; }
        if (sent < 0 && AuthNet::WouldBlock()) break;
        mImpl->open = false;
        return false;
    }
    if (mImpl->pending.size() > (1u << 20)) { mImpl->open = false; return false; }   // the server stopped reading
    return true;
}

bool AuthConnection::Pump(std::vector<AuthFrame>& out)
{
    if (!mImpl->open) return false;
    std::uint8_t buffer[4096];
    for (int rounds = 0; rounds < 16; ++rounds) {
        const int n = static_cast<int>(recv(mImpl->socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0));
        if (n > 0) {
            if (mImpl->secure) {
                std::vector<std::uint8_t> plain;
                if (!mImpl->secure->Feed(buffer, static_cast<std::size_t>(n), plain)) { mImpl->open = false; break; }   // wrong server key or tampering
                mImpl->parser.Feed(plain.data(), plain.size());
                if (mImpl->secure->Ready() && !mImpl->waiting.empty()) {
                    const std::vector<std::uint8_t> sealed = mImpl->secure->Seal(mImpl->waiting.data(), mImpl->waiting.size());
                    mImpl->pending.insert(mImpl->pending.end(), sealed.begin(), sealed.end());
                    mImpl->waiting.clear();
                }
            }
            else {
                mImpl->parser.Feed(buffer, static_cast<std::size_t>(n));
            }
            continue;
        }
        if (n == 0 || !AuthNet::WouldBlock()) mImpl->open = false;
        break;
    }
    AuthFrame frame;
    while (mImpl->parser.Next(frame.type, frame.payload)) out.push_back(frame);
    if (mImpl->parser.Failed()) mImpl->open = false;
    Flush();
    // Whatever arrived before a close is still delivered to the caller above; a lost connection is reported after it.
    return mImpl->open;
}

#endif
