from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts import check_release_safety


class ReleaseSafetyTests(unittest.TestCase):
    def test_placeholders_are_allowed_but_credentials_are_redacted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            token_key = "TWITCH_" + "OAUTH_TOKEN"
            (root / ".env.example").write_text(
                f"{token_key}=replace_me\n", encoding="utf-8"
            )
            self.assertEqual(check_release_safety.audit(root), [])

            (root / "secret.txt").write_text(
                f"{token_key}=" + "a" * 24 + "\n", encoding="utf-8"
            )
            findings = check_release_safety.audit(root)
            self.assertEqual(len(findings), 1)
            self.assertIn("TWITCH_OAUTH_TOKEN assignment", findings[0])
            self.assertNotIn("a" * 24, findings[0])

    def test_forced_tracked_artifacts_and_renamed_binaries_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(
                ["git", "init", "-q", str(root)], check=True, capture_output=True
            )
            token_key = "TWITCH_" + "OAUTH_TOKEN"
            (root / ".gitignore").write_text("/build/\n/bin/\n/obj/\n", encoding="utf-8")
            fixtures = {
                "bin/GeBot.exe": b"MZ\x00legacy",
                "obj/main.obj": b"object\x00legacy",
                "build/.env": f"{token_key}=not-a-real-token\n".encode(),
                "archive.dat": b"renamed\x00binary",
            }
            for relative, data in fixtures.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            subprocess.run(
                ["git", "-C", str(root), "add", "-f", ".gitignore", *fixtures],
                check=True,
                capture_output=True,
            )

            findings = check_release_safety.audit(root)
            finding_paths = {finding.split(":", maxsplit=1)[0] for finding in findings}
            self.assertEqual(finding_paths, set(fixtures))


if __name__ == "__main__":
    unittest.main()
