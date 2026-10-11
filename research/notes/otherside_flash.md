# The Other Side: Tower of Souls (My Games\8): Flash host RE notes

Target: `TheOtherSide_TowerofSouls.exe` (Delphi 2009 / CodeGear RAD Studio 12.0.3170, linked 2014-04-17).
PE facts: ImageBase 0x400000, **DllCharacteristics = 0** (no ASLR, no NX opt-in), not LARGE_ADDRESS_AWARE.
All VAs below are stable at runtime, so in-memory patches at fixed addresses are safe.
Embedded manifest only has comctl32 v6 and asInvoker, with **no dpiAware**.

Ghidra did not create functions for most of the game unit, so VCL event handlers and unit-init blocks
are missing from `interesting.c`. The entry points below were recovered from the RTTI method table
(TfrmMain VMT 0x56b37c, D2009 VMT layout: className at -56, methodTable at -64) and by prologue scan.
Pass them to `DecompileAt.java` directly.

| Addr | What |
|---|---|
| 0x57ef5c | program entry: Application.Initialize, MainFormOnTaskbar, Title "Spirit Seasons Tower", CreateForm(TfrmMain), CreateForm(TfrmFlashWarning) x2, Run |
| 0x57ddbc | F-IN-BOX unit initialization |
| 0x57e1f8..0x57e48d | DelphiX `DirectX` unit init (loads DDraw/DInput/DPlayX/DSetup/DSound) |
| 0x57ea4c | ComServ unit init: `OleInitialize(nil)` (COM is ready before the game init) |
| **0x57ea90** | **game unit initialization: command line, application.ini, Flash check, FlashWarning, ExitProcess** |
| 0x56e174 | TfrmMain.FormCreate |
| 0x56f2a8 | TfrmMain.FormShow (DPI/resolution checks, window sizing) |
| 0x56fd44 | ExternalInterface "command" dispatcher (called from ShockwaveFlashEx1FlashCall 0x573ab0) |
| 0x571bdc | Panel1Click: a debug duplicate of the dispatcher (same strings at 0x572b8c..) |
| 0x573f60 | SetFullScreen(self, toFull:DL, flag:CL) |
| 0x5754fc | TimerCheckPositionsTimer (re-centers, synthetic click, SetCursorPos) |
| 0x575248 / 0x56caf8 | TimerCheckCPU / RDTSC CPU-MHz probe |
| 0x4fb6d8 | IsFlashInstalled (CoCreateInstance probe) |
| 0x4f6d50 | GetInstalledFlashVersion (registry + VERSION.dll) |
| 0x4f900c | TFlashOCXCodeProviderBasedOnSystemInstalledOCX.Create (F-IN-BOX) |

---

## 1. Flash loading (startup → "Flash required")

### 1.1 Sequence

1. **F-IN-BOX unit init, 0x57ddbc** (runs first, result never used):
   - It sets the license string `578e98d7-…`.
   - It builds `TFlashOCXCodeProviderBasedOnSystemInstalledOCX` (0x4f900c):
     - `RegOpenKeyExW(HKEY_CLASSES_ROOT, "CLSID\{D27CDB6E-AE6D-11cf-96B8-444553540000}\InprocServer32", 0, KEY_QUERY_VALUE)`
     - `RegQueryValueExW(hKey, "" /*default*/, …, buf, &cb)` with **cb = 0x105 bytes** (LocalAlloc)
     - **Bug: `CreateFileA((LPCSTR)wideBuf, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0)` at 0x4f90a0.** The UTF-16 path is passed as ANSI, so CreateFileA sees the 1-char name "C" (or "D") and the call **always fails**.
     - On success it would wrap a THandleStream, run F-IN-BOX's own PE loader (0x4f6f70 via 0x4f91a4) and resolve `DllGetClassObject`. That never happens.
   - The provider's CreateInstance (VMT slot 0, 0x4f92fc) returns `E_FAIL` when `DllGetClassObject` is nil. There is **no fallback**.
   - Init also does `LoadLibraryW("urlmon.dll")` (CreateURLMoniker, RegisterBindStatusCallback) and `LoadLibraryW("user32.dll")` (UpdateLayeredWindow).

   **Result: F-IN-BOX is vestigial in this build.** `TTransparentFlashPlayerControl` is not linked at all. There is no VMT, only RTTI strings.
