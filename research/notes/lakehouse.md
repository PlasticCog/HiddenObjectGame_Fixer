# Lake House: Children of Silence — display / cursor / input / timing notes

Binary: `My Games\2\LakeHouse_ChildrenOfSilence.exe`. 32-bit, VS2010, PE timestamp 2012-12-27.
Engine: Friday's Games / Alawar in-house framework (`cWin32Application`, `cDirectXGraphics`,
`cCursorManager`, `FG_VERIFY`, source paths `D:\SVN\Engine\library\...`).

- ImageBase 0x400000. DllCharacteristics 0x8100 (NX, TS-aware), **no DYNAMIC_BASE**, so every
  address below is stable and can be hooked directly.
- Manifest: `asInvoker`, `<dpiAware>true</dpiAware>` (system-DPI-aware), **no** `supportedOS`
  GUIDs, so `GetVersionEx` is shimmed to 6.2 on Win10. It doesn't matter: only the crash handler and the
  statically linked D3DX call it.
- D3DX9 is statically linked (strings "D3DX9.1 Effect Compiler", "DisableD3DXPSGP"). It needs **no**
  `d3dx9_xx.dll`. The game imports d3d9.dll (`Direct3DCreate9` only), dinput8, winmm, dwmapi (loaded
  dynamically), magic.dll (particles) and OpenAL32/wrap_oal.
- **No `ChangeDisplaySettings*`, no `ClipCursor`, no `ShowCursor`, no D3D cursor API**
  (no device vtable 0x28/0x2C/0x30 calls anywhere in .text).

## Singletons and their fields (all lazily constructed: `if (!g) g = new X`)

| Global | Class | Useful fields |
|---|---|---|
| `DAT_00b71b44` | cWin32Application (ctor 0x00500c60, size 0x100) | +0x4C hInstance, +0x54 parent HWND (embedded mode), +0x64 **HWND**, +0x74 **current HCURSOR**, +0x78 arrow HCURSOR (`LoadImageA(NULL, IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_SHARED)`), +0x88..+0x94 mouse clamp RECT, +0x98/+0x9C desktop size at start, +0xA0 **fullscreen flag**, +0xA4/+0xA8 **last mouse client x/y (int)**, +0xB0 orientation code, +0xB8 QPC freq, +0xC0 QPC t0, +0x1C active flag, +0x30 quit flag |
| `DAT_00b71cb0` | cDirectXGraphics (ctor 0x0057fb70, size 0x7B0) | +0x584 IDirect3D9*, +0x588 D3DCAPS9, +0x6B8 **D3DPRESENT_PARAMETERS**, +0x6F0 backbuffer surface, +0x6F8 event query, +0x700 **mode {x,y,w,h,bpp,syncinterval,(byte)window,(byte)widescreen}**, +0x71C copy of mode used by Reset, +0x738 work area RECT, +0x748/+0x74C desktop W/H (`GetSystemMetrics(0/1)`), +0x754..+0x760 design rect (0,0,1024,768), +0x10..+0x24 D3DVIEWPORT9, +0x28/+0x2C viewport offset (letterbox) |
| `DAT_00b84164` | IDirect3DDevice9* | |
| `DAT_00b84168` | depth-stencil surface | |
| `DAT_00b71b78` | cCursorManager (ctor 0x0055cc00, size 0x1C4) | +0x16C current cursor name, +0x188 requested name, +0x44 current static sprite, +0x48..+0x50 hotspot, +0x1A4 visible, +0x1A5 **hardware_cursor**, +0x1A6 **system_cursor**, +0x1A7 animated "color" cursor, +0x1A8 "cursors" scene, +0x1AC current scene cursor node (node+0x68 = visible) |
| `DAT_00b71b60` | input (ctor 0x005b9100) | +0x1A8/+0x1AC cursor x/y (float, copied from app+0xA4/+0xA8), +0x148/+0x14C/+0x150 DI mouse dX/dY/dZ, +0x0C IDirectInput8, +0x14 keyboard dev, +0x18 mouse dev |
| `DAT_00b71e90` | config (ctor 0x00504e80) | key/value string map, see Settings |
| `DAT_00b4f9e0` | float mouse-speed factor (default 1.0) | see Mouse |

