#include "AuthConnection.h"

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
