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

inline AuthSocket Connect(const std::string& host, int port)
{
    AuthSocket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kInvalidSocket) return kInvalidSocket;
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 || connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { CloseSocket(s); return kInvalidSocket; }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));   // inputs are small and urgent
    SetNonBlocking(s);
    return s;
}
}

#endif
