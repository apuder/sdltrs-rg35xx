# RG35XX host controls and soft keyboard

This checkout contains both the host-keyboard RG35XX input simulator and a raw
SDL2 physical-device backend. The feature is off by default. The physical
backend requires the verified numerical mapping for the particular handheld;
no device button numbers are compiled into the emulator.

## Enable it

Build SDL2TRS normally, then prefix the normal launch command with
`SDLTRS_RG35XX_INPUT=keyboard`. For example, for a Model I ROM:

```sh
SDLTRS_RG35XX_INPUT=keyboard ./sdl2trs -m1 -romfile1 /path/to/level2.rom
```

`SDLTRS_RG35XX_INPUT=off` and an unset variable preserve the original input
path. `SDLTRS_RG35XX_INPUT=sdl` selects the physical backend described below.
Any other nonempty value prints an error and leaves RG35XX input disabled. Do
not set `SDL_VIDEODRIVER=mali` on a development host.

## Physical SDL2 controls

The physical backend consumes the `SDL_JOY*` events already delivered to
`trs_get_event()`; it does not poll SDL or open a competing event loop. Supply
the numerical mapping that was observed on the device:

```sh
SDLTRS_RG35XX_INPUT=sdl \
SDLTRS_RG35XX_DEVICE=0 \
SDLTRS_RG35XX_MAP="$RG35XX_MAP" \
./sdl2trs -m1 -romfile1 /path/to/level2.rom
```

`RG35XX_MAP` above must contain a comma-separated `logical=source` list. Source
numbers are decimal SDL raw-joystick indices:

| Logical names | Source syntax |
| --- | --- |
| `a,b,x,y,start,select,menu,l1,r1,l2,r2,ls,rs` | `bN` for SDL button N |
| `up,down,left,right` | `bN`, or `hNu`, `hNd`, `hNl`, `hNr` for hat N |
| `lx,ly,rx,ry` | `aN` for SDL axis N; `-aN` reverses it |

For example, the shape of a complete mapping is
`a=bA_INDEX,b=bB_INDEX,...,up=hHAT_INDEXu,...,lx=aLX_INDEX,ly=aLY_INDEX`.
The capitalized portions are placeholders for the values measured on the
handheld, not literal values. At minimum, define A, B, and either all four
D-pad directions or both left-stick axes. Duplicate physical sources and
malformed or incomplete required mappings are rejected with a diagnostic. On
connection, every mapped index is also checked against the button, hat, and
axis counts reported by that SDL joystick.

`SDLTRS_RG35XX_DEVICE` defaults to raw joystick index 0. Optionally set
`SDLTRS_RG35XX_GUID` to the exact 32-hex-digit GUID printed by the connection
diagnostic; when present, the GUID takes precedence over the index. After the
first successful index-based connection, the backend
remembers that GUID for reconnects. Events are filtered using the SDL joystick
instance ID; removal releases held controls, and reconnect suppresses controls
already held until they return to neutral.

Only physical A is reserved while the overlay is hidden. Other physical
events continue into SDLTRS's existing joystick handling. While the overlay is
visible, mapped controls are consumed by it, including the matching releases
of controls pressed before it closed. A gameplay control already held when A
opens the overlay remains gated from the overlay and its release is passed back
to the legacy handler, preventing a stuck legacy input. The backend handles raw
joystick events only and does not also interpret SDL controller events.

## Simulator controls

These are SDL scancode bindings, so they follow physical key positions rather
than translated text:

| Logical control | Host key |
| --- | --- |
| A | F6 |
| B | Space |
| X | Tab |
| Y | Backspace |
| D-pad | Arrow keys |
| Left stick | W/A/S/D (up/left/down/right) |
| Right stick | I/J/K/L (up/left/down/right) |
| L1 / R1 | Q / E |
| L2 / R2 | 1 / 3 |
| Left / right stick click | C / V |
| Start / Select | Return / Escape |
| Menu | F5 |

Simulator keys are reserved while this mode is enabled. While the overlay is
hidden, only logical A has an application action; it opens the keyboard.

While the overlay is open:

| Control | Action |
| --- | --- |
| A | Close the overlay |
| D-pad or left stick | Move between keycaps |
| B down/up | Press/release the selected TRS-80 key |
| X | Toggle one-shot Shift |
| Y down/up | Press/release the TRS-80 left-arrow editing key |
| Start down/up | Press/release TRS-80 ENTER |
| Select | Close without typing |

