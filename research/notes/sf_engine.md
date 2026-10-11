# sf:: engine ("Stargaze Framework", Alawar) — hook reference

Covers four executables that share one engine. All addresses are absolute VAs (image base 0x400000,
no ASLR: DllCharacteristics 0x8000/0x8100 has no DYNAMIC_BASE, so the addresses are stable).

| Key | Exe | Build | PE TimeDateStamp | File size | Framework string | Design res |
|---|---|---|---|---|---|---|
| **K** | `My Games\4\Kronville_StolenDreams.exe` | VS2013 (linker 12.0), optimised | 0x55A78FBA | 2 482 688 | (newer, not printed) | 1280x800 (hard-coded `SetSourceSize(0x500,800)`) |
| **P** | `My Games\5\PanopticonPathofReflections.exe` | VS2008 (9.0), frame-pointer style | 0x5370DFC5 | 3 152 896 | Quest Engine 2.1.2 / Library 1.4.2 | 1024x768 (hard-coded) |
| **M** | `My Games\6\MexicanaDeadlyHoliday.exe` | VS2008 | 0x5292D99B | 4 883 968 | Quest Engine 1.2.1 / Library 1.2.3 + Lua | from `properties/constants.xml` (1024x768) |
| **C** | `My Games\7\CruelGames.exe` | VS2008 | 0x5063FC9F | 4 569 600 | same family as M, + Lua | constants.xml has 0x0, so it falls back to 1024x768 |

K and P use the same "big" cursor manager (0x3628 bytes). M and C use a small Lua-exposed
`sf::gui::CCursor` (0x60/0x5C bytes). The renderer, the batcher, D3D setup, the frame limiter and
the main loop are the same code in all four. Only struct offsets differ: K's renderer layout is
different from P/M/C.

None of the exes is LARGE_ADDRESS_AWARE. None of the manifests declares dpiAware. All four call
`SetProcessDPIAware()` (system-DPI aware) unconditionally in the app constructor:
K 0x4C8EE0, P 0x4B3240, M 0x4B9780, C 0x4B9F70.

---------------------------------------------------------------------------------------------------

## 0. Object model / globals

| | K | P | M | C |
|---|---|---|---|---|
| `CApplication*` global | 0x62B1B0 | 0x6CFA70 | 0x844EA8 | 0x841CBC |
| app -> window (`CStandaloneApplicationWindows*`) | +0x74 | +0x84 | +0x84 | +0x84 |
| app -> `CRenderer*` | +0x78 | +0x88 | +0x88 | +0x88 |
| app minimized / active (bytes) | +0x80 / +0x81 | +0x90 / +0x91 | +0x90 / +0x91 | +0x90 / +0x91 |
| D3D holder global (+0 `IDirect3D9*`, +4 `IDirect3DDevice9*`, +8 `D3DCAPS9`, +0x138 saved back-buffer surface) | 0x62B1A8 | 0x6CFA68 | 0x844EA0 | 0x841CB4 |
| renderer -> batcher ("CRenderDevice") | **+0x20** | **+0x34** | **+0x34** | **+0x34** |
| window vtable | 0x5DBB40 | 0x674E4C | 0x794E8C | 0x791DEC |

**CRenderer layout**

| field | K | P/M/C |
|---|---|---|
| m_PreserveAspect (byte) | +0x00 | +0x00 |
| m_IsFullscreen (byte) | +0x01 | +0x01 |
| m_Width / m_Height (back buffer = client size) | +0x04 / +0x08 | +0x04 / +0x08 |
| 32bpp flag (byte) | +0x0C | +0x0C |
| device-lost / needs-reinit flag | +0x24 | +0x38 |
| m_SourceWidth / m_SourceHeight (design res) | +0x28 / +0x2C | +0x3C / +0x40 |
| m_AspectK.x / .y (float) | +0x30 / +0x34 | +0x44 / +0x48 |
| m_AspectOffset.x / .y (float, letterbox in back-buffer px) | +0x38 / +0x3C | +0x4C / +0x50 |
| pending windowed re-init (WM_SIZE/display change) | +0x40..0x42 | M/C +0x54/0x55 |

The InitGraphics log line prints the offsets as "AspectOffset.Y=…, .X=…" but actually passes x first,
so the labels are swapped. The dev-PC log
(`%APPDATA%\AlawarEntertainment\IllustrateGames\Kronville_StolenDreams\log.sflog`) shows
`m_SourceWidth=1280 m_SourceHeight=800 m_Width=3840 m_Height=2160 m_IsFullscreen=1` and
`K=(3.0, 2.7)`. The image is stretched non-uniformly because preserve_aspect_ratio=false.

**Batcher layout (same in all four)**

| offset | meaning |
|---|---|
| +0x00 | byte: per-frame event-query enabled (set to 1 in the constructor, see §4) |
| +0x3C | state-dirty flag |
| +0x40 | texture-stage count |
| +0x44 + stage*0x68 | cached stage state (texture at +0) |
| +0x1E4 | blend mode |
| +0x1EC | pixel shader |
| +0x1F4 | render-target texture (0 = back buffer) |
| +0x1FC | D3DPRIMITIVETYPE (engine index -> identity table) |
| +0x204 | FVF |
| +0x208 | stride |
| +0x20C | queued vertex count |
| +0x210 | inline vertex buffer, 0x2EE00 bytes |

FVF is always **0x1C4** (`XYZRHW | DIFFUSE | SPECULAR | TEX1`) with stride **32**:
`{float x, y, z, rhw; DWORD diffuse; DWORD specular; float u, v}`. Positions are in back-buffer
pixels, already scaled by m_AspectK and offset by m_AspectOffset. Sprites arrive as TRIANGLELIST,
6 vertices per quad.

In K the **only** `DrawPrimitiveUP` (vtbl+0x14C) in the whole exe is in BatchFlush. K calls no
SetViewport, Clear or SetScissorRect. P/M/C (VS2008 code loads vtable slots into registers, so an
exhaustive scan is noisy):
- same BatchFlush DrawPrimitiveUP;
- a full-target `Clear(0, NULL, D3DCLEAR_TARGET, color, 0, 0)` right after BeginScene (M 0x54BD90,
  called from BeginScene 0x54BDE0);
