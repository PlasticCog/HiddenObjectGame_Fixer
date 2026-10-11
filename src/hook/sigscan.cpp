#include "sigscan.h"

#include "log.h"

namespace hog::sig {
namespace {

struct Token {
    unsigned char value;
    bool wild;
};

std::vector<Token> Parse(const char* pattern) {
    std::vector<Token> out;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (p[0] == '?' ) {
            out.push_back({0, true});
            p += (p[1] == '?') ? 2 : 1;
            continue;
        }
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hex(p[0]), lo = hex(p[1]);
        if (hi < 0 || lo < 0) break;
        out.push_back({static_cast<unsigned char>((hi << 4) | lo), false});
        p += 2;
    }
    return out;
}

}  // namespace

HMODULE MainModule() { return GetModuleHandleW(nullptr); }

std::vector<void*> Scan(HMODULE module, const char* pattern) {
    std::vector<void*> hits;
    std::vector<Token> toks = Parse(pattern);
    if (toks.empty() || !module) return hits;

    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    const size_t n = toks.size();

    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned char* start = base + sec[s].VirtualAddress;
        size_t size = sec[s].Misc.VirtualSize ? sec[s].Misc.VirtualSize : sec[s].SizeOfRawData;
        if (size < n) continue;
        unsigned char* last = start + size - n;
        for (unsigned char* p = start; p <= last; ++p) {
            size_t i = 0;
            for (; i < n; ++i)
                if (!toks[i].wild && p[i] != toks[i].value) break;
            if (i == n) hits.push_back(p);
        }
    }
    return hits;
}

void* ScanUnique(HMODULE module, const char* pattern, const char* name) {
    std::vector<void*> hits = Scan(module, pattern);
    if (hits.size() == 1) {
        LOGV("sig %s: found @%p", name, hits[0]);
        return hits[0];
    }
    LOGW("sig %s: %zu matches (need exactly 1)", name, hits.size());
    return nullptr;
}

}  // namespace hog::sig