2. **ComServ init, 0x57ea4c:** `OleInitialize(nil)`.
3. **Game unit init, 0x57ea90:**
   ```
   cheats   := ParamStr(1) = 'useCheats'                         ; [0x58fb4c]
   checkFS  := not (ParamStr(2)='noCheckFS' or ParamStr(1)='noCheckFS') ; [0x58fb4d], default 1
   ini      := TStringList.LoadFromFile(ExtractFilePath(ParamStr(0))+'CONFIG\application.ini')
   SaveDir  := ini.Values['SaveDir']; sharedDir := (ini.Values['sharedDir']='1'); title := ini.Values['title']
   fpc := TFlashPlayerControl.Create(nil)   ; F-IN-BOX, inert because the provider is empty
   [0x58fd90] := fpc.MajorVersion           ; -1, never read
   GetSystemDirectoryW(...)                  ; result unused
   missing := not IsFlashInstalled()                          ; 0x4fb6d8
   if not missing: v := GetInstalledFlashVersion()            ; 0x4f6d50
                   missing := HIBYTE(HIWORD(v)) < 10          ; "10 or later"
   if missing:
      FlashWarning/FlashLink from ini ('\' -> '/' in link)
      TfrmFlashWarning.Create(Application); lbText := FlashWarning; ShowModal
      if mrOk: open FlashLink via CreateOleObject('InternetExplorer.Application').Navigate (0x56cd70)
      ExitProcess(0)                 ; always exits, whatever the button
   ```
4. **IsFlashInstalled, 0x4fb6d8:**
   `CoCreateInstance(CLSID_ShockwaveFlash {D27CDB6E-AE6D-11CF-96B8-444553540000}, NULL, CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER|CLSCTX_LOCAL_SERVER (7), IID_IUnknown, &p)`.
   It returns `p != nil` and releases p. The process goes through the exe IAT slot **0x591328** (thunk 0x40daf4).
5. **GetInstalledFlashVersion, 0x4f6d50:**
   - `RegOpenKeyExW(HKCR, "CLSID\{D27CDB6E-AE6D-11cf-96B8-444553540000}\InprocServer32", 0, KEY_QUERY_VALUE)`
   - `RegQueryValueExW(h, L"", NULL, NULL, buf, &cb)` with **cb = 0x104 bytes** (VirtualAlloc; at most 129 WCHARs + NUL). The type is not checked.
   - `GetFileVersionInfoSizeW(buf)` → `GetFileVersionInfoW` → `VerQueryValueW(L"\")`. It uses **dwProductVersionMS/LS**:
     `v = (((PMS.hi<<8)+PMS.lo)<<8 + PLS.hi)<<8 + PLS.lo`. The major version is in the top byte.
     For 32.0.0.371, v = 0x20000173, major = 32. It passes, and there is no upper limit.
   - Registry calls go through the IAT: RegOpenKeyExW **0x5912b4**, RegQueryValueExW **0x5912ac**, RegCloseKey **0x5912d8**. VERSION.dll: 0x5910d4/0x5910d8/0x5910d0.
6. **Real playback object.** `Application.CreateForm(TfrmMain)` streams the DFM. `ShockwaveFlashEx1: TShockwaveFlashEx` (TShockwaveFlash TLB wrapper, a TOleControl) runs `TOleControl.CreateInstance` (0x4c4ee4):
   - No license key: `CoCreateInstance(CLSID_ShockwaveFlash, nil, CLSCTX_INPROC_SERVER|CLSCTX_LOCAL_SERVER (5), IID_IOleObject {00000112-…}, …)`
   - With a license key (not the case): `CoGetClassObject(…, 5, nil, IID_IClassFactory2, …)`, IAT 0x59132c.
   - Then `IPersistStreamInit::Load(DFM ControlData)`, in-place activation, and an event sink on `_IShockwaveFlashEvents`.

   **So the standard ActiveX path IS what plays the game. Nothing goes through F-IN-BOX.**
