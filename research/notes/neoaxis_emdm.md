# EMDM ("Elementary My Dear Majesty!", My Games\3): NeoAxis 1.x display, cursor, input and timing

Sources: ILSpy output of Game.bin / EngineApp / Renderer / UISystem / GameCommon / Utils /
FileSystem / GameEntities, the Ghidra decompile of EMDM.exe, and pefile+capstone passes over
`NativeDlls\Windows_x86\{OgreMain,RenderSystem_Direct3D9}.dll`. Addresses are preferred VAs.
EMDM.exe uses base 0x00400000. OgreMain and RS_D3D9 both request base 0x10000000 and have no
dynamic-base flag, so one of them gets relocated. Use RVAs (VA - 0x10000000) when patching.
Static analysis only: the game was not run.

## TL;DR

* **The real game process is `Game.bin`.** It is a 32-bit-only .NET 2.0/3.5 exe started as a child
  of the native launcher `EMDM.exe`, which is itself started by the bundle menu `HiddenObject6in1.exe`.
  The hook DLL must end up inside Game.bin.
* **Window:** a raw Win32 window created from C# through P/Invoke `CreateWindowExW` (class
  `NeoAxisEngineWindowClass`). It is not a WinForms Form and not an OGRE window. OGRE receives it
  as `externalWindowHandle`.
* **Fullscreen:** C# `ChangeDisplaySettingsA(CDS_FULLSCREEN)`, then a `WS_POPUP|WS_EX_TOPMOST`
  window, then a D3D9 **exclusive** device (`Windowed=FALSE`). The default is fullscreen at the
  desktop resolution (3840x2160 on 4K), because the process calls `SetProcessDPIAware()`.
* **Defaults that hurt an Intel iGPU:**
  * **4x MSAA on the back buffer** (`FullSceneAntialiasing = 4`).
  * Vsync with **2 back buffers**.
  * Native 4K exclusive mode at the **highest refresh rate**.
  * 32-bit process that is not large-address-aware (2 GB address space).
* **Intel means no shaders.** If the adapter description contains `"Intel"`, OGRE
  `RenderSystem::setMaxShadersRecommendedSetting` (OgreMain 0x101f5bd0) limits VS and PS to "none".
  The game then runs on the fixed-function pipeline: no SM2 features, no shadows, no post effects.
  This can be overridden with `maxPixelShaders`/`maxVertexShaders` in Engine.config.
* **Cursor:** a real **OS hardware cursor**, `SetCursor(LoadCursorFromFileW(...))` with 32x32 `.cur`
  files extracted from Data.zip. The UI system's software cursor is never enabled by this game.
  The arrow itself should not lag. What lags is the GUI drawn at the mouse position (dragged items,
  hover highlights). That lag comes from render latency (vsync + 2 back buffers + D3D9 render-ahead
  + low FPS).
* **Stutter suspect:** `EngineApp.Time` → `WindowsPlatformFunctionality.GetSystemTime()` calls
  `SetThreadAffinityMask` twice per call, plus `Process.GetCurrentProcess().Handle` (OpenProcess).
  This runs many times per frame on the main thread. Make it a no-op from the hook DLL.

---

## 1. EMDM.exe and the process chain

* EMDM.exe is a small MSVC 2008 launcher (version info: Alawar Dream Dale, "Launcher" 1.0.97.0).
  Its main function is `FUN_00401040`. It does no CLR hosting and no OS/.NET/DirectX/Steam/registry
  checks.
* It sets an environment variable and calls
  `CreateProcessW(<exe dir>\Game.bin, <launch argument>, …, bInheritHandles=FALSE, flags=0,
  lpEnvironment=NULL (inherit), lpCurrentDirectory=<exe dir>)`:
  * `SetEnvironmentVariableW` call @0x00401414, `CreateProcessW` call @0x00401472
    (IAT slot 0x0040E020).
  * Then `WaitForSingleObject(hProcess, INFINITE)` @0x00401584.
  * The user's own command-line arguments are not passed on.
* Game.bin (`Game.Program.CheckLauncher`) only runs when started this way. It also re-checks every
  5 s, through `EngineApp.ExitDelegate` on a `System.Threading.Timer`, that the launcher process is
  still alive, and exits otherwise. **Practical rule for the fixer:** always start the game through
  EMDM.exe and inject into its child.
  * Each 5-second check enumerates all processes and builds a compiled Regex on a thread-pool
    thread. That may cause a small GC hitch every 5 s; it is not hookable cheaply.
* **Single instance:** `Process.GetProcessesByName("Game")`. The process name comes from the image
  name "Game.bin". If more than one is found, the game brings the first one's window forward and
  exits **silently**. An unrelated `Game.exe` running elsewhere blocks start-up.
* Game.bin PE details:
  * CLR metadata v2.0.50727, COR flags 0x3 (ILONLY|32BITREQUIRED), not strong-name signed.
  * File characteristics 0x102, so **not LARGEADDRESSAWARE**.
  * Manifest has `asInvoker` and no dpiAware.
  * No `Game.bin.config`, so it needs the Windows **.NET Framework 3.5** feature (no v4 roll-forward).
    A missing feature means failure to start.

## 2. Managed start-up order (`Game.Program.Main2`)