- statically linked D3DX (shader assembler / validator), whose device usage I did not enumerate.

All sprites, GUI and the cursor go through the batcher.

Textures are created by the engine's CreateTexture wrapper (K 0x50BA20, M 0x55B980):
- 1 mip level.
- Format is A8R8G8B8, A4R4G4B4, R5G6B5, A1R5G5B5 or DXT1/3/5.
- **Pool MANAGED**, except texture flag 0x08 (render target), which gets `D3DUSAGE_RENDERTARGET` +
  `D3DPOOL_DEFAULT`. The batcher can `SetRenderTarget` to such a texture (state apply K 0x503300).

---------------------------------------------------------------------------------------------------

## 1. CURSOR

### 1.1 How the custom cursor is drawn (all four)

The OS cursor is hidden and a sprite is drawn into the scene every frame, as the **last thing before
Present**:

```
AppRender (K 4CA1A0 / P 4B4000 / M 4BA4F0 / C 4BACF0):
    if !minimized && active && BeginFrame():
        app->vtbl[scene](renderer); gui/flash passes
        if active: CursorMgr::Instance()->DrawCursor(renderer)   <-- only draws the cursor sprite
        DrawLetterboxBars(renderer)   (K 4CA270, P 4B40F0, M 4BA5E0, C 4BADE0; solid rects, only when offsets > 0)
        EnginePresent(renderer->batch)   (BatchFlush, EndScene, Present, ...)
```

`DrawCursor` is **`__thiscall(CursorMgr* this, CRenderer* r)`, `ret 4`**, in all four:

| exe | DrawCursor | condition for custom draw |
|---|---|---|
| K | 0x4EA660 | `this+0x3617` (custom) && `this+0x360C` (sprite) && !`+0x3616` && !`+0x3614` (hidden) |
| P | 0x4E7610 | same offsets as K |
| M | 0x4FE470 | `this+0x0A` (custom) && `this+0x04` (sprite) && !`+0x09` && !`+0x08` |
| C | 0x4FFBC0 | `this+0x09` (custom) && `this+0x04` (sprite) && !`+0x08` |

Pseudo-code of the custom branch (same in all four):

```
p = window->GetCursorPos(); window->ScreenToClient(&p)        // live OS cursor position at render time
vp = renderer viewport {off.x, off.y, W-2*off.x, H-2*off.y}
if fullscreen || p inside vp:
    p = clamp(p, vp)
    d = int((p - vp.xy) / AspectK)                            // design-resolution coordinates (truncated!)
    DrawSprite(sprite, r, rect{d.x, d.y, imgW, imgH})         // sprite has translate(-w/2,-h/2) baked in
```

- **Hotspot = image centre.** The cursor sprite is built with a `-0.5*size` translation
  (K 0x4EA9C4 multiplies by -0.5 at 0x5D7A2C; P/M/C divide by 2.0). The cursor PNGs are padded so
  that the pointer tip sits at the image centre.
- **Position quantisation.** The position is truncated to design pixels. At 4K with K=3.0/2.7 the
  sprite moves in 3 px / 2.7 px steps (jerky). Combined with frame latency (see §4), this is the
  laggy cursor.
- **Draw chain.** `DrawSprite` (K 0x4E9CA0 -> 0x4E9B00) pushes the transform, then calls
  DrawImage (K 0x4C7C00, or the 0x4C7B00/0x4C7F70/0x4C8060 variants). DrawImage builds triangles
  (K 0x4C6B80) and sets blend, pixel shader and texture through the batcher setters
  (K 0x503DE0 / 0x503DB0 / 0x503E10). It then appends with `BatchAppend(4 /*TRIANGLELIST*/, verts, n)`
  (K 0x503C30 -> 0x503C50, FVF 0x1C4, stride 0x20).
- **Deferred flush.** BatchAppend flushes the *previous* batch when state changed, so the cursor
  quads stay queued **after** DrawCursor returns. They are only issued by the next flush, which
  happens in DrawLetterboxBars (texture change) or at the start of EnginePresent.
- **No ID3DXSprite, no vertex buffers.**

The **icon in OS-cursor mode** (K/P only): when custom cursors are off, DrawCursor instead draws
the small "icon" sprite (`+0x3610`, desc+0x15C) at the last WM_MOUSEMOVE position (`+0x3620/+0x3624`,
design coordinates) + (9,9) (K floats at 0x6258B8/BC). The real cursor is a Windows system cursor.
M/C draw nothing in this mode.

### 1.2 Where the cursor type/image is chosen

**K/P manager** (`CCursorManager`, 0x3628 bytes; singleton K 0x62B4AC, P 0x6D01C0):

| offset | meaning |
|---|---|
| +0x0000 | bool initialised |
| +0x0004 + i*0x1B0 | descriptors (max 32), see below |
| +0x3604 | last index (-1 = empty) |
| +0x3608 | current type: 100 pointer, 101 hand, 102 text, 103 hourglass, 0..N game-specific |
| +0x360C | -> current descriptor+0x10C (custom sprite) |
| +0x3610 | -> current descriptor+0x15C (icon sprite) |
| +0x3614 | hidden |
| +0x3615 | input lock (checked by the mouse handler) |
| +0x3616 | disabled |
| +0x3617 | custom cursors on |
| +0x3618 | D3D HW cursor mode (dormant) |
| +0x3619 | last fullscreen state |
| +0x361C | last uploaded HW type (-1/0x68 = invalid) |
| +0x3620 / +0x3624 | last mouse position in design coordinates |

Each descriptor (0x1B0 bytes) holds:

| desc offset | meaning |
|---|---|
| +0x00 | type id |
| +0x0C | cursor image name, e.g. `gui_cursors_pointer_gear` |
| +0x6C | icon image name, e.g. `gui_cursors_icon_gear_anim` |
| +0xC4..+0x104 | OS cursor descriptor (IDC_ARROW 0x7F00 / IDC_HAND 0x7F89 / IDC_IBEAM 0x7F01 / IDC_WAIT 0x7F02) |
| +0x108 | loaded flag |
| +0x10C | cursor sprite (0x50 bytes) |
| +0x15C | icon sprite |
| +0x1AC | HW texture `"<name>_acc"` |