7. FormCreate sets `Movie (DISPID 0x66) := <exeDir>\data` (the CWS SWF, if it exists).
   It then sends `setAppPath "file:///<exeDir with />"`, `useCheats`, `setOptions(<SAVE>\options)` and `setPlayers(<SAVE>\players)` through `CallFunction`.

### 1.2 What the hook DLL must fake (registry-free Flash)

You need all three of these, installed before unit initialization runs (so before the entry point code executes; suspended-start injection works):

1. **COM activation.** Hook the exe IAT `CoCreateInstance` (0x591328), and to be safe `CoGetClassObject` (0x59132c). An inline hook on combase also works.
   ```
   if (IsEqualCLSID(rclsid, CLSID_ShockwaveFlash) && (ctx & CLSCTX_INPROC_SERVER)) {
       static HMODULE h = LoadLibraryExW(ocxPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH); // never FreeLibrary
       DllGetClassObject(rclsid, IID_IClassFactory, &cf);   // CoGetClassObject: return cf / QI(riid)
       hr = cf->CreateInstance(pUnkOuter, riid, ppv); cf->Release(); return hr;
   }
   ```
   This must succeed for both the probe (CLSCTX 7, IID_IUnknown) and the real control (CLSCTX 5, IID_IOleObject).
   No typelib registration is needed: TOleControl uses raw DISPIDs and vtable calls, and F-IN-BOX shows Flash works unregistered.
   If an Invoke ever fails with TYPE_E_LIBNOTREGISTERED, also virtualize `HKCR\TypeLib\{D27CDB6B-AE6D-11CF-96B8-444553540000}\1.0\0\win32`.
2. **Registry read.** Hook `RegOpenKeyExW`, `RegQueryValueExW` and `RegCloseKey`. IAT-hooking only the exe is enough and safest, because COM and Flash keep the real API.
   - For `hKey == HKEY_CLASSES_ROOT` and subkey (case-insensitive) `CLSID\{D27CDB6E-AE6D-11cf-96B8-444553540000}\InprocServer32`, return a sentinel HKEY.
   - Query on the sentinel with name `""`/NULL returns `REG_SZ` = OCX path. Honour `lpData==NULL` and `ERROR_MORE_DATA`.
   - **Keep the path ≤ 129 chars.** Otherwise GetInstalledFlashVersion gets ERROR_MORE_DATA, the version reads 0 and the warning appears. Use `GetShortPathNameW` if needed.
   - Pass everything else through. VCL TRegistry also reads `Control Panel\Desktop\WindowMetrics`, `Software\TLSRVtest119LQ` (play-time) and FontSubstitutes.
3. **Version resource.** Nothing to fake. The real OCX's VS_FIXEDFILEINFO is read from the path you return.

Do **not** "fix" the F-IN-BOX `CreateFileA` at 0x4f90a0. Returning our path makes it call `CreateFileA("D")`, which fails harmlessly.
Making it succeed would load a second Flash copy through F-IN-BOX's custom PE loader, which also patches imports (CreateURLMoniker, RegisterClassExA, CreateWindowExA, LoadCursorA, LoadMenuA, waveOut*, FlushInstructionCache, CreateFileA). That is pure risk.

Zero-hook alternative: a per-user registration under `HKCU\Software\Classes\CLSID\{D27CDB6E-…}\InprocServer32` (default = path, ThreadingModel=Apartment). It satisfies both the COM lookup and the direct HKCR read, but it is a registry write.

### 1.3 Flash EOL / kill-switch interaction

- Neither check fails on a timebombed OCX. CoCreateInstance works and the version is 32. The failure shows up later: after 2021-01-12 a timebombed build swaps the movie for the "blocked" placeholder.
  The SWF then never calls ExternalInterface, so the window opens and stays black/blue with no error. That looks like a hang.
- Use a pre-timebomb build (32.0.0.371 is the one commonly reported as pre-timebomb), or patch the bomb.
  To check a build quickly, search the OCX for the double `1610409600000.0`, bytes `00 00 40 46 3E 6F 77 42`.
