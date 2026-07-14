#!/usr/bin/env python3
"""Verify and compare immutable Unified Bus lifecycle reference artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable, Mapping


CHUNK_SIZE = 1024 * 1024
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
BYTE_HEX_PATTERN = re.compile(r"^[0-9a-f]{2}$")


class BaselineError(Exception):
    """An invalid manifest, drifted input, or comparison mismatch."""


@dataclass(frozen=True)
class ComparisonResult:
    differing: dict[str, int]
    identical: set[str]
    managed: set[str]
    file_set_valid: bool


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise BaselineError(f"expected a JSON object: {path}")
    return value


def resolve_relative(root: Path, relative: str) -> Path:
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise BaselineError(f"path must be relative and cannot contain '..': {relative}")
    return root / path


def iter_hashed_files(
    manifest: Mapping,
    repo_root: Path,
    artifact_root: Path,
) -> Iterable[tuple[Path, str, str]]:
    reference = manifest.get("reference", {})
    for variant_id, variant in sorted(reference.get("variants", {}).items()):
        binary = variant["binary"]
        yield (
            resolve_relative(artifact_root, binary["path"]),
            binary["sha256"],
            f"reference variant {variant_id} binary",
        )
        for library in variant.get("runtime_libraries", []):
            yield (
                resolve_relative(artifact_root, library["path"]),
                library["sha256"],
                f"reference variant {variant_id} runtime library",
            )

    rules = manifest["comparison_rules"]
    yield (
        resolve_relative(repo_root, rules["path"]),
        rules["sha256"],
        "comparison rules",
    )

    for case in manifest.get("cases", []):
        for relative, expected in sorted(case.get("inputs", {}).items()):
            yield resolve_relative(repo_root, relative), expected, f"case {case['id']} input"
        for output in case.get("reference_outputs", []):
            paths = output.get("paths")
            if paths is None:
                paths = [output["path"]]
            for relative in paths:
                yield (
                    resolve_relative(artifact_root, relative),
                    output["sha256"],
                    f"case {case['id']} reference output",
                )


def relative_files(root: Path) -> set[str]:
    if not root.is_dir():
        raise BaselineError(f"missing directory: {root}")
    return {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file()
    }


def verify_case_input_sets(manifest: Mapping, repo_root: Path) -> None:
    for case in manifest.get("cases", []):
        inputs = case.get("inputs", {})
        if not inputs:
            continue
        parents = {Path(relative).parent for relative in inputs}
        if len(parents) != 1:
            raise BaselineError(f"case {case['id']} inputs must share one source directory")
        source_relative = parents.pop()
        source_root = resolve_relative(repo_root, source_relative.as_posix())
        expected = {Path(relative).name for relative in inputs}
        actual = relative_files(source_root)
        extras = sorted(actual - expected)
        if extras:
            extra = (source_relative / extras[0]).as_posix()
            raise BaselineError(f"unregistered case input: {extra}")
        missing = sorted(expected - actual)
        if missing:
            absent = (source_relative / missing[0]).as_posix()
            raise BaselineError(f"missing registered case input: {absent}")


def verify_manifest(manifest_path: Path, repo_root: Path, artifact_root: Path) -> int:
    manifest = read_json(manifest_path)
    if manifest.get("schema_version") != 1:
        raise BaselineError("unsupported manifest schema_version")

    verify_case_input_sets(manifest, repo_root)
    verified = 0
    for path, expected, description in iter_hashed_files(manifest, repo_root, artifact_root):
        if not path.is_file():
            raise BaselineError(f"missing {description}: {path}")
        actual = sha256(path)
        if actual != expected:
            try:
                display_path = path.relative_to(repo_root)
            except ValueError:
                try:
                    display_path = path.relative_to(artifact_root)
                except ValueError:
                    display_path = path
            raise BaselineError(
                f"hash mismatch for {display_path}: expected {expected}, actual {actual}"
            )
        verified += 1
    return verified


def first_byte_offset(reference: Path, candidate: Path) -> int | None:
    offset = 0
    with reference.open("rb") as old_stream, candidate.open("rb") as new_stream:
        while True:
            old_chunk = old_stream.read(CHUNK_SIZE)
            new_chunk = new_stream.read(CHUNK_SIZE)
            common_size = min(len(old_chunk), len(new_chunk))
            for index in range(common_size):
                if old_chunk[index] != new_chunk[index]:
                    return offset + index
            if len(old_chunk) != len(new_chunk):
                return offset + common_size
            if not old_chunk:
                return None
            offset += len(old_chunk)


def preserve_outputs(reference: Path, candidate: Path, diagnostics: Path) -> None:
    if diagnostics.exists():
        raise BaselineError(f"diagnostics path already exists: {diagnostics}")
    diagnostics.mkdir(parents=True)
    shutil.copytree(reference, diagnostics / "reference")
    shutil.copytree(candidate, diagnostics / "candidate")


def compare_case(
    rules_path: Path,
    case_id: str,
    reference: Path,
    candidate: Path,
    diagnostics: Path,
) -> ComparisonResult:
    rules = read_json(rules_path)
    if rules.get("schema_version") != 1:
        raise BaselineError("unsupported comparison-rules schema_version")
    try:
        case_rules = rules["cases"][case_id]
    except KeyError as error:
        raise BaselineError(f"unknown case in comparison rules: {case_id}") from error

    artifacts = case_rules.get("artifacts", [])
    artifacts_by_path = {artifact["path"]: artifact for artifact in artifacts}
    if len(artifacts_by_path) != len(artifacts):
        raise BaselineError(f"duplicate artifact path in comparison rules: {case_id}")
    for artifact in artifacts:
        if artifact.get("comparison") != "raw-byte-equality":
            raise BaselineError(
                f"unsupported comparison for {artifact['path']}: {artifact.get('comparison')}"
            )

    reference_files = relative_files(reference)
    candidate_files = relative_files(candidate)
    managed_files = set(artifacts_by_path)
    differing = {}
    identical = set()
    file_set_valid = reference_files == managed_files and candidate_files == managed_files
    for relative in sorted(managed_files | reference_files | candidate_files):
        if relative not in managed_files:
            differing[relative] = 0
            continue
        old_path = resolve_relative(reference, relative)
        new_path = resolve_relative(candidate, relative)
        if relative not in reference_files or relative not in candidate_files:
            differing[relative] = 0
            continue
        offset = first_byte_offset(old_path, new_path)
        if offset is not None:
            differing[relative] = offset
        else:
            identical.add(relative)
    if differing:
        preserve_outputs(reference, candidate, diagnostics)
    return ComparisonResult(differing, identical, managed_files, file_set_valid)


def require_object(value, description: str) -> Mapping:
    if not isinstance(value, dict):
        raise BaselineError(f"expected {description} to be an object")
    return value


def require_object_list(value, description: str) -> list[Mapping]:
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        raise BaselineError(f"expected {description} to be an array of objects")
    return value


def indexed_artifacts(value, description: str) -> dict[str, Mapping]:
    artifacts = require_object_list(value, description)
    indexed = {}
    for artifact in artifacts:
        path = artifact.get("path")
        if not isinstance(path, str) or not path:
            raise BaselineError(f"invalid path in {description}")
        resolve_relative(Path("."), path)
        if path in indexed:
            raise BaselineError(f"duplicate artifact path in {description}: {path}")
        indexed[path] = artifact
    return indexed


def checked_sha256(value, description: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise BaselineError(f"invalid sha256 for {description}")
    return value.lower()


def require_nonempty_string(value, description: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BaselineError(f"missing or empty {description}")
    return value


def require_unique_strings(value, description: str, allow_empty: bool = False) -> list[str]:
    if (
        not isinstance(value, list)
        or (not value and not allow_empty)
        or any(not isinstance(item, str) or not item for item in value)
        or len(set(value)) != len(value)
    ):
        raise BaselineError(f"invalid {description}")
    return value


def normalized_path(value: str | Path) -> Path:
    return Path(value).expanduser().resolve(strict=False)


def require_matching_context(
    record: Mapping,
    ticket: str,
    runtime: str,
    reference: Path,
    candidate: Path,
    description: str,
) -> None:
    if record.get("ticket") != ticket:
        raise BaselineError(f"{description} ticket mismatch")
    if record.get("runtime") != runtime:
        raise BaselineError(f"{description} runtime mismatch")
    for field, actual in (("reference_root", reference), ("candidate_root", candidate)):
        recorded = require_nonempty_string(record.get(field), f"{description} {field}")
        if normalized_path(recorded) != normalized_path(actual):
            raise BaselineError(f"{description} {field} mismatch")


def validate_evidence_record(
    record: Mapping,
    ticket: str,
    runtime: str,
    reference: Path,
    candidate: Path,
) -> str:
    record_id = require_nonempty_string(record.get("id"), "evidence record id")
    kind = require_nonempty_string(record.get("kind"), f"evidence kind for {record_id}")
    require_nonempty_string(record.get("summary"), f"evidence summary for {record_id}")
    if kind == "milestone-acceptance":
        require_matching_context(
            record, ticket, runtime, reference, candidate, f"evidence {record_id}"
        )
        require_unique_strings(
            record.get("affected_artifacts"), f"affected artifacts for evidence {record_id}"
        )
        claims = require_object(record.get("claims"), f"claims for evidence {record_id}")
        if not claims:
            raise BaselineError(f"empty claims for evidence {record_id}")
    elif kind == "independent-comparison-control":
        if record.get("ticket") != ticket:
            raise BaselineError(f"evidence {record_id} ticket mismatch")
        if record.get("runtime") != runtime:
            raise BaselineError(f"evidence {record_id} runtime mismatch")
        require_nonempty_string(record.get("source_commit"), f"source commit for {record_id}")
        artifact = require_nonempty_string(record.get("artifact"), f"artifact for {record_id}")
        resolve_relative(Path("."), artifact)
        affected = require_unique_strings(
            record.get("affected_artifacts"), f"affected artifacts for evidence {record_id}"
        )
        if affected != [artifact]:
            raise BaselineError(f"independent control affected artifact mismatch for {record_id}")
        reference_root = normalized_path(
            require_nonempty_string(record.get("reference_root"), f"reference root for {record_id}")
        )
        candidate_root = normalized_path(
            require_nonempty_string(record.get("candidate_root"), f"candidate root for {record_id}")
        )
        diagnostics_root = normalized_path(
            require_nonempty_string(
                record.get("diagnostics_root"), f"diagnostics root for {record_id}"
            )
        )
        if reference_root.parent != diagnostics_root or candidate_root.parent != diagnostics_root:
            raise BaselineError(f"independent control roots mismatch for {record_id}")
        reference_path = resolve_relative(reference_root, artifact)
        candidate_path = resolve_relative(candidate_root, artifact)
        offset = record.get("first_byte_offset")
        if isinstance(offset, bool) or not isinstance(offset, int) or offset < 0:
            raise BaselineError(f"invalid first byte offset for evidence {record_id}")
        if first_byte_offset(reference_path, candidate_path) != offset:
            raise BaselineError(f"stale first byte offset for evidence {record_id}")
        expected_reference = checked_sha256(
            record.get("reference_sha256"), f"independent reference for {record_id}"
        )
        expected_candidate = checked_sha256(
            record.get("candidate_sha256"), f"independent candidate for {record_id}"
        )
        if sha256(reference_path) != expected_reference:
            raise BaselineError(f"stale reference sha256 for evidence {record_id}")
        if sha256(candidate_path) != expected_candidate:
            raise BaselineError(f"stale candidate sha256 for evidence {record_id}")
    else:
        raise BaselineError(f"unsupported evidence kind: {kind}")
    return record_id


def byte_hex_at(path: Path, offset: int) -> str | None:
    with path.open("rb") as stream:
        stream.seek(offset)
        value = stream.read(1)
    return value.hex() if value else None


def validate_byte_hex(value, description: str) -> str | None:
    if value is not None and (not isinstance(value, str) or BYTE_HEX_PATTERN.fullmatch(value) is None):
        raise BaselineError(f"invalid {description}")
    return value


def validate_exception_manifest(
    manifest_path: Path,
    case_id: str,
    ticket: str,
    runtime: str,
    reference: Path,
    candidate: Path,
    diagnostics: Path,
    result: ComparisonResult,
) -> None:
    manifest = read_json(manifest_path)
    if manifest.get("schema_version") != 1:
        raise BaselineError("unsupported exception-manifest schema_version")
    approval = require_object(manifest.get("approval"), "approval")
    if approval.get("status") != "approved":
        raise BaselineError("exception manifest is not approved")
    require_nonempty_string(approval.get("source"), "approval source")
    require_nonempty_string(approval.get("policy_statement"), "approval policy_statement")
    if approval.get("approved_by") != "root-review":
        raise BaselineError("exception manifest must be approved_by root-review")
    approval_date = require_nonempty_string(approval.get("date"), "approval date")
    try:
        if date.fromisoformat(approval_date).isoformat() != approval_date:
            raise ValueError
    except ValueError as error:
        raise BaselineError("approval date must use ISO YYYY-MM-DD format") from error
    if manifest.get("ticket") != ticket:
        raise BaselineError(
            f"exception manifest ticket mismatch: expected {ticket}, actual {manifest.get('ticket')}"
        )
    if manifest.get("runtime") != runtime:
        raise BaselineError(
            "exception manifest runtime mismatch: "
            f"expected {runtime}, actual {manifest.get('runtime')}"
        )
    if manifest.get("case") != case_id:
        raise BaselineError(
            f"exception manifest case mismatch: expected {case_id}, actual {manifest.get('case')}"
        )
    if not result.file_set_valid:
        raise BaselineError("exception manifest cannot approve missing or unclassified artifacts")

    evidence = require_object(manifest.get("evidence"), "evidence")
    require_nonempty_string(evidence.get("specification"), "evidence specification")
    for field, actual in (("reference_root", reference), ("candidate_root", candidate)):
        recorded = require_nonempty_string(evidence.get(field), f"evidence {field}")
        if normalized_path(recorded) != normalized_path(actual):
            raise BaselineError(f"exception manifest {field} mismatch")
    recorded_diagnostics = require_nonempty_string(
        evidence.get("diagnostics_root"), "evidence diagnostics_root"
    )
    if normalized_path(recorded_diagnostics) != normalized_path(diagnostics):
        raise BaselineError("exception manifest diagnostics_root mismatch")
    evidence_records = require_object_list(evidence.get("records"), "evidence records")
    evidence_ids = set()
    evidence_by_id = {}
    for record in evidence_records:
        record_id = validate_evidence_record(record, ticket, runtime, reference, candidate)
        if record_id in evidence_ids:
            raise BaselineError(f"duplicate evidence record id: {record_id}")
        evidence_ids.add(record_id)
        evidence_by_id[record_id] = record

    causes = require_object_list(manifest.get("causes"), "causes")
    cause_ids = set()
    cause_artifacts = {}
    referenced_evidence_ids = set()
    for cause in causes:
        cause_id = require_nonempty_string(cause.get("id"), "cause id")
        require_nonempty_string(cause.get("cause"), f"cause description for {cause_id}")
        require_nonempty_string(cause.get("impact"), f"cause impact for {cause_id}")
        cause_evidence = require_object(cause.get("evidence"), f"cause evidence for {cause_id}")
        cause_evidence_ids = require_unique_strings(
            cause_evidence.get("ids"), f"cause evidence ids for {cause_id}"
        )
        unknown_evidence = set(cause_evidence_ids) - evidence_ids
        if unknown_evidence:
            raise BaselineError(
                f"unknown evidence id for {cause_id}: {sorted(unknown_evidence)[0]}"
            )
        referenced_evidence_ids.update(cause_evidence_ids)
        cause_artifacts[cause_id] = set(
            require_unique_strings(
                cause.get("artifacts"), f"cause artifacts for {cause_id}", allow_empty=True
            )
        )
        if cause_id in cause_ids:
            raise BaselineError(f"duplicate cause id in exception manifest: {cause_id}")
        cause_ids.add(cause_id)

    approved_cause_ids = set(
        require_unique_strings(approval.get("approved_cause_ids"), "approved cause ids")
    )
    if approved_cause_ids != cause_ids:
        raise BaselineError("approved cause ids do not exactly match manifest causes")

    affected = indexed_artifacts(manifest.get("affected_artifacts"), "affected_artifacts")
    unaffected = indexed_artifacts(manifest.get("unaffected_artifacts"), "unaffected_artifacts")
    if set(affected) != set(result.differing):
        raise BaselineError("affected_artifacts do not match the actual differing artifact set")
    if set(unaffected) != result.identical:
        raise BaselineError("unaffected_artifacts do not match the actual identical managed set")
    if set(affected) & set(unaffected) or set(affected) | set(unaffected) != result.managed:
        raise BaselineError("exception artifact classification does not match the managed set")

    expected_counts = {
        "managed_artifact_count": len(result.managed),
        "affected_artifact_count": len(affected),
        "unaffected_artifact_count": len(unaffected),
    }
    for field, expected in expected_counts.items():
        actual = evidence.get(field)
        if isinstance(actual, bool) or not isinstance(actual, int) or actual != expected:
            raise BaselineError(f"exception manifest count mismatch for {field}")

    referenced_cause_ids = set()
    for relative, artifact in affected.items():
        listed_causes = require_unique_strings(
            artifact.get("cause_ids"), f"cause_ids for affected artifact {relative}"
        )
        unknown = set(listed_causes) - cause_ids
        if unknown:
            raise BaselineError(f"unknown cause id for {relative}: {sorted(unknown)[0]}")
        referenced_cause_ids.update(listed_causes)
        offset = artifact.get("first_byte_offset")
        if isinstance(offset, bool) or not isinstance(offset, int) or offset < 0:
            raise BaselineError(f"invalid first_byte_offset for {relative}")
        if offset != result.differing[relative]:
            raise BaselineError(f"stale first_byte_offset for {relative}")
        for field in ("reference_byte_hex", "candidate_byte_hex"):
            if field not in artifact:
                raise BaselineError(f"missing {field} for {relative}")
        expected_reference_byte = validate_byte_hex(
            artifact.get("reference_byte_hex"), f"reference byte for {relative}"
        )
        expected_candidate_byte = validate_byte_hex(
            artifact.get("candidate_byte_hex"), f"candidate byte for {relative}"
        )
        reference_path = resolve_relative(reference, relative)
        candidate_path = resolve_relative(candidate, relative)
        if byte_hex_at(reference_path, offset) != expected_reference_byte:
            raise BaselineError(f"stale reference byte for {relative}")
        if byte_hex_at(candidate_path, offset) != expected_candidate_byte:
            raise BaselineError(f"stale candidate byte for {relative}")
        expected_reference = checked_sha256(
            artifact.get("reference_sha256"), f"affected reference {relative}"
        )
        expected_candidate = checked_sha256(
            artifact.get("candidate_sha256"), f"affected candidate {relative}"
        )
        if sha256(reference_path) != expected_reference:
            raise BaselineError(f"stale reference sha256 for {relative}")
        if sha256(candidate_path) != expected_candidate:
            raise BaselineError(f"stale candidate sha256 for {relative}")

    unused_causes = cause_ids - referenced_cause_ids
    if unused_causes:
        raise BaselineError(f"unused cause id in exception manifest: {sorted(unused_causes)[0]}")
    for cause_id in cause_ids:
        expected_artifacts = {
            relative
            for relative, artifact in affected.items()
            if cause_id in artifact["cause_ids"]
        }
        if cause_artifacts[cause_id] != expected_artifacts:
            raise BaselineError(f"cause artifacts mismatch for {cause_id}")
        cause = next(item for item in causes if item["id"] == cause_id)
        for evidence_id in cause["evidence"]["ids"]:
            record_artifacts = evidence_by_id[evidence_id].get("affected_artifacts")
            if record_artifacts is not None and set(record_artifacts) != expected_artifacts:
                raise BaselineError(f"evidence artifact set mismatch for {evidence_id}")
    unused_evidence = evidence_ids - referenced_evidence_ids
    if unused_evidence:
        raise BaselineError(f"unused evidence id: {sorted(unused_evidence)[0]}")

    for relative, artifact in unaffected.items():
        expected = checked_sha256(artifact.get("sha256"), f"unaffected artifact {relative}")
        reference_hash = sha256(resolve_relative(reference, relative))
        candidate_hash = sha256(resolve_relative(candidate, relative))
        if reference_hash != candidate_hash or reference_hash != expected:
            raise BaselineError(f"stale common sha256 for {relative}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    verify = subparsers.add_parser("verify", help="verify all hashes in a baseline manifest")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--repo-root", type=Path, required=True)
    verify.add_argument("--artifact-root", type=Path, required=True)

    compare = subparsers.add_parser("compare", help="compare one case's selected artifacts")
    compare.add_argument("--rules", type=Path, required=True)
    compare.add_argument("--case", required=True)
    compare.add_argument("--reference", type=Path, required=True)
    compare.add_argument("--candidate", type=Path, required=True)
    compare.add_argument("--diagnostics", type=Path, required=True)
    compare.add_argument("--exception-manifest", type=Path)
    compare.add_argument("--ticket")
    compare.add_argument("--runtime")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "verify":
            count = verify_manifest(args.manifest, args.repo_root, args.artifact_root)
            print(f"verified_files={count}")
            return 0

        if args.exception_manifest is not None:
            if not args.ticket:
                raise BaselineError("--ticket is required with --exception-manifest")
            if not args.runtime:
                raise BaselineError("--runtime is required with --exception-manifest")

        result = compare_case(
            args.rules,
            args.case,
            args.reference,
            args.candidate,
            args.diagnostics,
        )
        if not result.differing:
            print(f"case={args.case} artifacts=identical")
            return 0
        artifact = sorted(result.differing)[0]
        offset = result.differing[artifact]
        print(f"first_differing_artifact={artifact}", file=sys.stderr)
        print(f"first_byte_offset={offset}", file=sys.stderr)
        print(f"diagnostics={args.diagnostics}", file=sys.stderr)
        if args.exception_manifest is not None:
            validate_exception_manifest(
                args.exception_manifest,
                args.case,
                args.ticket,
                args.runtime,
                args.reference,
                args.candidate,
                args.diagnostics,
                result,
            )
            print(f"case={args.case} artifacts=approved-exceptions")
            return 0
        return 1
    except (BaselineError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