Base types are registered in K 0x4EA4C0 / P 0x4E7080. Game types: K 0x425780 (pointer_item,
pointer_gear + **icon_gear_anim**, magnifier, magnifier_puzzle, hourglass) and P 0x41CBF0 (adds video
and 8-way `cursor_move_*`).

**M/C manager** (`sf::gui::CCursor`; singleton M 0x8452A8 size 0x60, C 0x8420B4 size 0x5C):

| field | M | C |
|---|---|---|
| type (0 pointer, 1 hand, 2 text, 3 hourglass, 4 CursorCustom; ctor default M 0 / C 5; 6 = "invalid" sentinel) | +0x00 | +0x00 |
| current sprite | +0x04 | +0x04 |
| hidden | +0x08 | +0x08 |
| disabled | +0x09 | (none) |
| custom on | +0x0A | +0x09 |
| HW mode | +0x0B | +0x0A |
| last fullscreen | +0x0C | +0x0B |
| last HW type | +0x10 | +0x0C |
| SetCustomCursor texture | +0x54 | +0x50 |
| mouse position | +0x58/+0x5C | +0x54/+0x58 |

Sprites (one per type, centred hotspot) are built by EnableCustomCursors -> M 0x4FE670 / C 0x4FFDB0
from `gui_cursors_pointer/hand/text/hourglass` (+ unused `gui_cursors_acc_*_acc`). Lua exposes
SetCursor, SetCustomCursor, GetCursor, IsCustomCursorsEnabled, EnableCustomCursors, HideCursor
(binder M 0x55F9D0 / C 0x55F3E0).

**Flow (all four):**
- `CursorOnMouseMove` (K 0x4EAC60, P 0x4E72A0, M 0x4FE010, C 0x4FF790) is called from the WM_MOUSEMOVE
  handler. It resets the type to pointer unless hidden/hourglass (K/P) or `+0x08` (M/C), and stores
  the position.
- Game code then calls `SetCursorType(this, type)` (K 0x4EAD00, P 0x4E7330, M 0x4FE090, C 0x4FF810)
  every frame for hover states.
- `CursorUpdate(this)` (`__thiscall`, no args; K 0x4EA2F0, P 0x4E7380, M 0x4FE0F0, C 0x4FF870) runs
  from AppUpdate (only when the mouse is inside the client rect), from the WM_MOUSEMOVE handler, and
  **from WM_SETCURSOR** (the app message handler returns "handled"). It resolves
  type -> descriptor/sprite:
  - **custom on, HW off:** `window->ShowCursor(false)` (=> `SetCursor(LoadCursorW(0,NULL))` = `SetCursor(NULL)`),
    then current-sprite = desc+0x10C (K/P) or the sprite for the type (M/C). For M/C type 4 it rebuilds
    the sprite from the custom texture.
  - **custom off:** K/P `window->SetCursorDesc(desc+0xC4..., true)`; M/C `window->SetCursorType(type, true)`.
    Both end in `LoadCursorW(NULL, IDC_*)` + `SetCursor`. K/P also set the icon sprite.
  - **HW mode (dormant):** `device->ShowCursor(custom)` (vtbl+0x30), then when the type changes,
    `tex(desc+0x1AC)->GetSurfaceLevel(0)` -> `device->SetCursorProperties(0,0,surf)` (vtbl+0x28).
    Hotspot is (0,0). Also `SetCursorPosition` (vtbl+0x2C) on mouse move. The `_acc` textures
    **do not exist in any game.pack**, and nothing sets the flag: K only zeroes it; the setters
    P 0x4E78E0, M 0x4FE3C0, C 0x4FFB30 have no callers. Not usable without supplying textures.
- **Animation.** Sprites carry an animation state (sprite+0); the frame is picked by time
  (K 0x4E9CC0, frame durations in image info +0x6C[]), so the gear icon/anim changes UVs every few
  frames. The held inventory item / hint (K `CTakeToCursorCommand`, M/C `qe::CCursor` /
  `g_Cursor:SetCursorObject`) is drawn by the **scene**, not by DrawCursor, so it keeps lagging even
  with an OS cursor.

**Window-side cursor functions (`__thiscall`, `ret 4`):**

| exe | ShowCursor(bool) — LoadCursorW+SetCursor | slot | also |
|---|---|---|---|
| K | 0x4DF730 | vt+0x5C | SetCursorDesc vt+0x54 = 0x4DF6D0, GetCursorPos vt+0x50 = 0x4DF3C0, ScreenToClient vt+0x3C = 0x4DF670 |
| P | 0x520A70 | vt+0x5C | SetCursorDesc vt+0x54 = 0x520A00 |
| M | 0x508070 | vt+0x54 | SetCursorType vt+0x4C = 0x508020 |
| C | 0x509870 | vt+0x54 | SetCursorType vt+0x4C = 0x509820 |

The window class is registered with `hCursor = NULL` (K 0x4DF5E0).

### 1.3 Recommended hook (draw-capture into an HCURSOR)

Hook `DrawCursor`. The cursor's quads are deferred, so you must bracket the call with explicit flushes:

```
void __fastcall Hook_DrawCursor(void* mgr, void* /*edx*/, CRenderer* r) {
    if (!CustomOn(mgr)) return Orig(mgr, r);            // K/P +0x3617, M +0x0A, C +0x09
    void* batch = *(void**)((char*)r + BATCH_OFF);       // K 0x20, P/M/C 0x34
    BatchFlush(batch);                                   // pushes the pending scene batch out (normal draw)
    g_inCursor = true;
    Orig(mgr, r);                                        // state calls + append of the cursor quads
    BatchFlush(batch);                                   // -> exactly one DrawPrimitiveUP with the cursor
    g_inCursor = false;
}
// BatchFlush is __thiscall(batch), no args: K 0x5037E0, P 0x529360, M 0x54C4D0, C 0x54C8A0
```

Inside the device wrapper:
- In `DrawPrimitiveUP` while `g_inCursor`: `PrimitiveType == D3DPT_TRIANGLELIST`, stride 32, FVF 0x1C4.
  Take the stage-0 texture you tracked from `SetTexture`, plus the vertices: xy min/max = on-screen
  quad, uv min/max = sub-rect, diffuse = tint/alpha. Return D3D_OK without drawing.