```
VirtualFileSystem.Init("user:Logs/Game.log", correctCurrentDirectory:true)   // cwd := exe dir
EngineApp.ConfigName = "user:Configs/Game.config"
launcher check; single-instance check
EngineApp.UseDirectInputForMouseRelativeMode = true; AllowJoysticksAndCustomInputDevices = true
EngineApp.AllowWriteEngineConfigFile = true; AllowChangeVideoMode = true
RendererWorld.InitializationOptions.VerticalSync = true
EngineApp.Init(new GameEngineApp())  // InitInternal: 5 s timer; PlatformFunctionality.Get()
                                     //   -> WindowsPlatformFunctionality..ctor -> SetProcessDPIAware()
                                     //   fullScreen = true; videoMode = GetSystemMetrics(SM_CXSCREEN/CYSCREEN)
                                     //   Config.Init(Game.config)
EngineApp.Instance.Create()          // Engine.config -> WindowCreate() -> physics -> DirectInput/XInput
                                     //   -> RendererWorld._Init (OGRE + D3D9) -> sound -> EngineInitialization
                                     //   -> GameEngineApp.OnCreate (loading screen, materials, SplashWindow)
EngineApp.Instance.Run()             // WindowsPlatformFunctionality.RunMessageLoop
```

## 3. User directory and config files

`Engine.FileSystem.VirtualFileSystem` (FileSystem.dll) reads `userDirectory = EMDM` from
`Definitions/Deployment.config` inside Data.zip. The user dir is
`Environment.GetFolderPath(LocalApplicationData)` + `EMDM`, i.e. **`%LOCALAPPDATA%\EMDM`**.
Without that key it would be `<exe>\UserSettings`. The folder does not exist on this machine yet.

| file | read / written by | notes |
|---|---|---|
| `%LOCALAPPDATA%\EMDM\Configs\Engine.config` | read: `EngineApp.Create`; written: `EngineApp.ShutdownInternal` | renderer / video |
| `%LOCALAPPDATA%\EMDM\Configs\Game.config` | `Engine.Config` (`[Config(group,name)]` statics); load at init, save at exit | game options |
| `%LOCALAPPDATA%\EMDM\Logs\Game.log` | Log | logs GPU description/codename, SM2/SM3 yes/no and errors |
| `%LOCALAPPDATA%\EMDM\_Temp_<cursor>.cur/.ani` | `WindowsPlatformFunctionality.UpdateSystemCursorFileName` | extracted, loaded, deleted at once |

**Format:** NeoAxis `TextBlock` (`TextBlock.DumpToString`). A named child block, `{`,
`<tab>key = value` lines, `}`. A `Vec2i` is written as `"W H"`. Bools are `True`/`False`
(`bool.Parse`). Enums use case-sensitive `Enum.Parse`. Example:

```
Renderer
{
	fullScreen = True
	videoMode = 3840 2160
	fullSceneAntialiasing = 0
}
```

### Engine.config, block `Renderer` (parsed in `EngineApp.Create`; all optional)

| key | values | default if absent |
|---|---|---|
| `renderSystemName` | dll name | `Definitions/Renderer.config` → `RenderSystem_Direct3D9.dll` |
| `renderingDeviceName` / `renderingDeviceIndex` | → `OgreRoot.setRenderingDevice` | auto (§6.1) |
| `maxPixelShaders` | `RecommendedSetting, PS0, PS11, PS12, PS13, PS14, PS20, PS20a, PS20b, PS30` | Recommended (**Intel → PS0**) |
| `maxVertexShaders` | `RecommendedSetting, VS0, VS11, VS20, VS20a, VS30` | Recommended (**Intel → VS0**) |
| `fullSceneAntialiasing` | int → OGRE "FSAA" | **4** (`RendererWorld.InitializationOptionsClass`) |
| `filtering` | `RecommendedSetting, Trilinear, Anisotropic2x, Anisotropic4x, Anisotropic8x, Anisotropic16x` | Recommended → Trilinear for Intel/unknown GPU |
| `renderTechnique` | `Standard` / `HDR` (`GameCommon.GameEngineInitialization`) | → `Standard` (HDR never default) |
| `allowChangeDisplayFrequency` | bool (`DisplaySettings`) | **True**: fullscreen CDS picks the max Hz for the mode |
| `verticalSync` | bool | True (Program forces it unless the key is present) |
| `fullScreen` | bool (honored because AllowChangeVideoMode = true) | **True** |
| `multiMonitorMode` | bool | False |
| `videoMode` | `"W H"` | desktop size from `GetSystemMetrics`, physical px |

Other blocks: `SoundSystem.soundSystemName`, `Localization.language`,
`PhysicsSystem.physicsSystemName/allowHardwareAcceleration`.

On exit, `ShutdownInternal` reloads the file and keeps unknown keys. It then sets:

* `fullScreen` always.
* `videoMode` when windowed. This is the **outer** window size: `OnDestroy` sets
  `videoMode = GetWindowRect` size.
* `videoMode` in fullscreen only if Options changed it.
* It deletes `videoMode` if the window was maximized.

So keys we pre-seed persist.

### Game.config groups

Declared in `Game.GameEngineApp`; defaults in brackets.

* `Video`:
  * `gamma` [1]. Applied with `SetDeviceGammaRamp` on the desktop DC when it is not 1.
  * **`showRealSystemCursor` [False]**.
  * `drawFPS` [False]. Shows the engine FPS overlay; handy for testing.
  * `materialScheme`, `shadowTechnique` [ShadowmapHigh], `shadowUseMapSettings` [True],
    `shadowColor`, `shadowFarDistance`, `shadowPSSMSplitFactors`.
  * Shadow texture sizes and counts [2048/2048/1024; `ChangeToBetterDefaultSettings` lowers them to
    1024/1024/512 for non-GeForce/Radeon GPUs].
  * `waterReflectionLevel`, `showDecorativeObjects`.