- Ship our own `mms.cfg` by redirecting `CreateFileW` calls that open `%WINDIR%\SysWOW64\Macromed\Flash\mms.cfg` (from the OCX module) to a private file, for example:
  `AutoUpdateDisable=1`, `SilentAutoUpdateEnable=0`, `EOLUninstallDisable=1`, plus `EnableAllowList=1` and `AllowListUrlPattern=file:///*` for builds that honour the allow list.
  This suppresses update and EOL nags without touching the system.
- The SWF is local (no UseNetwork flag; FileAttributes 0x08/0x18). It loads sibling files with relative/file:/// URLs. No FlashPlayerTrust entry is needed.

---

## 2. Display: fullscreen vs windowed

### 2.1 Geometry

- Every SWF (data, engine, LIB\*.swf) has a **1366×768 stage at 30 fps**. data/engine/diary are SWF v10, icons.swf is v11, interface.swf is v9.
- The window sizes are derived from that stage.
- The fields involved:
  - `+0x408/+0x40a` W/H: `GetDeviceCaps(CreateDCW("DISPLAY"), HORZRES/VERTRES)` (0x573920). **DPI-virtualized** for this DPI-unaware process, e.g. 4K@150% gives 2560×1440.
  - `+0x4a8` dpi: `Screen.PixelsPerInch` (96 when unaware), **overridden** by `HKCU\Control Panel\Desktop\WindowMetrics\AppliedDPI` (REG_DWORD; read errors swallowed).
  - `+0x494/+0x498` window H/W: 768 / 1366 (1024 when W < 1366).
  - `+0x49c/+0x4a0` fullscreen mode H/W: 768 / 1366, or 1024×768 when `W < 1366` (0x5712d0: "widescreen" = W ≥ 1366; the rest of that function is dead code).
  - `[0x58fb64]` (FormCreate 0x56e1a8): some `EnumDisplaySettingsW` mode has `dmPelsWidth==1366 || dmPelsHeight==1366`. That is a bug; height should be 768. Without such a mode, fullscreen uses 1024×768.
  - `taskbarH` (0x56ddf0): `FindWindowW("Shell_TrayWnd")` + `SHAppBarMessage(ABM_GETSTATE)`. It is the GetWindowRect height when always-on-top and not auto-hide, else 0. A vertical taskbar reports the full screen height and trips every check.
  - `GetSystemMetrics` is used for SM_CXSCREEN/SM_CYSCREEN (VCL Screen.Width/Height for centering and special cases) and SM_CMONITORS (0x50; the multi-monitor warning is effectively disabled). FormShow's two raw calls are unused.

### 2.2 "Sorry, game does not support your resolution" (FormShow 0x56f500..0x56f5c3, then ExitProcess(0))

```
if not (W*dpi/96 > 800.0 and H*dpi/96 >= 768.0 and W >= H) -> MessageBox + ExitProcess(0)
```
At 4K@150% unaware: 2560×1440 × (144/96) gives 3840×2160, which passes. If the process is made DPI-aware it still passes.
It only trips on tiny or portrait screens. **This is not the cause of the start failure.**

### 2.3 Forced fullscreen at startup (FormShow 0x56f736)

```
forceFS = checkFS && (W <= 1024 || taskbarH + 800 >= H || H <= 800)
```
If set, the host sends `checkFullScreen(true)` and Timer1 then calls `SetFullScreen(1,1)`.

### 2.4 "Sorry, windowed mode is not supported…" (dispatcher command `fullscreen`, 0x570856..0x570a04)

The command `fullscreen` is a toggle; `param` only decides whether the error is shown.
- When windowed: set +0x42c and Timer1 enters fullscreen.
- When fullscreen:
  ```
  winW = (W < 1366) ? 1024 : 1366; winH = 768
  if checkFS && (W <= winW || H < winH || H <= 800):
       if param == "true": CallFunction checkFullScreen(true); ShowMessage("Sorry, windowed mode …")
       (stay fullscreen)
  else SetFullScreen(0,1) and center the form
  ```
  (The DPI-based recalculation at 0x5708ee..0x570999 is dead.)

Thresholds use logical (virtualized) W/H:

