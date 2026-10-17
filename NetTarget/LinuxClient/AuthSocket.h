// AuthSocket.h -- the few TCP calls the authoritative server and its bot need, for POSIX sockets and Winsock alike.
#ifndef AUTH_SOCKET_H
#define AUTH_SOCKET_H

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET AuthSocket;
static const AuthSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int AuthSocket;
static const AuthSocket kInvalidSocket = -1;
#endif

namespace AuthNet
{
inline bool Startup()
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

inline void CloseSocket(AuthSocket s)
{
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

inline bool SetNonBlocking(AuthSocket s)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

inline bool WouldBlock()
{
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
}

// Listening socket bound to bindAddress:port (port 0 picks a free one; the chosen port is returned in outPort).
inline AuthSocket Listen(const std::string& bindAddress, int port, int& outPort)
{
    AuthSocket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kInvalidSocket) return kInvalidSocket;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) { CloseSocket(s); return kInvalidSocket; }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 64) != 0) { CloseSocket(s); return kInvalidSocket; }
    socklen_t len = sizeof(addr);
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    outPort = ntohs(addr.sin_port);
    SetNonBlocking(s);
    return s;
}

inline bool ConnectInProgress()
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS || errno == EINTR;
#endif
}

// Connects to host:port: a name (looked up) or a numeric IPv4 address. Gives up after timeoutMs (a blocked port must not hang a menu).
inline AuthSocket Connect(const std::string& host, int port, int timeoutMs = 4000)
{
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) return kInvalidSocket;
    AuthSocket connected = kInvalidSocket;
    for (addrinfo* a = found; a != nullptr && connected == kInvalidSocket; a = a->ai_next) {
        AuthSocket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kInvalidSocket) continue;
        SetNonBlocking(s);
        const int rc = connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
        bool ok = rc == 0;
        if (!ok && ConnectInProgress()) {
            // Connecting: wait for the socket to become writable, then ask whether it worked.
            fd_set writable, failed;
            FD_ZERO(&writable); FD_ZERO(&failed);
            FD_SET(s, &writable);
#ifdef _WIN32
            FD_SET(s, &failed);
#endif
            timeval tv;
            tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
            if (select(static_cast<int>(s) + 1, nullptr, &writable, &failed, &tv) > 0 && FD_ISSET(s, &writable)) {
                int error = 0;
#ifdef _WIN32
                int length = sizeof(error);
#else
                socklen_t length = sizeof(error);
#endif
                ok = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0 && error == 0;
            }
        }
        if (ok) connected = s; else CloseSocket(s);
    }
    freeaddrinfo(found);
    if (connected == kInvalidSocket) return kInvalidSocket;
    int one = 1;
    setsockopt(connected, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));   // inputs are small and urgent
    return connected;   // already non-blocking
}
}

#endif
