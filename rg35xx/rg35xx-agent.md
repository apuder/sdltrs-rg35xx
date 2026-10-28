# RG35XX hard-disk tooling notes

This document describes the hard-disk builder, LDOS launcher, control
mappings, and StockOS deployment files used by `sdltrs-rg35xx`.
Game sources come from the `tpk/` submodule at
`tpk/src/main/resources/apps/`.

## Key files in this repository

The tooling lives in `rg35xx/`, with disk templates in `diskimages/`.
Keep those paths aligned with the references below.

| File path | Purpose and dependencies |
| --- | --- |
| `rg35xx/build.py` | Python standard-library LDOS image writer. Recursively discovers `.CMD`/`.BAS` files under `tpk/`, requires sibling `info.properties`, reads titles and custom mappings, invokes Pasmo, and produces images/catalog/manifest. |
| `rg35xx/LAUNCH.ASM` | Pasmo Z80 source for the LDOS game menu and paginated control guide. Reads `LIST/TXT:4`, transmits the selected mapping on port 41, and starts CMD or BASIC programs. |
| `rg35xx/action-map.csv` | Authoritative, manually edited custom control assignments. Rows match each game's `app.name`; columns identify TRS-80 keys/chords. Preserve the user's cells. |
| `diskimages/empty.hdv.gz` | Compressed LDOS-formatted data-disk template. Required by the builder and launcher integration tests. Expanded output is `empty.hdv`; game imports produce `games.hdv`. |
| `diskimages/ldos531-model3-boot.dsk.gz` | Compressed configured LDOS 5.3.1 Model III boot floppy. Contains XTRSHARD configuration for drive `:4`, disables the time prompt, and auto-runs `LAUNCH/CMD:4`. Required by the builder and launcher integration tests. |
| `rg35xx/XTRSHARD-LICENSE.txt` | Permission notice for the hard-disk driver in the templates. Keep it with produced disk images and launcher assets. |
| `rg35xx/TRS-80.sh` | StockOS APPS entry script. Selects Mali video, configures the physical SDL gamepad, and starts Model III with its ROM, boot floppy, and game hard disk. |
| `rg35xx/test_build.py` | Unit tests for CSV validation, compact mapping encoding, preservation of manual cells, and catalog limits/formatting. Reads the real mapping CSV as well as fixtures. |
| `rg35xx/test_launcher.py` | Optional emulator/debugger integration tests for assembled Z80 code, guide paging, parsing, CMD/BASIC handoff, and exact mapping-port output. Requires Pasmo, a debugger-capable SDLTRS executable, and a supplied Model III ROM. Some tests discover actual game sources. |
| `rg35xx/README.md` | Detailed build/deployment instructions, LDOS behavior, template provenance, disk geometry, filename aliases, limits, and historical validation. |
| `rg35xx/rg35xx-control-mapping.md` | Shared contract for CSV encoding, catalog records, the launcher control guide, and the port-41 protocol. Keep this copy aligned with the root `rg35xx-control-mapping.md`. |
| `rg35xx/rg35xx-action-map-notes.txt` | Short editing instructions and examples for custom bindings and inherited defaults. |
| `rg35xx/action-map-notes.txt` | Per-game binary-analysis evidence explaining physical TRS-80 actions, chords, and context. Reference material rather than generated mappings. |

## Game data and source discovery

The builder reads game sources from the `tpk/` submodule.
Each imported `.CMD` or `.BAS` file must have sibling `info.properties`
containing `app.name`, and that title must match a row in `action-map.csv`.

All paths in this table are relative to `tpk/src/main/resources/apps/`.
For every row, include `<directory>/info.properties`.

| Directory | Program |
| --- | --- |
| `armored_patrol` | `ARMORED.CMD` |
| `attack_force` | `ATTACK.CMD` |
| `battlestar_raven` | `RAVEN.BAS` |
| `beast` | `BEAST.CMD` |
| `breakdown` | `BREAKDWN.CMD` |
| `cosmic_fighter` | `COSMIC.CMD` |
| `dancing_demon` | `DEMON.BAS` |
| `defense_command` | `DEFENSE.CMD` |
| `defuse` | `defuse.cmd` |
| `donkey_kong` | `DONKKONG.CMD` |
| `eliminator` | `ELIMINAT.CMD` |
| `galaxy_invasion` | `GALAXY.CMD` |
| `meteor_mission` | `METMISS.CMD` |
| `missile_defense` | `DEFENSE.CMD` |
| `rear-guard` | `REARGUARD.CMD` |
| `robot_attack` | `ROBOT.CMD` |
| `rsclient` | `RSCLIENT.CMD` |
| `sea_dragon` | `SEADRAG.CMD` |
| `space_invaders_model1` | `SPACEINV.CMD` |
| `space_invaders_model3` | `SPACEINV.CMD` |
| `stellar_escort` | `STELLAR.CMD` |
| `super_nova` | `SUPNOVA.CMD` |
| `tankzone` | `TANKZONE.CMD` |
| `trek14` | `trek14.cmd` |
| `vicious-vipers` | `VIPERS.CMD` |
| `weerd` | `WEERD.CMD` |
| `zaxxon` | `ZAXXON.CMD` |

Important details:

- `source_files()` recursively scans `tpk/` for `.cmd` and `.bas` files
  (case-insensitive extensions) and requires sibling `info.properties`.
- Files outside `tpk/` are not treated as game sources.
- `game_title()` reads `info.properties` beside each source file and requires
  `app.name`.
- `--output-dir` changes only the output destination. There is no CLI option
  for an alternate game-source root.