| Screen / scaling | logical W×H | windowed allowed? | forced FS at start? | stage |
|---|---|---|---|---|
| 4K 150% | 2560×1440 | yes | no | 1366 |
| 4K 200% | 1920×1080 | yes | no | 1366 |
| 4K 300% | 1280×720 | **no** (H≤800) | **yes** | **1024 (crop)** |
| 1440p 150% | 1707×960 | yes | no | 1366 |
| 1440p 175% | 1463×823 | yes | **yes** (40+800 ≥ 823) | 1366 |
| 1440p 200% | 1280×720 | **no** | **yes** | **1024** |
| 1080p 150% | 1280×720 | **no** | **yes** | **1024** |

`noCheckFS` as argv[1] or argv[2] disables both the forced fullscreen and the windowed refusal (`[0x58fb4d]=0`).
Faking GetDeviceCaps/AppliedDPI is another way around it.

### 2.5 Windowed implementation (SetFullScreen(0) 0x573f60, FormShow)

- BorderStyle bsSingle (`0x4b4354` = TCustomForm.SetBorderStyle), FormStyle fsNormal (`0x4b4774` = SetFormStyle).
- ClientWidth = 1366 or 1024, ClientHeight = 768. TimerCheckPositions (0x5754fc) centers it on SM_CX/CYSCREEN.
- The DFM also has `FormStyle=fsStayOnTop`, and FormShow does `SetWindowPos(HWND_TOPMOST)`.
- `RegisterHotKey(hwnd, 0x5d, 0, VK_APPS)` steals the Apps/menu key system-wide while the game runs.
- At 150% scaling the client is 1366×768 logical, which DWM bitmap-stretches to 2049×1152. That causes the blur.

### 2.6 Fullscreen implementation: a real mode switch through DelphiX + DirectDraw 7

- There is **no ChangeDisplaySettings import**. The mode change happens inside ddraw.dll.
- `DXDraw1: TDXDraw` is invisible. DFM `Options=[doNoWindowChange, doAllowReboot, doWaitVBlank, doDirectX7Mode, doHardware]`; SetFullScreen ORs in doFullScreen.
- `SetFullScreen(1)`:
  - Display.Width×Height×32 = (+0x4a0, +0x49c).
  - Special cases: Screen 1600×900 or 1680×1050 forces 1366×768. No 1366 mode in the list ([0x58fb64]=0) forces 1024×768.
  - Then `TCustomDXDraw.Initialize` (0x5538c0) calls DirectDrawCreateEx and SetCooperativeLevel(EXCLUSIVE|FULLSCREEN|NOWINDOWCHANGES|ALLOWREBOOT), then SetDisplayMode.
  - On failure it retries 1360×768, then 1024×768, then (portrait only) 768×1024, else falls back to windowed and sends checkFullScreen(false).
  - Form: bsNone, fsStayOnTop, ClientHeight 768. The Flash control sits at Left = **−171** in 1024×768 mode, a center crop of the 1366 stage.
  - TimerDXDrawCheck (0x57566c) reverts to windowed if DelphiX has dropped doFullScreen, for example after alt-tab.
- **There is a disabled built-in "borderless desktop fullscreen" path:**
  - 0x573f7c `mov byte [ebp-0x1d],1` (imm8 at **0x573f7f**, file offset 0x17337f). With it set, the branch at 0x5740eb skips 0x5740f5..0x574189.
  - That block does: fsStayOnTop, bsNone, **ScaleMode := 2 (ExactFit)**, ClientWidth/Height := W/H, Left/Top := 0, HWND_TOPMOST, then returns without touching DDraw.
  - It does **not** resize the Flash control (still 1366×768; TimerCheckPositions only re-centers it). It is only half-finished, but it is a convenient hook point for a "no mode switch" fullscreen.
- 4K effects:
  - The 1366×768 mode is GPU/monitor-scaled by 2.81, which looks blurry, or appears as a small centered box if the Intel scaling is "maintain display scaling".
  - Without a 1366 mode in the driver list, 1024×768 crops the sides.
  - There is also an unverified risk: after the mode switch, a DPI-unaware window may still be scaled by the old DPI factor and get cropped.

---

## 3. Flash control sizing, quality, config, saves

