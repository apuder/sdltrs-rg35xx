# TRS-80 game hard disk

From the repository root, run:

```sh
python3 rg35xx/build.py
```

This creates `out/hard-disk/` containing:

| File | Contents |
| --- | --- |
| `games.hdv` | LDOS-formatted hard disk containing all discovered `.CMD` and `.BAS` game source files under `tpk/`, plus `LIST/TXT` and `LAUNCH/CMD` |
| `LIST.TXT` | Alphabetical game titles (`app.name`), LDOS filenames, RG35XXPRO control mappings, and optional RG35XXPRO control hints; also stored on the disk as `LIST/TXT` |
| `LAUNCH.CMD` | Z80 scrolling game launcher; also stored on the disk as `LAUNCH/CMD` |
| `empty.hdv` | The same formatted hard disk without the games or catalog |
| `ldos531-model3-boot.dsk` | LDOS 5.3.1 boot floppy with the hard disk driver already configured |
| `manifest.csv` | DOS filename, repository path, byte count and SHA-256 for every imported file |

The build dependencies are Python 3 and the **Pasmo** Z80 assembler
(`sudo apt install pasmo` on Debian/Ubuntu). Pass
`--assembler /path/to/pasmo` if it is not on your PATH.
The build assembles `LAUNCH.ASM` on every run and wraps the machine code in
TRS-80 CMD load records with a transfer address of `5200h`.
Files are copied directly on the
host, without running an emulator, changing BASIC tokenization, or converting line
endings. Game sources are discovered under `tpk/` by recursively scanning
for `.CMD` and `.BAS` files that have a sibling `info.properties`.
Extensions are matched without regard to case. Rebuilding replaces the
generated images. The compressed disk templates are read from
`diskimages/empty.hdv.gz` and `diskimages/ldos531-model3-boot.dsk.gz`. Use
`--output-dir /another/directory` to choose a different destination.

## Use with sdltrs

Run from the repository root, supplying your Model III ROM:

```sh
sdl2trs -model 3 -romfile3 /path/to/model3.rom \
  -disk0 out/hard-disk/ldos531-model3-boot.dsk \
  -hard0 out/hard-disk/games.hdv
```

Some installations name the executable `sdltrs`. The ROM is not included.
The floppy automatically runs `LAUNCH/CMD:4` after boot, with the time prompt
disabled. The explicit drive avoids searching other drives for the launcher.
Press BREAK (Escape in sdltrs) in the launcher to reach `LDOS Ready`.
The hard disk is the
LDOS logical drive `:4` (emulator hard disk unit 0). No utility disk is needed.
The hard disk itself is a data disk and is not independently bootable.

At `LDOS Ready`:

```text
LAUNCH/CMD:4
```

Use the **Left DPAD** to select a game, **A** to view its control overview,
and **B** to exit to LDOS. On the overview, **A** launches, **B** returns to
the game list, and **right-stick click** advances through pages and wraps.
The game-list footer reads `DPAD: Choose game     A: Select     B: Exit`.
The mapping-screen footer reads `A: Launch     B: Back`; its penultimate line
is blank. Right-stick click still pages, with no on-screen paging hint.
Launching sends the complete zero-terminated compact mapping to decimal I/O
port 41 (`29h`) before handing off to the game. The emulator/frontend must
implement the port receiver to apply it.

The table headings are `CONTROL` and `TRS-80 KEY/DESCRIPTION`. The overview shows four default entries: `Left DPAD | Arrow keys`,
`A | ENTER`, `B | BREAK`, and `X | CLEAR`, followed by game-specific custom assignments.
Multi-key action columns are simultaneous: Missile Defense's `R2:LEFT+RIGHT`
holds both arrow keys together. `+` can join any combination of supported
letters, digits, and control keys, within the mapping-length limit.
Direct custom overrides replace the corresponding default row; custom chords
and directional exceptions remain explicit. An empty mapping shows just those
four rows. Other emulator defaults remain active but are omitted from this
compact guide. The menu
shows 11 to 13 titles at a time and scrolls at either edge of the visible window.
Line 2 is a full-width divider made from graphics character 140. Line 3 shows
`^^^` when more games are above; line 15 shows `vvv` when more games
are below. When an indicator is unnecessary, its row displays a game instead:
the first game starts on line 3 at startup, with no reserved empty rows.
The header and footer
remain on lines 1 and 16. Selection
stops at the first and last games. CMD files execute directly; BAS files use
`BASIC/CMD:0 RUN"NAME/BAS:4"`. Exiting a game returns according to that game's
own behavior; run `LAUNCH/CMD:4` again to reopen the menu.