- Original LDOS boot disks under `tpk/src/main/resources/apps/ldos_model1/`
  and `tpk/src/main/resources/apps/ldos_model3/` are not needed for normal
  builds because compressed templates in `diskimages/` supply the inputs.

## Build and runtime contract

The pipeline is game bytes + titles + manual CSV + disk templates + assembled
launcher -> `out/hard-disk/` -> APPS installation. Building images does not build
the emulator or obtain a ROM. Runtime mappings are embedded into `LIST/TXT`
inside `games.hdv`; editing the CSV requires rebuilding and deploying the disk.

The generated files are `games.hdv`, `empty.hdv`, `ldos531-model3-boot.dsk`,
`LIST.TXT`, `LAUNCH.CMD`, and `manifest.csv`. They can be regenerated and need
not be copied as source files. Existing output may be copied separately for an
immediate runtime deployment. `out/hard-disk/verification.txt` is a historical
validation artifact, not a build input or deployment requirement.

The data HDV is not bootable by itself. The configured floppy mounts hard-disk
unit 0 as LDOS drive `:4` and launches `LAUNCH/CMD:4`. The builder preserves
original game bytes; it does not convert Model I programs for Model III.
Keep the flat-namespace aliases for Defense Command/Missile Defense and the
two Space Invaders versions.

`LIST.TXT` uses ASCII CR line endings, two header lines, then title/filespec
and a TAB-separated mapping. Mappings use `CONTROL:KEY[,CONTROL:KEY...]` with
`+` for simultaneous chords on either side. Blank cells and lowercase `x`
are ignored; uppercase `X` denotes the physical X button. A blank mapping
restores defaults. Do not generate substitute assignments or rewrite the CSV.

The launcher sends the selected mapping followed by NUL to decimal I/O port
41 (`0x29`, full address `0x0029`) only when launching from the guide.
Cancel/paging sends nothing. Maximum payload is 4095 ASCII bytes, with the
terminating NUL in a 4096-byte buffer at `B000h..BFFFh`. A raw Pasmo `.BIN`
is not a runnable TRS-80 `/CMD`; `build.py` wraps it in load/transfer records.

This repository already implements the receiver in `src/trs_io.c` and
`src/rg35xx/rg35xx_bridge.cpp`, with mapping/input code under `src/rg35xx/`.
Consult `docs/rg35xx.md`, root `rg35xx-control-mapping.md`, and existing tests
when integrating or maintaining this stack. Emulator C/C++ sources do not need
copying from `tpk/`.

## Deployment and physical controls

The default target layout is:

```text
/mnt/mmc/Roms/APPS/
  TRS-80.sh
  TRS80/
    sdl2trs
    roms/model3.rom
    disks/games.hdv
    disks/empty.hdv
    disks/ldos531-model3-boot.dsk
    LIST.TXT
    LAUNCH.CMD
    manifest.csv
    XTRSHARD-LICENSE.txt
```

`TRS-80.sh` sets `SDL_VIDEODRIVER=mali`, `SDLTRS_RG35XX_INPUT=sdl`, device index
0, GUID `19000000010000000100000000010000`, and explicit button/hat/axis
assignments. These settings describe this handheld; verify them for other
models/firmware. Use normal host settings in this repository for desktop tests.
The current script redirects stdout/stderr to `/dev/null`; change that locally
when diagnostics are needed, despite its comment about capturing output.

Deployment is manual. Keep `TRS-80.sh` and its `APPDIR=` path aligned with your
target location, and copy the generated disk image files from `out/hard-disk/`
to the device with your preferred transfer method. Supply a device-compatible
ARM64 emulator build and readable ROM at the target location; this repository's
build pipeline does not produce runtime binaries or ROM files. For on-device
build details, see `scripts/build-rg35xxpro-on-device.sh` and
`docs/rg35xx-buildchain.md`.

## Device dependency verification

Verified over SSH at `192.168.1.246` on 2026-10-27 using `root` / `root`:

- Ubuntu 22.04 LTS (Jammy), ARM64/aarch64.
- Python 3.10.12 and Git 2.34.1 installed.
- All eight `build.py` imports succeeded: `argparse`, `csv`, `gzip`, `hashlib`,
  `pathlib`, `re`, `subprocess`, `tempfile`. The actual script compiled and
  loaded successfully; gzip, SHA-256, and temporary-file I/O probes passed.
- No third-party Python packages are needed for the builder.
- Pasmo was absent. Configured `jammy/universe` repositories offer ARM64
  `pasmo` version `0.5.3-7`. `apt-get -s install pasmo` succeeded and proposed
  installing only Pasmo; its runtime libraries were already installed.
- No package was installed and no full image build was run on the device.

If installation is requested, `apt-get install pasmo` runs as root on this
device. The address and package state above are observations, not permanent
configuration guarantees.

## Validation commands

Run these commands from the repository root:

```sh
python3 rg35xx/build.py
python3 -B -m unittest discover -s rg35xx -p 'test_*.py'
```

For optional launcher integration tests, set these variables for the unittest
command (use absolute executable/ROM paths):

```sh
TPK_SDLTRS=/path/to/sdl2trs TPK_TRS80_ROM=/path/to/model3.rom \
  TPK_PASMO=/path/to/pasmo \
  python3 -B -m unittest discover -s rg35xx -p 'test_*.py'
```

Without `TPK_SDLTRS` and `TPK_TRS80_ROM`, launcher integration tests are skipped.
The `TPK_` names remain the current interface even if files move repositories.
`TPK_PASMO` configures the tests; the builder uses `--assembler /path/to/pasmo`.
This repository also has emulator input/mapping tests; keep them alongside
these disk/launcher tests.

`rg35xx/README.md` describes earlier image-byte and emulator validation.
Treat those as historical results and rerun relevant checks after integration
changes.
