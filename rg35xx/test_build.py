"""Run with python3 -B -m unittest discover -s rg35xx -p 'test_*.py'."""

import csv
import json
from pathlib import Path
import tempfile
import unittest

import build


class ControlMappingTests(unittest.TestCase):
    def test_empty_and_x_cells_are_ignored(self):
        self.assertEqual(build.encode_controls({"ENTER": "x", "CLEAR": "", "SPACE": " x ",
                                                "1": "L1", "2": "L2", "D": "Y"}),
                         "L1:1,L2:2,Y:D")
        self.assertEqual(build.encode_controls({"ENTER": "x", "ANY_KEY": "", "TEXT_INPUT": "x"}), "")

    def test_custom_chords_and_direction_tokens(self):
        self.assertEqual(build.encode_controls({
            "SPACE": "B", "LEFT": "D-pad Left", "LEFT+SPACE": "D_L+B",
            "SHIFT+BREAK": "SELECT+START", "A+PERIOD+SPACE": "LS_U+Right stick Down+R1",
        }), "B:SPACE,D_L:LEFT,D_L+B:LEFT+SPACE,SELECT+START:SHIFT+BREAK,LS_U+RS_D+R1:A+PERIOD+SPACE")

    def test_multi_key_action_cells_preserve_every_key(self):
        keys = "+".join(list("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@") + [
            "ENTER", "CLEAR", "BREAK", "SPACE", "UP", "DOWN", "LEFT", "RIGHT",
            "COMMA", "PERIOD", "SEMICOLON", "SHIFT"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "action-map.csv"
            with path.open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(["game", "SPACE", "LEFT+RIGHT", keys])
                writer.writerow(["Missile Defense", "R1", "R2", ""])
                writer.writerow(["All keys", "", "", "R2"])
            mappings = build.load_control_mappings(path)
        self.assertEqual(mappings["Missile Defense"], "R1:SPACE,R2:LEFT+RIGHT")
        self.assertEqual(mappings["All keys"], "R2:" + keys)

    def test_example_csv_bindings(self):
        # A fixture avoids pinning the user's manually edited game mappings.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "action-map.csv"
            path.write_text("game,SPACE,1,2,D,ENTER\nCosmic Fighter,R1,L1,L2,Y,x\nOther,x,,,,\n")
            mappings = build.load_control_mappings(path)
        self.assertEqual(mappings["Cosmic Fighter"], "R1:SPACE,L1:1,L2:2,Y:D")
        self.assertEqual(mappings["Other"], "")

    def test_every_custom_cell_survives_encoding(self):
        mappings = build.load_control_mappings()
        with (build.HERE / "action-map.csv").open(newline="") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(len(rows), len(mappings))
        for row in rows:
            game = row.pop("game")
            encoded = mappings[game]
            with self.subTest(game=game):
                self.assertFalse(any(c.isspace() for c in encoded))
                actions = [binding.split(":")[1] for binding in encoded.split(",")] if encoded else []
                self.assertEqual(actions, [key for key, value in row.items()
                                          if value.strip() not in {"", "x"}])

    def test_bad_controls_keys_and_duplicate_triggers_are_rejected(self):
        for actions in ({"ENTER": "Power"}, {"NOT_A_KEY": "A"},
                        {"TEXT_INPUT": "A"}, {"ANY_KEY": "A"}, {"NUMERIC_INPUT": "A"},
                        {"A": "A+A"}, {"A+A": "B"}, {"A": "B", "D": "B"},
                        {"A": "SELECT+B", "B": "B+SELECT"}):
            with self.subTest(actions=actions), self.assertRaises(ValueError):
                build.encode_controls(actions)

    def test_hint_encoding_supports_control_aliases(self):
        self.assertEqual(build.encode_hints({"R1": "Fire", "D-pad Left": "Move"}),
                         "R1:Fire,D_L:Move")

    def test_hint_encoding_rejects_invalid_values(self):
        bad_hints = [
            {"Power": "Oops"},
            {"R1": ""},
            {"R1": "Line\nBreak"},
            {"R1": "No,comma"},
            {"R1": "ASCII ok", "Right stick Down": "Also ok", "RS_D": "Duplicate"},
        ]
        for hints in bad_hints:
            with self.subTest(hints=hints), self.assertRaises(ValueError):
                build.encode_hints(hints)

    def test_uppercase_x_assigns_console_button(self):
        self.assertEqual(build.encode_controls({"SPACE": "X"}), "X:SPACE")

    def test_bad_csv_rows_are_rejected(self):
        cases = ["game,SPACE,SPACE\nG,B,B\n", "game,SPACE\nG,B,A\n",
                 "game,SPACE\nG\n", "game,SPACE\nG,B\nG,A\n", "name,SPACE\nG,B\n"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mapping.csv"
            for contents in cases:
                with self.subTest(contents=contents), self.assertRaises(ValueError):
                    path.write_text(contents)
                    build.load_control_mappings(path)

    def test_all_marker_rows_are_valid_and_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "action-map.csv"
            path.write_text("game,ENTER,SPACE\nG,x,\nH,,x\n")
            self.assertEqual(build.load_control_mappings(path), {"G": "", "H": ""})

    def test_catalog_keeps_mapping_in_third_field(self):
        mapping = ",".join(f"SELECT+{c}:{c}" for c in "ABXY")
        hints = "R1:Fire,L1:One player,L2:Two players"
        catalog = build.game_list([("Zulu", "Z/BAS", mapping, hints),
                                   ("A title with spaces", "FIRST/CMD", "L1:1,L2:2,Y:D", ""),
                                   ("Empty", "EMPTY/CMD", "", "")])
        self.assertNotIn(b"\n", catalog)
        lines = catalog.decode("ascii").split("\r")
        self.assertEqual(len(lines), 6)
        first, empty, last = [line.split("\t") for line in lines[2:5]]
        self.assertEqual(first[0].split()[-1], "FIRST/CMD")
        self.assertEqual(first[1], "L1:1,L2:2,Y:D")
        self.assertEqual(first[2], "")
        self.assertEqual(empty[1], "")
        self.assertEqual(empty[2], "")
        self.assertEqual(last[0].split()[-1], "Z/BAS")
        self.assertEqual(last[1], mapping)
        self.assertEqual(last[2], hints)

    def test_apps_loader_encodes_hint_overrides(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apps_json = root / "apps.json"
            source = root / "tpk/src/main/resources/apps/example/EXAMPLE.CMD"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"cmd")
            apps_json.write_text(json.dumps([{
                "name": "Example",
                "path": "tpk/src/main/resources/apps/example/EXAMPLE.CMD",
                "keymap": "R1:SPACE",
                "hint": {
                    "R1": "Fire",
                    "L1": "One player",
                    "L2": "Two players",
                },
            }]))
            old_root = build.ROOT
            try:
                build.ROOT = root
                apps = build.load_apps(apps_json)
            finally:
                build.ROOT = old_root
        self.assertEqual(apps[0]["hint"], "R1:Fire,L1:One player,L2:Two players")

    def test_catalog_rejects_oversized_or_multiline_prefix(self):
        for title in ("X" * 125, "A\tB", "A\rB", "A\nB"):
            with self.subTest(title=title), self.assertRaises(ValueError):
                build.game_list([(title, "GAME/CMD", "B:SPACE")])

    def test_catalog_mapping_matches_launcher_buffer_limit(self):
        mapping = ",".join(["A:A"] * 1024)  # Exercise the transport buffer, not token semantics.
        self.assertEqual(len(mapping), 4095)
        build.game_list([("Game", "GAME/CMD", mapping)])
        with self.assertRaisesRegex(ValueError, "exceeds 4095 bytes"):
            build.game_list([("Game", "GAME/CMD", mapping + "A")])


if __name__ == "__main__":
    unittest.main()
