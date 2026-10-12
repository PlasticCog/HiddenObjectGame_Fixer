// HOG Fixer launcher — SDL3 + Dear ImGui GUI to add, configure and launch hidden-object games
// with hogfix.dll applied. Profiles live in %APPDATA%\HOGFixer\profiles.
#include <windows.h>
#include <commdlg.h>

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "inject.h"
#include "library.h"
#include "profile.h"

using namespace hog;
using namespace hog::launcher;

namespace {

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

// A modal open-file dialog via the Win32 common dialog (keeps us off extra deps).
std::wstring BrowseForExe(SDL_Window* win) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    ofn.lpstrFilter = L"Game executable\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select a game executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : std::wstring();
}

#include <shlobj.h>
std::wstring BrowseForFolder(SDL_Window* win, const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return L"";
    wchar_t path[MAX_PATH] = L"";
    SHGetPathFromIDListW(idl, path);
    CoTaskMemFree(idl);
    return path;
}

const char* kEngines[] = {"generic", "sf", "lakehouse", "neoaxis", "flash"};
const char* kModes[] = {"borderless", "windowed", "passthrough"};
const char* kFilters[] = {"bilinear", "bicubic", "sharp", "nearest"};
const char* kAspects[] = {"keep", "stretch", "integer"};
const char* kCursors[] = {"system", "game"};
const char* kDpi[] = {"permonitor", "system", "unaware"};
const char* kRenderHeights[] = {"auto (<=1080)", "native", "720", "768", "900", "1080", "1440"};
const int kRenderHeightVals[] = {0, -1, 720, 768, 900, 1080, 1440};

int IndexOf(const char* const* arr, int n, const std::string& v) {
    for (int i = 0; i < n; ++i)
        if (v == arr[i]) return i;
    return 0;
}

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::vector<Entry> library;
    int selected = -1;
    std::string status;
    std::wstring bundleRoot;
    char pathInput[1024] = "";

    void Refresh() {
        library = LoadLibrary();
        if (selected >= int(library.size())) selected = int(library.size()) - 1;
    }

    void AddExe(const std::wstring& exe) {
        if (exe.empty()) return;
        Entry e;
        e.profile = MakeDefaultProfile(exe);
        if (SaveEntry(e)) {
            status = "Added " + Narrow(e.profile.name);
            Refresh();
            for (size_t i = 0; i < library.size(); ++i)
                if (_wcsicmp(library[i].file.c_str(), e.file.c_str()) == 0) selected = int(i);
        } else {
            status = "Could not save profile for " + Narrow(exe);
        }
    }

    // Accepts a pasted path (as from Explorer's address bar or "Copy as path"): a game .exe is
    // added directly; a folder is treated as a bundle root and imported.
    void AddPath(std::string raw) {
        // Trim whitespace and surrounding quotes that "Copy as path" adds.
        size_t a = raw.find_first_not_of(" \t\r\n\"");
        size_t b = raw.find_last_not_of(" \t\r\n\"");
        if (a == std::string::npos) {
            status = "Enter a path first";
            return;
        }
        std::wstring path = Widen(raw.substr(a, b - a + 1));
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            status = "Path not found: " + Narrow(path);
            return;
        }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            bundleRoot = path;
            ImportBundle(/*alreadyHavePath=*/true);
        } else {
            AddExe(path);
        }
    }

    void ImportBundle(bool alreadyHavePath = false) {
        auto games = ScanBundle(bundleRoot);
        if (games.empty() && !alreadyHavePath) {
            // Ask the user to point at the bundle (the folder that contains "My Games").
            std::wstring picked = BrowseForFolder(window, L"Select your hidden-object bundle folder");
            if (!picked.empty()) {
                bundleRoot = picked;
                games = ScanBundle(bundleRoot);
            }
        }
        int added = 0;
        for (const auto& g : games) {
            bool known = false;
            for (const auto& e : library)
                if (_wcsicmp(e.profile.exe.c_str(), g.c_str()) == 0) known = true;
            if (!known) {
                Entry e;
                e.profile = MakeDefaultProfile(g);
                if (SaveEntry(e)) ++added;
            }
        }
        Refresh();
        if (games.empty())
            status = "No games found under " + Narrow(bundleRoot) + " (need a \"My Games\" folder)";
        else
            status = "Imported " + std::to_string(added) + " game(s) from the bundle";
    }

    void Launch(Entry& e) {
        if (!e.profile.Save(e.file)) {
            status = "Could not write profile before launch";
            return;
        }
        std::wstring dll = (e.profile.mode == DisplayMode::Passthrough) ? L"" : DllPath();
        std::wstring log = e.profile.workDir.empty() ? L"" : e.profile.workDir + L"\\hogfix.log";
        LaunchResult r = LaunchWithHook(e.profile.exe, e.profile.args, e.profile.workDir, dll, e.file, log);
        status = r.ok ? ("Launched " + Narrow(e.profile.name) + " (pid " + std::to_string(r.pid) + ")")
                      : ("Launch failed: " + Narrow(r.error));
    }
};

