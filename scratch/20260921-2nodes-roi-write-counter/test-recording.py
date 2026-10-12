#!/usr/bin/env python3
"""Recorder failure-path characterization; no additional network experiments."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


class RecordingFailureTest(unittest.TestCase):
    def test_truncated_parameters_preserve_process_outcome_and_archive(self):
        script = Path(__file__).with_name("record-run.py")
        spec = importlib.util.spec_from_file_location("record_run", script)
        recorder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(recorder)
        for child_code in (-9, 0):
            with self.subTest(child_return_code=child_code), tempfile.TemporaryDirectory() as temporary:
                repo = Path(temporary)
                package = repo / "scratch/20260921-2nodes-roi-write-counter"
                output = package / "cases/control/output"
                output.mkdir(parents=True)
                (output / "prior-evidence.txt").write_text("preserve me")
                (package / "ub-roi-write-counter.cc").write_text("test source identity")
                binary = repo / "build" / package.relative_to(repo) / "ns3.44-ub-roi-write-counter"
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b"test executable identity")

                def killed_child(command, **kwargs):
                    (output / "resolved-parameters.json").write_text('{"truncated":')
                    return subprocess.CompletedProcess(command, child_code)

                with patch.object(recorder, "__file__", str(package / "record-run.py")), \
                     patch.object(recorder.subprocess, "check_output", return_value="test-head\n"), \
                     patch.object(recorder.subprocess, "run", side_effect=killed_child), \
                     patch("sys.argv", [str(script), "--variant=control"]), \
                     contextlib.redirect_stdout(io.StringIO()):
                    returned = recorder.main()
                record = json.loads((output / "run-record.json").read_text())
                self.assertEqual(record["return_code"], child_code)
                self.assertEqual(record["status"], "failed" if child_code else "success")
                self.assertEqual(returned, child_code if child_code else 2)
                self.assertTrue(record["ended"])
                self.assertTrue(record["evidence_errors"])
                self.assertEqual((repo / record["archived"] / "prior-evidence.txt").read_text(),
                                 "preserve me")
                history = (package / "output/run-history.jsonl").read_text().splitlines()
                self.assertEqual(json.loads(history[-1]), record)


if __name__ == "__main__":
    unittest.main()
