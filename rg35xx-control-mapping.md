# RG35XXPRO custom control mappings

`tools/hard-disk/action-map.csv` is the sole source for custom mappings. Edit it
manually; the builder never generates or rewrites this file. Its `game` column
matches each program's `app.name`. Other column headers identify physical
TRS-80 keys or simultaneous key chords.

Empty cells and cells containing lowercase `x` are ignored. An `x` preserves
an identified game action without assigning a console control. Uppercase `X`
is the console's X button. Surrounding whitespace in cells is ignored.
Other cells name the console control or control chord to bind to that column.
Rows with no custom controls produce an empty mapping, which clears a previous
game's mapping and restores the emulator's defaults.

For example, assigning Cosmic Fighter's (`COSMIC/CMD`) `SPACE`, `1`, `2`, and
`D` columns to `R1`, `L1`, `L2`, and `Y` produces:

```text
R1:SPACE,L1:1,L2:2,Y:D
```

Custom bindings augment the emulator's defaults. This mapping assigns R1 to
SPACE, L1 to 1, L2 to 2, and Y to D, while preserving START to ENTER, SELECT to
CLEAR, movement arrows, A's keyboard toggle,
and other defaults. An explicit assignment overrides that control's default;
for example, `START:SPACE` sends SPACE instead of ENTER. L1, L2, R1, and R2
have no default TRS-80 key assignments; use custom cells to assign them.

Bindings follow CSV column order. Unknown controls or physical keys, duplicate
game rows, malformed rows, repeated chord members, and multiple assignments of
the same control chord stop the build. To hold several TRS-80 keys with one
control, use a single key-chord column, such as `SHIFT+BREAK`. Alternate source
keys must be assigned explicitly; no automatic alias or phase selection occurs.
`ANY_KEY`, `TEXT_INPUT`, and `NUMERIC_INPUT` remain reference columns and may be
empty or marked `x`; assigning a control to them is rejected because they do
not identify physical keys. Prompt detection and keyboard-overlay sequences
are outside this format.

## Catalog and transmission

The catalog retains two header lines, alphabetical order, and ASCII CR (`0D`)
line endings. Each game record is:

```text
<padded title><two spaces><LDOS filespec><TAB><mapping><CR>
```

TAB is one byte (`09`). Filespecs use LDOS slash notation, including aliases,
e.g. `DEFCOM/CMD`, with no drive suffix. Title and filespec together must fit
in 127 bytes. Mapping payloads contain no spaces, tabs or line endings, are
at most 4095 ASCII bytes per game, and may be empty. The disk writer's 256 KiB
limit applies to the complete catalog file.

`LAUNCH.ASM` skips mappings while building the game menu. On selection it
reopens the catalog and buffers only that game's mapping in `B000h..BFFFh`,
including its terminating zero. The game table at `7000h..AE7Fh` remains intact.
The launcher accepts old records without a mapping, CR/LF/CRLF input, and an
unterminated final record. It checks compact delimiter structure and token
characters before showing the guide. The builder and emulator validate token
names. Invalid or oversized selected mappings report an error without sending
any bytes.

ENTER on the game menu opens a two-column control overview. It includes the
inherited defaults and custom assignments. D-pad directions share one row
(`Left DPAD` to `Arrow keys`), and the left stick has a similar grouped row.
Direct custom assignments replace the corresponding default row, so an
assignment such as `START:SPACE` shows START to SPACE, with no START to ENTER
row. Custom chords and direction overrides remain explicit; the footer notes
that custom bindings override defaults while they match. Grouped arrow rows
summarize the base directions, with explicit custom direction rows as exceptions.
Unassigned shoulder buttons have no rows because they have no default keys.

For the example above, the overview is:

| Control | TRS-80 key / action |
| --- | --- |
| Left DPAD | Arrow keys |
| Left stick | Arrow keys |
| START | ENTER |
| SELECT | CLEAR |
| Right stick click | SPACE |
| A | On-screen keyboard |
| Menu | Emulator menu (F7) |
| R1 | SPACE |
| L1 | 1 |
| L2 | 2 |
| Y | D |