Navigation moves immediately, repeats after 300 ms, then repeats every 90 ms.
Shift applies to the next nonmodifier activation and clears when that key is
released. Selection persists across an ordinary close and reopen. Controls
already held when the keyboard opens must be released before they act.

## Architecture and integration

- `src/rg35xx/RG35XXControls.*` defines the physical control/event vocabulary.
- `KeyboardRG35XXControls.*` translates the centralized host scancode map and
  never polls SDL itself.
- `SDLRG35XXControls.*` owns the selected raw SDL joystick, validates the
  explicit mapping, translates buttons/axes/hats, filters instance IDs, and
  handles disconnect/reconnect.
- `RG35XXInputRouter.*` owns mode routing, gating, navigation, repeat timing,
  Shift state, and lifecycle cleanup.
- `TrsKeyboardAdapter.*` validates symbols and reference-counts guest keys by
  input owner so a soft-key release cannot prematurely release the same host
  keyboard key.
- `SoftKeyboard.*` uses the existing 4x13 TRS-80 key definitions and computes
  nearest-center navigation for variable-width keycaps.
- `SoftKeyboardRenderer.*` composites a translucent panel after the guest
  texture and restores the SDL draw color/blend state.
- `rg35xx_bridge.*` is the exception-safe C ABI boundary used by the C frontend.
- `trs_sdl_keymap.*` is the shared source of truth for the old modal keyboard
  and the overlay.

`trs_get_event()` remains the sole nonmodal SDL queue owner. Reserved input is
routed before legacy keyboard/joystick handling and logical transitions are
drained after each raw event. `trs_screen_update()` draws the overlay after the
guest texture and before `SDL_RenderPresent()`. Overlay dirty state can request
a presentation without reapplying in-place scanline processing to a static
guest frame.

Modal GUI entry, clipboard paste, reset, state load, focus loss, and shutdown
release owned keys and reset latches/repeat state. Modal code retains its own
event loop; simulator interception is suspended until it returns.

## Build and test

A C++11 compiler is now required in addition to the existing C compiler. C
files remain C, C++ files are built separately, and the final executable is
linked with the C++ driver/runtime.

Direct Make route (also the console-oriented route):

```sh
make -C src -B sdl2
make -C src test-rg35xx
```

Autotools route:

```sh
autoreconf -fi
./configure
make -B -j2
make check
```

CMake remains supported in `CMakeLists.txt`, including the `rg35xx_tests`
CTest target, but it was not run on the audited host because CMake was absent.

For an AddressSanitizer/UndefinedBehaviorSanitizer logic run on Clang, the
project test was compiled with `-fsanitize=address,undefined` and
`-DRG35XX_TEST_NO_RENDERER -DRG35XX_TEST_NO_BRIDGE` plus
`-DRG35XX_TEST_TRANSLATOR_ONLY`; SDL rendering, the C bridge, and SDL-dependent
device ownership code remain compiled in the normal test target, while live
device ownership requires the handheld. The exact sanitizer commands were:

```sh
mkdir -p /tmp/sdltrs-rg35xx-sanitize
cc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  $(sdl2-config --cflags) -c src/trs_sdl_keymap.c \
  -o /tmp/sdltrs-rg35xx-sanitize/trs_sdl_keymap.o
c++ -std=c++11 -DSDL_MAIN_HANDLED -DRG35XX_TEST_NO_RENDERER \
  -DRG35XX_TEST_NO_BRIDGE -DRG35XX_TEST_TRANSLATOR_ONLY \
  -g -O1 -Wall -Wextra \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  $(sdl2-config --cflags) tests/rg35xx_tests.cpp \
  src/rg35xx/RG35XXControls.cpp \
  src/rg35xx/KeyboardRG35XXControls.cpp \
  src/rg35xx/RG35XXInputRouter.cpp src/rg35xx/SoftKeyboard.cpp \
  src/rg35xx/SDLRG35XXControls.cpp \
  src/rg35xx/TrsKeyboardAdapter.cpp \
  /tmp/sdltrs-rg35xx-sanitize/trs_sdl_keymap.o \
  -o /tmp/sdltrs-rg35xx-sanitize/rg35xx_logic_tests
ASAN_OPTIONS=detect_leaks=0 \
  /tmp/sdltrs-rg35xx-sanitize/rg35xx_logic_tests
```