Design resolution for platform index 5 (PC, `DAT_00b71ca8+4 == 5`) is **1024x768**. The tables are at
0x00aaf50c/0x00aaf524 (other indices are the mac/iphone/retina/ipad/android builds).

## 1. Window, display mode, D3D9 device

### Window: `FUN_00500e50` (cWin32Application::Create, called from main init `FUN_004fb1d0`)
- Optional single-instance mutex `CreateMutexA(name + "my_project")`. On `ERROR_ALREADY_EXISTS` it calls
  `FindWindowW(class)`, `ShowWindow(SW_RESTORE/SW_SHOW)`, `SetForegroundWindow` and returns 0, so the game exits.
- `RegisterClassExW`: style `CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS` (0xB), WndProc thunk **0x00500dc0**,
  which calls the real handler 0x00500020. **hCursor = NULL**, background = BLACK_BRUSH.
- `CreateWindowExW(WS_EX_APPWINDOW, cls, "Loading...", WS_POPUP|WS_CAPTION|WS_SYSMENU (0x80C80000), CW_USEDEFAULT x4)`.
  If a parent HWND is supplied it uses `WS_CHILD` / `WS_EX_TOOLWINDOW` instead.
- `DwmSetWindowAttribute(7 FORCE_ICONIC_REPRESENTATION, 1)` and `(10 HAS_ICONIC_BITMAP, 1)`, for the
  custom taskbar thumbnail (`WM_DWMSENDICONICTHUMBNAIL` handling).

### Window style switch: `FUN_004ffbe0(app, x, y, w, h, bool fullscreen)`
- fullscreen: `SetWindowLongA(GWL_STYLE, WS_POPUP|WS_SYSMENU)` 0x80080000,
  `GWL_EXSTYLE = WS_EX_TOPMOST|WS_EX_APPWINDOW` 0x40008,
  `SetWindowPos(HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW|SWP_FRAMECHANGED)`.
- windowed: `GWL_STYLE = WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX` 0x80CA0000,
  `AdjustWindowRectEx`, then `SetWindowPos(HWND_NOTOPMOST, rect, SWP_SHOWWINDOW)`. The window is not resizable.
- Always: `app+0xA0 = fullscreen`, `SetRect(app+0x88, x, y, x+w, y+h)` (the mouse clamp rect, see below).

### Mode selection: `FUN_0057dd60(gfx, out mode, in mode)`, called from `FUN_0057e170`
```
if (!mode.window) {                          // fullscreen
    enumerates GetAdapterModeCount/EnumAdapterModes for X1R5G5B5,R5G6B5,R8G8B8,X8R8G8B8 (result unused!)
    mode.w,h = desktop (gfx+0x748/0x74C = GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN))
    if (gfx+0x772 == 0)   // gfx+0x772 = !video.widescreen, set by FUN_0057ccb0 in Reset
        viewport = pillar/letterbox of design aspect (video.width/height) centred, offset in +0x28/+0x2C
    else  viewport = whole backbuffer
} else {                                      // windowed
    clamp w/h to the work area (SPI_GETWORKAREA, gfx+0x738), keep the design aspect, centre in the work area
    viewport = whole backbuffer
}
```
- In fullscreen the backbuffer is the **desktop resolution, never a lower mode**. With the dpiAware manifest that
  means a **native 3840x2160 exclusive-fullscreen** backbuffer at 4K, which is the main FPS cost on Intel iGPU.
- If the desktop is <= 800x600, the game forces fullscreen (`FUN_0057e370`).