* `Sound`: `soundVolume` [1], `musicVolume` [0.2].
* `HiddenObjects`: `autosave`, `cheats`, `easyMode` [True], `usePostEffects` [True]. On first profile
  creation `HOMainMenuWindow` sets it to `IsActivatePostEffectsByDefault()`, which is false on Intel.
* `Profile.currentProfileName`, `Environment.autorunMapName`, `Debug.*` (`EngineDebugSettings`),
  `GamControls.*`.
* If Game.config fails to parse, `EngineApp.Init` fails and Program then dereferences a null
  instance, so start-up crashes with a "Fatal" message box. Our config writer must emit valid
  TextBlock.

## 4. Video modes, DPI, window creation

* **DPI:**
  * `WindowsPlatformFunctionality..ctor` calls `SetProcessDPIAware()` when `OSVersion.Major >= 6`.
    The return value is ignored. This happens before the game window exists, so the process is
    **system-DPI-aware**.
  * `GetScreenSize()` = `GetSystemMetrics(SM_CXSCREEN, SM_CYSCREEN)` (physical pixels).
  * No per-monitor DPI, no `WM_DPICHANGED` handling. No `Screen`/`SystemInformation`/`DpiX`/WinForms
    autoscale anywhere (WinForms is not used).
  * UI scales by resolution (`UISystem.config scaleByResolutionBaseHeight = 768`), so its layout is
    proportional at any resolution. Fonts are rasterized at the pixel size.
* **Mode list** (Options "ScreenResolution", `Game.HOOptionsWindow.FillVideoModes`):
  * Source: `DisplaySettings.VideoModes` → `WindowsPlatformFunctionality.GenerateVideoModes`.
  * That walks `EnumDisplaySettingsA(NULL, i)` and keeps modes that have
    `DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT`, pass a `ChangeDisplaySettingsA(&dm, CDS_TEST|CDS_FULLSCREEN)`
    test, and have bpp equal to the desktop bpp.
  * Duplicates are removed; the list is sorted by width, then height. The fallback list is
    640x480 … 1600x1200.
  * The Options UI then keeps width ≥ 640 and aspect 1.25 to 1.8 (16:9, 16:10, 4:3 and 5:4 pass;
    21:9 does not), and shows only the **last 21** (largest).
  * The CDS_TEST loop runs the first time Options opens; it can hitch on 4K panels that report many
    modes.
* **Window** (`WindowsPlatformFunctionality.InitApplicationWindow`):
  * `RegisterClassW`: `NeoAxisEngineWindowClass`, `CS_DBLCLKS`, **hCursor = NULL**, black brush.
    The WndProc is the managed delegate `ApplicationWindowProc`.
  * Fullscreen: `CreateWindowExW(WS_EX_TOPMOST (unless a debugger is attached), …,
    WS_POPUP|WS_VISIBLE, 0, 0, VideoMode.X, VideoMode.Y)`.
  * Windowed: style `0x00CB0000` (CAPTION|SYSMENU|MINIMIZEBOX|MAXIMIZEBOX, **no WS_THICKFRAME**)
    plus WS_VISIBLE. Centered; **outer** size = VideoMode.
  * Windowed with VideoMode equal to the screen size: an 800x600 invisible window, then
    `ShowWindow(SW_MAXIMIZE)`.
  * Then `SetForegroundWindow`, `SetFocus`, and `SetTimer(id 31692, 10 ms)`.
* `OnResize` (`WM_SIZE`): when windowed, `videoMode = GetClientRect` size. It calls
  `RenderWindow.WindowMovedOrResized(FullScreen)` whenever the render window size differs from
  `videoMode` or the fullscreen flag changed.
* Hotkeys (`EngineApp.DoKeyDown` / WndProc): `Alt+Enter` toggles FullScreen, `Win+M` minimizes,
  `Alt+F4` exits, `F12` takes a screenshot.
* **At 4K** the first run is exclusive 3840x2160, 4xMSAA, vsync, at the maximum refresh rate. The
  window is not DWM-scaled because the process is DPI-aware. Blur therefore comes from a non-native
  fullscreen mode picked in Options (panel/GPU scaler) or from the cursor (§8).

## 5. Fullscreen, window styles, focus

* **Entering fullscreen** (`EngineApp.WindowCreate`, `WindowsPlatformFunctionality.ProcessChangingVideoMode`):
  1. `DisplaySettings.ChangeVideoMode` → `ChangeDisplaySettingsA(DEVMODE{w, h, desktop bpp,
     [max Hz if allowChangeDisplayFrequency]}, CDS_FULLSCREEN)`. If it fails, `WindowCreate`
     returns false and the game quits. A saved mode that is unsupported, for example after a
     monitor change, is a start-up failure.
  2. `SetWindowPos(HWND_TOPMOST)`.
  3. `SetWindowLongW(GWL_STYLE)`: clear `0x00CB0000`, set `WS_POPUP`.
  4. Move to (0,0) and size to VideoMode.
  5. Minimize/restore.
  6. The D3D device is reset through `WindowMovedOrResized(true)`.

  Leaving fullscreen is the reverse: `ChangeDisplaySettingsA(NULL, 0)`, NOTOPMOST,
  style `0xCB0000`, centered.