- Hotspot = quad centre (image centre). If you want it exact: (OS cursor client position clamped to
  the viewport) minus the quad's top-left.
- Cache the HCURSOR by (texture ptr, uv rect, quad size, diffuse); the animation has a finite number
  of frames.
- Textures can be DXT/16-bit/atlased. The robust way is to re-render the captured triangles
  (translated to the origin) into a small A8R8G8B8 render target with the same states, then
  `GetRenderTargetData` -> `CreateIconIndirect`. Do this outside the capture, e.g. right before Present.
- Size: the quad is image size x AspectK, which is non-uniform when preserve_aspect is off.
  Use a uniform scale (min(Kx,Ky) or the DPI factor).
- To show it, make the engine's "hide" call show yours instead:
  - generic: hook `user32!SetCursor` and replace `NULL` with your HCURSOR while custom mode is
    active and a capture happened this frame; or
  - hook WndShowCursor (table above): when `show == 0`, call `SetCursor(yours)`.
  WM_SETCURSOR already goes through the engine (it calls CursorUpdate -> ShowCursor(false)), so no
  window subclassing is needed.
- If DrawCursor ran but drew nothing (hidden flag, mouse outside the viewport in windowed mode), use
  `SetCursor(NULL)` or the arrow. When the app is inactive the engine itself calls `ShowCursor(true)`
  (system arrow) — let that through.

**Is there a better hook point?** No live function takes (texture, hotspot):
- The only one is the dormant HW path (`SetCursorProperties`, §1.2).
- M/C also have the Lua-only `SetCustomCursor(texture)`, which sets type 4.
- An engine-side alternative is to read the current sprite at DrawCursor entry: K/P `mgr+0x360C`,
  M/C `mgr+0x04`. Sprite layout (K, 0x50 bytes):
  - +0x00 animation state, +0x04 image (image+4 -> info: w/h u16 at +0x68/+0x6A, frame count u16 at
    +0xDA, frame UV rects 16 bytes each at +0xDC)
  - +0x08 rect, +0x18 alternate texture, +0x1C 3x3 transform (holds the -w/2,-h/2), +0x40 scale,
    +0x44 colour, +0x4C flags
  - The texture object is at image+0; `K 0x4F4AD0(texObj)` lazy-loads it and returns the
    `IDirect3DTexture9*`.
  - Frame selection by time is K 0x4E9CC0.

  This needs more per-exe structure work than the capture, which gives the final texture + UV +
  on-screen size for free. Use the capture.

Cheaper fallback, no image work: force custom cursors off (K `EnableCustomCursors(mgr, 0)`
0x4EA8F0, P 0x4E71E0, M 0x4FE420, C 0x4FFB70, or edit the setting, §2). The engine then uses
Windows arrow/hand/IBeam/wait cursors (zero lag, DPI-scaled). K/P still draw the item icon next to
the cursor. You lose the themed look.

### 1.4 Mouse-input mapping (for reference)

The WM_* mouse handler is `__thiscall(input, hwnd, msg, wParam, lParam)`: K 0x506620, P 0x52CFE0,
M 0x54DA30, C 0x54DE10. It maps raw client coordinates with the input object's viewport rect and K
(K: input+8..+0x14 rect, +0x18/+0x1C K, set by K 0x506600 from InitGraphics):
`design = int((clamp(x) - vp.x)/K.x)`.

In **fullscreen** a move outside the viewport (letterbox bars) triggers
`SetCursorPos(clampedClientX, clampedClientY)`. It passes client coordinates, which is only correct
because the window is at (0,0). ClipCursor to the viewport: K 0x4CAC50, M 0x4BB410, C 0x4BBDD0.
`ClipCursor(NULL)` on deactivate: K 0x4C9A00, M 0x4BB620, C 0x4BBFF0.

---------------------------------------------------------------------------------------------------

## 2. Settings (custom_cursors / preserve aspect / fullscreen)

All four write to `SHGetFolderPathW(CSIDL_APPDATA=0x1A)\<company>\<product>\saves\`, where company
and product come from `game.xml`:
- K `AlawarEntertainment\IllustrateGames\Kronville_StolenDreams` (verified on the dev PC)
- P `AlawarEntertainment\CyberCradle\Panopticon_PathoftheReflections`
- M `AlawarEntertainment\HappyArtist\Mexicana Deadly Holiday`
- C `AlawarEntertainment\Cruel Games_Red Riding Hood`

**K and P:**
- `saves\settings.xml`:
  `<GameSettings screen_mode="1" preserve_aspect_ratio="false" current_profile_uid="…"/>`.
  - `screen_mode`: 1 = fullscreen at the desktop resolution; anything else = windowed (design size).
  - `preserve_aspect_ratio`: "true"/"false".
  - Load: K 0x4B5400, P 0x4164E0. Save: K 0x4B5AE0, P 0x416970.
  - If settings.xml is missing, the profile manager's screen_mode default **= 1 (fullscreen)**
    (K +0x20C init at 0x4B4ABA, P +0x208 at 0x416BDA). preserve defaults to **false**.
- `saves\<uid>.prf`: `<Profile name uid sound_vol music_vol [voice_vol] custom_cursors="true" difficulty …/>`.
  - Load: K 0x4B4370 (byte at profile+0x32), P 0x40FD70. Save: K 0x4B47C0, P 0x410040.
  - Applied through `EnableCustomCursors` (K 0x4B5900 / 0x4B4FB0 -> 0x4EA8F0, P 0x4163D0 -> 0x4E71E0).
  - The dev-PC profile has `custom_cursors="true"`.

**M and C:** `saves\options.xml`:

```
<Options>
  <Common_Options full_screen="…" save_proportions="…"/>
  <Profiles_Options>
    <profile_options guid sound_volume music_volume voice_volume brightness custom_cursors dificulty_level/>
  </Profiles_Options>