### Present parameters: `FUN_0057e170` (memset PP at gfx+0x6B8)
| field | fullscreen (`video.window=false`, default) | windowed |
|---|---|---|
| BackBufferWidth/Height | desktop W/H | configured w/h after work-area clamp |
| BackBufferFormat | bpp 32 → X8R8G8B8 (0x15), 16 → R5G6B5, **24 → R8G8B8 (fails on modern GPUs)** | D3DFMT_UNKNOWN |
| BackBufferCount / MultiSample | 0 / NONE | same |
| SwapEffect | DISCARD (1) | DISCARD |
| hDeviceWindow | app HWND | app HWND |
| Windowed | FALSE | TRUE |
| EnableAutoDepthStencil / fmt | TRUE / D24S8 (0x4B) | same |
| Flags / RefreshRate | 0 / 0 | 0 / 0 |
| PresentationInterval | `video.syncinterval != 0 ? ONE : IMMEDIATE (0x80000000)`; **ctor default is 0 → IMMEDIATE** | same |

Then it calls `FUN_004ffbe0(x, y, w, h, fullscreen)`.

### Device creation: `FUN_0057eaa0` (cDirectXGraphics::Init, called from `FUN_004fb1d0`)
- `Direct3DCreate9(D3D_SDK_VERSION 0x1F)`. On NULL it only logs and **continues**, so a later call crashes.
- `CreateDevice(adapter 0, D3DDEVTYPE_HAL, hFocus = PP.hDeviceWindow, flags, &PP, &DAT_00b84164)`.
  It tries flags `0x44` (HARDWARE_VERTEXPROCESSING|MULTITHREADED) and then `0x24` (SOFTWARE_VP|MULTITHREADED).
  There is **no FPU_PRESERVE**. If both fail: log "Unable create 3d device. Error: %x" and return 0 (the game exits).
- After success: `CreateQuery(D3DQUERYTYPE_EVENT)`, `GetDepthStencilSurface`, `GetRenderTarget(0)`, `GetDeviceCaps`,
  `GetAvailableTextureMem` (compared against a threshold to pick a texture quality level, `DAT_00b4fb80` = 2 or 14),
  `CheckDeviceFormat(..., D3DRTYPE_TEXTURE, 'YUY2')` for WebM video. It uses anisotropic filtering if the
  TextureFilterCaps allow it.
- **The only caps usage is filter caps. There is no shader-version gate** ("ps3shader" is just an effect name).
- It ends by calling `FUN_0057e370` (a full Reset) immediately.

### Reset / device lost
- `FUN_0057e370(gfx, mode*)` (also used for option changes): re-reads `GetSystemMetrics(0/1)`, calls OnLostDevice on
  all resources (list `DAT_00b8417c`, vfunc +4), releases DS/RT/query, rebuilds the PP, then
  `Reset` (vtbl 0x40, call site 0x0057e4fa). On failure it logs "Reset error: %x" and returns 0 (it retries next frame).
  After that: `FUN_004ffbe0` window style (fullscreen: with the viewport rect, so the mouse clamp is the letterbox area),
  OnResetDevice (vfunc +8), render-state and sampler setup, and `SetActiveWindow(hwnd)`.
- `FUN_0057e970`: `TestCooperativeLevel` (call 0x0057e994). If the result is `D3DERR_DEVICENOTRESET` or the forced flag
  gfx+0x775 is set, it resets. `D3DERR_DEVICELOST` keeps rendering into the lost device (calls fail silently).
  Windowed mode also calls `RedrawWindow(NULL, ..., 0x387)`.
- `FUN_0057f790` (on activate): `FUN_0057d560` (TCL ok?), otherwise `FUN_0057e970`.
- Orientation: `FUN_00505690` = `EnumDisplaySettingsA(NULL, ENUM_CURRENT)` → dmDisplayOrientation. On
  `WM_DISPLAYCHANGE` with a landscape↔portrait change **in fullscreen**, `FUN_0057daa0` logs "Screen orientation changed
  in fullscreen mode. Quiting.", the game quits and shows a MessageBox. This hits auto-rotating Intel 2-in-1 laptops.

