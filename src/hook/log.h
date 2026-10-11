// Minimal thread-safe file logger. Lines go straight to the OS (no user-mode buffering), so
// they survive a game crash.
#pragma once
#include <string>

namespace hog::log {

void Init(const std::wstring& path);
void Write(const char* level, const char* fmt, ...);
void SetVerbose(bool v);
bool Verbose();

}  // namespace hog::log

#define LOGI(...) ::hog::log::Write("INFO", __VA_ARGS__)
#define LOGW(...) ::hog::log::Write("WARN", __VA_ARGS__)
#define LOGE(...) ::hog::log::Write("ERR ", __VA_ARGS__)
#define LOGV(...) do { if (::hog::log::Verbose()) ::hog::log::Write("DBG ", __VA_ARGS__); } while (0)
