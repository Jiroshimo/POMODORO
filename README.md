# POMODORO

A C++17 desktop Pomodoro timer with a black interface, pixel timer, readable task text, rounded corners, and session checklists. Built with GTK 3.

## Interface

- Numeric timer; no clock face, ticking, motivational text, or empty-list messages.
- The timer and title use the bundled **Silkscreen** pixel font. Smaller controls, counters, and tasks use a clear monospace font at **13–14 px**, with brighter text for readability. No separate font installation or runtime download.
- The expanded view shows the current mode, timer, Start/Reset, session count, and task list. Mode selection, session target, durations, and audio options stay in **···** settings.
- **F11** or the **⛶** header button toggles true fullscreen. **Esc** exits fullscreen, restoring the previous expanded, maximized, or compact view. F11 also works while typing a task. The running timer and task drafts stay intact.
- Fullscreen and maximized views enlarge the timer, controls, and task area to suit the available space. The timer grows from 76 px to 96–104 px, or 160 px on larger displays. Tasks use larger, readable text in a centered column up to 680 px wide. Smaller displays keep the normal sizing so the controls fit. Window controls remain at the top; screen-filling views have square corners and no resize grip.
- Rounded window and controls, including transparent corners on composited desktops.
- **−** minimizes to a **360 × 360 square panel** with the timer, session count, task input, and checkboxes always visible.
- **↙**, **M**, or **Esc** expands the panel. **F11** opens fullscreen directly from the panel. Each view remembers its size until you close the app.
- Resize using the window edges or bottom-right grip. The square panel requests a 1:1 aspect ratio, with a minimum size of 340 × 340. The expanded view resizes freely from 420 × 560 and supports maximizing with **□**.
- The floating panel requests “Always on Top.” Some Wayland compositors manage aspect ratios and stacking themselves; use their window controls if necessary.

## Sessions and tasks

1. Open **···** settings and set **SESSIONS** to your focus-session target, **1–99**. The counter below the timer shows completed sessions against your target.
2. Under **TO DO**, use **‹ / ›** to select a session number. Add the tasks you want to do in that session using **Enter** or **+**.
3. Press **START**. The default durations are 25 minutes for focus, 5 for a short break, and 15 for a long break. Change them in **···** settings; each supports 1–180 minutes. The **FOCUS / SHORT / LONG** buttons in settings select a timer manually and pause the cycle until you press Start again.
4. Check tasks off when you finish them. Completed tasks show a checkmark and struck-through text. You can uncheck them or delete them with the row's **×**. Both views stay synchronized.
5. Finishing a focus timer marks that numbered session complete, increments the counter, and shows the next session's tasks. Completed session numbers have **✓** beside them. You can still browse earlier or future session lists.
6. After you press **START**, focus automatically continues into a short break, then back into focus. Every fourth completed focus session starts a long break instead. You do not need to press Start at each transition. **PAUSE** stops the current countdown and automatic progression until you resume; **Reset** stops the loop and restores the current phase's full duration.
7. When the final focus session reaches your target, the timer stops at **00:00**, without starting another break, and the button becomes **NEW PLAN**. Click it to reset the session count and uncheck the tasks while keeping their text, session assignments, target, and duration settings. Settings also has a **NEW PLAN** button for restarting earlier.

Completing a timer marks the **session**, while task checkboxes remain under your control. Increasing the target extends the plan; lowering it caps the completed count at the new target. Tasks assigned to higher session numbers stay saved and reappear if you increase the target again.

Changing the task list you are viewing does not change the running session. Draft task text is shared between the two views and kept separately for each session while the app is open; press Enter to save a task before closing.

## Audio

A cue plays with **10 seconds remaining**, followed by a completion chime at each focus/break transition and at the end of the plan. The chimes have been raised in volume while retaining a soft fade. Enable or disable **AUDIO** and preview the chime with **TEST SOUND** in settings. No audio files are downloaded.

Choose **Automatic output**, **Windows speakers**, or **Linux / WSLg audio** in settings. The choice is saved and takes effect on the next chime or preview. This is a preferred output: the interface tries the other WSL output if the preferred one fails, so a previously saved Windows selection cannot disable recovery.