## 2. Mouse input and cursor

### Cursor position: WM_MOUSEMOVE (absolute, OS-accurate)
WndProc body **0x00500020**:
- `WM_MOUSEMOVE`: `app+0xA4/+0xA8 = LOWORD/HIWORD(lParam)` (client px). In fullscreen it is clamped to `app+0x88` rect.
  Then it notifies listener app+0xD8. Each frame `FUN_005b85d0` copies app+0xA4/+0xA8 into input+0x1A8/+0x1AC
  (float), and that is the game cursor.
- **Mouse-speed warp:** if active and `DAT_00b4f9e0 != 1.0`, it scales the delta from the previous point,
  `ClientToScreen` and **`SetCursorPos`** (call 0x00500653). The factor is set by `FUN_004ffb80`: 0.1 by the
  click-penalty "slowdown_effect" (`FUN_00471f10`) and back to 1.0 in `FUN_00472170`. It is intentional, but a coordinate
  virtualizer must handle this ClientToScreen+SetCursorPos path.
- `WM_LBUTTONDOWN/UP` → listeners +0xD0/+0xE0 (with point). `WM_LBUTTONDBLCLK/RBUTTONDBLCLK/MBUTTONDBLCLK` →
  `FUN_005b7e30` sets input+0x1F0 double-click flags.
- `WM_SETCURSOR`: if `LOWORD(lParam)==HTCLIENT`, `SetCursor(app+0x74)`; it always returns 0 (no DefWindowProc).
  `WM_NCMOUSEMOVE`: `SetCursor(app+0x78 arrow)` and returns 0.
- Other messages: `WM_ACTIVATEAPP` (fullscreen) and `WM_NCACTIVATE` (windowed) → `FUN_005018e0(active)`;
  `WM_SIZE` RESTORED → active, MAXIMIZED → `ShowWindow(SW_RESTORE)`; `WM_ENTER/EXITSIZEMOVE` → pause/resume;
  `WM_SYSCOMMAND` SC_SCREENSAVE is swallowed, SC_KEYMENU → `EndMenu` (unless Alt+Space), SC_CLOSE → listener.

### DirectInput8 (buttons, wheel, deltas, keyboard; not the cursor position)
`FUN_005b8740` (input init):
- Keyboard: `SetDataFormat(c_dfDIKeyboard)`, `SetCooperativeLevel(top-level hwnd, DISCL_BACKGROUND|DISCL_NONEXCLUSIVE (10))`.
- Mouse: `SetDataFormat` (DIMOUSESTATE, 16 bytes), coop 10, `SetEventNotification(event)`.
- A worker thread (`FUN_005b7c60` object, loop body `FUN_005b80a0`) waits on the event (200 ms timeout). When the app
  is active and `GetCursorPos` is inside the client rect (`FUN_005b7fb0`: `GetClientRect` + `MapWindowPoints` + `PtInRect`),
  it calls `GetDeviceState(0x10)` and accumulates lX/lY/lZ and latched buttons (`FUN_005b7cf0`).
- Per frame `FUN_005b86c0` → `FUN_005b8180` (keyboard `GetDeviceState(256)`), `FUN_005b82e0` (mouse `GetDeviceState`,
  `SM_SWAPBUTTON` swap, deltas → input+0x148..), `FUN_005b85d0` (cursor position from WM_MOUSEMOVE).
- Initial position: `GetCursorPos` + `ScreenToClient` at init.
- The **cursor position does not come from DI deltas**, so there is no DPI/acceleration mismatch. Cursor lag comes from
  rendering a software cursor at the game's (low) frame rate.

### Cursor modes (cCursorManager, `DAT_00b71b78`)
Three user-selectable modes (Options checkboxes `window_options/hardware_cursor`, `.../system_cursor`,
`.../color_cursor`, handled in `FUN_0050faf0`; applied in `FUN_0050ab40`):
1. **color cursor (default on PC)**: animated scene-graph sprites from the "cursors" scene (data pack, with
   "cursor_animation"). The OS cursor is hidden via `SetCursor(NULL)`.