* **D3D9:** `Windowed = !(fullScreen && !multiMonitor)`; refresh = OGRE `mDisplayFrequency`, which
  is 0 because NeoAxis passes no `displayFrequency`. So it is **true exclusive mode**.
  RenderSystem_Direct3D9.dll imports only `GetClientRect` and `ClientToScreen` from user32. It never
  calls ChangeDisplaySettings or creates windows; without an external HWND it fails with
  "OgreD3D9RenderWindow: create: !externalHandle.".
* **Losing focus** (`WM_ACTIVATE` with WA_INACTIVE):
  * In relative mouse mode: `ReleaseCapture` and `ClipCursor(NULL)`.
  * With `SuspendWorkingWhenApplicationIsNotActive` (default true): `DoSystemPause(true)` pauses
    sound and releases all keys. `GameEngineApp.OnSystemPause` adds an `HOPauseWindow` in HO scenes.
  * In fullscreen it **minimizes** (`ShowWindow(SW_SHOWMINIMIZED)`) and restores the desktop mode.
    Any focus thief (overlay, notification) therefore minimizes the game.
  * `WM_ENTERMENULOOP` also pauses.
* **Getting focus back:** if fullscreen and VideoMode ≠ screen size, the mode is changed again on
  the next idle. The pause is lifted in `ApplicationIdle` once `GetForegroundWindow() == hwnd`, the
  window is not iconic, and no menu loop is active.
* `WM_ACTIVATE`, `WM_SETFOCUS` and `WM_KILLFOCUS` return 0 **without DefWindowProc**. DefWindowProc
  is what normally calls `SetFocus` on activation. After an alt-tab, `GetFocus()` can therefore stay
  NULL; clicks do not call SetFocus either. Many cursor paths are gated by
  `IsWindowFocused() = (GetFocus() == hwnd)` (§8). Verify at runtime.
* `WM_SETCURSOR` is not handled. DefWindowProc applies the NULL class cursor, which changes nothing
  in the client area, so the cursor shows whatever was last set.

## 6. Renderer: OGRE 1.6-based D3D9 render system, modified by NeoAxis

### 6.1 Loading and device creation

* `RendererWorld.InitInternal` → `OgreRoot.loadPlugin("RenderSystem_Direct3D9.dll")`. OgreMain's
  `DynLib::load` (0x102618cc) calls `LoadLibraryExW(<NativeDlls\Windows_x86>\RenderSystem_Direct3D9.dll,
  0, LOAD_WITH_ALTERED_SEARCH_PATH)`. As a result:
  * The RS's static imports `d3d9.dll` and `d3dx9_43.dll` are searched in **NativeDlls\Windows_x86
    first**, so the bundled d3dx9_43 and D3DCompiler_43 are always used.
  * A `d3d9.dll` that is already loaded in the process is reused by name.
  * Game.bin itself (IL-only) has no d3d9 import.
* `D3D9RenderSystem` ctor: `Direct3DCreate9(32)` (call @0x1002547c through the thunk @0x1000b8ee).
  There is **no Direct3DCreate9Ex**.
* `D3D9RenderSystem::updateActiveD3DDriver` (~0x10024330) prefers the first adapter whose
  description contains `"GeForce"` or `"Radeon"`, otherwise adapter 0. `renderingDeviceName`
  overrides this.
* `D3D9RenderWindow::createD3DResources` (0x10016530); present params live at `this+0xBC`:

  | field | value |
  |---|---|
  | BackBufferWidth/Height | `GetClientRect(hwnd)` (calls @0x100175aa and @0x10017abb) |
  | BackBufferFormat | `X8R8G8B8` (R5G6B5 if desktop is 16 bpp) |
  | BackBufferCount | **vsync ? 2 : 1** |
  | SwapEffect | DISCARD |
  | MultiSampleType | FSAA (4 by default), stepped down until `CheckDeviceMultiSampleType(Windowed=TRUE)` passes; quality = FSAAQuality (0) |
  | AutoDepthStencil | on; D24S8 → D24X8 → D32 → D16 |
  | Windowed / refresh | see §5 |
  | PresentationInterval | vsync ? ONE : IMMEDIATE (logs a warning when windowed without vsync) |
  | hDeviceWindow / focus window | the NeoAxis HWND |

* CreateDevice chain (0x10016a91…0x10016b43):
  * `HAL + HARDWARE_VERTEXPROCESSING` (retried once), then `MIXED`, then `SOFTWARE`, then
    `D3DDEVTYPE_REF`.
  * `D3DCREATE_FPU_PRESERVE` only when `Direct3DFPUPreserve` is set, which happens only in web-player
    mode. On the desktop, D3D9 therefore puts x87 into 24-bit precision on the CLR main thread. That
    is harmless here because engine time is a float anyway.
  * **Never `D3DCREATE_MULTITHREADED`.** Rendering is single-threaded on the main (STA) thread.
* **"Allow NVPerfHUD"** exists as an OGRE option / `useNVPerfHUD` misc parameter. When set, it picks
  an adapter named "PerfHUD" with the REF device type. NeoAxis never passes it, so it is off.
* Device lost:
  * `D3D9RenderWindow::update` (0x100176f0) calls `TestCooperativeLevel` every frame. While
    `D3DERR_DEVICELOST` it releases surfaces and does `Sleep(50)`; otherwise it restores (0x10021c60).
  * C# also polls `IsDeviceLostByTestCooperativeLevel` several times per frame.
  * `WindowMovedOrResized(bool fullScreen)` (0x10017a80, NeoAxis signature) rewrites Width/Height,
    Windowed and refresh, then resets through 0x10021530.
