// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int BOOL;
typedef int SOCKET;
typedef socklen_t SocketLength;

#define TRUE 1
#define FALSE 0
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

inline int closesocket(SOCKET socket) { return close(socket); }
inline int WSAGetLastError() { return errno; }
inline int WSAStartup(unsigned short, void*) { return 0; }
inline int WSACleanup() { return 0; }
#endif

#include <csignal>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <map>
#include <vector>
#include <queue>
#include <string>
#include <memory>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

// Placeholder types for HoverRace compatibility
// These will be replaced with actual HoverRace types during integration
typedef struct {
    float x, y, z;
} MR_Vector3;

typedef struct {
    float x, y, z, w;
} MR_Quaternion;
