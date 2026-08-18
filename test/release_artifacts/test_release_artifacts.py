import importlib.util
import json
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[2] / "scripts" / "release_artifacts.py"
SPEC = importlib.util.spec_from_file_location("release_artifacts", SCRIPT)
release_artifacts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release_artifacts)


class ReleaseArtifactTest(unittest.TestCase):
    def test_metadata_round_trip(self):
        from tempfile import TemporaryDirectory

        with TemporaryDirectory() as directory:
            tmp_path = Path(directory)
            binary = tmp_path / "firmware-x3-x4-v1.2.3.bin"
            binary.write_bytes(b"firmware")
            release_artifacts.write_metadata(binary, tmp_path, commit="a" * 40, version="1.2.3", environment="default")
            self.assertEqual(
                release_artifacts.validate(tmp_path, commit="a" * 40, version="1.2.3", environment="default"),
                binary,
            )
            metadata = json.loads((tmp_path / "provenance.json").read_text())
            self.assertEqual(metadata["filename"], binary.name)

    def test_validation_rejects_wrong_commit(self):
        from tempfile import TemporaryDirectory

        with TemporaryDirectory() as directory:
            tmp_path = Path(directory)
            binary = tmp_path / "firmware-x4-pro-v1.2.3.bin"
            binary.write_bytes(b"firmware")
            release_artifacts.write_metadata(binary, tmp_path, commit="a" * 40, version="1.2.3", environment="x4-pro")
            with self.assertRaisesRegex(ValueError, "provenance mismatch"):
                release_artifacts.validate(tmp_path, commit="b" * 40, version="1.2.3", environment="x4-pro")

    def test_validation_rejects_tampered_binary(self):
        from tempfile import TemporaryDirectory

        with TemporaryDirectory() as directory:
            tmp_path = Path(directory)
            binary = tmp_path / "firmware-x3-x4-v1.2.3.bin"
            binary.write_bytes(b"firmware")
            release_artifacts.write_metadata(binary, tmp_path, commit="a" * 40, version="1.2.3", environment="default")
            binary.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "provenance mismatch"):
                release_artifacts.validate(tmp_path, commit="a" * 40, version="1.2.3", environment="default")

    def test_validation_rejects_extra_binary(self):
        from tempfile import TemporaryDirectory

        with TemporaryDirectory() as directory:
            tmp_path = Path(directory)
            binary = tmp_path / "firmware-x4-pro-v1.2.3.bin"
            binary.write_bytes(b"firmware")
            release_artifacts.write_metadata(binary, tmp_path, commit="a" * 40, version="1.2.3", environment="x4-pro")
            (tmp_path / "unexpected.bin").write_bytes(b"other")
            with self.assertRaisesRegex(ValueError, "unexpected firmware files"):
                release_artifacts.validate(tmp_path, commit="a" * 40, version="1.2.3", environment="x4-pro")

    def test_cli_write_accepts_provenance_arguments(self):
        # Exercise the same required arguments passed by the CI workflow.
        args = release_artifacts.build_parser().parse_args(
            [
                "write",
                "--binary",
                "firmware.bin",
                "--output",
                ".",
                "--commit",
                "a" * 40,
                "--version",
                "1.2.3",
                "--environment",
                "default",
            ]
        )
        self.assertEqual(args.environment, "default")
