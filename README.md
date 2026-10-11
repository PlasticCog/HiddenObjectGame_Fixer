# HOG Fixer

A compatibility wrapper that makes old Alawar-style hidden-object games run well on modern
Windows PCs — fixing display scaling, blurry/stretched output, laggy hardware cursors, and
weak-GPU frame rate — with an easy launcher you can add more games of the same type to.

It is in the same family as tools like dgVoodoo2 and Special K: it launches a game you own with a
small helper library (`hogfix.dll`) that corrects how the game talks to Windows and Direct3D. The
original game files are never modified.

## What it fixes

- **Scaling / blurry / wrong size.** On a 4K screen these games either switch the whole desktop to
  an exclusive mode or render the full scene at 4K. The fixer makes the game render at a sensible
  internal resolution and upscales it once, aspect-correct and letterboxed, into a borderless
  window on the monitor you choose. Much cheaper for a weak Intel iGPU; no desktop mode changes.
- **Laggy / jerky hardware cursor.** Many of these games fight the OS pointer (warping it every
  frame) or hide it and draw their own sprite that only moves at the game's frame rate. The fixer
  decouples the visible pointer from the game loop so it moves at full hardware-cursor speed again.
- **Low FPS / stutter.** An internal-resolution cap plus an optional FPS limiter and a one-frame
  GPU latency limiter keep things smooth and responsive on integrated graphics.
- **Won't start on modern Windows.** Per-engine compatibility shims and DPI virtualization address
  the common "won't launch / wrong resolution" failures (including Flash-hosted games).

## Layout

```
src/common     Profile (per-game INI) + process launch/injection helpers
src/hook        hogfix.dll — the injected fix layer:
                  display   virtual monitor resolution (no real mode changes)
                  window    borderless/windowed placement, coordinate mapping
                  cursor    smooth hardware cursor (+ software-sprite conversion)
                  d3d9      windowed upscale, vsync/latency, cursor-sprite capture hook
                  framepace FPS cap + one-frame GPU latency limiter
                  engine_shim per-engine tweaks selected by the profile
src/loader      hogrun.exe — CLI that starts a game with the fix applied
src/launcher    SDL3 + Dear ImGui GUI (add/configure/launch games)  [in progress]
research/       Ghidra scripts and reverse-engineering notes for each engine
third_party/    SDL, Dear ImGui, MinHook (git submodules)
```

## Building

Requires Visual Studio 2022 with the C++ x86 toolset (the games are 32-bit). From a checkout:

```
git submodule update --init --recursive
build.cmd Release
```

Output lands in `build\Release\bin`.

## Status

Early work in progress. The hook DLL and CLI loader build and the generic display/window/cursor/
D3D9 layers are in place; per-engine cursor-sprite conversion and the GUI launcher are being wired
up from the reverse-engineering notes.
