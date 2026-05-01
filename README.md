# DMA Testing -- Need Offset Dump

> Read-only DMA
> Aimbot · ESP · Anti-Recoil · Hotkeys · Web Menu · Cloud Configs.

---

## Read This First -- This is for educational purposes only

**This is a hardware-DMA research/education project. It is not a one-click "download and play" cheat.**
Before you go any further, make sure you understand all of the following:

| | |
|---|---|
| **Anti-cheat risk is real** | Anticheat can and does ban accounts that trigger heuristics. **Use a throwaway account and isolated hardware** until *you* have validated your full pipeline end-to-end. The maintainers take zero responsibility for your bans. |
| **Hardware is mandatory** | You need a second PC ("attack PC"), a DMA FPGA card (Screamer / 35T / 75T or similar) with **clean anti-fingerprint firmware**, and a separate **USB HID injector** (KMBox Net / B+ recommended; Arduino / Teensy / MAKCU / Ferrum also work). There is **no software-only path**. None. |
| **Steep learning curve** | You should already be comfortable with: DMA cheating fundamentals, PCIe FPGAs, IOMMU/VT-d, basic Frostbite memory layouts, Win32 virtual-key codes, and IDA-style signature scanning. If any of these is new, learn them on an offline / non-AC target first. |
| **Game patches break offsets** | Every game update may shift static VAs and struct layouts. The project ships a 3-tier offset resolver and a `--discover` mode, but expect to reverse fresh sigs every so often. |
| **BIOS prep is required** | Disable **VT-d / IOMMU** on the *gaming* PC. With it on, DMA reads return zeros or `0xFFFFFFFF`. |
| **Read-only by design** | This codebase has **zero write paths** into the game process. No DLL injection, no kernel driver, no hooks, no `WriteProcessMemory`. Output goes through a separate USB HID device the gaming PC sees as a normal mouse. Don't fork to add writes — the moment you do, the AC detection surface explodes. |
| **For personal research use** | Don't use this to grief, sell carries, or run smurf farms. Don't redistribute compiled binaries. |

**TL;DR — if you don't already own a DMA card *and* a KMBox/Arduino, getting this repo to compile is not your next step. Acquire the hardware first.**

---

## Table of Contents

