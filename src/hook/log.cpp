#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace hog::log {
namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;
bool g_verbose = false;
LARGE_INTEGER g_start, g_freq;
}  // namespace

void Init(const std::wstring& path) {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_start);
    }
    if (path.empty()) return;
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

void SetVerbose(bool v) { g_verbose = v; }
bool Verbose() { return g_verbose; }

void Write(const char* level, const char* fmt, ...) {
    if (!g_lockInit) return;
    char buf[2048];
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = double(now.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart);
    int n = snprintf(buf, sizeof(buf), "[%9.3f][%5lu] %s ", t, GetCurrentThreadId(), level);
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    n = (n + m < int(sizeof(buf)) - 2) ? n + m : int(sizeof(buf)) - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_file, buf, n, &written, nullptr);
    }
    LeaveCriticalSection(&g_lock);
    buf[n - 2] = 0;
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

}  // namespace hog::log