2. **hardware_cursor=true**: Win32 HCURSORs from the exe's own resources. `SetCursor(hcur)` per cursor type.
3. **system_cursor=true**: always the OS arrow (app+0x78).

Code:
- `FUN_0055a950(mgr, visible)`: chooses `SetCursor(NULL / map[name].hcursor / arrow)` via **`FUN_004ffb60`**
  (`SetCursor(h); app+0x74 = h;` called from 0x0055a5b0, 0x0055a950, 0x0055c900) and sets scene node+0x68 visible
  only when `!hw && !sys`.
- Setters: `FUN_0055b060` / `FUN_0055b010` (hardware_cursor; FUN_0055b010 is also driven by the "low_quality" option),
  `FUN_0055aa40` / `FUN_0055aa20` (system_cursor). The config bindings are registered in the ctor via `FUN_0055cb30`
  (getters 0x00559e40 / 0x00559e60).
- **Cursor-type selection:** `FUN_0055a930(mgr, name)` writes the requested name to mgr+0x188 (about 26 callers in gameplay
  code). The per-frame apply is **`FUN_0055c900`**, called from `FUN_0055caf0` (cursor update in the frame update `FUN_004f4730`).
  If the name changed it calls SetCursor (hardware mode), picks the static sprite (mgr+0x44, hotspot +0x48), and in color
  mode hides the old scene node, finds the new one (`FUN_0054efc0`) and plays "cursor_animation".
- Names and resource IDs loaded in `FUN_004fb1d0` via `FUN_0055c810` = `LoadImageA(hInst, MAKEINTRESOURCE(id), IMAGE_CURSOR, 0, 0, 0)`:
  `light` 101, `cursor` 102, `down` 103, `hand`/`finger` 104, `lens` 105, `ul` 106, `up` 107, `ur` 108, `hot_animation` 102.
  All are 32x32 32bpp .cur. Hotspots (group 101..108): (12,2) (2,1) (15,31) (12,2) (11,10) (13,1) (15,1) (30,1).
  They are loaded at their **native 32x32 size**, so they look tiny at 4K in this DPI-aware process.
- Init on PC (`FUN_004fb1d0` around 0x004fcdc0–0x004fd05a): load the HCURSORs, `FUN_0055d1f0(mgr,1)` (+0x1A7 = 1,
  load "cursors" scene), request "cursor", `FUN_0055a370` (cursor camera/ortho), `FUN_0055a950(1)`.
  Non-PC platforms take the system-cursor path instead.

### Where the software cursor is drawn
- Frame render **`FUN_004f2520`**: `FUN_0057f590` BeginFrame → `FUN_00551190` scene render → **`FUN_0055a230`** cursor
  → (`FUN_0058af40` FPS overlay, debug only) → **`FUN_0057c7d0`** EndScene + `Present(NULL, NULL, NULL, NULL)` (call 0x0057c7f8,
  HRESULT ignored).
- `FUN_0055a230` is the narrow "draw only the cursor" function. It draws the **static** sprite (mgr+0x44) at
  input pos + hotspot through the sprite's draw vfunc. But it only runs when
  `!hw && !+0x1A7 && visible && sprite && !sys`, and on PC +0x1A7 is 1. So **with default settings it draws nothing.**
- The default animated cursor is a scene node (mgr+0x1AC). `FUN_0055a4a0` (from `FUN_0055caf0`) positions it every
  update (`FUN_0054cf60` mouse → scene) and gives it draw priority 100000 (`FUN_0054d370`). The node is drawn inside the
  normal scene render `FUN_00567d70` → `FUN_00567bd0`, sorted last, through the per-item call **0x00567d42**
  (`call [vtbl+0x28]`). There's no dedicated function. To capture it you'd filter render items whose node is (or descends
  from) `*(DAT_00b71b78+0x1AC)` at 0x00567d42, which is fragile.