* `swapBuffers` (0x10017960) is a plain `Present(NULL, NULL, NULL, NULL)` (swap-chain `Present` for
  secondary windows, which this game does not use).

### 6.2 `fixDirect3DSynchronizationIssueInWindowedModeOnRadeon`

* `Definitions/Renderer.config` (in Data.zip) sets it to `false`.
  `RendererWorld.InitInternal` → `OgreRoot_setFixDirect3DSynchronizationIssueInWindowedModeOnRadeon`
  stores it at `root+0x1C0`.
* In `D3D9RenderWindow::update` (0x10017892…0x10017945): when the flag is set, the adapter
  description contains `"Radeon"`, and the window is **not fullscreen**, it runs
  `CreateQuery(D3DQUERYTYPE_EVENT)` → render → `Issue(D3DISSUE_END)`, then **busy-spins**
  `GetData(…, D3DGETDATA_FLUSH)` until the GPU is done, then releases the query. That gives zero
  queued frames: lowest latency, but no CPU/GPU overlap (lower FPS, one core spinning).
* It never applies to Intel, and it is off anyway. The same idea is a good template for our wrapper,
  done less aggressively (§10).

### 6.3 Caps, shaders, render techniques (Intel specifics)

* `RenderSystem::setMaxShadersRecommendedSetting` (OgreMain 0x101f5bd0, called from RS @0x1002463e
  before caps are built):
  ```
  if strstr(adapterDescription, "Intel"):
      if maxVS == RecommendedSetting: maxVS = VS0
      if maxPS == RecommendedSetting: maxPS = PS0
  ```
  The RS caps conversion (pixel caps in function 0x100181f0) then clamps shader caps to "none".
  Knock-on effects in C#:
  * `RenderSystem.InitGPUSettingsAndCapabilities` reports `HasShaderModel2/3 = false`.
  * `GameEngineApp.OnCreate` adds no LDRBloom/ColorCorrection compositors.
  * `ChangeToBetterDefaultSettings` sets shadows to None.
  * The GlowTextureManager glow compositor is skipped.
  * The shader cache would be "ShaderModel0", which is not shipped and not needed.
  * Everything renders through **fixed-function** fallbacks. That costs less GPU but looks worse,
    and some materials may lack a good FFP technique.
* Setting `maxPixelShaders = PS30` and `maxVertexShaders = VS30` restores the SM3 path. Data.zip ships
  `Caches/ShaderCache/Direct3D9_ShaderModel3_General.shaderCache`, which Intel would use, so there is
  no runtime HLSL compile. Without the cache it would call `D3DXCompileShader` through the bundled
  d3dx9_43.
* `GPUCodeNames` has **no Intel entry**, so `RenderSystem.GPUIsIntel()` is always false and Intel is
  treated as "Unknown":
  * `IsActivatePostEffectsByDefault()` returns false.
  * The Options window hides "PostEffects" and shows a gamma slider instead (any non-GeForce/
    non-Radeon GPU).
  * Filtering is Trilinear.
  * The shader-cache shadow variant is "General" (ATI/NVidia variants are only for Radeon/GeForce).
* Render techniques: HDR needs the "HDR" compositor plus a Float16RGB render target, and is never
  the default (`IsActivateHDRByDefault` returns false). Depth shadow maps need a Float32R render target.
* Large render targets:
  * `Game.GlowTextureManager.CreateGlowTexture` creates an A8R8G8B8 RTT of **VideoMode size**.
  * It recreates that RTT whenever `glowViewport.DimensionsInPixels != DefaultViewport.DimensionsInPixels`.
  * So any scheme that makes the back buffer differ from VideoMode makes the glow RTT be recreated
    **every frame** while something glows.
* No `Direct3DCreate9Ex`, and textures and buffers use `D3DPOOL_MANAGED`. Do not upgrade the device
  to 9Ex.

## 7. Main loop and timing

* `WindowsPlatformFunctionality.RunMessageLoop` is a busy loop:
  ```
  loop: if PeekMessageW(PM_REMOVE): (WM_QUIT -> break) Translate/Dispatch; continue
        if needExit: break
        if IsAllowApplicationIdle(): EngineApp.ApplicationIdle(false)   // DoTick + RenderSceneInternal
        else WaitMessage()
  IsAllowApplicationIdle = !IsIconic && !(FullScreen && GetForegroundWindow()!=hwnd) && !SystemPause
  ```
* There is **no Sleep, no FPS cap and no timeBeginPeriod**. Vsync (default on) is the only limiter.
  * With vsync off it runs uncapped and keeps a core at 100%, which also starves the Intel iGPU's
    shared power budget.
  * With vsync on in fullscreen it runs at the maximum refresh rate chosen by CDS (e.g. 144 Hz).
* While idle is not allowed (paused or minimized), the 10 ms `WM_TIMER` (id 31692) calls
  `ApplicationIdle(doTickOnly:true)`, which ticks without rendering. `WM_PAINT` renders one frame
  while paused.
* `EngineApp.DoTick` clamps deltas over 1 s (re-syncs time). The entity simulation is fixed at
  **30 Hz** (`EntitySystem.config gameFPS = 30`); the UI ticks every frame.