1. [Architecture](#architecture)
2. [Hardware & Software Requirements](#hardware--software-requirements)
3. [Build](#build)
4. [Setup & First Run](#setup--first-run)
5. [Web Menu](#web-menu)
6. [Hotkeys](#hotkeys)
7. [Features](#features)
8. [Anti-Recoil Internals](#anti-recoil-internals)
9. [Pattern Scanner & Offset Resolution](#pattern-scanner--offset-resolution)
10. [Configuration Reference](#configuration-reference)
11. [REST API](#rest-api)
12. [Tests Tab (Diagnostics)](#tests-tab-diagnostics)
13. [Troubleshooting](#troubleshooting)
14. [Project Status](#project-status)

---

## Architecture

```
[Gaming PC running game] ←──PCIe DMA──→ [Attack PC running this software]
                                              │
                                      KMBox / Arduino / etc.
                                       (USB HID input device)
                                              │
                                  [back to Gaming PC mouse]
```

**No software runs on the gaming PC. Zero writes to game memory.**
DMA reads only. Output is via a separate USB HID device that the gaming PC sees as a normal mouse. The KMBox can additionally relay the gaming PC's keyboard/mouse state back to the attack PC over UDP — that is what the hotkey system uses to detect game-PC input (see [Hotkeys](#hotkeys)).

---

## Hardware & Software Requirements

### Hardware

| Item | Notes |
|---|---|
| **2× Win10/11 PCs** | Gaming PC (runs game) + Attack PC (runs this software) |
| **DMA FPGA card** | Screamer, 35T, 75T or similar, with clean anti-fingerprint firmware. EA AC's CR3 cloning is handled automatically (see `dma/dma_handler.cpp`). |
| **HID input device** | **KMBox Net / B+** (recommended — also enables hotkey input monitoring), *or* Arduino / Teensy / MAKCU / Ferrum |
| *(optional)* Fuser | Mirrors the ESP overlay onto the gaming PC's monitor |

### Software

| Item | Where to get it |
|---|---|
| **Visual Studio 2022** with C++ toolchain | <https://visualstudio.microsoft.com> |
| **CMake 3.20+** | <https://cmake.org/download/> |
| **MemProcFS SDK** | <https://github.com/ufrisk/MemProcFS/releases> — drop `vmmdll.h` + `vmmdll.lib` into `./sdk/memprocfs/` |
| **DMA card drivers / tools** | Vendor-specific (PCILeech for Screamer; specific tool for 75T; etc.) |

The build pulls `nlohmann/json` and `cpp-httplib` automatically via CMake `FetchContent`.

---

## Build

```bat
git clone <this-repo> bf6_dma
cd bf6_dma

REM 1. Drop the MemProcFS SDK files in
copy <somewhere>\vmmdll.h sdk\memprocfs\
copy <somewhere>\vmmdll.lib sdk\memprocfs\

REM 2. Configure + build (Release)
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Output:

```
build/Release/
  bf6_dma.exe
  web_menu/         ← static UI; served by built-in HTTP server
  configs/          ← config storage (created at first run)
```

---

## Setup & First Run

### 1. Wire up the DMA card

1. PCIe end → gaming PC's PCIe slot (USB-3 backplane is fine for Screamer-class).
2. USB end → attack PC.
3. Load the card's firmware using its vendor tool (PCILeech / 75T tool / etc.).

### 2. Wire up the input device

#### KMBox Net / B+
- Plug the KMBox between the gaming PC and your physical mouse (and optionally keyboard).
- LAN-cable the KMBox to your home network so the attack PC can reach it.
- Note the IP and port (default `192.168.2.188:4096`).

#### Arduino / Teensy / MAKCU / Ferrum
- Flash `arduino_sketch/bf6_mouse.ino` using Arduino IDE with the Mouse library.
- USB it to the gaming PC's mouse port; data side connects to the attack PC.
- Same protocol works for all four: `M <dx> <dy>\n` over the COM port.

### 3. BIOS prep on the gaming PC

- **Disable VT-d / Intel VT-d / AMD IOMMU.** Without this, DMA reads return zeros.
- (Recommended) Disable Resizable BAR if you see flaky reads.

### 4. First run

1. Edit `configs/default.json` to match your setup (KMBox IP, screen resolution, mouse sens). Or do this from the web menu after start.
2. Launch BF6 on the gaming PC, get to the main menu.
3. On the attack PC: `bf6_dma.exe`.
4. Open a browser to `http://localhost:8080` (or `http://<attack-pc-ip>:8080` from a phone/tablet on the LAN).

### What happens at startup

1. **DMA init** — connects to the FPGA via MemProcFS, finds `bf6.exe`.
2. **CR3 fix** — BF6 runs under EA AntiCheat, which clones the process page tables; the OS-reported CR3 maps to decoy memory. The handler scans physical RAM for the *real* CR3 (page-aligned, self-referenced PML4, verifies VA→Phys translation lands on a valid `MZ`/`PE` header), then pushes it into MemProcFS via `VMMDLL_OPT_PROCESS_DTB`. Takes 2–8 seconds.
3. **Input device init** — KMBox UDP socket *or* serial COM port. KMBox additionally starts the input-monitor reader (see [Hotkeys](#hotkeys)).
4. **Web server** comes up on port 8080.
5. **ESP overlay** — transparent always-on-top click-through window on the attack PC's primary monitor (Fuser captures this if Fuser mode is enabled).
6. **Main loop** runs at ~120Hz (8 ms frame cap).

---

## Web Menu

Access from any device on your LAN at `http://<attack-pc-ip>:8080`.

| Tab | What it does |
|---|---|
| **Aimbot** | Master toggle, **hotkey + mode**, FOV, smoothness, bone, lead prediction, visibility filter |
| **ESP** | Master toggle, **hotkey + mode**, box / skeleton / name / weapon / health / snaplines, max distance |
| **Anti-Recoil** | Master toggle, **hotkey + mode**, strength multiplier, reset timeout |
| **Misc** | Input device, mouse sensitivity, screen resolution, Fuser mode, auto-discover offsets |
| **Configs** | Save / load / delete named JSON configs (cloud-style — files live in `./configs/`) |
| **Players** | Live enemy list with HP and weapon (polled every 1s while tab is open) |
| **Tests** | DMA self-test + input wiggle / circle / dry-fire — see [Tests Tab](#tests-tab-diagnostics) |

Header status pills update from `/api/status` every 2s — DMA/CR3 health on the left, match state + enemy count on the right.

All UI inputs auto-apply on change (no Save button); ranges and text fields are debounced ~180 ms, checkboxes/selects push immediately.

---

## Hotkeys

Each of the three master features (Aimbot / ESP / Anti-Recoil) has its own hotkey row in the web menu with:

- **Mode** — `Off` (always-on whenever the master toggle is on) · `Hold` (active only while the key is held) · `Toggle` (press flips the active state).
- **Bind** — click and press any key on the **gaming PC** keyboard or mouse; the binder writes the captured VK into config and shows it as a readable name.

### How input gets to the attack PC

In a 2-PC DMA setup, the attack PC's keyboard has nothing to do with the game. So the cheat reads input from the **gaming PC** via the KMBox monitor protocol:

1. The KMBox sits between the gaming PC and your physical keyboard/mouse.
2. On startup, the cheat sends `CMD_MONITOR (0x0007)` to the KMBox over UDP.
3. The KMBox starts streaming HID state back to the attack PC at high rate.
4. `InputMonitor` decodes USB HID usage codes → Win32 VK codes and keeps a thread-safe bitmap.
5. `HotkeyManager` polls the bitmap each frame and produces `IsActive(Feature::X)` for each gateable feature.

If KMBox monitor is unreachable (single-PC test setup, or different firmware), the system **gracefully falls back to Win32 `GetAsyncKeyState`** so the binder still works for keys local to the attack PC.

### Supported keys

| Family | Examples |
|---|---|
| Letters | `A`–`Z` |
| Digits | `0`–`9` (top row) and `Num0`–`Num9` |
| Function | `F1`–`F12` (`F13`–`F24` if your keyboard sends them) |
| Modifiers | `LShift` / `RShift` / `LCtrl` / `RCtrl` / `LAlt` / `RAlt` |
| Special | `Esc`, `Enter`, `Tab`, `Space`, `Backspace`, `Insert`, `Delete`, `Home`, `End`, `PgUp`, `PgDn`, arrows, `CapsLock` |
| Mouse | `LMB`, `RMB`, `MMB`, `XMB1`, `XMB2` |

Anything outside the table shows up in the menu as `VK 0xNN` and still works as a hotkey — you just don't get a pretty name.

### KMBox firmware compatibility note

The monitor wire-format I implemented matches the most common B Pro / B+ firmware:

- `cmd = 0x0007` to enable, `0x0008` to disable.
- Reply payload (within the 100-byte `point[]` field of the standard envelope):
  - `point[0]` = HID modifier byte (Ctrl/Shift/Alt/Win, L/R)
  - `point[2..7]` = up to 6 simultaneously-held HID usage codes
  - `point[8]` = mouse buttons (bit0 L, bit1 R, bit2 M, bit3 X1, bit4 X2)

If your KMBox firmware uses a different layout, the only place to touch is `ParseMonitorPacket` in `src/input/input_monitor.cpp`. Everything above that (queries, threading, the web binder) is wire-format agnostic.

---

## Features

### Aimbot
- **FOV-based target selection** — closest enemy within `aimFov` degrees of crosshair.
- **Bone selection** — head (default) or chest.
- **Smoothing** — bezier ease-in-out + sub-pixel jitter + variable inter-step delay (humanized, *not* a uniform linear divide-by-N — that's statistically obvious in input timing analysis).
- **Hotkey gate** — Off / Hold / Toggle (see [Hotkeys](#hotkeys)). Default is Hold-RMB.
- **Lead prediction** — reads the active weapon's bullet velocity from memory, computes lead point as `target_velocity × distance / bullet_speed`. Target velocity is tracked frame-to-frame via head-position delta.

### Player ESP
- **Box** — 1px black outline + colored hairline.
- **Skeleton** — 15-bone (head / neck / chest / pelvis / L+R arm / L+R leg).
- **Name** — read from ClientPlayer (inline char buffer; pointer-based fallback if `NamePtr` offset is filled in).
- **Weapon** — active weapon name from the `SoldierWeapon` chain.
- **Health bar** — vertical, color-graded red → green.
- **Snaplines** — bottom-center → target.

Rendered with **Direct2D** on a D3D11 swap chain, with **DirectWrite** for text. Transparent overlay via `WS_EX_LAYERED + LWA_COLORKEY` on a click-through topmost popup window.

### Anti-Recoil
See [Anti-Recoil Internals](#anti-recoil-internals).

---

## Anti-Recoil Internals

Three-tier source priority for recoil patterns (highest → lowest):

1. **Live memory read** — walks `WeaponEntityData → SoldierWeaponData → ShotConfigData` for the active weapon, reads Frostbite's per-shot recoil pitch/yaw tables directly from game memory. Refreshes on weapon switch. Zero external data files; auto-updates with patches.
2. **Cached pattern** — once read, kept in memory for the session.
3. **Hardcoded fallback** — last resort if memory walk fails. Substring-matches the weapon name against AR / SMG / LMG patterns tuned for typical Frostbite recoil shapes.

Shot detection is via ammo decrement (`SoldierWeapon::CurrentAmmo`). On stop-fire (no shot for `antiRecoilResetMs` ms), the pattern index resets.

---

## Pattern Scanner & Offset Resolution

`DMAHandler::FindPatternInModule(moduleName, idaPattern)` does IDA-style signature scans across the target module via DMA. Used to relocate offsets that drift between game patches.

```cpp
uintptr_t addr = DMAHandler::Get().FindPatternInModule(
    "bf6.exe",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 80");
```

Results are cached per `(module, pattern)` key so repeat calls are free. Module is read in 64KB chunks with overlap; one-time scan cost is ~50–200 ms for a typical 200 MB module over an FPGA.

### Three-tier resolver (`OffsetResolver`)

Static VAs are no longer hardcoded at the call site. `OffsetResolver::ResolveStatic` runs:

1. **Pattern scan** — match the IDA sig from `signatures.h`, dereference the RIP-relative `disp32`, return the target VA.
2. **Static fallback** — on sig miss or validation reject, fall back to the constexpr value in `offsets.h`.
3. **Validation** — every result is sanity-checked against a validator (`LooksLikePointer`, `LooksLikeClassInstance`, `HasVTable`).

```cpp
uintptr_t gameCtxStatic = OffsetResolver::ResolveStatic(
    "ClientGameContext", "bf6.exe",
    Signatures::ClientGameContext,
    Offsets::ClientGameContext,
    OffsetResolver::LooksLikePointer);
```

### Discovery mode (`--discover`)

When a game patch breaks both the sig and the static fallback:

```bat
bf6_dma.exe --discover
```

This:
- Resolves every static VA via sig + fallback and prints the comparison.
- Walks the live struct chain (`gameCtx → LocalPlayer → ClientPlayer → SoldierEntity → components`) and reports any link that no longer dereferences to a class-shaped instance.
- For each broken link, runs `OffsetResolver::DiscoverPtrField` — heuristic struct-walk that scans pointer-aligned slots for one whose dereference looks like the expected type (vtable in `bf6.exe` `.rdata`).
- Prints copy-pasteable candidates for `offsets.h`, then exits.

All discovery happens via DMA reads from the attack PC. No code, hooks, or syscalls run on the gaming PC, so AC has no surface to detect.

Auto-discovery can also run inline at startup if `offsetAutoDiscover: true` — useful for unattended setups.

### Offsets file

`src/game/offsets.h` is sourced from the latest `2026-04-27` BF6 Steam build dump.

Confirmed:
- `ClientGameContext` static VA
- `ClientPlayer::SoldierEntity` / `TeamId`
- All `Soldier::*` component offsets (Health, Camera, Weapons, Anima, etc.)
- `HealthComponent::Health`
- `RenderSkeleton` / `AnimaComponent` chain for bones
- Weapon entity-data chain through `BulletData::Speed`

Still TBD (placeholder `0x0`):
- `Soldier::Transform` / `NamePtr` (we use ClientPlayer chain instead for names)
- `ShotConfigData::*` recoil-table fields (memory walk falls back to scalar/hardcoded patterns when these are 0)
- `Camera::Pos` / `Pitch` / `Yaw` (we use `ViewMatrix` extraction instead)
- `SoldierWeapon::CurrentAmmo` exact offset

When you reverse a missing one, fill it in and the corresponding code path activates automatically.

---

## Configuration Reference

`configs/default.json` is auto-loaded at startup. Edit via the web menu or by hand.

```json
{
  "aimbotEnabled":     true,
  "aimbotHotkey":      2,            // VK code; 2 = VK_RBUTTON
  "aimbotHotkeyMode":  1,            // 0=Off (always-on) 1=Hold 2=Toggle
  "aimFov":            8.0,          // degrees radius for target selection
  "gameFov":           90.0,         // your in-game horizontal FOV
  "aimSmoothness":     4.0,          // 1 = instant, higher = slower
  "aimSmoothSteps":    8,            // bezier sub-steps per movement
  "aimBone":           0,            // 0 = head, 1 = chest
  "aimPrediction":     true,         // bullet drop / lead
  "aimOnlyVisible":    true,
  "mouseSensitivity":  0.022,        // tune per game/user sens

  "espEnabled":        true,
  "espHotkey":         0,            // 0 = no hotkey
  "espHotkeyMode":     0,            // 0=Off 1=Hold 2=Toggle
  "espBox":            true,
  "espSkeleton":       true,
  "espName":           true,
  "espWeapon":         true,
  "espHealth":         true,
  "espSnaplines":      false,
  "espTeammates":      false,
  "espMaxDistance":    200.0,

  "screenW":           1920,
  "screenH":           1080,
  "fuserMode":         false,

  "inputDevice":       0,            // 0=KMBox 1=Arduino 2=Teensy 3=MAKCU 4=Ferrum
  "kmboxIP":           "192.168.2.188",
  "kmboxPort":         4096,
  "arduinoPort":       "COM3",

  "antiRecoilEnabled":    true,
  "antiRecoilHotkey":     0,
  "antiRecoilHotkeyMode": 0,
  "antiRecoilStrength":   1.0,
  "antiRecoilResetMs":    350,

  "offsetAutoDiscover":   false,
  "configName":           "default"
}
```

### Tuning notes

- **`mouseSensitivity`** is the unified scale factor applied to both aimbot and antirecoil mouse output. Tune it so a 90° turn at your in-game sensitivity matches what the cheat sends. `0.022` is a Source-engine starting point — for BF6 you'll likely need to halve/double until it lines up. Use the Tests tab's circle-trace to see if the cursor traces a clean circle.
- **`aimSmoothness`** is inverted intentionally — higher = slower / more human-looking. `1.0` is instant snap (don't ship this).
- **Hotkey modes** — see [Hotkeys](#hotkeys). When you switch a feature into Toggle mode, the toggle starts ON so you don't lose the feature unexpectedly.

---

## REST API

```
GET  /api/config              → current settings JSON
POST /api/config              → update settings (partial JSON merged)
GET  /api/players             → live enemy data (polling)
GET  /api/status              → DMA / CR3 / match / input snapshot
GET  /api/configs             → list saved configs
POST /api/configs/save        → save current as named config       body: {"name":"my_cfg"}
POST /api/configs/load        → load named config                  body: {"name":"my_cfg"}
DEL  /api/configs/:name       → delete named config

POST /api/input/bind/start    → begin hotkey capture
GET  /api/input/bind/status   → poll capture                      → {capturedVk, kmboxConnected, kmboxPackets}
POST /api/input/bind/cancel   → abort capture
GET  /api/hotkeys/state       → live per-feature active+toggle state

POST /api/test/dma            → DMA self-test: chain validation + read latency
POST /api/test/wiggle         → input wiggle test                  body: {"amplitude":30}
POST /api/test/circle         → input circle trace                 body: {"radius":25,"steps":16}
POST /api/test/recoil         → dry-fire recoil                    body: {"shots":5,"dyPerShot":8}
```

---

## Tests Tab (Diagnostics)

Wraps the diagnostics endpoints in a clickable UI:

- **Connection → DMA self-test** — resolves `ClientGameContext` via sig + fallback, walks the live struct chain, probes single-read and 64-read scatter latency. Reports each link's health, the resolved VAs, the entity count, and µs-level timings. Read-only — safe to run anytime.
- **Aiming device → Wiggle / Trace circle / Dry-fire recoil** — sends small bounded mouse deltas through the configured input device. If the cursor doesn't move on the gaming PC, the device isn't wired up. The cursor returns near its starting position after each test.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `[DMA] MemProcFS init failed` | DMA card not detected | Check USB connection. Try MemProcFS standalone first to isolate. |
| `[CR3] Scan complete. Candidates: N PE-verified: 0` | CR3 scan didn't find a valid candidate | Check `bf6.exe` is actually running. If RAM > 16 GB, bump `SCAN_RAM_MAX` in `dma_handler.cpp`. |
| Reads return zeros / `0xFFFFFFFF` | IOMMU / VT-d enabled on gaming PC | Disable VT-d in BIOS. |
| `[KMBox] Connected …` but hotkeys do nothing | KMBox monitor protocol mismatch | See [KMBox firmware compatibility note](#kmbox-firmware-compatibility-note) — adjust `ParseMonitorPacket`. Until then, hotkeys still work via Win32 fallback for keys on the *attack* PC. |
| `[!] Input monitor failed to start` | KMBox UDP unreachable | Verify `kmboxIP` / `kmboxPort` in config. Verify network reachability with `ping <kmboxIP>`. |
| `[ESP] D3D11CreateDevice failed` | Graphics driver issue / no compatible GPU | Update GPU drivers; verify D3D11 support. |
| ESP overlay doesn't appear | Window obscured / wrong monitor | Window goes fullscreen on primary monitor. For Fuser: capture this window with Fuser. |
| Aimbot moves the wrong direction | Camera matrix axis mismatch | Should be fixed in current build (Frostbite y-up convention). If still inverted, swap signs in `aimbot.cpp::CalcAngleDelta`. |
| Aimbot massively over-/undershoots | `mouseSensitivity` mistuned | Tune via the web menu. Halve/double until 90° turns line up. |
| `[AntiRecoil] Using fallback pattern for [Unknown]` | Memory walk failed | Either `ShotConfigData::*` offsets aren't filled in (expected) or the active weapon chain has a different layout this patch. Hardcoded fallback table covers common BF6 weapon families. |
| `usbxhci.sys: Connect signal arrived for …` flooding | Arduino keyboard re-enumerating | Check Arduino sketch is running and the HID descriptor is sane. |

---

## Project Status

### Working
- DMA + CR3 fix
- Aimbot (bezier smoothing, hotkey, lead prediction, game-state gating, visibility filter when offsets present)
- Anti-Recoil (memory walk + fallback table)
- ESP (Direct2D + DirectWrite, transparent overlay)
- **Hotkey system** with Off / Hold / Toggle modes per feature, KMBox-monitor input source, Win32 fallback, web-menu binder
- Web menu (config + cloud configs + live player list + tests)
- `VMMDLL_Scatter` batched reads — `EntityCache::Update` collapses to ~9 DMA round trips/frame regardless of player count
- `OffsetResolver` — three-tier static-VA resolution (sig → fallback → discovery)
- `--discover` mode — heuristic offset finder, prints copy-paste-able `offsets.h` candidates

### Stubbed / TBD
- `ShotConfigData::*` offsets — antirecoil falls back to hardcoded table without these
- `ClientPlayer::NamePtr` — name reading uses `NameInline` first; if BF6 stores name as wide string at a pointer offset, set `NamePtr` and the code uses it
- `SoldierVisibility::*` and `Awareness::IsVisibleByte` sub-offsets — `Entity::IsVisible()` is wired and aimbot honours `aimOnlyVisible`, but until these are RE'd the check returns true (i.e. doesn't filter anyone). Set the offset, the gate activates.

### Notes & Limitations

- **120 Hz main loop** (8 ms frame cap). Lower in `main.cpp` if you want lower CPU usage.
- **Web server is HTTP, no TLS.** Fine for LAN. Add TLS if you ever expose it externally.
- **Offsets need updating per game patch** — `signatures.h` holds the IDA patterns; `OffsetResolver` resolves at startup with fallback to `offsets.h`. When both miss, run `bf6_dma.exe --discover`.
- **Read-only by design.** No writes to the game process. No DLL injection, no kernel driver, no hooks. The DMA handler exposes no `Write*` method; the codebase contains zero references to `VMMDLL_MemWrite*`, `WriteProcessMemory`, `NtWriteVirtualMemory`, `inject*`, or any equivalent. Mouse output is delivered via a separate USB HID device — KMBox over UDP or Arduino-class over COM — not via DMA. The `VMMDLL_ConfigSet(VMMDLL_OPT_PROCESS_DTB)` call during the CR3 fix writes only to MemProcFS's local per-PID state on the attack PC; no bytes reach the gaming PC. Startup logs `[DMA] Read-only mode — no write paths exposed.` as a visible reminder.
- **Anti-Cheat compatibility** — Javelin (BF6's AC) does various checks. Real-world test results inform hardening; community reports as of mid-2026 indicate this category of DMA setup works for BF6 with reasonable safety, contingent on:
  - DMA firmware that defeats Xilinx fingerprinting (your card's firmware concern, not this project's)
  - VT-d / IOMMU disabled on the gaming PC
  - HID input via separate hardware (KMBox / Arduino / etc.) — not WinAPI `mouse_event` or `SendInput` on the gaming PC
