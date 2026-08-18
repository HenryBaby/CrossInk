#!/usr/bin/env python3
"""Regression tests for the published release-note selection rules."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "generate_release_notes.py"


class ReleaseNotesGeneratorTest(unittest.TestCase):
    def test_publishes_requested_version_and_filters_internal_history(self):
        changelog = """\
## [Unreleased]

### Internal

- Docker secret that must never ship.

## [v1.5.4] - 2026-08-18

### Upstream CrossInk v1.5.1-rc-3

This is the public upstream provenance.

## Internal

- Internal section secret.

### Nested Internal

- Nested internal secret.

## Public follow-up

- Public follow-up content must remain.

### Internal

- Internal secret.

## [v1.5.3] - 2026-08-15

### Added

- Adjacent release text.
"""
        readme = """\
## Changes maintained by this downstream fork

- Stable downstream footer.
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changelog_path = root / "CHANGELOG.md"
            readme_path = root / "README.md"
            output_path = root / "notes.md"
            changelog_path.write_text(changelog, encoding="utf-8")
            readme_path.write_text(readme, encoding="utf-8")
            subprocess.run(
                [sys.executable, str(SCRIPT), "--version", "v1.5.4",
                 "--changelog", str(changelog_path),
                 "--readme", str(readme_path), "--output", str(output_path)],
                check=True, capture_output=True, text=True,
            )
            notes = output_path.read_text(encoding="utf-8")

        self.assertIn("public upstream provenance", notes)
        self.assertIn("Public follow-up", notes)
        self.assertIn("Public follow-up content must remain.", notes)
        self.assertIn("Stable downstream footer", notes)
        self.assertNotIn("Internal section secret", notes)
        self.assertNotIn("Nested internal secret", notes)
        self.assertNotIn("Docker secret", notes)
        self.assertNotIn("Internal secret", notes)
        self.assertNotIn("Adjacent release text", notes)
        self.assertNotIn("Unreleased", notes)
        self.assertIn("main branch", notes)
        self.assertNotIn("master branch", notes)


if __name__ == "__main__":
    unittest.main()
