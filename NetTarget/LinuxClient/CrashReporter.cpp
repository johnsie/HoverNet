#include "CrashReporter.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
char gDir[512];
char gVersion[64];
std::time_t gStarted;

void BuildPath(char* out, std::size_t size)
{
    std::snprintf(out, size, "%s/crash-%lld.txt", gDir, static_cast<long long>(std::time(nullptr)));
}

#ifdef _WIN32
LONG WINAPI OnException(EXCEPTION_POINTERS* info)
{
    char path[640];
    BuildPath(path, sizeof(path));
    if (FILE* f = std::fopen(path, "w")) {
        std::fprintf(f, "HoverNet server %s crashed after %lld seconds\nexception code 0x%08lx at address %p\n", gVersion,
                     static_cast<long long>(std::time(nullptr) - gStarted), info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
void OnSignal(int sig)
{
    char path[640];
    BuildPath(path, sizeof(path));
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        char head[256];
        const int n = std::snprintf(head, sizeof(head), "HoverNet server %s crashed after %lld seconds\nsignal %d (%s)\n", gVersion,
                                    static_cast<long long>(std::time(nullptr) - gStarted), sig, strsignal(sig));
        if (n > 0 && write(fd, head, static_cast<std::size_t>(n)) < 0) {}
        void* frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    // Die the way the process would have.
    std::signal(sig, SIG_DFL);
    raise(sig);
}
#endif
}

void CrashReporter::Install(const std::string& dir, const std::string& version)
{
    std::snprintf(gDir, sizeof(gDir), "%s", dir.c_str());
    std::snprintf(gVersion, sizeof(gVersion), "%s", version.c_str());
    gStarted = std::time(nullptr);
#ifdef _WIN32
    SetUnhandledExceptionFilter(OnException);
#else
    std::signal(SIGSEGV, OnSignal);
    std::signal(SIGABRT, OnSignal);
    std::signal(SIGFPE, OnSignal);
    std::signal(SIGILL, OnSignal);
    std::signal(SIGBUS, OnSignal);
#endif
}
