"""Run the synthetic universal tests via the PS2 test entry point."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'universal' / 'tests'))
from test_patcher import PatcherTests, SafetyTests  # noqa: F401,E402
