<div align="center">

# POMODORO

**A minimal desktop Pomodoro timer built with C++17 and GTK 3.**

Focus on your work, organize tasks by session, and keep your timer visible while you study or code.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white)
![GTK](https://img.shields.io/badge/GUI-GTK%203-4A86CF)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL%20%7C%20Windows-lightgrey)

</div>

## Overview

POMODORO is a lightweight productivity app with a distraction-free black interface, a pixel-style countdown, and checklists for individual focus sessions. It combines a familiar Pomodoro cycle with a session plan so you can see not only _how long_ you're working, but also _what_ you're working on.

The app runs locally, automatically saves your plan, and does not require an account or a web service.

## Preview

<!-- Add screenshots to docs/ after capturing them. The smoke test can create these files. -->

If screenshots are available in this repository, see [`docs/`](docs/). You can generate application screenshots using the desktop smoke test described under [Testing](#testing).

## Features

- **Pomodoro cycle:** 25-minute focus, 5-minute short break, and 15-minute long break by default. A long break follows every fourth completed focus session.
- **Customizable plans:** Set 1–99 focus sessions and configure focus/break durations from 1–180 minutes.
- **Per-session tasks:** Add, complete, uncheck, or delete tasks for each session. Browse other session lists without interrupting the running timer.
- **Automatic transitions:** Once started, the timer progresses between focus and breaks until the plan is complete. Pause or reset whenever needed.
- **Multiple window modes:** Expanded, resizable, fullscreen, and a compact 360 × 360 floating panel (subject to desktop/window-manager support).
- **Sound notifications:** A warning at 10 seconds remaining and a completion chime, with selectable Windows or Linux/WSLg output.
- **Auto-save:** Stores your session progress, task lists, timing preferences, and audio settings across launches.
- **Offline-friendly:** Core functionality is local, with no account, cloud sync, or continuous background service.

## Tech Stack

| Component                | Technology                                              |
| ------------------------ | ------------------------------------------------------- |
| Language                 | C++17                                                   |
| Desktop UI               | GTK 3                                                   |
| Build system             | CMake 3.16+                                             |
| Styling                  | CSS, bundled Silkscreen and DejaVu Sans Mono fonts       |
| Audio                    | PulseAudio / ALSA (Linux), WinMM (Windows), WSL interop |
| Supported source targets | Linux, WSLg, Windows via MSYS2                          |

> **Platform note:** Linux and native Windows release builds have been verified. The Windows installation passed all five native automated tests and the desktop smoke test, including font selection, compact mode, resizing, and fullscreen. Windows accepted the test chime through WinMM; actual audibility depends on your speakers, headphones, and volume settings.

## Getting Started

### Linux / Ubuntu / Debian (including WSLg)

**1. Install build dependencies**

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libgtk-3-dev libfontconfig1-dev
```

For Linux/WSLg audio, also install:

```bash
sudo apt install libpulse0
```

**2. Open the project directory**

If you cloned the repository, `cd` into the folder containing `run.sh` and `CMakeLists.txt`.

**3. Run the application**

```bash
./run.sh
```

Or compile it manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/pomodoro
```

A graphical Linux desktop or WSLg session is required to display the interface. The Linux launcher can also provision a local PulseAudio runtime if the system library is missing.

### Windows native installation

Install [MSYS2](https://www.msys2.org/) at `C:\msys64`, then double-click **`install-windows.bat`** in this project. The installer updates MSYS2, installs the build dependencies, builds the Windows app, and runs its automated tests. Set `MSYS2_ROOT` first if MSYS2 is installed elsewhere.

The app is installed for your Windows account at `%LOCALAPPDATA%\Programs\QuietPomodoro\pomodoro.exe`, with **Quiet Pomodoro** shortcuts on the Desktop and in the Start Menu. The shortcuts open the native `.exe` directly without a terminal. GTK and the required runtime DLLs are bundled beside the app; launching the installed app does not need WSL or MSYS2.

The Windows build requires Pango 1.56 or newer (included with current MSYS2 packages). It loads the bundled Silkscreen timer font and DejaVu Sans Mono text font directly into Pango's font map, so GTK uses the same faces as WSL without installing fonts system-wide.

To reinstall after source changes, close the Windows app and run `install-windows.bat` again. Saved tasks and preferences are kept separately in `%LOCALAPPDATA%\quiet-pomodoro\state.ini`.

For a build with dependencies already installed, or to import existing settings only when Windows has no saved state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\install_windows.ps1 -Launch
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\install_windows.ps1 -StateFile "C:\path\to\state.ini"
```

The installer also works from a WSL source folder: invoke `powershell.exe` with the script's Windows UNC path (`wslpath -w tools/install_windows.ps1`). Compilation takes place in a temporary Windows folder.

### Windows development (MSYS2)

1. Install [MSYS2](https://www.msys2.org/) and open the **UCRT64** terminal.
2. Install the required packages:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3
```

3. In the project directory, build and run:

```bash
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
./build-windows/pomodoro.exe
```

Alternatively, `run-windows.bat` builds and launches the app after dependencies are installed. It assumes MSYS2 is located at `C:\msys64`; otherwise, set `MSYS2_ROOT`.

The executable needs GTK runtime dependencies if moved to another PC. See [GTK's Windows installation guide](https://www.gtk.org/docs/installations/windows/).

## How to Use

1. Open **··· Settings** and choose your focus-session target and timer durations.
2. Under **TO DO**, choose a session using **‹ / ›** and add tasks with **Enter** or **+**.
3. Press **START** to begin your focus timer.
4. Mark individual tasks as complete as you work. The timer will move automatically to the appropriate break.
5. When you complete your final focus session, select **NEW PLAN** to restart the session count while keeping your task text and settings.

> Completing a timer marks the **session** as finished; tasks are marked complete only when you check them off yourself.

## Keyboard Shortcuts

| Shortcut         | Action                                                    |
| ---------------- | --------------------------------------------------------- |
| `Space`          | Start / pause timer (when not editing a field)            |
| `R`              | Reset the current timer or restart a completed plan       |
| `M`              | Toggle compact / expanded view                            |
| `F11`            | Toggle fullscreen                                         |
| `Esc`            | Exit fullscreen, dismiss settings, or expand compact view |
| `Left` / `Right` | Browse session task lists                                 |
| `Enter`          | Add a task while editing the task field                   |
| `Ctrl + H`       | Hide to taskbar                                           |
| `Ctrl + Q`       | Close application                                         |

The interface also offers buttons for these actions. On some Wayland desktops, always-on-top behavior and requested window dimensions are controlled by the compositor.

## Audio & Troubleshooting

Audio cues are generated in the app; no separate audio files are downloaded. Choose **Automatic**, **Windows speakers**, or **Linux / WSLg audio** in settings and press **TEST SOUND**.

From a WSL/Linux terminal, test the saved output preference:

```bash
./run.sh --test-sound
```

To test a specific output without fallback:

```bash
./run.sh --test-sound --output=windows
./run.sh --test-sound --output=linux
./run.sh --test-sound --output=auto
```

On WSL, automatic playback tries the Windows output first, then WSLg PulseAudio if needed. If no output works, open **AUDIO ERROR DETAILS** in the app or review terminal output. Windows audio bridging requires working `powershell.exe` interop; Linux/WSLg audio needs the PulseAudio runtime. Audio may also be muted at the operating-system level.

## Saving Your Progress

Session tasks, completion status, plans, duration settings, and audio preferences are automatically saved to:

| System      | Saved state                                                                           |
| ----------- | ------------------------------------------------------------------------------------- |
| Linux / WSL | `~/.config/quiet-pomodoro/state.ini` (or `$XDG_CONFIG_HOME/quiet-pomodoro/state.ini`) |
| Windows     | `%LOCALAPPDATA%\quiet-pomodoro\state.ini`                                             |

The next unfinished session is prepared when the app reopens, but an active countdown itself is not resumed. Use one running instance at a time. To reset saved data completely, close the app and delete its `state.ini` file.

## Project Structure

```text
src/
  main.cpp                  # Desktop interface and window behavior
  timer.hpp                 # Countdown and Pomodoro phase cycle
  plan.hpp                  # Session target and progress
  storage.*                 # Tasks, settings, and persistence
  font.*                    # App-local font registration
  audio.*                   # Queued playback and audio outputs
  audio_samples.hpp         # Generated tones / WAV data
  windows_audio_script.hpp # Windows audio bridge
assets/
  style.css                 # Interface styling
  ...                       # Font, license, icon, desktop launcher
tests/                       # Automated tests
tools/setup_audio.py         # Linux audio runtime setup
run.sh                       # Linux / WSL launcher
run-windows.bat               # Windows launcher
install-windows.bat           # Native Windows installation and shortcuts
tools/install_windows.ps1     # Per-user Windows installer
tools/build_windows.sh        # Windows build, tests, and runtime packaging
CMakeLists.txt                # CMake build configuration
```

## Testing

Run automated tests after building:

```bash
ctest --test-dir build --output-on-failure
```

Tests cover timer behavior, phase transitions, session plans, persistence, audio handling, and embedded assets. An optional graphical smoke test checks window layouts, tasks, and UI behavior:

To verify the installed Windows package, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_windows.ps1
```

This checks native audio, desktop behavior, and both shortcuts with WSL/MSYS2 runtime paths removed. It uses an isolated test profile and keeps logs and screenshots in the Windows temporary folder.

For the Linux graphical smoke test:

```bash
QUIET_SMOKE_CONFIG="$(mktemp -d)" ./build/pomodoro --smoke-test
```

Run this from the project root within a graphical session. The smoke test may generate preview screenshots such as `docs/main.png`, `docs/compact.png`, and `docs/expanded.png` (plus additional views where supported).

## Credits

The app includes [Silkscreen](https://github.com/googlefonts/silkscreen) by the Silkscreen Project Authors, licensed under the **SIL Open Font License 1.1**. The bundled font and its license are located at `assets/Silkscreen-Regular.ttf` and `assets/Silkscreen-OFL.txt`.

The app also includes [DejaVu Sans Mono](https://dejavu-fonts.github.io/) for readable task text across platforms. Its Bitstream Vera license and DejaVu public-domain notice are included in `assets/DejaVu-LICENSE.txt`.

## License

No license for the application source code is specified in the provided project documentation. Add a root `LICENSE` file if you intend to publish the source under an explicit license. The bundled fonts retain their separate licenses listed above.