* **Time source:** `WindowsPlatformFunctionality.GetSystemTime()`, used by `EngineApp.Time` and
  `MyRenderTimerManager`, is called many times per frame:
  ```
  GetProcessAffinityMask(Process.GetCurrentProcess().Handle)   // new Process + OpenProcess each call
  old = SetThreadAffinityMask(GetCurrentThread(), lowestBit)    // migrate to the first CPU
  QueryPerformanceCounter; SetThreadAffinityMask(thread, old)
  if |QPC_ms - GetTickCount_ms| > 100: shift the QPC base       // old multi-core QPC workaround
  ```
  Repeated thread migration plus handle and allocation churn on the render thread is a plausible
  source of micro-stutter. OGRE's own timer does not touch affinity: OgreMain does not import
  SetThreadAffinityMask, and NeoAxis feeds it via `OgreRoot_setTimerMilliseconds`.

## 8. Cursor and mouse

* **OS cursor, set per frame.**
  * `WindowsPlatformFunctionality.UpdateSystemCursorFileName` extracts the virtual file from Data.zip
    to `%LOCALAPPDATA%\EMDM\_Temp_<name>`, calls `LoadCursorFromFileW`, deletes the file, caches the
    handle per name, then calls `SetCursor(h)`. It falls back to `LoadCursorW(NULL, IDC_ARROW)` when
    the name is empty or loading fails.
  * It is triggered by the `EngineApp.SystemCursorFileName` setter. That setter is gated by
    `IsWindowFocused()` and requires the mouse to be inside the window.
  * Callers:
    * `GameEngineApp.OnRenderFrame`: every frame while no map is loaded (menus). It uses
      `"HO\\Cursors\\standart.cur"`, or "" when `showRealSystemCursor` is set.
    * `HOMainMenuWindow.OnAttach`.
    * `Game.HiddenObjectGameWindow.SetCursor(name)`, from per-frame hit tests:
      `Standart.cur`, `Standart-click.cur` (LMB down), `Use/Scale/Target.cur` (per object
      `GetCursorType()`), `Move.cur`, `Move.ani` (2-frame animated).
* **Show/hide:** `EngineApp.ShowSystemCursor` → `UpdateShowSystemCursor`, which loops
  `while (ShowCursor(TRUE) < 0)` or `while (ShowCursor(FALSE) >= 0)`.
  * `IsRealShowSystemCursor` returns false in relative mode and true when unfocused or when the
    mouse is outside the window.
  * `GameEngineApp._ShowSystemCursor` defaults to false, so the cursor is hidden during loading.
    Menus and HO scenes then set it to true.
* **Software cursor:** `Engine.UISystem.ControlManager.DoRenderUI` can draw `CurrentCursor` (a
  texture) at `MousePosition`. This game never sets `DefaultCursor` (only to null), so it is
  **unused**. The PNG twins under `HO\Cursors\*.png` are leftovers.
  Things that *are* GUI-drawn at the mouse and therefore lag:
  * the dragged inventory item (`attachedItem`),
  * `KartinaWindow` drag pieces,
  * `MolbertWindow` "ECursor" brush,
  * hover glow.
* **Cursor files** (Data.zip):
  * `HO/Cursors/{Standart,Standart-click,Move,Scale,Use,Target}.cur`: single 32x32 32-bpp image,
    hotspot (0,0); Target is (15,15).
  * `HO/Cursors/Move.ani`: 2 frames.
  * Also `Cursors/DefaultSystem.cur` (32x32 4-bpp), unused.
  * At 4K they are either tiny (32 px) or upscaled by the system to the cursor size. Either way they
    look wrong at 150–200%.
* **Config tweak:** `Game.config Video.showRealSystemCursor = True`, or the in-game Options
  "SystemCursor" checkbox. It uses the Windows arrow, which is DPI-scaled and hardware-drawn, but
  loses the custom cursor shapes.
* **Mouse position:** event driven.
  * `WM_MOUSEMOVE` and `WM_NCMOUSEMOVE` → `EngineApp.DoMouseMove` → `MousePosition` =
    `GetCursorPos` + `ScreenToClient`, divided by **`VideoMode`, not the client rect**.
  * Buttons come from `WM_[LRMX]BUTTON{DOWN,UP,DBLCLK}`, the wheel from `WM_MOUSEWHEEL`.
  * Stuck buttons/keys are released by `GetKeyState` polling in `UpdateInputDevices`.
  * No raw input. UI hit-testing calls `GetCursorPos` again many times per frame.
* **Relative mode** (`EngineApp.MouseRelativeMode`): used by `SplashWindow` and `EngineLogoWindow`
  (the start-up logo screens) and some minigames (`BallGameWindow`, `EvolutionGameWindow`,
  `ActionGameWindow`, `RTSGameWindow`).
  * The cursor is hidden.
  * Every tick: `SetCapture` + `ClipCursor(window rect minus 1 px)`.
  * Deltas come from a DirectInput8 mouse (`Engine.DirectInputMouseDevice`, `DISCL_NONEXCLUSIVE |
    DISCL_FOREGROUND`, `GetDeviceState`).
  * Every tick it warps with `SetCursorPos` to the window centre.
  * On exit from the splash it warps to (0.9, 0.8) of the window.
* Joysticks: DirectInput8 enumeration plus a WMI-based XInput-device check (`DirectInputNativeWrapper`),
  and `XInput1_3.dll` when present. This runs at start-up and can be slow with odd USB devices.

## 9. Likely failure and performance causes on Intel UHD + Windows 10 + 1440p/4K

1. **Fill-rate:** native 4K exclusive mode with 4x MSAA back buffer and depth, plus a 4K glow RTT,
   with no shaders. → Low FPS.