To inspect the catalog or launch a game manually:

```text
DIR :4
LIST LIST/TXT:4
WEERD/CMD:4
```

Press a key when a directory listing pauses. To load a BASIC game:

```text
BASIC
RUN"DEMON/BAS:4"
```

The archive includes games for different TRS-80 models. Copying them onto this
disk preserves their bytes; individual games still require their original model
and hardware. For Model I use the repository's Model I LDOS boot disk and install
XTRSHARD from the sdltrs utility disk as described below; the same data image can
then be mounted. The supplied configured boot floppy is for Model III.

LDOS uses a flat directory with names of at most eight characters and extensions
of at most three characters. The duplicate names have these explicit aliases:

| Source directory | Original | DOS name |
| --- | --- | --- |
| `defense_command` | `DEFENSE.CMD` | `DEFCOM/CMD` |
| `missile_defense` | `DEFENSE.CMD` | `MISSDEF/CMD` |
| `space_invaders_model1` | `SPACEINV.CMD` | `SPACEIN1/CMD` |
| `space_invaders_model3` | `SPACEINV.CMD` | `SPACEIN3/CMD` |

Other names are uppercased; future long or colliding names are shortened and
given numeric suffixes. `manifest.csv` records the resulting mapping.
`LIST/TXT` uses ASCII with TRS-80 carriage-return line endings and lists the
final aliases, so each title can be matched to its actual file on the disk.
The third field is a tab-separated ASCII mapping such as `L1:1,L2:2,Y:D`,
generated solely from the manually edited `action-map.csv`. Empty cells and
lowercase `x` cells are ignored; other cells assign the console control to the
TRS-80 key named by the column. Uppercase `X` assigns the X button. Cosmic
Fighter's `1`, `2`, and `D` columns contain `L1`, `L2`, and `Y`, respectively.
Custom bindings augment the emulator defaults: A sends ENTER, B sends BREAK, X sends CLEAR,
and tapping SELECT opens the keyboard for Cosmic Fighter. START has no default
guest key. SELECT toggles on release; a tap used in a custom chord is consumed. An explicit custom
assignment overrides the same control's default; matching custom chords
suppress the defaults of their constituent controls while held. The guide shows
custom assignments and inherited defaults in a two-column table.
The builder never rewrites the source CSV. Games with no custom assignments
have an empty mapping, clearing the previous map and restoring emulator defaults.
Every imported game's title must have a row. Use physical key/chord columns;
semantic input columns (`ANY_KEY`, `TEXT_INPUT`, `NUMERIC_INPUT`) remain
unassigned or marked `x`. Multiple assignments to the same console chord are
rejected. No separate proposed-mapping CSV is consulted.

Syntax, control tokens, and zero-terminated port-41 transmission are documented
in [rg35xx-control-mapping.md](rg35xx-control-mapping.md). Mapping notes and the
format document live alongside the builder in `rg35xx/`.

## Launcher assembly

The source is `rg35xx/LAUNCH.ASM`. It uses standard Z80 instructions
and LDOS file and command services; the launcher needs no emulator traps.
To assemble a raw binary and symbol map separately:

```sh
pasmo --bin rg35xx/LAUNCH.ASM /tmp/LAUNCH.BIN /tmp/LAUNCH.SYM
```

Use `build.py` to assemble and package the runnable `LAUNCH.CMD` and install it
on the image. A raw `.BIN` is not a TRS-80 `/CMD` file.

The launcher reads `LIST/TXT:4` on startup, skips its two header lines, and
skips each record's optional tab-delimited control mapping and hints, then separates the
title from the filename at the final space. It accepts CR, LF
or CRLF text, including a final line without a newline. Supported entries
end in `/CMD` or `/BAS`; unsupported lines are skipped. Missing, empty or
unreadable catalogs report an error and return to LDOS. Limits are 250 games
and 127 bytes before the tab per line; exceeding either reports an error. Mapping
bytes and hint bytes are skipped without a buffer during the initial scan.
Selecting a game reopens the catalog and loads only its mapping and hints,
up to 4095 mapping bytes plus 1023 hint bytes, each printable ASCII,
with final zeros in RAM. The builder rejects payloads beyond those limits.
Invalid/oversized selected mappings or hints return an error to LDOS.
Titles longer than
47 characters are clipped on screen while their filenames remain intact.
The table uses RAM from `7000h` through `AE7Fh`.

