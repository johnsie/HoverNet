#include "AuthConnection.h"

#include "AuthSocket.h"
#include "../Authority/Protocol3.h"

struct AuthConnection::Impl
{
    AuthSocket socket = kInvalidSocket;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> pending;
    bool open = false;
};

AuthConnection::AuthConnection() : mImpl(new Impl) {}
AuthConnection::~AuthConnection() { Close(); }

bool AuthConnection::Connect(const std::string& host, int port, std::string& error)
{
    Close();
    if (!AuthNet::Startup()) { error = "network start-up failed"; return false; }
    mImpl->socket = AuthNet::Connect(host, port);
    if (mImpl->socket == kInvalidSocket) { error = "cannot connect to " + host + ":" + std::to_string(port); return false; }
    mImpl->parser = Protocol3::FrameParser();
    mImpl->pending.clear();
    mImpl->open = true;
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
    mImpl->pending.insert(mImpl->pending.end(), frame.begin(), frame.end());
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
        if (n > 0) { mImpl->parser.Feed(buffer, static_cast<std::size_t>(n)); continue; }
        if (n == 0 || !AuthNet::WouldBlock()) mImpl->open = false;
        break;
    }
    AuthFrame frame;
    while (mImpl->parser.Next(frame.type, frame.payload)) out.push_back(frame);
    if (mImpl->parser.Failed()) mImpl->open = false;
    // Whatever arrived before a close is still delivered to the caller above; a lost connection is reported after it.
    return mImpl->open;
}
