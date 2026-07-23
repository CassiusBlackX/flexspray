#!/usr/bin/env python3
"""Focused fail-closed unit tests for the T55 analyzer."""

from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

from analyze_t55 import (
    classify_treatment,
    parse_canonical,
    parse_snapshot,
    verify_artifact_hashes,
    verify_case_inputs,
    verify_phase_ordering,
)


class AnalyzerTest(unittest.TestCase):
    def snapshot(self, **overrides: int) -> dict[str, int]:
        values = {
            "canonical_connections": 32,
            "resident_tp_endpoints": 64,
            "dormant_tp_records": 64,
            "tp_backing_rehydrations": 64,
            "peak_live_tp_backing_objects": 64,
            "live_tp_backing_objects": 0,
            "traffic_stored_tasks": 64,
            "traffic_completed_tasks": 64,
            "traffic_dependency_references": 0,
        }
        values.update(overrides)
        return values

    def test_classifier_matches_only_complete_witness(self) -> None:
        self.assertEqual(classify_treatment(True, True, True, self.snapshot()), "matched")

    def test_classifier_rejects_zero_restoration(self) -> None:
        self.assertEqual(classify_treatment(
            True, True, True, self.snapshot(tp_backing_rehydrations=0)
        ), "mismatched")

    def test_classifier_rejects_non_exact_restoration_count(self) -> None:
        self.assertEqual(classify_treatment(
            True, True, True, self.snapshot(tp_backing_rehydrations=63)
        ), "mismatched")

    def test_classifier_marks_missing_snapshot_partial(self) -> None:
        self.assertEqual(classify_treatment(True, True, False, None), "partially_matched")

    def test_classifier_marks_failed_run_inconclusive(self) -> None:
        self.assertEqual(classify_treatment(False, False, False, None), "inconclusive")

    def test_classifier_marks_canonical_mismatch(self) -> None:
        self.assertEqual(classify_treatment(True, False, True, self.snapshot()), "mismatched")

    def test_snapshot_accepts_storage_and_traffic_invariants(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "snapshot.csv"
            rows = [
                ("runtime", "canonical_connections", "", 32),
                ("runtime", "resident_tp_endpoints", "", 64),
                ("runtime", "live_tp_backing_objects", "", 0),
                ("runtime", "dormant_tp_records", "", 64),
                ("runtime", "peak_live_tp_backing_objects", "", 64),
                ("runtime", "tp_backing_rehydrations", "", 64),
                ("traffic", "stored_tasks", "", 64),
                ("traffic", "completed_tasks", "", 64),
                ("traffic", "dependency_references", "", 0),
            ]
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream, lineterminator="\n")
                writer.writerow(["schema_version", "category", "metric", "resource_kind", "value"])
                for category, metric, kind, value in rows:
                    writer.writerow([1, category, metric, kind, value])
            self.assertEqual(parse_snapshot(path)["tp_backing_rehydrations"], 64)

    def test_snapshot_rejects_duplicate_metric(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "snapshot.csv"
            path.write_text(
                "schema_version,category,metric,resource_kind,value\n"
                "1,runtime,canonical_connections,,32\n"
                "1,runtime,canonical_connections,,32\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(RuntimeError, "duplicate"):
                parse_snapshot(path)

    def test_snapshot_rejects_non_exact_restoration_count(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "snapshot.csv"
            source = (
                Path(__file__).resolve().parents[1]
                / "results/t55-after/runtime-storage-snapshot.csv"
            )
            path.write_text(
                source.read_text(encoding="utf-8").replace(
                    "1,runtime,tp_backing_rehydrations,,64",
                    "1,runtime,tp_backing_rehydrations,,63",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "exact witness"):
                parse_snapshot(path)

    def test_canonical_rejects_incomplete_event_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "canonical.csv"
            path.write_text("0,START,0,0,1\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "event count"):
                parse_canonical(path)

    def test_case_rejects_phase_one_identity_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            source = Path(__file__).resolve().parents[1] / "results/t55-after/case"
            for path in source.iterdir():
                if not path.is_file():
                    continue
                (case / path.name).write_bytes(path.read_bytes())
            traffic = case / "traffic.csv"
            traffic.write_text(
                traffic.read_text(encoding="utf-8").replace(
                    "32,0,1,65536,URMA_WRITE,7,1000ns,1,0",
                    "32,0,2,65536,URMA_WRITE,7,1000ns,1,0",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "input hash drift|identity mismatch"):
                verify_case_inputs(case, require_frozen_hashes=False)

    def test_artifact_hash_drift_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = root / "stdout.txt"
            artifact.write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "artifact hash drift"):
                verify_artifact_hashes(root, {"stdout.txt": "0" * 64})

    def test_phase_one_must_start_after_all_phase_zero_completion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            canonical = root / "canonical.csv"
            source = Path(__file__).resolve().parents[1] / "results/t55-after"
            lines = source.joinpath("canonical-output.csv").read_text(
                encoding="utf-8"
            ).splitlines()
            lines = [
                line.replace("2799,START,32,", "1799,START,32,")
                for line in lines
            ]
            canonical.write_text("\n".join(sorted(lines)) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "phase ordering"):
                verify_phase_ordering(canonical, source / "case")


if __name__ == "__main__":
    unittest.main()