Leak detection was disabled because this macOS sanitizer runtime reports it as
unsupported.

## Validation record (2026-10-22)

Original audited revision: branch `sdl2`, commit
`446e9bc867f7bf366ceffbd00a74419beafc3dc2`. The physical-backend work was
performed on branch `rg355xx`, based on host-overlay commit `8393192c`.

Host tools: Apple Clang 21.0.0 C/C++, GNU Make 3.81, Autoconf 2.73,
Automake 1.19, SDL 2.32.74. CMake was not installed.

Initial repository state had no tracked edits. The supplied task/handoff files,
Autotools output, build products, `var/`, and dependency directories were
untracked and were preserved.

Results:

- Baseline `make -C src -B sdl2`: passed.
- Baseline configured `make -B -j2`: passed.
- Updated `make -C src -B sdl2`: passed.
- Updated `make -C src test-rg35xx`: passed.
- Updated `autoreconf -fi && ./configure && make -B -j2 && make check`:
  passed; 1 test program, 1 pass.
- Address/undefined sanitizer logic suite, including the physical SDL mapping
  and event translator: passed with no findings.
- SDL software-renderer test at 640x480 and 1024x768/logical 800x600: passed,
  including alpha composition, an unchanged source surface, clean redraw after
  close, safe bounds, and draw color/blend restoration.
- The integrated Autotools binary started and continued running with the
  user-owned Model I ROM under SDL's dummy driver in simulator mode. It was
  stopped after startup; this does not verify visible composition or typing.
- The integrated direct-Make binary also started with the same ROM under the
  dummy driver in physical SDL mode, reported that it was waiting for a
  joystick, and continued running. Missing hardware is therefore nonfatal.
- The unbundled native macOS SDL window was not exposed to the available UI
  automation inventory, so the visual/guest-typing walkthrough was not marked
  complete.
- No physical RG35XX, SDL 2.0.12, or Mali test was performed.

The test suite also covers physical mapping validation, instance filtering,
button/hat/axis translation, axis inversion, disconnect-style release/rearm,
and the hidden-overlay joystick passthrough policy without requiring hardware.

The direct and Autotools builds share object paths. When switching routes with
different feature flags, use `-B` (as above) or the appropriate clean target so
objects compiled with one route are not reused by the other.

## Repeatable BASIC smoke test

Use a user-owned Model I ROM and its normal startup options:

1. Launch with `SDLTRS_RG35XX_INPUT=keyboard` as shown above.
2. Press F6 and confirm the translucent keyboard appears while the guest keeps
   running.
3. Use both arrows and WASD to move. Use Space to select keys and enter
   `PRINT "HELLO"`, then press Return (logical Start) for ENTER.
4. Check one-shot Shift with Tab and shifted punctuation. Check Backspace as
   the TRS-80 left-arrow editing key, plus the CLEAR and BREAK keycaps.
5. Hold Space on a normal key, close or change focus, then confirm no guest key
   remains stuck.
6. Enter and leave the existing F7 menu and exercise the ordinary quit path.
7. Relaunch with the environment variable unset or set to `off` and confirm the
   original keyboard and joystick behavior.

For the physical backend, repeat the same typing test with
`SDLTRS_RG35XX_INPUT=sdl` and the verified `SDLTRS_RG35XX_MAP`. Confirm the
startup diagnostic names the intended device and GUID. With the overlay
hidden, verify that B and directional events still reach the legacy joystick
path while A alone opens the overlay. With it open, verify the printed A/B,
D-pad, and left stick follow the documented overlay actions. Finally,
disconnect and reconnect the controls and confirm that held buttons neither
stick nor activate until released.

Keep ROMs, disk images, firmware, generated binaries, and local launch settings
outside version control.

## Remaining hardware validation

The physical `SDLRG35XXControls` adapter is implemented, but the device's
verified identity and numerical button/axis/hat mapping are deliberately not
embedded because they were not supplied in this checkout. A handheld run must
provide those values and verify the overlay, guest typing, legacy joystick
passthrough, hotplug behavior, SDL 2.0.12, and Mali presentation. Deployment
must also confirm an available C++ compiler or a compatible
cross-toolchain/runtime and use the existing stock APPS launcher.