## 3. Main loop, timing, focus
- Run loop **`FUN_004ff9c0`**: on entry, if `GetForegroundWindow() != hwnd` it marks the app inactive.
  ```
  while (!app.quit):
      if PeekMessageW(PM_NOREMOVE): GetMessageW; break on WM_QUIT; Translate/Dispatch   (one msg per iteration)
      elif !app.active: FUN_00502070 (pause listeners); WaitMessage()                   // fully paused, 0% CPU
      else: FUN_00501f30(0)  -> game.update(dt) [FUN_004f4730] ; game.render() [FUN_004f2520]
  ```
- Clock: `FUN_00500c60` ctor does `timeBeginPeriod(1)` (`timeEndPeriod` in dtor `FUN_004ffe00`), QPF/QPC t0.
  `FUN_004ffaf0` reads the time: `SetThreadAffinityMask(GetCurrentThread(), 0)` (**mask 0, so it always fails and does nothing**),
  `Sleep(0)`, `QPC`, the same restore call, and returns `(qpc - t0) / freq` in x87.
- `FUN_00501f30`: dt = now − last (+ remainder), clamped to [0, 0.1 s]. **No frame limiter / no Sleep.** It is uncapped
  unless vsync is on, and the default syncinterval is 0.
- `FUN_0057f590` BeginFrame: TCL/Reset check, then **CPU↔GPU sync every frame**: `query->GetData(FLUSH)`, then
  `while (GetData == S_FALSE) Sleep(0);` and `Issue(END)`. That allows at most one frame in flight and **burns a core spinning**.
  Then it does Clear and SetViewport(letterbox) and calls BeginScene.
- No FPU_PRESERVE, so x87 runs in 24-bit precision after CreateDevice. Time values are seconds since start, so dt quantization
  is about t/2^23 (0.4 ms after 1 h, about 4 ms after 10 h). This is a minor stutter source in very long sessions.
- Unfocused: rendering stops (WaitMessage). DI devices are BACKGROUND but are not read. Alt-tab from exclusive fullscreen
  goes through WM_ACTIVATEAPP → inactive → device lost → Reset when reactivated.

## 4. Settings: XML file, not registry
- Path built in `FUN_005041d0`: `SHGetFolderPathW(CSIDL_APPDATA|CREATE)` + `\AlawarEntertainment\Friday's games\` +
  `short_name_path` + `\` + `short_name` + `.xml`.
  - `short_name` = `LakeHouse_ChildrenOfSilence` (`_CE` / `_survey` variants via `FUN_0046aff0` flags).
  - `short_name_path` = `LakeHouse_ChildrenOfSilence\SE` for the standard edition (just the name for CE/survey).
  - So the file is **`%APPDATA%\AlawarEntertainment\Friday's games\LakeHouse_ChildrenOfSilence\SE\LakeHouse_ChildrenOfSilence.xml`**.
    It isn't present on this PC yet. The same folder holds the logs, `crash_log_YYYYMMDD-hhmmss.dmp` and the profiles.
- Format (read `FUN_005041d0`, written on exit by `FUN_005049e0` from `FUN_004f2740`):
  `<?xml version="1.0" encoding="UTF-8"?><config><item id="KEY" value="VALUE"/>...</config>`. Bools are `true`/`false`.
