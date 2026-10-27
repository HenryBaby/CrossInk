"""Focused native checks for the wolfSSL build patches; run inside Docker."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest

class WolfSslPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        class Env:
            def subst(inner, value): return str(self.root)
            def Append(inner, **kwargs): pass
        self.module = runpy.run_path(str(Path(__file__).with_name("patch_wolfssl.py")),
                                    init_globals={"Import": lambda _: None, "env": Env()})

    def test_shrink_patch_is_exact_and_idempotent(self):
        path = self.root / "internal.c"
        path.write_text("before\n" + self.module["SHRINK_ORIGINAL"] + "after\n")
        self.module["patch_input_buffer_shrink"](path)
        once = path.read_bytes()
        self.module["patch_input_buffer_shrink"](path)
        self.assertEqual(once, path.read_bytes())
        self.assertIn(b"if (!forcedFree)", once)

    def test_reverting_build_flag_restores_original_guard(self):
        path = self.root / "guard.c"
        path.write_text(self.module["SHRINK_PATCHED"])
        original = subprocess.check_output(["cc", "-E", "-P", "-x", "c", str(path)], text=True)
        retained = subprocess.check_output(["cc", "-E", "-P", "-x", "c", "-DFREEINK_WOLFSSL_RETAIN_INPUT_BUFFER", str(path)], text=True)
        self.assertIn("usedLength > STATIC_BUFFER_LEN", original)
        self.assertNotIn("usedLength > STATIC_BUFFER_LEN", retained)
        self.assertIn("if (!forcedFree)", retained)

    def test_unknown_source_is_rejected(self):
        path = self.root / "internal.c"
        path.write_text("upstream changed")
        with self.assertRaises(RuntimeError): self.module["patch_input_buffer_shrink"](path)
        self.assertEqual(path.read_text(), "upstream changed")

    def test_reserve_patch_is_exact_and_idempotent(self):
        path = self.root / "internal.c"
        path.write_text("before\n" + self.module["RESERVE_ORIGINAL"] + "after\n")
        self.module["patch_input_buffer_reserve"](path)
        once = path.read_bytes()
        self.module["patch_input_buffer_reserve"](path)
        self.assertEqual(once, path.read_bytes())
        self.assertIn(b"capacity - usedLength, usedLength", once)
        self.assertIn(b"ssl->options.handShakeDone && !ssl->options.dtls", once)

    def test_reserve_reverting_build_flag_restores_original_source(self):
        path = self.root / "guard.c"
        path.write_text(self.module["RESERVE_ORIGINAL"])
        original = subprocess.check_output(["cc", "-E", "-P", "-x", "c", str(path)], text=True)
        path.write_text(self.module["RESERVE_PATCHED"])
        # An ordinary script revert still defines the old retention flag.
        # Cached reserve code must also be disabled in that configuration.
        for flags in ([], ["-DFREEINK_WOLFSSL_RETAIN_INPUT_BUFFER"],
                      ["-DFREEINK_WOLFSSL_RESERVE_INPUT_BUFFER"]):
            with self.subTest(flags=flags):
                reverted = subprocess.check_output(
                    ["cc", "-E", "-P", "-x", "c", *flags, str(path)], text=True)
                self.assertEqual(reverted, original)

    def test_reserve_unknown_or_corrupt_source_is_rejected(self):
        for source in ("upstream changed", self.module["RESERVE_MARKER"],
                       self.module["RESERVE_PATCHED"] * 2,
                       self.module["RESERVE_ORIGINAL"] * 2):
            with self.subTest(source=source[:80]):
                path = self.root / "internal.c"
                path.write_text(source)
                with self.assertRaises(RuntimeError):
                    self.module["patch_input_buffer_reserve"](path)
                self.assertEqual(path.read_text(), source)

    def test_fragmented_allocator_and_connection_lifecycle(self):
        fixtures = Path(__file__).with_name("fixtures")
        source = self.root / "wolfssl_input_buffer.c"
        harness = self.root / "wolfssl_input_buffer_harness.c"
        shutil.copyfile(fixtures / harness.name, harness)
        # Baseline still retains buffers: demonstrate why retention alone fails.
        for reserve in (False, True):
            with self.subTest(reserve=reserve):
                shutil.copyfile(fixtures / source.name, source)
                self.module["patch_input_buffer_shrink"](source)
                # Keep reserve code in both cached sources; only the new flag
                # activates it, so reverting to the old script is covered too.
                self.module["patch_input_buffer_reserve"](source)
                binary = self.root / ("reserved" if reserve else "retention_only")
                flags = ["-DFREEINK_WOLFSSL_RESERVE_INPUT_BUFFER"] if reserve else []
                subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                                "-DFREEINK_WOLFSSL_RETAIN_INPUT_BUFFER",
                                *flags,
                                f"-DEXPECT_RESERVE={int(reserve)}", str(harness),
                                "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True)


if __name__ == "__main__": unittest.main()