2. **Latency:** vsync with BackBufferCount=2, D3D9 render-ahead (up to 3 frames) and no frame-pacing.
   → Laggy feel of hover and drag at low FPS.
3. **Main-thread stalls:** `SetThreadAffinityMask` churn in `GetSystemTime`, the 5 s launcher check
   with GC, busy-spin Present. → Jerky frames.
4. **Exclusive-mode fragility:** focus loss minimizes the game and resets the mode, and the device
   is lost/reset on alt-tab. CDS at the maximum refresh rate. A saved mode that is no longer valid
   means no start.
5. **2 GB address space** (not LAA) with D3DPOOL_MANAGED system-memory copies of large HO textures,
   plus 4K MSAA surfaces. → Out-of-memory crashes are possible.
6. **Start failures:** .NET 3.5 feature missing; the "Game" process-name collision; started without
   EMDM.exe or EMDM.exe killed; Game.config parse error; a `Fatal` MessageBox hidden behind a topmost
   fullscreen window.
7. **FFP-only rendering on Intel** (by design). Visual quality issues and possibly missing effects.
   Not a crash cause on its own.

## 10. Hook points and recommendations for the hook DLL

**Getting in**
* Inject into **Game.bin**.
  * Hook `CreateProcessW` (kernel32/kernelbase) in EMDM.exe (call site 0x00401472). When
    `lpApplicationName` ends in `\Game.bin`, OR in `CREATE_SUSPENDED`, inject the DLL, then resume.
  * Keep `lpEnvironment` / inheritance exactly as passed, because the launch handshake relies on it.
    Leave EMDM.exe running.
  * The bundle menu `HiddenObject6in1.exe` → EMDM.exe is one more hop with the same technique.
* Build the DLL as 32-bit with a static CRT. The process already uses a private VC90 SxS CRT
  (9.0.21022.8) in NativeDlls.
* Consider sanitizing `__COMPAT_LAYER` DPI/compat flags inherited from the parents.

**Config pre-seeding** (write `%LOCALAPPDATA%\EMDM\Configs\Engine.config` before Game.bin's
`EngineApp.Create`, keeping existing keys)
* `fullSceneAntialiasing = 0`. Biggest single GPU win. The D3D wrapper can enforce it too.
* `verticalSync = True`, `allowChangeDisplayFrequency = False` (avoids 120/144 Hz CDS).
* `filtering = Trilinear` (or `Anisotropic4x`).
* Shaders: leave `RecommendedSetting` (FFP, cheapest) or set `maxPixelShaders = PS30`,
  `maxVertexShaders = VS30` (full SM3 path; SM3 cache is shipped). Make it a user option, test both.
* Pick the display mode per §"Display" below via `fullScreen` / `videoMode`.
* Optional: `Game.config` `Video.showRealSystemCursor = True` as the "safe cursor" choice;
  `Video.drawFPS = True` for testing.

**Display** (recommended default: borderless at desktop resolution)
* Keep the engine's `fullScreen = True` and `videoMode = <desktop W H>`. The engine then creates a
  `WS_POPUP` window at (0,0) covering the monitor, and mouse normalization (÷VideoMode) stays correct.
* `ChangeDisplaySettingsA/W/ExA/ExW` hook: when the requested size equals the current desktop mode
  (ignore frequency), or for `(NULL, 0)` while nothing was changed, return `DISP_CHANGE_SUCCESSFUL`
  without switching. The game uses the **A** variants (DllImport without CharSet); `CDS_TEST`
  calls also come from the mode list.
* D3D9 wrapper: in **both** `CreateDevice` and `Reset`, force `Windowed = TRUE` and
  `FullScreen_RefreshRateInHz = 0`. The engine rewrites `Windowed=FALSE` before every
  `Reset` in `WindowMovedOrResized(true)`.
* `CreateWindowExW` (class `NeoAxisEngineWindowClass`): drop `WS_EX_TOPMOST`. In `SetWindowPos`
  for that HWND, replace `HWND_TOPMOST` with `HWND_NOTOPMOST`.
* Optionally swallow `ShowWindow(hwnd, SW_SHOWMINIMIZED)` that follows `WM_ACTIVATE(WA_INACTIVE)`,
  so alt-tab does not minimize. Allow it when Win is held (Win+M) or on a taskbar minimize.
  Rendering still pauses when not foreground (engine logic), which is fine.
* Fast low-res option: real exclusive fullscreen at a lower `videoMode` (e.g. `1920 1080` on a 4K
  panel, an exact 2x). Let the engine do CDS; do not force Windowed.
* **Do not** try a hidden internal resolution by faking `GetClientRect` only for
  RenderSystem_Direct3D9. The glow RTT (VideoMode-sized) would then be recreated every frame (§6.3).
  The mouse also depends on VideoMode. If render scaling is wanted, the consistent cheap variant is
  DPI-unaware + borderless (below).

**DPI**
* Default: leave it. The game makes itself system-DPI-aware before its window exists, so rendering
  is native and crisp.
* Per-monitor-V2 is optional. Call `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` from the
  DLL before the CLR creates any HWND; the game's later `SetProcessDPIAware()` then fails harmlessly.
  This only matters on mixed-DPI multi-monitor setups.
* "Performance mode" render scale: hook `SetProcessDPIAware` to return TRUE without doing anything,
  so the process stays DPI-unaware. `GetSystemMetrics` then reports logical size (e.g. 1920x1080 at
  200%), the engine renders at that size, and DWM upscales (bilinear, softer). All coordinates stay
  consistent. Use it **only with borderless/windowed**, never with exclusive CDS.