- Keys and defaults (graphics ctor `FUN_0057fb70`, config ctor `FUN_00504e80`):
  | key | default | meaning |
  |---|---|---|
  | `video.window` | false | true = windowed (anything else is exclusive fullscreen at desktop res) |
  | `video.width` / `video.height` | 1024 / 768 | windowed client size (clamped to work area) and design aspect for letterboxing |
  | `video.x` / `video.y` | 0 / 0 | fullscreen origin (windowed is always centred) |
  | `video.bpp` | 32 | keep 32 (24 → R8G8B8, CreateDevice fails) |
  | `video.syncinterval` | 0 | 0 = PRESENT_INTERVAL_IMMEDIATE, ≠0 = ONE |
  | `video.widescreen` | false | false = viewport fills the backbuffer, true = pillar/letterbox to 4:3 (decoded from code, verify in-game) |
  | `hardware_cursor` | (unset = false) | OS .cur cursors |
  | `system_cursor` | (unset = false) | plain OS arrow |
  | `low_quality` | false | Options checkbox. Depending on mask `DAT_00b4faa4` it may write 800x600 + syncinterval 0 + hardware_cursor + lower texture quality |
  | `music_volume` / `sound_volume` | 0.7 | |
  | `log` | true | |
- The registry is only used by the wallpaper extra (`FUN_00517e00`: `HKCU\Control Panel\Desktop` WallpaperStyle=2,
  TileWallpaper=0, `SPI_SETDESKWALLPAPER`, `Documents\Fridays Wallpaper.bmp`; resolution via `EnumDisplayDevicesA` primary +
  `EnumDisplaySettingsA(ENUM_REGISTRY_SETTINGS)`), and by D3DX (`Software\Microsoft\Direct3D`).

## 5. Risks on Intel iGPU / Win10 / high DPI
1. **4K native exclusive fullscreen** (dpiAware + SM_CXSCREEN): heavy fill-rate for a 2D sprite engine with particles,
   which causes low FPS and makes the software cursor laggy.
2. **No vsync by default + spin-wait** (`Sleep(0)` loop on the event query + `Sleep(0)` per clock read): 100% CPU core and GPU.
   On shared-TDP iGPU laptops this lowers GPU clocks, so expect stutter and tearing.
3. **Tiny window**: windowed 1024x768 at 200% DPI. The hardware cursors are a fixed 32x32 (not DPI-scaled).
4. Auto-rotate / orientation change in fullscreen → forced quit (see §1).
5. `video.bpp=24` → `D3DFMT_R8G8B8` → CreateDevice fails → exit. The fullscreen path doesn't validate the desktop mode
   against the enumerated modes.
6. Exclusive fullscreen alt-tab → device loss. Reset failure only logs and retries every frame (possible black screen if a
   D3DPOOL_DEFAULT resource leaks).
