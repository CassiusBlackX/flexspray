import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class BaselineHarnessCliTest(unittest.TestCase):
    def setUp(self):
        self.repo_root = Path(__file__).resolve().parents[2]
        self.harness = self.repo_root / "utils" / "unified_bus_baseline.py"
        self.temp_dir = tempfile.TemporaryDirectory()
        self.work_dir = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    @staticmethod
    def sha256(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def run_harness(self, *args):
        return subprocess.run(
            [sys.executable, str(self.harness), *map(str, args)],
            cwd=self.repo_root,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_verify_rejects_case_input_drift(self):
        repo_root = self.work_dir / "repo"
        artifact_root = self.work_dir / "artifacts"
        (repo_root / "cases" / "minimal").mkdir(parents=True)
        (repo_root / "rules").mkdir()
        (artifact_root / "variants" / "local-single" / "bin").mkdir(parents=True)

        case_input = repo_root / "cases" / "minimal" / "traffic.csv"
        rules = repo_root / "rules" / "comparison-rules.json"
        binary = artifact_root / "variants" / "local-single" / "bin" / "ub-quick-example"
        output_run_1 = artifact_root / "runs" / "minimal" / "run-1" / "result.txt"
        output_run_2 = artifact_root / "runs" / "minimal" / "run-2" / "result.txt"
        case_input.write_bytes(b"taskId,dataSize\n0,64\n")
        rules.write_text('{"cases": {}}\n', encoding="utf-8")
        binary.write_bytes(b"reference executable")
        output_run_1.parent.mkdir(parents=True)
        output_run_2.parent.mkdir(parents=True)
        output_run_1.write_bytes(b"stable output")
        output_run_2.write_bytes(b"stable output")

        manifest = {
            "schema_version": 1,
            "reference": {
                "variants": {
                    "local-single": {
                        "binary": {
                            "path": "variants/local-single/bin/ub-quick-example",
                            "sha256": self.sha256(binary),
                        }
                    }
                }
            },
            "comparison_rules": {
                "path": "rules/comparison-rules.json",
                "sha256": self.sha256(rules),
            },
            "cases": [
                {
                    "id": "minimal",
                    "inputs": {
                        "cases/minimal/traffic.csv": self.sha256(case_input),
                    },
                    "reference_outputs": [
                        {
                            "paths": [
                                "runs/minimal/run-1/result.txt",
                                "runs/minimal/run-2/result.txt",
                            ],
                            "sha256": self.sha256(output_run_1),
                        }
                    ],
                }
            ],
        }
        manifest_path = self.work_dir / "reference-manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        verified = self.run_harness(
            "verify",
            "--manifest",
            manifest_path,
            "--repo-root",
            repo_root,
            "--artifact-root",
            artifact_root,
        )
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn("verified_files=5", verified.stdout)

        case_input.write_bytes(b"taskId,dataSize\n0,128\n")
        drifted = self.run_harness(
            "verify",
            "--manifest",
            manifest_path,
            "--repo-root",
            repo_root,
            "--artifact-root",
            artifact_root,
        )
        self.assertEqual(drifted.returncode, 1)
        self.assertIn("hash mismatch", drifted.stderr)
        self.assertIn("cases/minimal/traffic.csv", drifted.stderr)

    def test_compare_reports_first_difference_and_preserves_both_outputs(self):
        reference = self.work_dir / "reference"
        candidate = self.work_dir / "candidate"
        diagnostics = self.work_dir / "diagnostics"
        reference.mkdir()
        candidate.mkdir()
        (reference / "a.txt").write_bytes(b"same")
        (candidate / "a.txt").write_bytes(b"same")
        (reference / "b.txt").write_bytes(b"abcXtail")
        (candidate / "b.txt").write_bytes(b"abcYtail")
        (reference / "context.txt").write_bytes(b"old context")
        (candidate / "context.txt").write_bytes(b"new context")

        rules = {
            "schema_version": 1,
            "cases": {
                "medium-shared-tp": {
                    "artifacts": [
                        {"path": "a.txt", "comparison": "raw-byte-equality"},
                        {"path": "b.txt", "comparison": "raw-byte-equality"},
                    ],
                    "canonical_only_artifacts": [],
                }
            },
        }
        rules_path = self.work_dir / "comparison-rules.json"
        rules_path.write_text(json.dumps(rules), encoding="utf-8")

        result = self.run_harness(
            "compare",
            "--rules",
            rules_path,
            "--case",
            "medium-shared-tp",
            "--reference",
            reference,
            "--candidate",
            candidate,
            "--diagnostics",
            diagnostics,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=b.txt", result.stderr)
        self.assertIn("first_byte_offset=3", result.stderr)
        self.assertEqual((diagnostics / "reference" / "context.txt").read_bytes(), b"old context")
        self.assertEqual((diagnostics / "candidate" / "context.txt").read_bytes(), b"new context")

    def test_repository_manifest_is_path_independent_and_complete(self):
        manifest_path = (
            self.repo_root / "tests" / "unified_bus_lifecycle" / "reference-manifest.json"
        )
        rules_path = self.repo_root / "tests" / "unified_bus_lifecycle" / "comparison-rules.json"
        manifest_text = manifest_path.read_text(encoding="utf-8")
        manifest = json.loads(manifest_text)
        rules = json.loads(rules_path.read_text(encoding="utf-8"))

        self.assertNotIn("/Users/", manifest_text)
        self.assertEqual(
            set(manifest["reference"]["variants"]),
            {"local-single", "local-mtp", "parallel-all"},
        )
        self.assertEqual(
            {case["id"] for case in manifest["cases"]},
            {
                "minimal-single-rtp-write",
                "medium-shared-tp-concurrency",
                "medium-multi-tp-retransmission",
                "medium-ctp-entity-vl-pressure",
                "medium-cross-owner-runtime",
            },
        )
        self.assertEqual(
            set(manifest["reference"]["runtime_variant_contract"]),
            {"local-single", "local-mtp", "mpi-single", "hybrid"},
        )
        for case in manifest["cases"]:
            self.assertEqual(case["canonical_only_artifacts"], [], case["id"])
        for profile, case_rules in rules["cases"].items():
            self.assertEqual(case_rules["canonical_only_artifacts"], [], profile)



if __name__ == "__main__":
    unittest.main()