**D3D9 wrapper**
* Install it when `d3d9.dll` is loaded by RenderSystem_Direct3D9.dll. That happens late, during
  `EngineApp.Create`, from NativeDlls with altered search path. Options:
  * (a) early in the DLL, `LoadLibraryW(<system dir>\d3d9.dll)` and MinHook `Direct3DCreate9`. The
    RS import binds to the already-loaded module.
  * (b) hook `LoadLibraryExW` / `LdrLoadDll` and patch RenderSystem_Direct3D9's IAT when it appears.
  * Do not rely on Game.bin's IAT.
  * A proxy `d3d9.dll` in NativeDlls\Windows_x86 would also be picked up, but that is a deployment
    change.
* Only `IDirect3D9` (SDK 32): `CreateDevice`, `CheckDeviceMultiSampleType`, `CheckDeviceFormat`,
  `CheckDepthStencilMatch`, `GetAdapterIdentifier`, `GetAdapterCount`, mode enumeration.
* Device methods seen in the RS:
  * `TestCooperativeLevel`, `Present`, `Reset`, `CreateAdditionalSwapChain`.
  * `GetBackBuffer` / `GetDepthStencilSurface` / `CreateDepthStencilSurface`.
  * `CreateQuery`.
  * The usual resource/state calls. Keep refcounts exact and pass through `D3DERR_DEVICELOST` /
    `D3DERR_DEVICENOTRESET`.
* Present-param overrides:
  * `MultiSampleType = NONE`, `MultiSampleQuality = 0`.
  * Keep `PresentationInterval = ONE`.
  * `BackBufferCount = 1` when our latency limiter is active (else leave 2).
  * Windowed per display mode above.
  * No need to add `MULTITHREADED`. Do not add `FPU_PRESERVE` (not needed; changes engine behavior).
* **Frame-latency limiter** (main input-lag fix): after each `Present`, `Issue(D3DISSUE_END)` an event
  query into a ring of 2. Before returning from the next `Present`, or at the next `BeginScene`, wait
  for the query from frame N−1 using `GetData(FLUSH)` with `SwitchToThread`/`Sleep(0)` backoff. This
  caps queued frames at 1. It is a softer version of the engine's own Radeon fix (§6.2), which waits
  on the current frame.
* Optional **frame cap**, for vsync-off or high-refresh setups: high-resolution waitable timer sleep
  in `Present`, e.g. 60 fps. This also gives the iGPU more power headroom because the main loop
  never sleeps.
* **Do not spoof `GetAdapterIdentifier().Description`.**
  * "Intel" in the string disables shaders.
  * "GeForce"/"Radeon" changes adapter choice, GPU code name, default options, the Radeon latency
    hack, and shader-cache vendor variants (ATI/NVIDIA hardware shadows), which would break on Intel.
  * Use the Engine.config keys instead.
* Steam overlay (if launched from Steam) may hook `Present` in the same process. Chain-friendly
  hooking is required.

**Cursor**
* Hook `LoadCursorFromFileW`. The engine passes `%LOCALAPPDATA%\EMDM\_Temp_<name>.cur/.ani`, and the
  file exists only during this call.
  * Load it yourself with `LoadImageW(path, IMAGE_CURSOR, s, s, LR_LOADFROMFILE)` where
    `s = 32 * GetDpiForSystem() / 96`, or `GetSystemMetricsForDpi(SM_CXCURSOR)`.
  * For a crisp result, decode the 32x32 image, upscale by an integer factor (nearest neighbour),
    scale the hotspot, and `CreateIconIndirect(fIcon=FALSE)`.
  * Return a normal HCURSOR; the engine `DestroyCursor`s it at shutdown.
* `SetCursor` needs no change. It is called with the same handle every frame or hit-test, which is
  cheap. If the alt-tab focus issue (§5) is confirmed:
  * Subclass the NeoAxis HWND. On `WM_ACTIVATE` active (not minimized), call `SetFocus(hwnd)`.
  * Optionally answer `WM_SETCURSOR`/`HTCLIENT` by re-applying the last engine cursor, which stops a
    stale cursor after the mouse crosses the caption/borders in windowed mode.
* **If `ShowCursor` is ever hooked, it must return a real changing display counter.** The engine
  spins `while (ShowCursor(1) < 0)` / `while (ShowCursor(0) >= 0)`, so a constant return value
  hangs the game.
* Leave `GetCursorPos`, `ScreenToClient` and `SetCursorPos` alone unless the client size and VideoMode
  are deliberately changed. Mouse coordinates are client pixels ÷ VideoMode.

**Timing**
* Hook `SetThreadAffinityMask` (kernel32 and kernelbase) for the Game.bin main thread: when the
  request is a single-CPU mask, return the current mask without applying it. QPC is invariant on
  target hardware.
* No `timeBeginPeriod` is needed by the game. Only add it if our frame cap sleeps.

**Robustness**
* Before launch, check that the .NET 3.5 feature is present and that no other process is named
  `Game`; give a clear error instead of the silent exit.
* Validate or repair `Engine.config` `videoMode` against `EnumDisplaySettings`, or make the CDS hook
  tolerant.
* Validate that `Game.config` parses.
* Optionally offer an *offline* LARGEADDRESSAWARE patch of Game.bin with a backup. Game.bin is not
  strong-name signed. The flag cannot be set at runtime.
