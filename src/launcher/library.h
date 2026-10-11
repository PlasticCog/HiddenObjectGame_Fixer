// The launcher's game library: profiles stored as .ini files under %APPDATA%\HOGFixer\profiles.
#pragma once
#include <string>
#include <vector>

#include "profile.h"

namespace hog::launcher {

struct Entry {
    hog::Profile profile;
    std::wstring file;   // the .ini path backing this entry
    bool missing = false;  // exe no longer exists
};

// Directory that holds profiles (%APPDATA%\HOGFixer\profiles), created if needed.
std::wstring ProfilesDir();
// hogfix.dll / hogrun.exe sitting next to the launcher executable.
std::wstring DllPath();
std::wstring SelfDir();

std::vector<Entry> LoadLibrary();
// Writes a profile to its .ini (deriving a file name from the game name if `file` is empty).
bool SaveEntry(Entry& e);
bool DeleteEntry(const Entry& e);

// Guesses the engine id ("sf"/"lakehouse"/"neoaxis"/"flash"/"generic") from an exe and its folder.
std::wstring DetectEngine(const std::wstring& exePath);
// A sensible default profile for a freshly added exe (engine detected, name from file).
hog::Profile MakeDefaultProfile(const std::wstring& exePath);

// Finds the Alawar-style bundle this launcher ships with (…\My Games\N\*.exe) and returns the
// game executables it can import, so the user can add them all with one click.
std::vector<std::wstring> ScanBundle(const std::wstring& bundleRoot);

}  // namespace hog::launcher
