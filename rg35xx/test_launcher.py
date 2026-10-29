"""Optional SDLTRS integration tests for the assembled Z80 launcher.

Set TPK_SDLTRS and TPK_TRS80_ROM to an SDLTRS executable and Model III ROM.
TPK_PASMO optionally selects the Pasmo executable (default: pasmo).
All disks, debugger files and assembly outputs live in a temporary directory.
"""

import gzip
import os
from pathlib import Path
import re
import select
import subprocess
import tempfile
import time
import unittest

import build


class Debugger:
    def __init__(self, directory, symbols):
        self.directory = directory
        self.symbols = symbols
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SDL_RENDER_DRIVER="software")
        self.process = subprocess.Popen([
            os.environ["TPK_SDLTRS"], "-model", "3", "-romfile3", os.environ["TPK_TRS80_ROM"],
            "-disk0", str(directory / "boot.dsk"), "-hard0", str(directory / "games.hdv"),
            "-debug", "-nofullscreen", "-nosound", "-turbo",
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        try:
            self.read()
            for name in ("keyboard", "guide_keyboard", "launch"):
                self.command(f"b {symbols[name]:x}")
            self.command("c")  # The configured floppy automatically starts LAUNCH.
        except BaseException:
            self.close()
            raise

    def read(self):
        output = bytearray()
        deadline = time.monotonic() + 30
        while not output.endswith(b"(zbx) "):
            if time.monotonic() >= deadline:
                raise TimeoutError(output.decode(errors="replace")[-2000:])
            if select.select([self.process.stdout], [], [], 0.2)[0]:
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError(output.decode(errors="replace"))
                output.extend(chunk)
        return output.decode(errors="replace")

    def command(self, command):
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        return self.read()

    def memory(self, address, length):
        path = self.directory / "memory"
        self.command(f"save {address:x}/{length:x} {path}")
        return path.read_bytes()

    def screen(self):
        return self.memory(0x3c00, 1024).decode("ascii", errors="replace")

    def key(self, code, loop="keyboard"):
        self.command(f"set $a = {code:x}")
        self.command(f"set $pc = {self.symbols[loop] + 3:x}")
        return self.command("c")

    def trace_output(self):
        self.command("iodebug 2")

    @staticmethod
    def transmitted(output):
        return bytes(int(v, 16) for v in re.findall(r"out \(0x29\), 0x([0-9A-Fa-f]+)", output))

    def close(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.process.stdin.close()
        self.process.stdout.close()


@unittest.skipUnless(os.environ.get("TPK_SDLTRS") and os.environ.get("TPK_TRS80_ROM"),
                     "Set TPK_SDLTRS and TPK_TRS80_ROM for Z80 integration tests")
class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="tpk-launch-test-")
        cls.directory = Path(cls.work.name)
        assembler = os.environ.get("TPK_PASMO", "pasmo")
        binary, symbols = cls.directory / "launch.bin", cls.directory / "launch.sym"
        subprocess.run([assembler, "--bin", str(build.HERE / "LAUNCH.ASM"),
                        str(binary), str(symbols)], check=True)
        cls.symbols = {name: int(value, 16) for name, value in re.findall(
            r"^(\w+)\s+EQU\s+0([0-9A-F]+)H", symbols.read_text(), re.M)}
        cls.launcher = build.assemble_launcher(assembler)
        cls.controls = build.load_control_mappings()

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def boot(self, catalog):
        (self.directory / "boot.dsk").write_bytes(gzip.decompress(
            (build.HERE / "ldos531-model3-boot.dsk.gz").read_bytes()))
        image = build.empty_image()
        build.add_file(image, "LAUNCH.CMD", self.launcher)
        build.add_file(image, "LIST.TXT", catalog)
        (self.directory / "games.hdv").write_bytes(image)
        debugger = Debugger(self.directory, self.symbols)
        self.addCleanup(debugger.close)
        return debugger

    @staticmethod
    def table_rows(screen):
        rows = []
        for offset in range(0, 1024, 64):
            line = screen[offset:offset + 64]
            if not line.startswith("| "):
                continue
            columns = line.split("|")
            if len(columns) == 4 and columns[1].strip() != "CONTROL":
                rows.append((columns[1].strip(), columns[2].strip()))
        return rows

    @staticmethod
    def display_control(control):
        return {
            "D_U": "Left DPAD Up",
            "D_D": "Left DPAD Down",
            "D_L": "Left DPAD Left",
            "D_R": "Left DPAD Right",
        }.get(control, control)

    @staticmethod
    def expected_rows(mapping):
        defaults = [("Left DPAD", "Arrow keys"), ("A", "ENTER"), ("B", "BREAK"), ("X", "CLEAR")]
        custom = [tuple(binding.split(":")) for binding in mapping.split(",")] if mapping else []
        if any(control in {"D_U", "D_D", "D_L", "D_R"} for control, _ in custom):
            defaults = [row for row in defaults if row[0] != "Left DPAD"]
        custom = [(LauncherTests.display_control(control), action) for control, action in custom]
        assigned = {control for control, _ in custom}
        return [(control, action) for control, action in defaults if control not in assigned] + custom

    def test_all_games_guide_paging_cancel_and_exact_port_output(self):
        used = {"LAUNCH.CMD"}
        games = [(build.game_title(path), build.dos_name(path, used).replace(".", "/"),
                  self.controls[build.game_title(path)]) for path in build.source_files()]
        games.sort(key=lambda game: game[0].casefold())
        e = self.boot(build.game_list(games))
        self.assertEqual(e.memory(self.symbols["count"], 1), bytes([len(games)]))
        menu_footer = e.screen()[15 * 64:].strip()
        self.assertEqual(menu_footer, "DPAD: Choose game     A: Select     B: Exit")
        e.trace_output()
        for index, (title, _, mapping) in enumerate(games):
            with self.subTest(game=title):
                self.assertEqual(e.transmitted(e.key(13)), b"")
                self.assertIn(title[:47], e.screen())
                self.assertEqual(e.memory(0xb000, len(mapping) + 1), mapping.encode() + b"\0")
                pages = []
                for _ in range(10):
                    screen = e.screen()
                    if screen in pages:
                        break
                    pages.append(screen)
                    self.assertEqual(e.transmitted(e.key(32, "guide_keyboard")), b"")
                else:
                    self.fail("Control guide did not cycle within ten pages")
                guide = "\n".join(pages)
                rows = [row for page in pages for row in self.table_rows(page)]
                self.assertEqual(rows, self.expected_rows(mapping))
                self.assertIn("CONTROL", guide)
                self.assertIn("TRS-80 KEY/DESCRIPTION", guide)
                for page in pages:
                    self.assertEqual(page[15 * 64:].strip(),
                                     "A: Launch     B: Back")
                    self.assertEqual(page[14 * 64:15 * 64], " " * 64)
                    self.assertEqual(page[64:128].split("|")[2].strip(),
                                     "TRS-80 KEY/DESCRIPTION")
                # Cancellation sends nothing and preserves the selection.
                self.assertEqual(e.transmitted(e.key(1, "guide_keyboard")), b"")
                self.assertEqual(e.memory(self.symbols["selected"], 1), bytes([index]))
                e.key(13)
                output = e.key(13, "guide_keyboard")  # Stops just before launch.
                self.assertEqual(e.transmitted(output), mapping.encode() + b"\0")
                self.assertRegex(e.command("p"), r"B C: 00 29")
                # Stop before DOS can overwrite the launcher with the game.
                e.command(f"set $pc = {self.symbols['guide_back']:x}")
                e.command("c")
                if index + 1 < len(games):
                    e.key(10)

    def test_manual_bindings_page_and_wrap_without_changing_transmission(self):
        controls = ["A", "B", "X", "Y", "L1", "L2", "R1", "R2", "START", "SELECT",
                    "D_U", "D_D", "D_L", "D_R", "LS_U", "RS_D"]
        bindings = [f"{control}:A" for control in controls]
        mapping = ",".join(bindings)
        e = self.boot(build.game_list([("Manual controls", "MANUAL/CMD", mapping)]))
        e.trace_output()
        e.key(13)
        first = e.screen()
        expected = self.expected_rows(mapping)
        self.assertEqual(self.table_rows(first), expected[:11])
        self.assertEqual(e.transmitted(e.key(32, "guide_keyboard")), b"")
        self.assertEqual(self.table_rows(e.screen()), expected[11:])
        self.assertEqual(e.transmitted(e.key(32, "guide_keyboard")), b"")
        self.assertEqual(e.screen(), first)
        self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), mapping.encode() + b"\0")

    def test_empty_mapping_shows_only_requested_defaults_and_console_hints(self):
        e = self.boot(build.game_list([("Defaults", "GAME/CMD", "")]))
        self.assertEqual(e.screen()[15 * 64:].strip(),
                         "DPAD: Choose game     A: Select     B: Exit")
        e.key(13)
        self.assertEqual(self.table_rows(e.screen()),
                         [("Left DPAD", "Arrow keys"), ("A", "ENTER"), ("B", "BREAK"), ("X", "CLEAR")])
        self.assertEqual(e.screen()[15 * 64:].strip(),
                         "A: Launch     B: Back")
        self.assertEqual(e.screen()[14 * 64:15 * 64], " " * 64)
        self.assertEqual(e.screen()[64:128].split("|")[2].strip(),
                         "TRS-80 KEY/DESCRIPTION")
        e.trace_output()
        self.assertEqual(e.transmitted(e.key(32, "guide_keyboard")), b"")
        self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), b"\0")

    def test_direct_overrides_replace_default_rows_and_chords_remain_explicit(self):
        mapping = "START:SPACE,A:Q,B:1,X:F,SELECT:CLEAR,D_L:A,SELECT+START:SHIFT+BREAK"
        e = self.boot(build.game_list([("Overrides", "GAME/CMD", mapping)]))
        e.key(13)
        rows = self.table_rows(e.screen())
        self.assertEqual(rows, self.expected_rows(mapping))
        self.assertNotIn(("START", "ENTER"), rows)
        self.assertNotIn(("A", "ENTER"), rows)
        self.assertNotIn(("B", "BREAK"), rows)
        self.assertNotIn(("X", "CLEAR"), rows)
        self.assertIn(("X", "F"), rows)
        self.assertNotIn(("SELECT", "On-screen keyboard"), rows)
        self.assertIn(("SELECT", "CLEAR"), rows)
        self.assertNotIn(("Left DPAD", "Arrow keys"), rows)
        self.assertIn(("Left DPAD Left", "A"), rows)
        self.assertEqual(e.screen()[14 * 64:15 * 64], " " * 64)
        e.trace_output()
        self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), mapping.encode() + b"\0")

    def test_hints_override_custom_control_descriptions(self):
        mapping = "R1:SPACE,L1:1,L2:2,X:1,Y:0"
        hints = "R1:Fire,L1:One player,L2:Two players,X:Expert,Y:Novice"
        e = self.boot(build.game_list([("Hints", "GAME/CMD", mapping, hints)]))
        e.key(13)
        rows = self.table_rows(e.screen())
        self.assertIn(("R1", "Fire"), rows)
        self.assertIn(("L1", "One player"), rows)
        self.assertIn(("L2", "Two players"), rows)
        self.assertIn(("X", "Expert"), rows)
        self.assertIn(("Y", "Novice"), rows)
        self.assertNotIn(("R1", "SPACE"), rows)
        self.assertNotIn(("L1", "1"), rows)
        self.assertNotIn(("L2", "2"), rows)
        self.assertNotIn(("X", "1"), rows)
        self.assertNotIn(("Y", "0"), rows)

    def test_long_columns_are_clipped_without_modifying_payload(self):
        control = "SELECT+START+LS_U+RS_D+L1+R1"
        action = "SHIFT+BREAK+LEFT+RIGHT+COMMA+PERIOD+SEMICOLON"
        mapping = control + ":" + action
        e = self.boot(build.game_list([("Long chord", "GAME/CMD", mapping)]))
        e.key(13)
        rows = self.table_rows(e.screen())
        self.assertIn((control[:19] + "...", action[:31] + "..."), rows)
        e.trace_output()
        self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), mapping.encode() + b"\0")

    def test_cmd_and_basic_handoff_follow_confirmation(self):
        for filespec, command in (("FIRST/CMD", b"FIRST/CMD:4"),
                                  ("SECOND/BAS", b'BASIC/CMD:0 RUN"SECOND/BAS:4"')):
            with self.subTest(filespec=filespec):
                e = self.boot(build.game_list([("Game", filespec, "L1:1,L2:2,Y:D")]))
                e.trace_output()
                e.key(13)
                output = e.key(13, "guide_keyboard")
                self.assertEqual(e.transmitted(output), b"L1:1,L2:2,Y:D\0")
                e.command("b 4299")
                e.command("c")
                self.assertEqual(e.memory(self.symbols["command"], 64).split(b"\r")[0], command)
                e.close()

    def test_legacy_and_line_ending_variants_send_empty_or_exact_mapping(self):
        for newline in (b"\r", b"\n", b"\r\n"):
            for mapping in (None, b"L1:1,L2:2,Y:D"):
                with self.subTest(newline=newline, mapping=mapping):
                    suffix = b"" if mapping is None else b"\t" + mapping
                    catalog = newline.join([b"Game\tMapping", b"----", b"Unsupported  NO/TXT",
                                            b"First  ONE/CMD" + suffix, b"Second  TWO/BAS" + suffix])
                    e = self.boot(catalog)  # Final record has no newline.
                    e.key(10)
                    e.key(13)
                    self.assertIn("Second", e.screen())
                    e.trace_output()
                    self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), (mapping or b"") + b"\0")
                    e.close()

    def test_selected_mapping_limit_and_invalid_bytes(self):
        accepted = b",".join([b"A:A"] * 1024)
        cases = [(accepted, None), (accepted + b"A", b"exceeds 4095"),
                 (b"A:A\0B", b"Invalid mapping"), (b"RG1;B=SPACE", b"Invalid control"),
                 (b"A:", b"Invalid control"), (b":A", b"Invalid control"),
                 (b"A:A,", b"Invalid control"), (b"A::A", b"Invalid control"),
                 (b"A++B:A", b"Invalid control"), (b"A:A,,B:B", b"Invalid control")]
        for mapping, error in cases:
            with self.subTest(length=len(mapping), error=error):
                e = self.boot(b"Game\r----\rFirst  ONE/CMD\t" + mapping + b"\r")
                e.trace_output()
                if error:
                    e.command("b 402d")
                output = e.key(13)
                self.assertEqual(e.transmitted(output), b"")
                if error:
                    self.assertIn(error.decode(), e.screen())
                else:
                    self.assertEqual(e.transmitted(e.key(13, "guide_keyboard")), mapping + b"\0")
                e.close()


if __name__ == "__main__":
    unittest.main()