void DrawSettings(Entry& e) {
    Profile& p = e.profile;
    char name[256];
    strncpy_s(name, Narrow(p.name).c_str(), _TRUNCATE);
    if (ImGui::InputText("Name", name, sizeof(name))) p.name = Widen(name);
    ImGui::TextWrapped("Exe: %s", Narrow(p.exe).c_str());
    if (e.missing) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "  (file not found)");

    int engine = IndexOf(kEngines, IM_ARRAYSIZE(kEngines), Narrow(p.engine));
    if (ImGui::Combo("Engine", &engine, kEngines, IM_ARRAYSIZE(kEngines))) p.engine = Widen(kEngines[engine]);

    ImGui::SeparatorText("Display");
    int mode = int(p.mode);
    if (ImGui::Combo("Window mode", &mode, kModes, IM_ARRAYSIZE(kModes))) p.mode = DisplayMode(mode);
    ImGui::SetItemTooltip("borderless = full-screen window with internal upscale (recommended).\n"
                          "windowed = a normal window.\npassthrough = no fixes.");

    int rh = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kRenderHeightVals); ++i)
        if (kRenderHeightVals[i] == p.renderHeight) rh = i;
    if (ImGui::Combo("Internal resolution", &rh, kRenderHeights, IM_ARRAYSIZE(kRenderHeights)))
        p.renderHeight = kRenderHeightVals[rh];
    ImGui::SetItemTooltip("How many lines the game renders internally before upscaling.\n"
                          "Lower = faster on weak GPUs. 'auto' caps at 1080p.");

    int filter = int(p.filter);
    if (ImGui::Combo("Scaling filter", &filter, kFilters, IM_ARRAYSIZE(kFilters))) p.filter = ScaleFilter(filter);
    int aspect = int(p.aspect);
    if (ImGui::Combo("Aspect", &aspect, kAspects, IM_ARRAYSIZE(kAspects))) p.aspect = AspectMode(aspect);
    ImGui::InputInt("Monitor (0 = primary)", &p.monitor);
    ImGui::Checkbox("VSync", &p.vsync);
    ImGui::SameLine();
    ImGui::Checkbox("Low latency", &p.lowLatency);
    ImGui::SetItemTooltip("Keep at most one frame queued on the GPU; reduces cursor/click lag.");
    ImGui::InputInt("FPS limit (0 = off)", &p.fpsLimit);

    ImGui::SeparatorText("Cursor");
    int cur = int(p.cursor);
    if (ImGui::Combo("Cursor", &cur, kCursors, IM_ARRAYSIZE(kCursors))) p.cursor = CursorMode(cur);
    ImGui::SetItemTooltip("system = smooth hardware cursor (recommended).\n"
                          "game = leave the game's own cursor.");
    ImGui::Checkbox("Confine cursor to window", &p.confineCursor);

    ImGui::SeparatorText("Compatibility");
    int dpi = int(p.dpi);
    if (ImGui::Combo("DPI awareness", &dpi, kDpi, IM_ARRAYSIZE(kDpi))) p.dpi = DpiMode(dpi);
    ImGui::Checkbox("Force hardware vertex processing", &p.forceHardwareVP);
    ImGui::Checkbox("Verbose log", &p.verboseLog);
    if (p.engine == L"flash") {
        char ocx[512];
        strncpy_s(ocx, Narrow(p.flashOcx).c_str(), _TRUNCATE);
        if (ImGui::InputText("Flash OCX path", ocx, sizeof(ocx))) p.flashOcx = Widen(ocx);
        ImGui::SetItemTooltip("Path to a Flash ActiveX OCX (<=129 chars) for Flash-hosted games.");
    }
}

}  // namespace