- **Control**: `ShockwaveFlashEx1: TShockwaveFlashEx`. TShockwaveFlash adds mouse/RightClick events; OnRightClick (0x571b00) sends `rightclick`.
  - It is a windowed ActiveX with its own child HWND. DFM ControlData: **WMode="Window"**, **Quality="High"**, Scale="ShowAll", AllowNetworking="all", AllowFullScreen="false".
- **Runtime property writes** (DISPIDs confirmed from TShockwaveFlash RTTI):
  - FormShow 0x56f3e8: Width := 1366, then **ScaleMode (0x78) := 3 (NoScale)** (call at 0x56f40e).
  - Height := 768. Left := 0, or −171 in 1024 mode.
  - The dead branch above sets ScaleMode := 2.
  - Movie (0x66) is set in FormCreate. Nothing else touches Quality or WMode.
- **No layered or transparent rendering.** TTransparentFlashPlayerControl is not linked, and UpdateLayeredWindow is only resolved by F-IN-BOX init, never called. So the slowness is not UpdateLayeredWindow. The Flash ActiveX in Window mode rasterizes on the CPU with GDI, so the iGPU barely matters except for DWM scaling.
- TimerSetWidth (0x5758ac) sends `setWidth(<form width>)`, or `"1024"` when bsNone and there is no 1366 mode. The SWF lays out its UI for the visible width.
- **CPU probe** (0x56caf8, from TimerCheckCPU 5 s after FormShow, and from dead TimerCheckFullScreen):
  - It sets REALTIME priority class and TIME_CRITICAL thread priority, then measures RDTSC (low 32 bits) over `Sleep(500)` and divides by 500000.
  - A result below 2000.0 MHz makes it send `setSlowCPU`, which turns off the glow effects (`slowCPU`/`useGlow` in the engine).
  - With invariant TSC this measures the **base clock**, so many Intel U-series iGPU laptops (1.6–1.8 GHz base) get slowCPU mode. The REALTIME burst causes a ~0.5 s hitch.
- **CONFIG**:
  - `application.ini` is parsed as `TStringList.Values` with no sections. Keys: `title` (also applied in FormShow), `FlashWarning`, `FlashLink`, `SaveDir`, `sharedDir`.
  - `Config.xml`, `Init.xml` and `Locale.xml` are pure game content (VARs, scenes, achievements, strings). None of them has display settings.
  - Locale has the option labels `IM_FULLSCREEN`, `IM_CUSTOMCURS` ("Use custom cursor") and `IALERT_WRONG_RESOLUTION`.
