// Byte-pattern scanning over a module's executable sections.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace hog::sig {

// Pattern syntax: hex byte pairs separated by spaces, "??" = wildcard. e.g. "55 8B EC ?? 68".
// Returns all matching addresses within the module's code sections.
std::vector<void*> Scan(HMODULE module, const char* pattern);

// Convenience: the single unique match, or nullptr (logs a warning if 0 or >1 matches).
void* ScanUnique(HMODULE module, const char* pattern, const char* name);

HMODULE MainModule();

}  // namespace hog::sig
