#!/usr/bin/env python3
"""Tests for the prepared structured-case launcher."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("run_structured_topo_case.py")
SPEC = importlib.util.spec_from_file_location("run_structured_topo_case", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class StructuredCaseRunnerTest(unittest.TestCase):
    def _case(self, route_generation):
        temporary = tempfile.TemporaryDirectory()
        case = Path(temporary.name)
        (case / "structured_topology.txt").write_text("prepared\n", encoding="utf-8")
        (case / "compressed_routing.bin").write_bytes(b"prepared")
        (case / "structured_summary.json").write_text(
            json.dumps({"routeGeneration": route_generation}),
            encoding="utf-8",
        )
        self.addCleanup(temporary.cleanup)
        return case

    @mock.patch.object(RUNNER.subprocess, "run")
    def test_compressed_runtime_forwards_binary_and_simulator_arguments(self, run):
        run.return_value.returncode = 0
        case = self._case("full")

        result = RUNNER.main(
            [
                "--case-path",
                str(case),
                "--route-runtime-mode",
                "compressed-table-lookup",
                "--",
                "--stop-ms=5",
            ]
        )

        self.assertEqual(result, 0)
        command = run.call_args.args[0]
        self.assertIn("--route-runtime-mode=compressed-table-lookup", command[-1])
        self.assertIn("--compressed-routing-file=", command[-1])
        self.assertIn("--stop-ms=5", command[-1])

    @mock.patch.object(RUNNER.subprocess, "run")
    def test_structured_runtime_omits_compressed_binary(self, run):
        run.return_value.returncode = 0
        case = self._case("mixed")

        result = RUNNER.main(
            [
                "--case-path",
                str(case),
                "--route-runtime-mode",
                "structured-rule-compute",
            ]
        )

        self.assertEqual(result, 0)
        command = run.call_args.args[0]
        self.assertIn("--route-runtime-mode=structured-rule-compute", command[-1])
        self.assertNotIn("--compressed-routing-file=", command[-1])

    @mock.patch.object(RUNNER.subprocess, "run")
    def test_full_generation_rejects_structured_runtime(self, run):
        case = self._case("full")

        result = RUNNER.main(
            [
                "--case-path",
                str(case),
                "--route-runtime-mode",
                "structured-rule-compute",
            ]
        )

        self.assertEqual(result, 2)
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
