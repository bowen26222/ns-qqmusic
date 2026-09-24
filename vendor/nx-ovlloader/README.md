# nx-ovlloader (HOS 16.0.0+)

[![platform](https://img.shields.io/badge/platform-Switch-898c8c?logo=C.svg)](https://gbatemp.net/forums/nintendo-switch.283/?prefix_id=44)
[![language](https://img.shields.io/badge/language-C-ba1632?logo=C.svg)](https://github.com/topics/c)
[![ISC License](https://img.shields.io/badge/license-ISC-189c11.svg)](https://github.com/ppkantorski/nx-ovlloader/blob/main/LICENSE.md)
[![Latest Version](https://img.shields.io/github/v/release/ppkantorski/nx-ovlloader?label=latest&color=blue)](https://github.com/ppkantorski/nx-ovlloader/releases/latest)
[![GitHub Downloads](https://img.shields.io/github/downloads/ppkantorski/nx-ovlloader/total?color=6f42c1)](https://github.com/ppkantorski/nx-ovlloader/releases)
[![GitHub issues](https://img.shields.io/github/issues/ppkantorski/nx-ovlloader?color=222222)](https://github.com/ppkantorski/nx-ovlloader/issues)
[![GitHub stars](https://img.shields.io/github/stars/ppkantorski/nx-ovlloader)](https://github.com/ppkantorski/nx-ovlloader/stargazers)

**nx-ovlloader** is the core loader sysmodule of the [Ultrahand](https://github.com/ppkantorski/Ultrahand-Overlay) / Tesla overlay ecosystem for the Nintendo Switch. It runs as a background process at boot, maps overlay NROs into memory, and chainloads `/switch/.overlays/ovlmenu.ovl` — enabling instant overlay access from any game or application.

Derived from [nx-hbloader](https://github.com/switchbrew/nx-hbloader), it extends the original with dynamic heap sizing, HOS-version-aware defaults, exit flag signaling, and automatic self-relaunch support via [nx-ovlreloader](https://github.com/ppkantorski/nx-ovlreloader).

---

## Features

- **Overlay chainloading** — automatically loads `/switch/.overlays/ovlmenu.ovl` (Ultrahand Overlay or Tesla Menu) at boot and after each overlay exits
- **Dynamic heap sizing** — reads a persistent config at `/config/nx-ovlloader/heap_size.bin`; if none is set, selects **4 MB** by default.
- **Live heap change detection** — checks for a heap size change between every overlay load, allowing Ultrahand's System Settings to apply a new heap size without a full reboot
- **Exit flag signaling** — reads `/config/nx-ovlloader/exit_flag.bin` to allow a clean, user-requested exit rather than an automatic reload; the flag is deleted immediately after being consumed
- **Auto-relaunch via nx-ovlreloader** — on process exit, uses `pmshell` to launch [nx-ovlreloader](https://github.com/ppkantorski/nx-ovlreloader) (`0x420000000007E51B`), which respawns nx-ovlloader without a full console restart
- **Rotating NRO address strategy** — maps overlay binaries within a predictable 64 GB–256 GB window with 2 MB alignment, falling back to a random search only if the window is exhausted
- **Atomic load guard** — prevents concurrent `loadNro()` calls via an atomic flag, ensuring safe process-level re-entry
- **Cached SD filesystem handle** — the SD card filesystem is opened once and reused across all load cycles rather than reopened on every overlay switch
- **Minimal resource footprint** — one FS session, minimal directory cache, no CWD support; designed to stay out of the way of everything else running on the system

---

## Installation

### Recommended (via Ultrahand full package)

The easiest way to install nx-ovlloader is through the complete [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) `sdout.zip`. Extract it to the root of your SD card — it includes nx-ovlloader, nx-ovlreloader, `ovlmenu.ovl`, and all required folder structure.

### Manual Install

1. Download the latest release zip from [Releases](https://github.com/ppkantorski/nx-ovlloader/releases/latest) and extract it to the root of your SD card.
    - This archive includes the correct directory structure for both nx-ovlloader and nx-ovlreloader.
2. Place the overlay menu at `/switch/.overlays/ovlmenu.ovl` — either [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) or Tesla Menu.
3. Reboot your Switch. nx-ovlloader will start automatically with Atmosphère and chainload `ovlmenu.ovl`.

> **Note:** nx-ovlloader requires [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) and HOS 9.0.0 or later.

---

## File Layout

```
sdmc:/
├── atmosphere/
│   └── contents/
│       ├── 420000000007E51A/       ← nx-ovlloader sysmodule (Title ID)
│       │   ├── exefs.nsp           ← compiled sysmodule binary
│       │   ├── toolbox.json        ← sysmodule metadata
│       │   └── flags/
│       │       └── boot2.flag      ← enables auto-start at boot
│       └── 420000000007E51B/       ← nx-ovlreloader sysmodule (for on-demand reloads)
│           └── exefs.nsp
├── config/
│   └── nx-ovlloader/
│       ├── heap_size.bin           ← persistent heap size override (4 / 6 / 8 MB); written by Ultrahand
│       └── exit_flag.bin           ← runtime exit signal (transient; consumed and deleted on next load)
└── switch/
    └── Ultrahand-Reload/
        └── Ultrahand-Reload.nro    ← respawn nx-ovlloader on-demand from the hbmenu
```

---

## Building from Source

### Prerequisites

- [devkitPro](https://devkitpro.org) with `devkitA64` and `libnx`

### 1. Clone

```sh
git clone --recurse-submodules https://github.com/ppkantorski/nx-ovlloader.git
cd nx-ovlloader
```

### 2. Build

```sh
export DEVKITPRO=/opt/devkitpro
make
```

If you want to build the complete package including [nx-ovlreloader](https://github.com/ppkantorski/nx-ovlreloader), also clone it into `external/nx-ovlreloader`:

```sh
cd external/nx-ovlreloader
export DEVKITPRO=/opt/devkitpro
make
```

The Makefile auto-detects available CPU cores for parallel compilation. Output is `ovll.nsp`, packaged into `out/` with the correct Atmosphère directory structure.

### 3. Distribute (optional)

To produce a zip ready to extract to an SD card:

```sh
make dist
```

This creates `nx-ovlloader.zip` from the contents of `out/`.

---

## Contributing

Contributions are welcome. Please open an [issue](https://github.com/ppkantorski/nx-ovlloader/issues/new) or submit a [pull request](https://github.com/ppkantorski/nx-ovlloader/compare).

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/X8X3VR194) [![sponsor](https://github.com/ppkantorski/ppkantorski/raw/refs/heads/main/.pics/sponsor-icon.png)](https://github.com/sponsors/ppkantorski)

---

## License

Copyright © 2017–2018 nx-hbloader Authors

Copyright © 2020–2023 WerWolv

Copyright © 2024–2026 ppkantorski

Licensed under the [ISC License](LICENSE.md).
