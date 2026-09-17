#!/usr/bin/env python3
"""Compare explicit-SUMMARY artifacts before and after T32."""

from __future__ import annotations

import importlib.util
import json
import re
from pathlib import Path


HERE = Path(__file__).resolve().parent
OUTPUT_ROOT = HERE / "evidence/t32-off-transfer/summary-v1"
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "t32_off_transfer", HERE / "run_t32_off_transfer.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the T32 performance runner")
t32 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t32)


def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
    case_dir = t32.original_materialize(destination, role, sizes)
    attributes = case_dir / "network_attribute.txt"
    text = attributes.read_text(encoding="utf-8")
    replacement = 'global UB_LIFECYCLE_TRACE_LEVEL "SUMMARY"'
    if "UB_LIFECYCLE_TRACE_LEVEL" in text:
        text = re.sub(
            r'global UB_LIFECYCLE_TRACE_LEVEL "[^"]+"', replacement, text
        )
    else:
        text += replacement + "\n"
    attributes.write_text(text, encoding="utf-8")
    return case_dir


def main() -> int:
    if OUTPUT_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite SUMMARY evidence: {OUTPUT_ROOT}")
    OUTPUT_ROOT.mkdir(parents=True)
    t32.diagnosis.materialize = materialize

    results = {}
    for role in ("main", "current"):
        results[role] = t32.run_once(role, OUTPUT_ROOT / role, SIZES)

    before = results["main"]["artifact_hashes"]
    after = results["current"]["artifact_hashes"]
    changed = sorted(path for path in before.keys() & after.keys() if before[path] != after[path])
    lifecycle_paths = sorted(path for path in after if "LifecycleTrace" in path)
    summary = {
        "schema_version": 1,
        "pre_t32_library_sha256": t32.diagnosis.t28.sha256(
            t32.diagnosis.t28.library_for("main")
        ),
        "t32_library_sha256": t32.diagnosis.t28.sha256(
            t32.diagnosis.t28.library_for("current")
        ),
        "before_only": sorted(before.keys() - after.keys()),
        "after_only": sorted(after.keys() - before.keys()),
        "changed": changed,
        "lifecycle_paths": lifecycle_paths,
        "exact": before == after and bool(lifecycle_paths),
    }
    OUTPUT_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["exact"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