7. `Direct3DCreate9` NULL isn't fatal and leads to a later null deref. `Present`'s HRESULT is ignored.
8. Harmless: `SetThreadAffinityMask(…, 0)` bug. GetVersionExA only in the crash handler (`FUN_005090c0`/`FUN_00509e10`) and
   D3DX. `CreateProcessA("bin\\mb.dll")` only in the crash handler `FUN_004f94f0` (the file doesn't exist).
   `ShellExecuteA` only for URLs. No OS-version or vendor checks.

## Hook points & recommendations

**Cursor (best fix, no D3D capture needed):** use the engine's own OS-cursor path.
- Pre-seed or patch the config XML with `<item id="hardware_cursor" value="true"/>` and `system_cursor=false`. Or at runtime,
  after `DAT_00b71b78` exists, call `FUN_0055b060(mgr, &true)` (thiscall). Each cursor type keeps its own art, Windows
  moves it at hardware rate, and it works in exclusive fullscreen too.
- Hook `LoadImageA` for `hInst == game module && type == IMAGE_CURSOR && IS_INTRESOURCE(id) && 101 <= id <= 108 && cx == cy == 0`.
  Return the same resource scaled to `32 * dpi / 96` (e.g. via `LoadImageW(..., cx, cy, 0)` or our own upscale +
  `CreateIconIndirect`; the hotspot scales with it). The arrow comes from `LoadImageA(NULL, IDC_ARROW, …, LR_SHARED)` and is
  already system-sized.
- Don't add a class cursor. WM_SETCURSOR already does `SetCursor(app+0x74)` for HTCLIENT.
- Only if the animated colour cursor must be kept: the capture point is the per-item render call 0x00567d42 for nodes under
  `*(DAT_00b71b78+0x1AC)`, or the static-sprite function `FUN_0055a230`, which is only active if mgr+0x1A7 is forced to 0.
  This is not recommended.

**Display (pick one):**
- A. Borderless upscale (preferred). In the D3D9 wrapper force `Windowed=TRUE`, keep the game's backbuffer at a reduced
  "virtual" size (e.g. 1920x1080 or 2560x1440), and make the window `WS_POPUP` covering the monitor. Present with a dest rect
  (aspect-correct) / linear stretch. Virtualize `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` (called in 0x0057e370, 0x0057fb70,
  0x00500e50, 0x004ffbe0) to the virtual size so `FUN_0057dd60` builds that backbuffer.
  - Intercept `SetWindowLongA`/`SetWindowPos`/`MoveWindow` from `FUN_004ffbe0` (0x004ffbe0) so the window stays borderless
    and full-monitor, and drop WS_EX_TOPMOST.
  - **Coordinates:** treat client coords as virtual and screen coords as real. Scale lParam of WM_MOUSEMOVE/WM_LBUTTON*
    (and dblclk) real→virtual before WndProc 0x00500020, scale `ScreenToClient` output (used once at input init) and
    `ClientToScreen` input (the speed-warp path before `SetCursorPos`). Leave `GetCursorPos`/`GetClientRect`/`MapWindowPoints`
    real (`FUN_005b7fb0` compares them with each other in screen space).
- B. Simplest: virtualize only `GetSystemMetrics(0/1)` to a lower resolution and keep exclusive fullscreen. D3D9 then
  mode-switches the display, the monitor/GPU scaler upsamples, and mouse coords stay consistent. Costs a mode switch on start
  and alt-tab.
- C. Config only: `video.window=true` plus `video.width/height` (4:3, e.g. 1920x1440). The window is centred and clamped to the
  work area. It renders natively at that size, sharp but at full GPU cost.
- Also fake `dmDisplayOrientation = DMDO_DEFAULT` in `EnumDisplaySettingsA` (0x005056c3) to avoid the rotate-quit, or use
  windowed/borderless, where rotation isn't fatal.

**Frame pacing:**
- Force `PresentationInterval = D3DPRESENT_INTERVAL_ONE` in CreateDevice/Reset (the game default is IMMEDIATE).
- Optionally tame the spin in `FUN_0057f590`. In the wrapper's `IDirect3DQuery9::GetData` for EVENT queries, yield with
  `Sleep(1)`/`SwitchToThread` before returning S_FALSE, or just let it be once vsync is on.
- Optionally add `D3DCREATE_FPU_PRESERVE` to the behaviour flags (0x44/0x24) for timer precision in long sessions.
- Leave `timeBeginPeriod(1)` as is.

**Don't** change DPI awareness at runtime: it's manifest-declared and `SetProcessDpiAwareness*` will fail. If DWM bitmap
scaling is wanted instead, it has to be done from the launcher (AppCompat layer `DPIUNAWARE`/`GDIDPISCALING DPIUNAWARE`
before process start). That's blurry and not recommended over option A.

Key addresses: WndProc 0x00500dc0/0x00500020, run loop 0x004ff9c0, update 0x004f4730, render 0x004f2520,
BeginFrame 0x0057f590, EndScene+Present 0x0057c7d0, device init 0x0057eaa0, PP fill 0x0057e170, mode calc 0x0057dd60,
Reset 0x0057e370, TCL/Reset 0x0057e970, window style 0x004ffbe0, window create 0x00500e50, DInput init 0x005b8740,
DI mouse poll 0x005b82e0, cursor pos copy 0x005b85d0, cursor mgr ctor 0x0055cc00, cursor apply 0x0055c900,
set visible 0x0055a950, App::SetCursor 0x004ffb60, cursor resource load 0x0055c810, static cursor draw 0x0055a230,
anim cursor position 0x0055a4a0, config load/save 0x005041d0/0x005049e0.