- **Saves**: `SHGetSpecialFolderLocation(CSIDL_APPDATA=0x1a)` + `\TikisLab\` + SaveDir + `\SAVE\`
  - That is `%APPDATA%\TikisLab\TheOtherSide_TowerOfSouls\SAVE\`. With `sharedDir=1` it is CSIDL_COMMON_APPDATA (%ProgramData%).
  - The files are `options` and `players` (XML text handed to the SWF), plus whatever the SWF writes through the `data` command (`fileName=` relative to that folder).
  - Play time is stored in `HKCU\Software\TLSRVtest119LQ\TotalPlayTime`. The 10-day/65-minute trial code at 0x574fbd is unreachable.
- The host's ExternalInterface command set (dispatcher 0x56fd44): `data` (save file), `delete`, `save_bonus`, `set_wallpaper`, `beforeLoad`, `stopPlay`, `playSound`, `exit`, `setSurveyURL`, `speech`, `fullscreen`, `music(level)`.
- Host → SWF calls: setAppPath, useCheats, setOptions, setPlayers, checkFullScreen, setWidth, setSlowCPU, externalPause, externalEscape, rightclick.

---

## 4. Cursor

- **It is drawn by the SWF.**
  - The `engine` SWF has `MouseController`, `UI:CustomCursor`, `Utils.customCursor`, `showHideMouse`, `Mouse.hide`, `changeCursor`/`hideCursor`/`showCursor`, `standardCursor`, and `customCursorLoadedFromOptions`.
  - The cursor art is the `i_cursor_*` symbols in **LIB\interface.swf** (standard, hand, zoom, look, go_*, use, talk, question, film), with copies in icons/diary/data.
- The in-game option **"Use custom cursor"** (Options screen, `btnCustomCursor`) is saved by the SWF in `SAVE\options`. The XML attribute name is not confirmed; probably `customCursor`, so diff the file after toggling it.
- **The host sets no cursor.** Its SetCursor/LoadCursorW calls are VCL internals (0x48ad59.., 0x4ba224, TScreen init). LoadCursorA/CreateIconFromResource exist only inside F-IN-BOX's inactive import-hook layer. There is no CreateIconIndirect import.
  - The only host cursor action is GetCursorPos/SetCursorPos around a synthetic `WM_LBUTTONDOWN(0,0)` to the Flash HWND in TimerCheckPositions (0x5754fc).
- **No ExternalInterface message carries cursor state** (see the command list). A host can only:
  - turn the custom cursor off in `options` (system hardware cursor, no lag);
  - reduce the lag by fixing frame pacing and the CPU affinity (see §6);
  - or patch the SWF to use native cursors: AS3 `flash.ui.MouseCursorData`, which needs the SWF header version raised to 11 or higher, applied in the engine's CustomCursor/MouseController with the bitmaps from interface.swf.
- The sprite cursor updates at Flash's 30 fps plus render and DWM latency. That is the "laggy cursor", and it gets worse with single-core affinity.

---

## 5. DirectX / DDraw.dll / DSetup.dll

- They come from **DelphiX** (TDXDraw, TDirectDraw, DXClass, DXDrivers), not F-IN-BOX.
- The DelphiX `DirectX` unit init (.itext 0x57e1f8..0x57e48d) runs at **startup**, but only when not running inside the IDE (0x4ff588 checks for DELPHI32.EXE).
  It calls `LoadLibraryW` on `DDraw.dll` (0x57e213), `DInput.dll` (0x57e2e1), `DPlayX.dll` (0x57e346), DSetup and `DSound.dll` (0x57e3ed), and only resolves exports. A NULL handle is tolerated, because GetProcAddress(NULL,…) just returns NULL.
- DSetup (0x501bcc → 0x501a3c) reads `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\DirectXDrivers\UninstallString`, drops 11 chars and appends `DSetup.dll`.
  On Win10 the key is absent, so the code calls `LoadLibraryW("")`, which fails harmlessly. **It is not a start blocker.**
- `DPlayX.dll` exists on Win10 (SysWOW64, 215 KB on the dev PC). On some installs loading DirectPlay triggers the "An app needs DirectPlay" Feature-on-Demand dialog, which looks like a start problem. The DLL can return NULL for `LoadLibraryW("DPlayX.dll")` safely, since the game has no networking.
- DDraw is actually used **only when entering fullscreen** (TDXDraw.Initialize). See §2.6 for the Win10 issues:
  - exclusive mode is emulated;
  - mode-switch flicker and alt-tab reverts (TimerDXDrawCheck);
  - DPI-virtualization mismatch after the mode switch;
  - non-integer scaling at 4K, or 1024×768 cropping when the driver lists no 1366 mode.

---

## 6. Bundle launcher HiddenObject6in1.exe (Ghidra project "Launcher")

- It is an Alawar Arcade client (HTMLayout UI, CrashRpt, MSXML). Most Win32 calls go through lazily resolved thunks at 0x43ebxx..0x43f5xx, with a pointer table around 0x49eb00.
- **Launch path**: FUN_00406d2b → FUN_00406f5f → **FUN_0041b5da**:
  - The exe comes from the catalog, or from `My Games\N\manifest.xml` `/Game/GameExecutable` read through FUN_00407532/FUN_004080b9 (MSXML DOM).
  - `ShellExecuteExW` is called with:
    - `fMask = SEE_MASK_NOCLOSEPROCESS|SEE_MASK_UNICODE (0x4040)`, verb `"open"`
    - `lpFile` = full exe path, **`lpParameters = ""`**, **`lpDirectory` = the exe's folder**, `nShow = SW_SHOWNORMAL`
  - With `<HideLauncherOnGameStart/>` (present in client.xml) the launcher hides its window, does `WaitForSingleObject(hProcess, INFINITE)` and then restores or closes.
- It passes no command-line flags or environment variables and has no IPC with the game.
  Launcher-only switches: `-uninstall`, `-buy`, `-adrmsupdate`, `-page:"…"`.
- **Important: WinMain (0x4019c8) calls `SetProcessAffinityMask(GetCurrentProcess(), 1)` at 0x401a0b..0x401a18.**
  The affinity mask is inherited by ShellExecuteEx/CreateProcess children, so **every game started from the bundle launcher runs pinned to logical CPU 0**.
  The Flash player threads (render, MP3 decode, GC/JIT, image decode), the Delphi timers and DWM interaction all share one core, next to CPU0's interrupt/DPC load. That is a direct cause of low FPS and cursor lag.
  It also sets its own CWD to the launcher folder (0x43d3b4), but the game gets its folder through lpDirectory.

---

## 7. Recommendations for the hook DLL (priority order)

1. **Start fix (required):**
   - COM hook for CLSID_ShockwaveFlash using our OCX's `DllGetClassObject`.
   - Fake HKCR `…\InprocServer32` default value with the OCX path (≤ 129 chars).
   - Leave the F-IN-BOX CreateFileA bug alone.
   - Use a pre-timebomb OCX or patch the bomb; redirect mms.cfg.
   - Optionally make `LoadLibraryW("DPlayX.dll")` return NULL.
2. **Affinity:** at DLL init call `GetProcessAffinityMask` and then `SetProcessAffinityMask(self, systemMask)`. Our own launcher should never go through HiddenObject6in1.exe, or should reset the mask. This is the cheapest FPS and cursor win.
3. **Avoid the DDraw mode switch:**
   - Either patch imm8 at 0x573f7f from 1 to 0 at runtime (enables the borderless path), or hook IDirectDraw7::SetCooperativeLevel/SetDisplayMode to no-ops.
   - Then do borderless fullscreen and enlarge the Flash child yourself.
   - TOleControl.SetBounds sizes Flash through `IOleInPlaceObject::SetObjectRects`. Hook that vtable entry on the instance created in step 1, since SetWindowPos alone gets overridden, and map the host's (0,0,1366,768) to a letterboxed s·1366 × s·768 rect.
   - Force ScaleMode to 0 (ShowAll) or keep 2 (ExactFit; identical at 16:9) by intercepting `IDispatch::Invoke` DISPID 0x78.
   - Verify that the SWF does not set `stage.scaleMode` itself. The host forcing NoScale suggests it does not; only the FLVPlayback component caches it.
   - Render cost grows with s².
     - **DPI-unaware** (default): the window fills 2560×1440 logical, Flash renders at s ≈ 1.875 (about 3.5× the pixels) and DWM does the final 1.5× on the GPU.
     - **System-DPI-aware** (`SetProcessDpiAwarenessContext(SYSTEM_AWARE)` before the first window): everything is native and crisp, but at 4K s = 2.81 (about 7.9× the pixels), which is probably too slow for software Flash.
   - Offer a "performance" mode that keeps the 1366×768 DDraw switch. If you keep that path, make EnumDisplaySettingsW list 1366×768 so it does not fall back to 1024 cropping.
   - Delphi forms are `Scaled`, but the code sets every size explicitly, so DPI-aware mode is safe.
4. **Windowed sizing:**
   - The same SetObjectRects/ShowAll trick gives a crisp k× window.
   - Pass `noCheckFS` (argv[1]) from our launcher, or fake GetDeviceCaps, so high-scaling setups (logical height ≤ 800) are not forced fullscreen and can still go windowed.
   - Do **not** pass `useCheats` in argv[1] by accident.
5. **Cursor:**
   - Default the custom cursor off (edit `SAVE\options` once the attribute name is confirmed), or document the in-game toggle.
   - Longer term, patch the SWF to native MouseCursorData.
6. **Nice-to-have:**
   - Neutralize the REALTIME priority burst by hooking SetPriorityClass with REALTIME/HIGH from 0x56cb25.
   - Optionally keep `setSlowCPU` (it helps performance).
   - `timeBeginPeriod(1)` for steadier 30 fps timing.
   - Unregister the VK_APPS hotkey, or ignore it.