- **Automatic on WSL:** tries Windows' default sound output through PowerShell and WinMM first. If startup or playback fails, it tries WSLg's PulseAudio output. The Windows bridge writes a private temporary WAV, passes its Windows path as encoded data, and deletes it after playback or failure. It no longer streams the WAV into PowerShell's stdin, which could produce a broken pipe and hide the underlying startup error.
- **Linux / WSLg audio:** uses the default PulseAudio output, including PipeWire compatibility and [WSLg's audio forwarding](https://github.com/microsoft/wslg). Native Linux also supports ALSA as a fallback. This route requires the `libpulse0` runtime on Ubuntu/Debian.
- **Windows speakers:** uses WinMM directly on Windows, or the PowerShell bridge on WSL. Windows interop and `powershell.exe` must be available in WSL.

**TEST SOUND** shows **Playing...**, followed by the output that actually accepted the chime. If all routes fail, it shows **Sound unavailable. See error details.** Click **AUDIO ERROR DETAILS** to view the full failure and copy it with **COPY DETAILS**. Errors are also printed to the launching terminal. A **!** in the header indicates audio or saving errors. Windows must acknowledge a successful WinMM playback call; simply exiting with status zero is insufficient. Completion cues queue behind an active preview or warning.

Close the old app and test the rebuilt version from your normal WSL terminal:

```bash
./run.sh --test-sound
```

This uses the saved output preference with recovery, matching the interface, and returns a nonzero status on failure. To diagnose either output directly without fallback, specify it explicitly:

```bash
./run.sh --test-sound --output=windows
./run.sh --test-sound --output=linux
./run.sh --test-sound --output=auto
```

On Ubuntu/Debian, `./run.sh` checks for the PulseAudio runtime and supplies missing libraries inside `build/audio-runtime`. It uses packages and SHA256 hashes from the system apt metadata, requires no administrator access, and changes no system packages. Once ready, subsequent launches use the local files without downloading again. CMake builds and optional installs look for these libraries beside the app.

If local setup cannot download a package, you can install the runtime from your normal terminal instead:

```bash
sudo apt install libpulse0
```

If Windows interop cannot start, `powershell.exe -NoProfile -Command 'Write-Output OK'` should show the startup error independently of the app. If an output accepts the chime but it remains silent, check **Windows Settings → System → Sound → Volume mixer**, the selected playback device, and the application's volume. Playback acceptance cannot establish that sound reached your speakers. The app does not change system volume or devices.

## Run on Linux

Requires a C++17 compiler, CMake 3.16+, pkg-config, GTK 3 development files, and Fontconfig development files. On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev libfontconfig1-dev
```

From the project folder:

```bash
./run.sh
```

Or build and launch directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/pomodoro
```

Launch from a graphical desktop session. For **Linux audio and WSLg fallback**, install the PulseAudio runtime; no PulseAudio development package is required. WSL can also use Windows directly without it:

```bash
sudo apt install libpulse0
```

For an ALSA-only native Linux setup, the optional ALSA output plugin is:

```bash
sudo apt install libasound2-plugins
```

Optional installation into your account, including a desktop launcher:

```bash
cmake --install build --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is in your `PATH` for the launcher. The stylesheet and font are embedded in the executable.

## Run on Windows

Windows source support includes private font registration and native audio. **The Windows build has not been verified in this Linux workspace.**

Install [MSYS2](https://www.msys2.org/), open its **UCRT64** terminal, and install:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3
```

Navigate to the project folder, then:

```bash
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
./build-windows/pomodoro.exe
```

After installing the dependencies, you can also double-click **run-windows.bat**. It builds the app and sets up the GTK runtime path for launching. It expects MSYS2 at `C:\msys64`; set `MSYS2_ROOT` if yours is elsewhere.

The executable needs GTK and its runtime dependencies to run on another PC. See the official [GTK Windows setup](https://www.gtk.org/docs/installations/windows/) and [MSYS2 environments](https://www.msys2.org/docs/environments/) documentation.

## Controls

| Action | Control |
| --- | --- |
| Start / pause | **Space** or **START / PAUSE** |
| Reset current timer | **R** or **↺** |
| Restart a completed plan | **NEW PLAN**, **R**, or **↺** |
| Minimize to square panel | **−** or **M** |
| Expand square panel | **↙**, **M**, or **Esc** |
| Maximize / restore expanded view | **□** |
| Enter / exit fullscreen | **F11** or **⛶**; **Esc** exits |
| Change mode, session target, durations, or audio | **···** settings |
| Resize | Drag edges or the bottom-right grip |
| Move window | Drag header or timer numbers |
| Toggle square panel with mouse | Double-click header or timer numbers |
| Browse session tasks | **‹ / ›** or **Left / Right** |
| Add a task | **Enter** in the task field or **+** |
| Mark / unmark task | Click its checkbox |
| Hide to taskbar | **Ctrl+H** or **HIDE** in settings |
| Close | Header **×** or **Ctrl+Q** |

Timer and session shortcuts stay inactive while typing or editing number fields; fullscreen controls remain available. When a checkbox has keyboard focus, **Space** toggles it. **Esc** also dismisses settings.

## Saving and timing

Tasks, their session assignments and completion marks, session target and progress, durations, sound preference, and audio output save automatically on change. Existing task lists from version 1.0 move into **session 01**, preserving their checkmarks.

The data file remains:

- Linux: `~/.config/quiet-pomodoro/state.ini`, or under `$XDG_CONFIG_HOME` when set.
- Windows: normally `%LOCALAPPDATA%\quiet-pomodoro\state.ini`.

Use one app instance at a time. Delete this file with the app closed to reset saved data. A plan's progress persists across restarts and date changes. Reopening prepares a fresh focus timer for the next unfinished session; an in-progress countdown is not restored.

Timing uses elapsed time, including system sleep on Linux and Windows. UI refresh delays do not accumulate as countdown drift. Resuming the PC after a deadline records one completion and starts a full next phase, unless the session target has been reached. It does not count additional unattended sessions for the time spent asleep. The app must be running to play audio; hiding or shrinking it keeps it running, while closing stops it.

## Performance

- Timer callbacks run only while the automatic cycle is active and stop when paused, reset, or the session target is reached.
- Numeric labels change only when their displayed values change.
- Task lists rebuild only when their contents or selected session change. Checking a task updates the existing rows in both views.
- Audio is synthesized and played through a bounded queue on a worker thread, which sleeps when idle. WSL helper processes have a timeout.
- Rounded window masks are recalculated only when the window size changes.
- The app makes no web requests and runs no background service or continuous file polling. The Linux launcher may download missing audio libraries during initial setup.

## Verification

```bash
ctest --test-dir build --output-on-failure
```

Seventeen Linux test cases cover timer accuracy, automatic phase transitions, pause/reset and session targets, plan bounds, task storage and migration, audio output persistence, PCM/WAV data, queued audio, playback error/retry handling, native PulseAudio loading/write/drain, and embedded assets. WSL regressions verify both warning and completion WAVs, Windows routing, early helper startup errors, playback acknowledgments, automatic WSLg recovery, recovery from either preferred output, strict diagnostic output selection, explicit output changes, combined failures, and temporary paths containing spaces, apostrophes, and Unicode. Those audio tests use test helpers and do not play through speakers or execute the Windows API. When the local runtime is present, an eighteenth test loads the real client with a deliberately missing private server socket. This verifies the bundled dependency loading without sending sound to any speaker.

An optional desktop smoke test checks task synchronization and deletion, per-session filtering, progress persistence, automatic focus/break transitions, stopping at the target, idle timer shutdown, square compact geometry, visible compact tasks, and resizing both views. It also checks that the working area is centered on both axes and that the timer and tasks share a center line after expanding and maximizing. It also checks F11 with an entry focused, Escape, fullscreen scaling, restoration of normal and compact sizes, and preservation of the timer and task draft. It writes `docs/main.png`, `docs/compact.png`, `docs/expanded.png`, and, when supported by the display, `docs/maximized.png` and `docs/fullscreen.png`:

```bash
QUIET_SMOKE_CONFIG="$(mktemp -d)" ./build/pomodoro --smoke-test
```

Run it from the project root with a graphical display and a fresh isolated configuration directory. It does not play audio. Manually set focus and short break to one minute to check the ten-second cues, completion chimes, and automatic focus → break → focus cycle, plus dragging and resizing with your desktop compositor.

The Linux release build and automated tests pass in the development workspace. The workspace blocks Windows interop and desktop/audio socket access, while the PulseAudio client runtime is supplied locally in the build directory. Actual audibility and desktop fullscreen behavior must therefore be checked from a normal WSL terminal with your graphical session. These live checks have not been verified here.

## Source and font license

```text
src/main.cpp       Desktop interface, session controls, window behavior
src/timer.hpp      Elapsed-time countdown and break cycle
src/plan.hpp       Session target and completed-session progress
src/storage.*      Saved tasks and preferences; version 1.0 migration
src/font.*         Private process font registration
src/audio.*        Queued playback; PulseAudio, ALSA, Windows/WSL output
src/audio_samples.hpp  Synthesized tones and portable PCM WAV data
src/windows_audio_script.hpp  Embedded Windows WinMM bridge
tools/setup_audio.py  Local Ubuntu/Debian audio runtime setup
assets/style.css   Pixel interface styles
assets/            Embedded font, its license, desktop launcher, icon
tests/             Timer, plan, storage, audio, and asset verification
```

[Silkscreen](https://github.com/googlefonts/silkscreen) is by the Silkscreen Project Authors, licensed under the SIL Open Font License 1.1. The unmodified font and full license are included in `assets/Silkscreen-Regular.ttf` and `assets/Silkscreen-OFL.txt`, and embedded in the executable. It is registered only for the app's process and is not installed system-wide.