</Options>
```

- Load: M 0x437FB0, C 0x4394D0. Save: M 0x437DD0, C 0x4392F0.
- Common_Options read/write: M 0x436C70, C 0x438190.
- Profile load: M 0x437190, C 0x4386B0. Profile save: M 0x437550, C 0x438A70.
- Defaults come from `game.pack:properties/constants.xml` `<default_options><option id=… value=…/>`
  (M 0x436990 / 0x436DD0):
  - M: `full_screen=true`, no `save_proportions` (only `logos_save_proportions=true`), so it is false;
    `screen_width/height 1024/768`.
  - C: `full_screen=true`, `save_proportions=true`, `screen_width/height 0/0`.
  - `custom_cursors` default **true** (M profile ctor 0x436D40 sets +0x30=1; init also calls
    `EnableCustomCursors(1)`).
- Applied by ApplyOptions (M 0x437B00, C 0x439020):
  `ChangeScreenMode(fs +0xB1, bpp +0xB0, w +0x110, h +0x114)`, `SetPreserveAspect(+0xB2)`,
  `EnableCustomCursors(+0xA4)`.
- `game.xml settings` also has `show_fps`/`debug_render` (P sets show_fps=true).

---------------------------------------------------------------------------------------------------

## 3. Render size, aspect, letterbox

- `m_SourceWidth/Height` comes from `SetSourceSize(renderer, w, h)`: K 0x4C8A60 (1280x800),
  P 0x4AE110 (1024x768), M 0x4CE070 / C 0x4CE6F0 (constants.xml or 1024x768).
- `m_Width/m_Height`:
  - **fullscreen:** `ChangeScreenMode(app, fs, bpp32, w=0, h=0)` (K 0x4C9D20, P 0x4B4830, M 0x4BAD30,
    C 0x4BB2A0; `__thiscall`, 4 args). w=0 is replaced by `window->GetDesktopMode` (vt+0x24:
    K 0x4DF3F0 / P 0x51FEA0 / M 0x507480 / C 0x508D90). That returns the DEVMODE saved by
    `EnumDisplaySettingsExW(NULL, ENUM_CURRENT_SETTINGS)` in the window constructor and on
    WM_DISPLAYCHANGE (K window+0xC4/+0xC8 = dmPelsWidth/Height).
    - The size is then matched against the D3D adapter mode list (w,h,32bpp). With no exact match,
      the **last** mode in the list is used.
    - The device is created with `BackBufferWidth/Height = w,h` and `Windowed=FALSE` (exclusive mode).
  - **windowed:** requested size (design size, or the M/C options window size) x
    `GetDeviceCaps(LOGPIXELSX/Y)/96`. If it exceeds `SPI_GETWORKAREA`, it is shrunk keeping the design
    aspect (K 0x4C9B30, P 0x4B4580, M 0x4BAAE0, C inline in 0x4BB2A0).
    - The window is centred on the *primary* work area (WndCreate K 0x4DEE80 / P 0x51FED0 /
      M 0x5074B0 / C 0x508DC0).
    - InitGraphics then sets `m_Width/H = GetClientRect`, so **back buffer = client size**
      (pp BackBufferWidth/Height left 0).
    - With the resizable style (K window+0x139), WM_SIZE triggers a re-init at the new client size.
- `CalcAspect(K*, Off*, srcW, srcH, dstW, dstH, preserve)` (cdecl; K 0x4C60C0, P 0x4AE280,
  M 0x4CE1E0, C 0x4CE860):

```
K=(1,1); Off=(0,0)
if src != dst:
    if preserve:
        if dstA - srcA > 0.001: w' = dstH*srcA; Off.x = (dstW - w')/2; dstW = w'
        elif srcA - dstA > 0.001: h' = dstW/srcA; Off.y = (dstH - h')/2; dstH = h'
    K = (dstW/srcW, dstH/srcH)
