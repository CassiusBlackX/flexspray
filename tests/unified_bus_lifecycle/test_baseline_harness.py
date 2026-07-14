import hashlib
import json
import shutil
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
        self.compare_count = 0

    def tearDown(self):
        self.temp_dir.cleanup()

    @staticmethod
    def sha256(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    @staticmethod
    def byte_hex(path, offset):
        data = path.read_bytes()
        return None if offset == len(data) else f"{data[offset]:02x}"

    def run_harness(self, *args):
        return subprocess.run(
            [sys.executable, str(self.harness), *map(str, args)],
            cwd=self.repo_root,
            capture_output=True,
            text=True,
            check=False,
        )

    def write_compare_fixture(self, reference_bytes=b"old", candidate_bytes=b"new"):
        reference = self.work_dir / "reference"
        candidate = self.work_dir / "candidate"
        reference.mkdir()
        candidate.mkdir()
        (reference / "changed.txt").write_bytes(reference_bytes)
        (candidate / "changed.txt").write_bytes(candidate_bytes)
        (reference / "stable.txt").write_bytes(b"stable")
        (candidate / "stable.txt").write_bytes(b"stable")
        rules = {
            "schema_version": 1,
            "cases": {
                "milestone": {
                    "artifacts": [
                        {"path": "changed.txt", "comparison": "raw-byte-equality"},
                        {"path": "stable.txt", "comparison": "raw-byte-equality"},
                    ],
                    "canonical_only_artifacts": [],
                }
            },
        }
        rules_path = self.work_dir / "comparison-rules.json"
        rules_path.write_text(json.dumps(rules), encoding="utf-8")
        return reference, candidate, rules_path

    def write_exception_manifest(self, reference, candidate, **updates):
        manifest = {
            "schema_version": 1,
            "ticket": "T06",
            "case": "milestone",
            "runtime": "local-single",
            "approval": {
                "status": "approved",
                "source": "known acceptable difference policy",
                "date": "2026-07-14",
                "policy_statement": "Known, evidenced, acceptable differences may pass root review.",
                "approved_by": "root-review",
                "approved_cause_ids": ["known-cause"],
            },
            "evidence": {
                "specification": "test milestone requires the known byte change",
                "reference_root": str(reference),
                "candidate_root": str(candidate),
                "diagnostics_root": str(self.work_dir / "diagnostics"),
                "managed_artifact_count": 2,
                "affected_artifact_count": 1,
                "unaffected_artifact_count": 1,
                "records": [
                    {
                        "id": "fixture-proof",
                        "kind": "milestone-acceptance",
                        "summary": "Fixture hashes and byte offsets identify the known change.",
                        "ticket": "T06",
                        "runtime": "local-single",
                        "reference_root": str(reference),
                        "candidate_root": str(candidate),
                        "affected_artifacts": ["changed.txt"],
                        "claims": {"fixture_change": "one intentional byte change"},
                    }
                ],
            },
            "causes": [
                {
                    "id": "known-cause",
                    "cause": "The test candidate intentionally changes one byte.",
                    "impact": "Only changed.txt differs.",
                    "evidence": {"ids": ["fixture-proof"]},
                    "artifacts": ["changed.txt"],
                }
            ],
            "affected_artifacts": [
                {
                    "path": "changed.txt",
                    "cause_ids": ["known-cause"],
                    "first_byte_offset": 0,
                    "reference_byte_hex": self.byte_hex(reference / "changed.txt", 0),
                    "candidate_byte_hex": self.byte_hex(candidate / "changed.txt", 0),
                    "reference_sha256": self.sha256(reference / "changed.txt"),
                    "candidate_sha256": self.sha256(candidate / "changed.txt"),
                }
            ],
            "unaffected_artifacts": [
                {"path": "stable.txt", "sha256": self.sha256(reference / "stable.txt")}
            ],
        }
        for key, value in updates.items():
            if key == "evidence":
                manifest["evidence"].update(value)
            else:
                manifest[key] = value
        path = self.work_dir / "exception-manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        return path

    def independent_control_record(self):
        diagnostics = self.work_dir / "independent-control"
        reference = diagnostics / "reference"
        candidate = diagnostics / "candidate"
        reference.mkdir(parents=True, exist_ok=True)
        candidate.mkdir(parents=True, exist_ok=True)
        reference_artifact = reference / "changed.txt"
        candidate_artifact = candidate / "changed.txt"
        reference_artifact.write_bytes(b"before")
        candidate_artifact.write_bytes(b"after")
        return {
            "id": "fixture-proof",
            "kind": "independent-comparison-control",
            "summary": "An independent control isolates changed.txt.",
            "ticket": "T06",
            "runtime": "local-single",
            "source_commit": "fixture-control",
            "reference_root": str(reference),
            "candidate_root": str(candidate),
            "diagnostics_root": str(diagnostics),
            "artifact": "changed.txt",
            "first_byte_offset": 0,
            "reference_sha256": self.sha256(reference_artifact),
            "candidate_sha256": self.sha256(candidate_artifact),
            "affected_artifacts": ["changed.txt"],
        }

    def compare_fixture(
        self,
        reference,
        candidate,
        rules,
        exception_manifest=None,
        ticket="T06",
        runtime="local-single",
    ):
        self.compare_count += 1
        diagnostics = self.work_dir / "diagnostics"
        if diagnostics.exists():
            shutil.rmtree(diagnostics)
        args = [
            "compare",
            "--rules",
            rules,
            "--case",
            "milestone",
            "--reference",
            reference,
            "--candidate",
            candidate,
            "--diagnostics",
            diagnostics,
        ]
        if exception_manifest is not None:
            args.extend(["--exception-manifest", exception_manifest])
            if ticket is not None:
                args.extend(["--ticket", ticket])
            if runtime is not None:
                args.extend(["--runtime", runtime])
        return self.run_harness(*args)

    def test_compare_accepts_matching_approved_exception_manifest(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("case=milestone artifacts=approved-exceptions", result.stdout)

    def test_compare_rejects_unapproved_exception_manifest(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(
            reference, candidate, approval={"status": "pending"}
        )

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=changed.txt", result.stderr)

    def test_compare_rejects_missing_approval_source(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(
            reference,
            candidate,
            approval={"status": "approved", "date": "2026-07-14"},
        )

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("approval source", result.stderr)

    def test_compare_rejects_missing_cause_impact(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(
            reference,
            candidate,
            causes=[{"id": "known-cause", "cause": "Known test change"}],
        )

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("cause impact", result.stderr)

    def test_compare_rejects_missing_exception_ticket_or_runtime(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)

        missing_ticket = self.compare_fixture(reference, candidate, rules, manifest, ticket=None)
        missing_runtime = self.compare_fixture(reference, candidate, rules, manifest, runtime=None)

        self.assertEqual(missing_ticket.returncode, 1)
        self.assertIn("--ticket", missing_ticket.stderr)
        self.assertEqual(missing_runtime.returncode, 1)
        self.assertIn("--runtime", missing_runtime.stderr)

    def test_compare_requires_exception_context_even_when_bytes_match(self):
        reference, candidate, rules = self.write_compare_fixture(b"same", b"same")
        manifest = self.write_exception_manifest(reference, candidate)

        result = self.compare_fixture(reference, candidate, rules, manifest, ticket=None)

        self.assertEqual(result.returncode, 1)
        self.assertIn("--ticket", result.stderr)

    def test_compare_rejects_ticket_runtime_and_root_mismatch(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)

        ticket = self.compare_fixture(reference, candidate, rules, manifest, ticket="T99")
        runtime = self.compare_fixture(reference, candidate, rules, manifest, runtime="mpi-single")
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["evidence"]["candidate_root"] = str(self.work_dir / "other")
        manifest.write_text(json.dumps(value), encoding="utf-8")
        root = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertIn("ticket mismatch", ticket.stderr)
        self.assertIn("runtime mismatch", runtime.stderr)
        self.assertIn("candidate_root mismatch", root.stderr)

    def test_compare_rejects_approval_cause_mismatch(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["approval"]["approved_cause_ids"] = ["other"]
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("approved cause", result.stderr)

    def test_compare_rejects_cause_artifact_mismatch(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["causes"][0]["artifacts"] = ["stable.txt"]
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("cause artifacts mismatch", result.stderr)

    def test_compare_rejects_unknown_evidence_id(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["causes"][0]["evidence"]["ids"] = ["unknown"]
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown evidence id", result.stderr)

    def test_compare_rejects_invalid_date(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["approval"]["date"] = "2026-02-30"
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("ISO YYYY-MM-DD", result.stderr)

    def test_compare_rejects_weak_generic_evidence(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["evidence"]["records"] = [
            {"id": "fixture-proof", "kind": "generic", "summary": "Trust this summary."}
        ]
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("unsupported evidence kind", result.stderr)

    def test_compare_rejects_stale_diagnostics_root(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(
            reference,
            candidate,
            evidence={"diagnostics_root": str(self.work_dir / "old-diagnostics")},
        )

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("diagnostics_root mismatch", result.stderr)

    def test_compare_rejects_forged_independent_control_root_and_hash(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        record = self.independent_control_record()
        record["candidate_root"] = str(self.work_dir / "independent-control" / "forged")
        value["evidence"]["records"] = [record]
        manifest.write_text(json.dumps(value), encoding="utf-8")
        forged_root = self.compare_fixture(reference, candidate, rules, manifest)

        value = json.loads(self.write_exception_manifest(reference, candidate).read_text())
        record = self.independent_control_record()
        record["candidate_sha256"] = "0" * 64
        value["evidence"]["records"] = [record]
        manifest.write_text(json.dumps(value), encoding="utf-8")
        stale_hash = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(forged_root.returncode, 1)
        self.assertIn("No such file", forged_root.stderr)
        self.assertEqual(stale_hash.returncode, 1)
        self.assertIn("stale candidate sha256", stale_hash.stderr)

    def test_compare_rejects_independent_control_runtime_mismatch(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        record = self.independent_control_record()
        record["runtime"] = "mpi-single"
        value["evidence"]["records"] = [record]
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("runtime mismatch", result.stderr)

    def test_compare_rejects_missing_classification_and_count_mismatch(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["unaffected_artifacts"] = []
        manifest.write_text(json.dumps(value), encoding="utf-8")
        missing = self.compare_fixture(reference, candidate, rules, manifest)

        value = json.loads(self.write_exception_manifest(reference, candidate).read_text())
        value["evidence"]["affected_artifact_count"] = 2
        manifest.write_text(json.dumps(value), encoding="utf-8")
        count = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertIn("identical managed set", missing.stderr)
        self.assertIn("count mismatch", count.stderr)

    def test_compare_rejects_missing_affected_artifact_entry(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["affected_artifacts"] = []
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("actual differing artifact set", result.stderr)

    def test_compare_exception_rejects_missing_or_unclassified_artifacts(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        (candidate / "stable.txt").unlink()
        missing = self.compare_fixture(reference, candidate, rules, manifest)

        (candidate / "stable.txt").write_bytes(b"stable")
        (candidate / "unexpected.txt").write_bytes(b"unexpected")
        unclassified = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertIn("missing or unclassified", missing.stderr)
        self.assertIn("missing or unclassified", unclassified.stderr)

    def test_compare_rejects_stale_common_hash(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["unaffected_artifacts"][0]["sha256"] = "0" * 64
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("stale common sha256", result.stderr)

    def test_compare_rejects_unknown_and_unused_causes(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["affected_artifacts"][0]["cause_ids"] = ["unknown"]
        manifest.write_text(json.dumps(value), encoding="utf-8")
        unknown = self.compare_fixture(reference, candidate, rules, manifest)

        value = json.loads(self.write_exception_manifest(reference, candidate).read_text())
        value["causes"].append(
            {
                "id": "unused",
                "cause": "Unused",
                "impact": "None",
                "evidence": {"ids": ["fixture-proof"]},
                "artifacts": [],
            }
        )
        value["approval"]["approved_cause_ids"].append("unused")
        manifest.write_text(json.dumps(value), encoding="utf-8")
        unused = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertIn("unknown cause id", unknown.stderr)
        self.assertIn("unused cause id", unused.stderr)

    def test_compare_rejects_stale_first_byte_evidence(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["affected_artifacts"][0]["candidate_byte_hex"] = "ff"
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("stale candidate byte", result.stderr)

    def test_compare_accepts_explicit_null_for_eof_byte(self):
        reference, candidate, rules = self.write_compare_fixture(b"a", b"ab")
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = value["affected_artifacts"][0]
        artifact["first_byte_offset"] = 1
        artifact["reference_byte_hex"] = None
        artifact["candidate_byte_hex"] = "62"
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("artifacts=approved-exceptions", result.stdout)

    def test_compare_rejects_missing_eof_byte_field(self):
        reference, candidate, rules = self.write_compare_fixture(b"a", b"ab")
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = value["affected_artifacts"][0]
        artifact["first_byte_offset"] = 1
        artifact.pop("reference_byte_hex")
        artifact["candidate_byte_hex"] = "62"
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("missing reference_byte_hex", result.stderr)

    def test_compare_rejects_stale_candidate_hash(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        value = json.loads(manifest.read_text(encoding="utf-8"))
        value["affected_artifacts"][0]["candidate_sha256"] = "0" * 64
        manifest.write_text(json.dumps(value), encoding="utf-8")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=changed.txt", result.stderr)

    def test_compare_rejects_unlisted_new_difference(self):
        reference, candidate, rules = self.write_compare_fixture()
        manifest = self.write_exception_manifest(reference, candidate)
        (candidate / "stable.txt").write_bytes(b"now changed")

        result = self.compare_fixture(reference, candidate, rules, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=changed.txt", result.stderr)

    def test_compare_exact_match_needs_no_exception_manifest(self):
        reference, candidate, rules = self.write_compare_fixture(b"same", b"same")

        result = self.compare_fixture(reference, candidate, rules)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("case=milestone artifacts=identical", result.stdout)

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

    def test_verify_rejects_unregistered_case_input(self):
        repo_root = self.work_dir / "repo"
        artifact_root = self.work_dir / "artifacts"
        case_root = repo_root / "cases" / "minimal"
        rules_root = repo_root / "rules"
        case_root.mkdir(parents=True)
        rules_root.mkdir()

        case_input = case_root / "traffic.csv"
        rules = rules_root / "comparison-rules.json"
        case_input.write_bytes(b"taskId,dataSize\n0,64\n")
        rules.write_text('{"cases": {}}\n', encoding="utf-8")
        manifest = {
            "schema_version": 1,
            "reference": {"variants": {}},
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
                    "reference_outputs": [],
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

        (case_root / "fault.csv").write_bytes(b"faultId\n0\n")
        unregistered = self.run_harness(
            "verify",
            "--manifest",
            manifest_path,
            "--repo-root",
            repo_root,
            "--artifact-root",
            artifact_root,
        )
        self.assertEqual(unregistered.returncode, 1)
        self.assertIn("unregistered case input", unregistered.stderr)
        self.assertIn("cases/minimal/fault.csv", unregistered.stderr)

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
                        {"path": "context.txt", "comparison": "raw-byte-equality"},
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

    def test_compare_rejects_missing_managed_artifact(self):
        reference = self.work_dir / "reference"
        candidate = self.work_dir / "candidate"
        diagnostics = self.work_dir / "diagnostics"
        reference.mkdir()
        candidate.mkdir()
        (reference / "required.txt").write_bytes(b"stable")

        rules = {
            "schema_version": 1,
            "cases": {
                "minimal": {
                    "artifacts": [
                        {"path": "required.txt", "comparison": "raw-byte-equality"},
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
            "minimal",
            "--reference",
            reference,
            "--candidate",
            candidate,
            "--diagnostics",
            diagnostics,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=required.txt", result.stderr)
        self.assertIn("first_byte_offset=0", result.stderr)

    def test_compare_rejects_unclassified_reference_artifact(self):
        reference = self.work_dir / "reference"
        candidate = self.work_dir / "candidate"
        diagnostics = self.work_dir / "diagnostics"
        reference.mkdir()
        candidate.mkdir()
        (reference / "stable.txt").write_bytes(b"stable")
        (candidate / "stable.txt").write_bytes(b"stable")
        (reference / "new-output.txt").write_bytes(b"unexpected")

        rules = {
            "schema_version": 1,
            "cases": {
                "minimal": {
                    "artifacts": [
                        {"path": "stable.txt", "comparison": "raw-byte-equality"},
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
            "minimal",
            "--reference",
            reference,
            "--candidate",
            candidate,
            "--diagnostics",
            diagnostics,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=new-output.txt", result.stderr)
        self.assertIn("first_byte_offset=0", result.stderr)

    def test_compare_rejects_unclassified_candidate_artifact(self):
        reference = self.work_dir / "reference"
        candidate = self.work_dir / "candidate"
        diagnostics = self.work_dir / "diagnostics"
        reference.mkdir()
        candidate.mkdir()
        (reference / "stable.txt").write_bytes(b"stable")
        (candidate / "stable.txt").write_bytes(b"stable")
        (candidate / "new-output.txt").write_bytes(b"unexpected")

        rules = {
            "schema_version": 1,
            "cases": {
                "minimal": {
                    "artifacts": [
                        {"path": "stable.txt", "comparison": "raw-byte-equality"},
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
            "minimal",
            "--reference",
            reference,
            "--candidate",
            candidate,
            "--diagnostics",
            diagnostics,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("first_differing_artifact=new-output.txt", result.stderr)
        self.assertIn("first_byte_offset=0", result.stderr)

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
            self.assertTrue(case_rules["artifacts"], profile)
            self.assertEqual(
                {artifact["comparison"] for artifact in case_rules["artifacts"]},
                {"raw-byte-equality"},
                profile,
            )

        runtime_profiles = {
            ("minimal-single-rtp-write", "local-single"): "minimal-single-rtp-write",
            ("medium-shared-tp-concurrency", "local-single"): "medium-shared-tp-concurrency",
            ("medium-multi-tp-retransmission", "local-single"): (
                "medium-multi-tp-retransmission"
            ),
            ("medium-ctp-entity-vl-pressure", "local-single"): (
                "medium-ctp-entity-vl-pressure"
            ),
            ("medium-cross-owner-runtime", "local-single"): (
                "medium-cross-owner-runtime-local"
            ),
            ("medium-cross-owner-runtime", "local-mtp"): (
                "medium-cross-owner-runtime-local"
            ),
            ("medium-cross-owner-runtime", "mpi-single"): (
                "medium-cross-owner-runtime-mpi"
            ),
            ("medium-cross-owner-runtime", "hybrid"): (
                "medium-cross-owner-runtime-hybrid"
            ),
        }
        for case in manifest["cases"]:
            input_names = {Path(relative).name for relative in case["inputs"]}
            runtimes = case.get("runtimes")
            if runtimes is None:
                runtimes = [case["runtime"]]
            for runtime in runtimes:
                profile = runtime_profiles[(case["id"], runtime)]
                managed = {artifact["path"] for artifact in rules["cases"][profile]["artifacts"]}
                expected_outputs = managed - input_names
                recorded_outputs = {
                    output["artifact"]
                    for output in case["reference_outputs"]
                    if output["runtime"] == runtime
                }
                self.assertEqual(recorded_outputs, expected_outputs, (case["id"], runtime))



if __name__ == "__main__":
    unittest.main()
