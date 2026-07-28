from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("match.py")
SPEC = importlib.util.spec_from_file_location("ctr_match", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
ctr_match = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ctr_match
SPEC.loader.exec_module(ctr_match)


class ExtractFunctionTests(unittest.TestCase):
    def test_extracts_only_named_function(self) -> None:
        source = """
void before(void) {}
// Keep this comment outside the generated source.
void target(void)
{
    const char *brace = "}";
    /* { ignored } */
}
void after(void) {}
"""
        self.assertEqual(
            ctr_match.extract_function(source, "target"),
            'void target(void)\n{\n    const char *brace = "}";\n    /* { ignored } */\n}\n',
        )

    def test_rejects_ambiguous_definition(self) -> None:
        with self.assertRaises(ctr_match.MatchError):
            ctr_match.extract_function("void nope(void) {}\n", "target")

    def test_manifest_probes_extract_from_production_source(self) -> None:
        manifest = ctr_match.load_json(ctr_match.DEFAULT_MANIFEST)
        for probe in manifest["compiler_probes"]:
            source = ctr_match.ROOT / probe["source"]
            extracted = ctr_match.extract_function(source.read_text(), probe["symbol"])
            self.assertIn(probe["symbol"], extracted)


class ComparisonTests(unittest.TestCase):
    def test_rejects_unknown_artifact_selection(self) -> None:
        manifest = {"artifacts": [{"id": "exe"}]}
        with self.assertRaises(ctr_match.MatchError):
            ctr_match.parse_selected(manifest, ["typo"])

    def test_first_difference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.bin"
            actual = root / "actual.bin"
            expected.write_bytes(b"abcdef")
            actual.write_bytes(b"abcxef")
            self.assertEqual(ctr_match.first_file_difference(expected, actual), 3)

    def test_length_difference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.bin"
            actual = root / "actual.bin"
            expected.write_bytes(b"abcdef")
            actual.write_bytes(b"abc")
            self.assertEqual(ctr_match.first_file_difference(expected, actual), 3)

    def test_exact_files_have_no_difference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "expected.bin"
            actual = root / "actual.bin"
            expected.write_bytes(b"same")
            actual.write_bytes(b"same")
            self.assertIsNone(ctr_match.first_file_difference(expected, actual))


class ToolchainTests(unittest.TestCase):
    def test_manifest_pins_vendored_gcc(self) -> None:
        manifest = ctr_match.load_json(ctr_match.DEFAULT_MANIFEST)
        compiler = manifest["toolchain"]["compiler"]
        self.assertEqual(compiler["version"], "2.8.1")
        self.assertTrue(compiler["directory"].startswith("externals/"))

    def test_require_tool_rejects_wrong_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tool"
            path.write_bytes(b"not the pinned tool")
            with self.assertRaises(ctr_match.MatchError):
                ctr_match.require_tool(path, "test tool", "0" * 64)

    def test_repository_path_rejects_escape(self) -> None:
        with self.assertRaises(ctr_match.MatchError):
            ctr_match.repository_path("../outside")


class PsxHeaderTests(unittest.TestCase):
    def test_header_contract(self) -> None:
        artifact = {
            "id": "exe",
            "load_address": "0x80010000",
            "mapped_size": "0x7d800",
            "header": {
                "entrypoint": "0x8007793c",
                "global_pointer": "0",
                "stack_pointer": "0x801ffff0",
            },
        }
        header = bytearray(0x800)
        header[:8] = b"PS-X EXE"
        struct.pack_into("<I", header, 0x10, 0x8007793C)
        struct.pack_into("<I", header, 0x14, 0)
        struct.pack_into("<I", header, 0x18, 0x80010000)
        struct.pack_into("<I", header, 0x1C, 0x7D800)
        struct.pack_into("<I", header, 0x30, 0x801FFFF0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "SCUS_944.26"
            path.write_bytes(header)
            self.assertEqual(ctr_match.check_psx_exe_header(path, artifact), [])


if __name__ == "__main__":
    unittest.main()
