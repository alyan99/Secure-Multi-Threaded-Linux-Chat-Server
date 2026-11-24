"""Phase 1 executable smoke checks. Run after `make` on Linux."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FoundationTests(unittest.TestCase):
    def test_executable_interfaces(self):
        for name in ("server_app", "client_app"):
            cases = ((["--help"], 0, "Usage:"),
                     (["--version"], 0, "1.0.0"),
                     (["--invalid"], 1, "Usage:"),
                     (["--help", "extra"], 1, "Usage:"))
            for args, status, expected in cases:
                with self.subTest(program=name, args=args):
                    result = subprocess.run(
                        [str(ROOT / name), *args], capture_output=True,
                        text=True, timeout=5, check=False,
                    )
                    self.assertEqual(result.returncode, status)
                    self.assertIn(expected, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
