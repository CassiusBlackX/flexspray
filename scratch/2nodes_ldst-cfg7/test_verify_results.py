#!/usr/bin/env python3
"""Exercise the verifier on isolated copies of the real successful run."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from prepare_run import INPUTS

CASE = Path(__file__).resolve().parent


class RealResultVerifierTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ldst-cfg7-verify-")
        self.addCleanup(self.temp.cleanup)
        self.case = Path(self.temp.name)
        for name in (*INPUTS, "run-start.json", "runner.exit", "runner.log"):
            shutil.copy2(CASE / name, self.case / name)
        for name in ("runlog", "output"):
            shutil.copytree(CASE / name, self.case / name)

    def check(self, error=None):
        result = subprocess.run(
            [sys.executable, str(CASE / "verify_results.py"), "--case-path", str(self.case)],
            capture_output=True, text=True, check=False,
        )
        if error is None:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["tasks"], [0, 1, 2, 3])
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(error, result.stderr)
            print(result.stderr.strip())

    def test_real_success(self):
        self.check()

    def test_missing_file(self):
        (self.case / "output/task_statistics.csv").unlink()
        self.check("Missing evidence")

    def test_missing_task(self):
        path = self.case / "runlog/TaskTrace_node_0.csv"
        path.write_text("\n".join(line for line in path.read_text().splitlines()
                                  if ",0,," not in line) + "\n")
        self.check("Missing start/completion for task 0")

    def test_missing_completion(self):
        path = self.case / "runlog/TaskTrace_node_0.csv"
        path.write_text("\n".join(line for line in path.read_text().splitlines()
                                  if "MEM Task Completes,3," not in line) + "\n")
        self.check("Missing start/completion for task 3")

    def test_duplicate_completion(self):
        path = self.case / "runlog/TaskTrace_node_0.csv"
        duplicate = next(line for line in path.read_text().splitlines()
                         if "MEM Task Completes,0," in line)
        path.write_text(path.read_text() + duplicate + "\n")
        self.check("Duplicate MEM Task Completes for task 0")

    def test_failed_runner(self):
        (self.case / "runner.exit").write_text("1\n")
        self.check("Runner exited unsuccessfully")

    def test_runtime_drop(self):
        path = self.case / "runner.log"
        path.write_text(path.read_text() + "[error] Packet dropped while retransmission is disabled.\n")
        self.check("Runner reports failure or packet drop")

    def test_stale_evidence(self):
        os.utime(self.case / "output/task_statistics.csv", ns=(1, 1))
        self.check("Stale evidence")

    def test_changed_input(self):
        path = self.case / "network_attribute.txt"
        path.write_text(path.read_text() + "\n")
        self.check("Inputs changed since run")

    def test_incomplete_summary(self):
        path = self.case / "output/task_statistics.csv"
        path.write_text("\n".join(path.read_text().splitlines()[:-1]) + "\n")
        self.check("Summary must contain exactly tasks")

    def test_bad_time(self):
        path = self.case / "runlog/TaskTrace_node_0.csv"
        lines = path.read_text().splitlines()
        path.write_text("\n".join("nan," + line.split(",", 1)[1]
                                  if "MEM Task Completes,0," in line else line
                                  for line in lines) + "\n")
        self.check("Invalid timestamp")

    def test_missing_switch_rx(self):
        path = self.case / "runlog/PortTrace_node_2_port_0.csv"
        path.write_text("\n".join(line for line in path.read_text().splitlines()
                                  if ",Port Rx," not in line) + "\n")
        self.check("Missing middle-switch Rx/Tx")


if __name__ == "__main__":
    unittest.main(verbosity=2)
