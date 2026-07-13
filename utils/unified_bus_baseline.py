#!/usr/bin/env python3
"""Verify and compare immutable Unified Bus lifecycle reference artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path
from typing import Iterable, Mapping


CHUNK_SIZE = 1024 * 1024


class BaselineError(Exception):
    """An invalid manifest, drifted input, or comparison mismatch."""


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
) -> tuple[str, int] | None:
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
    for relative in sorted(managed_files | reference_files | candidate_files):
        if relative not in managed_files:
            preserve_outputs(reference, candidate, diagnostics)
            return relative, 0
        old_path = resolve_relative(reference, relative)
        new_path = resolve_relative(candidate, relative)
        if relative not in reference_files or relative not in candidate_files:
            preserve_outputs(reference, candidate, diagnostics)
            return relative, 0
        offset = first_byte_offset(old_path, new_path)
        if offset is not None:
            preserve_outputs(reference, candidate, diagnostics)
            return relative, offset
    return None


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
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "verify":
            count = verify_manifest(args.manifest, args.repo_root, args.artifact_root)
            print(f"verified_files={count}")
            return 0

        mismatch = compare_case(
            args.rules,
            args.case,
            args.reference,
            args.candidate,
            args.diagnostics,
        )
        if mismatch is None:
            print(f"case={args.case} artifacts=identical")
            return 0
        artifact, offset = mismatch
        print(f"first_differing_artifact={artifact}", file=sys.stderr)
        print(f"first_byte_offset={offset}", file=sys.stderr)
        print(f"diagnostics={args.diagnostics}", file=sys.stderr)
        return 1
    except (BaselineError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