int main(int, char**) {
    App app;
    {
        // Walk up from the launcher looking for a folder that contains "My Games"; that's the
        // bundle root. Works whether the launcher sits in the bundle or a subfolder of it.
        std::wstring dir = SelfDir();
        app.bundleRoot = dir;
        for (int up = 0; up < 5 && !dir.empty(); ++up) {
            if (GetFileAttributesW((dir + L"\\My Games").c_str()) != INVALID_FILE_ATTRIBUTES) {
                app.bundleRoot = dir;
                break;
            }
            size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir = dir.substr(0, slash);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        MessageBoxA(nullptr, SDL_GetError(), "HOG Fixer: SDL_Init failed", MB_ICONERROR);
        return 1;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    app.window = SDL_CreateWindow("HOG Fixer", 1000, 680, flags);
    app.renderer = SDL_CreateRenderer(app.window, nullptr);
    SDL_SetRenderVSync(app.renderer, 1);

    // Use the embedded hog icon for the window (taskbar + title bar).
    if (HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(app.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))) {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        if (HICON big = static_cast<HICON>(
                LoadImageW(inst, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        if (HICON sm = static_cast<HICON>(
                LoadImageW(inst, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, 0)))
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(sm));
    }
    if (!app.window || !app.renderer) {
        MessageBoxA(nullptr, SDL_GetError(), "HOG Fixer: window creation failed", MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLRenderer(app.window, app.renderer);
    ImGui_ImplSDLRenderer3_Init(app.renderer);

    app.Refresh();
    if (app.library.empty()) app.ImportBundle();  // first run: pull in the bundled games

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("HOG Fixer", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::Button("Browse…")) app.AddExe(BrowseForExe(app.window));
        ImGui::SameLine();
        if (ImGui::Button("Import bundled games")) app.ImportBundle();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) app.Refresh();

        // Address-bar style: paste a path to a game .exe or a bundle folder and press Enter.
        ImGui::SetNextItemWidth(-90.0f);
        bool submitted = ImGui::InputTextWithHint(
            "##path", "Paste a path to a game .exe (or a bundle folder) and press Enter", app.pathInput,
            sizeof(app.pathInput), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Add", ImVec2(70, 0)) || submitted) {
            app.AddPath(app.pathInput);
            app.pathInput[0] = '\0';
        }

        ImGui::Separator();
        ImGui::BeginChild("list", ImVec2(300, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        for (int i = 0; i < int(app.library.size()); ++i) {
            Entry& e = app.library[i];
            std::string label = Narrow(e.profile.name) + "##" + std::to_string(i);
            if (e.missing) label = "(!) " + label;
            if (ImGui::Selectable(label.c_str(), app.selected == i)) app.selected = i;
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("detail", ImVec2(0, 0));
        if (app.selected >= 0 && app.selected < int(app.library.size())) {
            Entry& e = app.library[app.selected];
            if (ImGui::Button("Play", ImVec2(120, 36))) app.Launch(e);
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                SaveEntry(e);
                app.status = "Saved";
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                DeleteEntry(e);
                app.Refresh();
            }
            ImGui::Separator();
            DrawSettings(e);
        } else {
            ImGui::TextWrapped("Add a game or import the bundled games, then select one to configure and play.");
        }
        ImGui::EndChild();

        if (!app.status.empty()) {
            ImGui::SetCursorPosY(vp->WorkSize.y - 28);
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s", app.status.c_str());
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(app.renderer, 24, 24, 28, 255);
        SDL_RenderClear(app.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app.renderer);
        SDL_RenderPresent(app.renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