```

  The result feeds the renderer base matrix (K 0x505680 -> render-state +0x1314; M 0x555840), the
  input viewport (K 0x506600; M 0x54D2B0 / 0x4D1340), and the cursor/letterbox code.
  Setters: SetPreserveAspect K 0x4C7160, P 0x4AE1B0, M 0x4CE110, C 0x4CE790.

**Forcing a smaller back buffer (e.g. 1920x1080 on 4K):**
1. **Recommended (consistent, no coordinate fixes): change the fullscreen mode the engine asks for.**
   - Hook `EnumDisplaySettingsExW(NULL, ENUM_CURRENT_SETTINGS)` to report 1920x1080 (generic, works
     in all four), or
   - hook ChangeScreenMode and replace `w,h` when `fs && (w==0 || w==desktopW)`. This also covers
     the re-entry from DeviceCheck/display-change, which calls ChangeScreenMode(1,…,0,0).
   - The engine then switches the display to 1920x1080 exclusive. m_Width/m_Height, K, the viewport
     and WM_MOUSEMOVE coordinates are all in 1920x1080, so mouse mapping stays correct.
   - Requires 1920x1080x32 in the adapter mode list.
2. **Do not** only patch `D3DPRESENT_PARAMETERS.BackBufferWidth/Height` in CreateDevice/Reset.
   The engine keeps m_Width=3840, so it emits XYZRHW vertices for 3840x2160 (image cropped) and
   mouse mapping no longer matches.
3. Render at 1080p while displaying at native 4K (borderless): run the device windowed with a
   1920x1080 back buffer (Present stretches it) and **scale XYZRHW x,y by bb/m_Width in
   DrawPrimitiveUP**. Mouse stays correct because the engine still believes in client = 3840.
   - In K this is complete (single DrawPrimitiveUP, no viewport/scissor/Clear calls). P/M/C also do
     one full-target Clear without rects, which is harmless.
   - Skip the scaling while a render-target texture is bound (batcher +0x1F4 != 0, or your
     SetRenderTarget tracking).
   - Alternatively, overwrite renderer+4/+8 after InitGraphics; then every mouse source (handler
     lParam, window GetCursorPos/ScreenToClient, SetCursorPos/ClipCursor) must be scaled. More
     invasive.

---------------------------------------------------------------------------------------------------

## 4. Frame pacing

- **Main loop** `Run` (window vt+0x68 K / +0x64 P / +0x5C M,C):
  - **K 0x4DF550 / P 0x520250: one `PeekMessageW(PM_REMOVE)` per iteration, then Update + Render.**
    At low FPS the input backlog drains at one message per frame, so clicks lag.
  - M 0x507830 / C 0x5090F0 drain the whole queue first.
  - Possible fix for K/P: in a `PeekMessageW` hook, dispatch all but the last pending message yourself.
- **FPS limiter** (`CFrameLimiter {int intervalMs; HANDLE hEvent; HANDLE hThread}`; objects K 0x625984,
  P 0x6D1354, M 0x848D04, C 0x845B0C):
  - `SetFPS(obj, 60)` at init (wrapper call sites K 0x41C55C, P 0x459041, M 0x41FD9E, C 0x41FD9C;
    wrappers K 0x4CA7F0, P 0x4B42B0, M 0x4BA810, C 0x4BB010). SetFPS is K 0x509E90,
    P 0x531F90, M 0x550250, C 0x550420. It computes interval = 1000/60 = **16 ms** and creates an
    auto-reset event plus a `THREAD_PRIORITY_TIME_CRITICAL` thread (K 0x509E50, P 0x531F30,
    M 0x5501F0, C 0x5503C0) that loops `Sleep(16); SetEvent`.
  - AppUpdate starts with `WaitForSingleObject(event, INFINITE)` (Wait K 0x509F50, P 0x532080,
    M 0x550340, C 0x550510).
  - Net effect: about 62.5 Hz, not synchronised to the 60 Hz display.
  - `SetFPS(0)` stops the thread and makes Wait a no-op (handle = -1).
  - M/C have a debug PgUp/PgDn ±5 FPS key (0x420BE0, needs debug ≥ 2).
- **Present:** `PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE`, SwapEffect DISCARD,
  1 back buffer. No vsync, so exclusive fullscreen tears and 62.5-vs-60 Hz judder.
- **Present bug, all four:** EnginePresent (K 0x503690, P 0x528D50, M 0x54BEC0, C 0x54C290) does
  `EndScene; CreateQuery(EVENT); Issue(END); Present`. It then spins `GetData` + `Sleep(0)` **only
  if Present FAILED**: the helper returns true on failure, so the condition is inverted.
  - Normal frames are never throttled, and the driver can queue up to 3 frames. With a GPU-bound
    Intel iGPU at 4K, that is several frames of input/cursor latency.
  - The branch: K 0x503784 `74 3C`, P 0x528E57 `74 25`, M 0x54BFC7 `74 25`, C 0x54C397 `74 25`.
  - Better than patching (busy wait): in the wrapper, wait on the previous frame's event query, or
    use D3D9Ex `SetMaximumFrameLatency(1)`.
- **Timer:** `timeBeginPeriod(1)` (kept for the process lifetime) + `timeGetTime()`, integer-ms frame
  deltas (Time init K 0x4E5A70, P 0x4A9F90, M 0x4D2AC0, C 0x4D2F30). rdtsc (1 s calibration) is used
  only if timeBeginPeriod fails. A QPC helper exists (P 0x4A9690, M 0x4D21E0) but is unused.
- **Focus loss** (WM_ACTIVATEAPP sets the app active byte; app handler K 0x4CAEC0, P 0x4B5370,
  M 0x4BB840, C 0x4BC1E0):
  - `ClipCursor(NULL)`, `window->ShowCursor(true)` (system cursor), sound pause, game clock paused.
  - AppUpdate then does `Sleep(50)` and skips the update unless app state is 1/2. AppRender draws
    nothing while inactive or minimized.
  - WM_WTSSESSION_CHANGE lock also pauses. SC_KEYMENU is swallowed.
  - WM_PAINT renders (modal move/size).
- **Suggested pacing fix:** in the CreateDevice/Reset wrapper set `PresentationInterval = ONE`, then
  call `SetFPS(obj, 0)` once after init (or keep 60 on a 60 Hz panel). Add 1-frame latency limiting
  in Present.

---------------------------------------------------------------------------------------------------

## 5. Risks on Intel iGPU / Win10 / high DPI

- **M and C pin the whole process to one CPU core.** The window constructor (M 0x5070C0, C 0x508A40)
  takes the lowest set bit of the process affinity and calls `SetProcessAffinityMask` +
  `SetThreadAffinityMask`. Main thread, OpenAL, video decode, the TIME_CRITICAL limiter thread and
  the Intel UMD worker threads all share one core: stutter. Make both calls no-ops (hook kernel32).
  K/P don't do this.
- **CreateDevice (CreateOrResetDevice):** K 0x503F00, P 0x52B9B0, M 0x54CAF0, C 0x54CF00;
  `__thiscall(holder, hwnd, w, h, bool fullscreen)`.
  - adapter 0 (primary only), HAL, `D3DCREATE_SOFTWARE_VERTEXPROCESSING` (0x20) with no
    FPU_PRESERVE and no MULTITHREADED.
  - `Direct3DCreate9(0x20)` in K, `(0x1F)` in P/M/C.
  - X8R8G8B8 back buffer, no depth buffer. The back-buffer size is set only in fullscreen.
    RefreshRate 0.
  - The return value is ignored by InitGraphics: if CreateDevice fails, DeviceCheck retries
    InitGraphics every frame (black screen, no crash). Reset failure is logged and retried the same way.
- **Device lost:** DeviceCheck (K 0x4C61E0, P 0x4B1270, M 0x4D1230, C 0x4D17A0) calls
  `TestCooperativeLevel`.
  - DEVICELOST: skip rendering.
  - DEVICENOTRESET: InitGraphics calls the texman hooks (K 0x4D0DC0/0x4D0E40; presumably they drop
    DEFAULT-pool textures, not verified), releases the saved back-buffer RT (K 0x504020(1)), then calls
    `Reset`. After a display change in fullscreen (renderer +0x40 set) it instead releases device+D3D
    (0x504020(0)) and recreates both via `ChangeScreenMode(1, …, 0, 0)`.
  - Render-target textures are DEFAULT pool. If a wrapper adds its own DEFAULT resources, release
    them in Reset.
- **Memory:** all four are not LARGE_ADDRESS_AWARE. Every texture is MANAGED (system-memory copy)
  inside a 2 GB address space, and the Intel UMD shares that address space. Running out of address
  space is a likely cause of the "occasional crashes" (the engine has BadAllocException and
  MiniDumpWriteDump). Setting the LAA bit on a copy of the exe is worth testing; it can't be done
  from the DLL.
- **Display assumptions:**
  - No GetSystemMetrics at all.
  - The desktop size comes from `EnumDisplaySettingsExW(NULL…)` (primary device), the work area from
    `SPI_GETWORKAREA` (primary), D3D adapter 0. Anything not on the primary monitor is wrong.
  - System DPI comes from `GetDeviceCaps(LOGPIXELSX)` of the screen DC.
  - System-aware only: on a secondary monitor with a different scale the window is bitmap-stretched
    by DWM (blurry).
  - Exclusive fullscreen 4K with preserve off stretches 16:10 (K) / 4:3 (P/M/C) art to 16:9.
- **Cursor:** custom cursor is drawn by the GPU with frame latency + design-pixel quantisation (§1.1).
  The engine forces `SetCursor(NULL)` on every update and every WM_SETCURSOR, including over the
  non-client area in windowed mode.
- **Video decoding** runs on worker tasks (K `CDecodeTask`, vtable 0x5DC240) while the device is not
  MULTITHREADED. I did not confirm whether the worker touches D3D; the decode functions call into
  libvpx only.
- **Present:** IMMEDIATE + no frame-latency control (§4). Plus a per-frame CreateQuery/Release.

---------------------------------------------------------------------------------------------------

## 6. Byte signatures

**Method.** Masked prologue patterns: rel32 call/jmp targets and absolute addresses inside the image
range are wildcarded, trailing wildcards are trimmed. Uniqueness was checked with Python/capstone
over each exe's `.text`; the only hit is the function start. "P/M/C" = one pattern that is unique in
P, M and C at the listed per-exe addresses. "M/C" = the same for M and C. K always needs its own
pattern (different compiler).

**Recommendation:** the four binaries are fixed. Identify the exe by PE TimeDateStamp (table at top),
then use the per-exe addresses below, and verify the pattern at that address before installing a
hook. Treat the pattern as a sanity check, not a search key.

### Cursor

| target | K | P | M | C |
|---|---|---|---|---|
| **DrawCursor** (thiscall, ret 4) | 0x4EA660 `83 EC 38 53 8B D9 56 80 BB 17 36 00 00 00 0F 85` | 0x4E7610 `55 8B EC 81 EC 94 00 00 00 89 8D 7C FF FF FF 8B 85 7C FF FF FF` | 0x4FE470 `55 8B EC 83 EC 70 89 4D A0 8B 45 A0 0F B6 48 0A` | 0x4FFBC0 `55 8B EC 83 EC 70 89 4D A0 8B 45 A0 0F B6 48 09` |
| **CursorUpdate** (thiscall) | 0x4EA2F0 `51 53 56 57 8B F9 80 BF 16 36 00 00 00 0F 85` | 0x4E7380 `55 8B EC 83 EC 24 56 57 89 4D E8 8B 45 E8 0F B6 88 16 36 00 00` | 0x4FE0F0 } M/C | 0x4FF870 } `55 8B EC 83 EC 70 89 4D A4 8B 45 A4 0F B6 48 09` |
| CursorMgr::Instance (cdecl, returns eax) | 0x4EABE0 `6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 51 A1 ?? ?? ?? ?? 33 C4 50 8D 44 24 08 64 A3 00 00 00 00 A1 ?? ?? ?? ?? 85 C0 75 40 68 28 36 00 00` | 0x4E6C20 `55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 0C A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 75 47 68 28 36 00 00` | 0x4FDEC0 `…same prefix… 83 3D ?? ?? ?? ?? 00 75 44 6A 60` | 0x4FF650 `…same prefix… 83 3D ?? ?? ?? ?? 00 75 44 6A 5C` |
| EnableCustomCursors (thiscall, ret 4) | 0x4EA8F0 `8A 44 24 04 56 8B F1 57 38 86 17 36 00 00 74 0A` | 0x4E71E0 `55 8B EC 51 89 4D FC 8B 45 FC 0F B6 88 17 36 00 00` | 0x4FE420 `55 8B EC 51 89 4D FC 8B 45 FC 0F B6 48 0A 0F B6 55 08` | 0x4FFB70 `55 8B EC 51 89 4D FC 8B 45 FC 0F B6 48 09 0F B6 55 08` |
| CursorOnMouseMove (thiscall, ret 4) | 0x4EAC60 `83 EC 08 56 8B F1 80 BE 14 36 00 00 00 75 1D 83 BE 08 36 00 00 67` | 0x4E72A0 `55 8B EC 83 EC 10 89 4D F0 8B 45 F0 0F B6 88 14 36 00 00` | 0x4FE010 } M/C | 0x4FF790 } `55 8B EC 83 EC 10 89 4D F0 8B 45 F0 0F B6 48 08` |
| SetCursorType (thiscall, ret 4) | 0x4EAD00 `56 8B F1 80 3E 00 75 05 E8` | 0x4E7330 `55 8B EC 51 89 4D FC 8B 45 FC 0F B6 08 85 C9 75 08` | 0x4FE090 (trivial setter; use the address) | 0x4FF810 (same) |
| **Wnd ShowCursor(bool)** | 0x4DF730 `53 8A 5C 24 08 56 8B F1 84 DB 74 1B 66 83 BE F8 00 00 00 00` | 0x520A70 `55 8B EC 51 89 4D FC 0F B6 45 08 85 C0 74 50 8B 4D FC` | 0x508070 } M/C | 0x509870 } `55 8B EC 83 EC 08 89 4D F8 0F B6 45 08 85 C0 74 67` |

### Render / D3D

| target | K | P/M/C shared pattern (P / M / C addresses) |
|---|---|---|
| **BatchFlush** (thiscall batch) | 0x5037E0 `56 8B F1 8B 8E 0C 02 00 00 85 C9 0F 84` | `55 8B EC 83 EC 0C 89 4D F8 8B 45 F8 83 B8 0C 02 00 00 00` — 0x529360 / 0x54C4D0 / 0x54C8A0 |
| EnginePresent (thiscall batch) | 0x503690 `83 EC 08 56 8B F1 E8 ?? ?? ?? ?? 33 D2 39 56 40` | `55 8B EC 83 EC 20 89 4D EC 8B 4D EC E8` — 0x528D50 / 0x54BEC0 / 0x54C290 |
| CreateOrResetDevice | 0x503F00 `83 EC 38 56 8B F1 83 3E 00 75 16 6A 20 E8` | `55 8B EC 83 EC 48 89 4D B8 8B 45 B8 83 38 00 75 0C` — 0x52B9B0 / 0x54CAF0 / 0x54CF00 |
| InitGraphics (thiscall renderer) | 0x4C6DC0 `83 EC 10 53 8B D9 E8 ?? ?? ?? ?? 8B C8 E8` | `55 8B EC 83 EC 14 89 4D EC 6A 01 8B 4D EC E8` — 0x4ADEA0 / 0x4CDE00 / 0x4CE480 |
| CalcAspect (cdecl, 7 args) | 0x4C60C0 `8B 44 24 10 53 8B 5C 24 10 56 8B 74 24 0C 57 8B 7C 24 14` | `55 8B EC 83 EC 18 8B 45 0C D9 EE D9 58 04 8B 4D 0C` — 0x4AE280 / 0x4CE1E0 / 0x4CE860 |
| SetPreserveAspect | 0x4C7160 `83 EC 10 8B 44 24 14 53 56 57 8B F9` | `55 8B EC 83 EC 14 89 4D EC 8B 45 EC 8A 4D 08 88 08` — 0x4AE1B0 / 0x4CE110 / 0x4CE790 |
| SetSourceSize | 0x4C8A60 `83 EC 10 8B 54 24 18 53 55 8B D9 56` | `55 8B EC 83 EC 14 89 4D EC 8B 45 EC 8B 4D 08 89 48 3C` — 0x4AE110 / 0x4CE070 / 0x4CE6F0 |
| DeviceCheck (TestCooperativeLevel) | 0x4C61E0 `A1 ?? ?? ?? ?? 56 8B F1 57 85 C0 75 0A E8` | `55 8B EC 83 EC 0C 89 4D F8 E8 ?? ?? ?? ?? 8B C8` — 0x4B1270 / 0x4D1230 / 0x4D17A0 |
| ChangeScreenMode (thiscall app; fs, bpp32, w, h) | 0x4C9D20 `8B 54 24 0C 8B 44 24 10 89 91 C4 00 00 00 89 81 C8 00 00 00` | P 0x4B4830 / M 0x4BAD30 `55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 70 56 A1` (same bytes in P and M: search P and M separately); C 0x4BB2A0 `55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 81 EC 60 01 00 00 56` |
| WndCreate/restyle (vt+0x20) | 0x4DEE80 `83 EC 30 A1 ?? ?? ?? ?? 33 C4 89 44 24 2C 53 8B 5C 24 38 88 5C 24 0C` | `55 8B EC 83 EC 48 89 4D BC 0F B6 45 08 50 8B 4D BC` — 0x51FED0 / 0x5074B0 / 0x508DC0 |

### Loop / timing / input

| target | K | P/M/C shared pattern (P / M / C addresses) |
|---|---|---|
| LimiterSetFPS (thiscall obj, fps) | 0x509E90 `56 8B F1 8B 4C 24 08 85 C9 74 0B B8 E8 03 00 00` | `55 8B EC 83 EC 0C 89 4D F8 83 7D 08 00 74 0F B8 E8 03 00 00` — 0x531F90 / 0x550250 / 0x550420 |
| LimiterWait (thiscall obj) | 0x509F50 `8B 41 04 83 F8 FF 74 09 6A FF 50 FF 15` | `55 8B EC 51 89 4D FC 8B 45 FC 83 78 04 FF 74 0F` — 0x532080 / 0x550340 / 0x550510 |
| Limiter thread proc | 0x509E50 `83 3D ?? ?? ?? ?? 00 74 29 56 8B 35` | `55 8B EC 83 3D ?? ?? ?? ?? 00 74 1B` — 0x531F30 / 0x5501F0 / 0x5503C0 |
| Run / message loop | 0x4DF550 `83 EC 1C 56 8B F1 80 7E 0D 00 75 6C 53 8B 1D` | `55 8B EC 83 EC 20 89 4D E0 8B 45 E0 0F B6 48 0D` — 0x520250 / 0x507830 / 0x5090F0 |
| AppRender | 0x4CA1A0 `56 8B F1 80 BE 80 00 00 00 00 75 08 8B 4E 78 E8` | `55 8B EC 83 EC 08 89 4D F8 8B 45 F8 0F B6 88 90 00 00 00` — 0x4B4000 / 0x4BA4F0 / 0x4BACF0 |
| AppUpdate | 0x4CAA90 `6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 18 56 A1` | `55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 38 A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 00 00 00 00 89 4D BC 8B 4D BC E8` — 0x4B42D0 / 0x4BA830 / 0x4BB030 |
| Mouse message handler | 0x506620 `55 8B EC 83 E4 F8 83 EC 6C 53 56 57 8B F9 8B 0D` | `55 8B EC 81 EC CC 00 00 00 89 8D 48 FF FF FF E8` — 0x52CFE0 / 0x54DA30 / 0x54DE10 |
| Time init | 0x4E5A70 `83 EC 08 56 6A 01 8B F1 FF 15 ?? ?? ?? ?? 85 C0` | `55 8B EC 83 EC 18 89 4D FC 6A 01 FF 15` — 0x4A9F90 / 0x4D2AC0 / 0x4D2F30 |

Data offsets that a hook needs, all verified in the code:
- custom flag: K/P +0x3617, M +0x0A, C +0x09
- renderer -> batch: K +0x20, P/M/C +0x34
- batch queued-vertex count: +0x20C
- the limiter objects and singletons in §0/§4

### MinHook notes
- `__thiscall` targets: detour as `__fastcall(void* ecx, void* edx_unused, args…)` and call the
  trampoline the same way.
- DrawCursor/ShowCursor/SetCursorType take 1 stack arg (`ret 4`). Update/Flush/Present/Wait take none.
- K's functions are optimised with no frame pointer. For every target above, the first whole
  instructions covering ≥ 5 bytes contain no rel32 branch, so MinHook's 5-byte patch is safe
  (absolute imm32 such as `push offset` or `mov eax,[abs]` are copied as-is). P/M/C all start with
  `55 8B EC`.
- Thunk-call alternative: DrawCursor has exactly one call site per exe
  (K 0x4CA24B, P 0x4B40C3, M 0x4BA5B3, C 0x4BADB3; `call rel32` with ECX = mgr, 1 pushed arg), so a
  call-site patch also works.