LDOS entry points and command handoff are described in the
[LDOS memory map](https://www.tim-mann.org/trs80/doc/ldosq2-1.pdf) and
[Linking to LDOS in Assembly](https://www.tim-mann.org/trs80/doc/ldosq1-1.pdf).

## Format and template provenance

The image uses the Reed HDV format supported by sdltrs, with a 256-byte header,
202 cylinders, 256 sectors per cylinder, and 256-byte sectors: 13,238,528 bytes
(about 12.6 MiB). LDOS allocates eight 32-sector granules per cylinder. Cylinder
1 contains the directory; game data starts at cylinder 2. This builder supports
254 user directory entries and one extent per file (at most 256 KiB); it reports
an error when a limit is exceeded. Imported file dates are fixed at 1 January
2000 for reproducible output and compatibility with LDOS's date range.

`diskimages/empty.hdv.gz` is a small, compressed, genuinely LDOS-formatted template. It was
created from `tpk/src/main/resources/apps/ldos_model3/ld3-531.dsk`
and the `XTRSHARD/DCT` from sdltrs's `diskimages/utility.dsk`, using:

```text
SYSTEM (DRIVE=4,DRIVER="XTRSHARD:1",ENABLE)
0
FORMAT :4 (DIR=1)
```

The volume label/date were normalized to `TPKGAMES` / `01/01/00` and the HDV
header checksum updated. DOS's reserved `BOOT/SYS` and `DIR/SYS` entries remain
in the template. The compressed template omits unwritten trailing cylinders;
the builder expands it to the full drive size before importing files.

`diskimages/ldos531-model3-boot.dsk.gz` was made from the same repository boot disk after
installing XTRSHARD for drive 4, unit 0, and running `SYSTEM (SYSGEN)`.
`SYSTEM (TIME=OFF,DRIVE=0)` disables the time prompt persistently on the boot
disk. To restore the prompt, use `SYSTEM (TIME=ON,DRIVE=0)`. These commands
write the disk setting directly; another SYSGEN is not needed. See the
[LDOS 5.3.1 update manual](https://www.tim-mann.org/trs80/doc/ld531upd.pdf).
`AUTO LAUNCH/CMD:4` starts the launcher directly from the mounted hard disk
automatically, avoiding the drive search used by an unqualified command. This setting is included in the compressed
boot template and persists across rebuilds.
The
original boot disk SHA-256 is
`22f6ad3cc913b1b0633a821ff6ce89939d5d60e99404fe391e900c28ceded9ea`.
The original LDOS copyright notices remain in place. XTRSHARD's permission notice
is reproduced in `XTRSHARD-LICENSE.txt`.

The driver and mounting procedure are documented in the
[sdltrs hard disk documentation](https://sdltrs.sourceforge.net/docs/Features.html).

## Validation

The generated image was opened in sdltrs under Model III LDOS 5.3.1. All 27
game files, `LIST/TXT` and `LAUNCH/CMD` were exported back to the host with
sdltrs's `EXPORT/CMD`; every byte
count and SHA-256 matched its repository source, including both BASIC files
and all duplicate-name variants. Cold boot and directory access were also
checked with only the supplied boot floppy and hard disk mounted, with
`emtsafe` enabled. This checks disk access and file preservation; it does not
establish that every game runs on Model III.

Launcher checks cover scrolling all 27 entries in both directions, first/last
selection bounds, BREAK exit, missing/empty/overlong catalog errors, command
handoff to LDOS, Armored Patrol reaching its CMD entry point, and Battlestar
Raven starting in BASIC (its "ACTUAL REALITY 2" opening screen).

For compact custom mappings, Python tests check empty/x filtering, Cosmic
Fighter's exact encoding, preservation of every manually assigned action,
chords, duplicate-trigger rejection and catalog formatting. Emulator
checks confirm all 27 title/filespec pairs, CMD and BASIC command handoff, old
two-field catalogs, CR/LF/CRLF and unterminated input, skipped mapping fields over
16 KiB, and the 127-byte title/filespec limit. The rebuilt disk's 29 files were
also checked byte for byte against their sources and manifest hashes.

Control-guide integration tests in `test_launcher.py` assemble and boot the
launcher, check the default/custom table and paging for all 27 games, verify
single-control overrides and long-column clipping, and verify that
cancel sends no mapping, and capture the actual OUT bytes through the zero
terminator before CMD/BASIC handoff. They also cover legacy entries and the
selected-mapping buffer limit. To run them along with the Python tests:

```sh
TPK_SDLTRS=/path/to/sdl2trs TPK_TRS80_ROM=/path/to/model3.rom \
  TPK_PASMO=/path/to/pasmo \
  python3 -B -m unittest discover -s rg35xx -p 'test_*.py'
```

Without the emulator/ROM environment variables, the integration tests are skipped.