An empty mapping still shows the default table. SPACE advances through pages
of eleven rows and wraps; BREAK returns with the selection preserved and sends
nothing. Long control/action columns are clipped independently with `...`,
without changing the buffered mapping or overflowing the table borders.
ENTER on the guide sends the complete original mapping, one byte at a time,
to **decimal port 41 (`29h`)**, followed by one zero byte. For Cosmic Fighter:

```text
52 31 3A 53 50 41 43 45 2C 4C 31 3A 31 2C 4C 32 3A 32 2C 59 3A 44 00
```

There is no version prefix, title, filename, tab or CR in the transmitted
string. `LD BC,41` and `OUT (C),A` keep the full I/O address at `0029h` for every
byte. The zero terminator is sent before CMD/BASIC handoff to LDOS. An empty
mapping sends only `00` to clear the previous map. Paging and cancellation send
nothing. The host `LIST.TXT` and disk `LIST/TXT` contain identical bytes.

## Binding syntax

```text
mapping  = binding *( "," binding ) / empty
binding  = controls ":" keys
controls = control *( "+" control )
keys     = key *( "+" key )
```

Tokens are case sensitive. `:` separates console controls from TRS-80 keys;
`,` separates bindings. There is no trailing comma or escape syntax. `+` means
simultaneous controls on the left and simultaneous physical keys on the right.
Order within a chord does not change its meaning.

Console tokens are `A B X Y L1 L2 R1 R2 START SELECT`, `D_U D_D D_L D_R`,
`LS_U LS_D LS_L LS_R`, and `RS_U RS_D RS_L RS_R`. These are printed hardware
labels, D-pad directions, and left/right stick directions, independent of SDL
or Linux numbering. In the CSV, `D-pad Left`, `Left stick Up`, and analogous
long direction names are also accepted and encoded as `D_L`, `LS_U`, etc.
M/Menu, power, reset, volume and stick clicks have no custom-mapping tokens.
Stick directions are digital after the frontend's dead zone.

TRS-80 tokens are letters `A` through `Z`, digits `0` through `9`, `@`, `ENTER`,
`CLEAR`, `BREAK`, `SPACE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `COMMA`, `PERIOD`,
`SEMICOLON`, and `SHIFT`. These identify physical matrix keys, rather than
ASCII characters. A letter does not imply SHIFT. Punctuation tokens identify
unshifted physical keys. `SHIFT+BREAK` and opposing arrows `LEFT+RIGHT` assert
both keys.

The emulator keeps outputs down while their resolved controls are held. Shared
keys remain down until every contributing source releases. Larger matching
control chords suppress their strict subset bindings; independent bindings
remain active. Matching custom chords also suppress the defaults of their
constituent controls. For example, `SELECT+START:SHIFT+BREAK` suppresses CLEAR
and ENTER while both controls are held; a constituent still held after the
chord ends resumes its default. A retains its keyboard toggle unless overridden
by a custom A binding or a matching chord containing A. Menu stays reserved.
The guide shows inherited defaults and explicit custom assignments together.
The builder, launcher, and emulator use only compact mappings.
Old `RG1;...` strings and their alternative, prompt, and overlay syntax are
rejected without replacing the emulator's active mapping.

## Checks

Run `python3 -B -m unittest discover -s tools/hard-disk -p 'test_*.py'` for CSV,
encoding and catalog checks. Run `python3 tools/hard-disk/build.py` to assemble
the launcher and rebuild the catalog and hard disk. Both commands operate on
the manually edited `action-map.csv`.

Optional assembled-launcher tests exercise the guide and actual OUT bytes in
SDLTRS. Set `TPK_SDLTRS` to its executable, `TPK_TRS80_ROM` to a Model III ROM,
and optionally `TPK_PASMO` to Pasmo's path before running the same unittest
command. They use temporary disk images.
