# HOG Fixer — Quick Start

HOG Fixer makes old Alawar-style hidden-object games run well on modern Windows:
it renders them at a sensible internal resolution and upscales cleanly (no more
4K-on-a-weak-GPU slowdowns or exclusive-fullscreen desktop flips), gives you a
smooth low-latency cursor, and launches each game in a tidy borderless window.

The original game files are never modified. HOG Fixer launches a game you already
own with a small helper library (`hogfix.dll`) applied — the same idea as tools
like dgVoodoo2 or Special K.

## Install

1. Unzip anywhere. The three files must stay together:
   - `HOGFixer.exe` — the launcher
   - `hogfix.dll` — the fix that gets applied to the game
   - `hogrun.exe` — a command-line launcher (optional)
2. If you have the "HiddenObject 6-in-1" bundle, unzip HOG Fixer **into that
   bundle's folder** (next to `My Games`). Then it auto-detects the games.

## Use

1. Run `HOGFixer.exe`.
2. Add games any of three ways:
   - **Import bundled games** (asks for the bundle folder if it can't find
     `My Games` automatically),
   - **Browse…** to pick a game's `.exe`, or
   - **paste a path** into the address box and press Enter — a `.exe` is added as
     a game, a folder is imported as a bundle. (Tip: in Explorer, Shift+right-click
     a file → "Copy as path", then paste here.)
3. Select a game on the left. The defaults are tuned for a weak GPU at 1440p/4K:
   - **Window mode: borderless** — full-screen window, internal upscale.
   - **Internal resolution: auto (≤1080p)** — lower this (e.g. 720) if the game
     still runs slowly; raise it (native) for maximum sharpness on a strong GPU.
   - **Cursor: system** — smooth hardware cursor.
   - **VSync + Low latency on** — smooth, responsive, no tearing.
4. Click **Play**.

Settings are saved per game in `%APPDATA%\HOGFixer\profiles`. A log is written
next to the game as `hogfix.log` (turn on **Verbose log** if you need detail).

## Tips / troubleshooting

- **Still slow?** Lower *Internal resolution* to 720 or 768.
- **Wrong monitor?** Set *Monitor* (0 = primary, 1 = first, 2 = second…).
- **Black bars / stretched?** *Aspect* = keep (bars, correct shape), stretch
  (fills, may distort), or integer (pixel-perfect).
- **A game misbehaves with the fix?** Set *Window mode* to **passthrough** to run
  it untouched, then tell us which game.

## Per-game notes

- **Lake House, Kronville, Panopticon, Mexicana, Cruel Games** — fully supported.
- **Elementary My Dear Majesty! (NeoAxis)** — supported; it launches a child
  process, which HOG Fixer follows automatically (keep *Follow children* on).
- **The Other Side: Tower of Souls** — a Flash-based game. It needs a Flash
  ActiveX control (`.ocx`) to run at all on modern Windows; set its path under the
  game's *Flash OCX* box. Until then it runs in passthrough.

This tool works only with games you own. Game titles and trademarks belong to
their respective owners.
