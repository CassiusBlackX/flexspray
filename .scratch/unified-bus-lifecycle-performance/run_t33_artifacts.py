#!/usr/bin/env python3
"""Compare T32 and T33 OFF/SUMMARY managed artifacts."""

from __future__ import annotations

import importlib.util
import json
import os
import re
from pathlib import Path


HERE = Path(__file__).resolve().parent
T32_ROOT = HERE / "evidence/t32-off-transfer/t32-root"
T33_ROOT = HERE / "evidence/t33-off-setup/t33-root"
OUTPUT_ROOT = HERE / "evidence/t33-off-setup/artifacts-v1"
SIZES = (16_384_009, 16_384_000)


spec = importlib.util.spec_from_file_location(
    "p01_diagnosis", HERE / "run_p01_detailed_diagnosis.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load the p01 diagnosis runner")
diagnosis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnosis)

diagnosis.t28.ROLES["main"] = {"source_root": T32_ROOT, "revision": "T32"}
diagnosis.t28.ROLES["current"] = {"source_root": T33_ROOT, "revision": "T33"}
original_materialize = diagnosis.materialize
original_run_once = diagnosis.run_once


def run_level(level: str) -> dict:
    def materialize(destination: Path, role: str, sizes: tuple[int, int]) -> Path:
        case_dir = original_materialize(destination, role, sizes)
        attributes = case_dir / "network_attribute.txt"
        text = attributes.read_text(encoding="utf-8")
        replacement = f'global UB_LIFECYCLE_TRACE_LEVEL "{level}"'
        if "UB_LIFECYCLE_TRACE_LEVEL" in text:
            text = re.sub(
                r'global UB_LIFECYCLE_TRACE_LEVEL "[^"]+"', replacement, text
            )
        else:
            text += replacement + "\n"
        attributes.write_text(text, encoding="utf-8")
        return case_dir

    diagnosis.materialize = materialize
    results = {}
    for role in ("main", "current"):
        library_dir = diagnosis.t28.library_for(role).parent
        previous = os.environ.get("DYLD_LIBRARY_PATH")
        os.environ["DYLD_LIBRARY_PATH"] = str(library_dir)
        try:
            results[role] = original_run_once(
                role, OUTPUT_ROOT / level.lower() / role, SIZES
            )
        finally:
            if previous is None:
                os.environ.pop("DYLD_LIBRARY_PATH", None)
            else:
                os.environ["DYLD_LIBRARY_PATH"] = previous
    before = results["main"]["artifact_hashes"]
    after = results["current"]["artifact_hashes"]
    return {
        "before_only": sorted(before.keys() - after.keys()),
        "after_only": sorted(after.keys() - before.keys()),
        "changed": sorted(
            path for path in before.keys() & after.keys() if before[path] != after[path]
        ),
        "exact": before == after,
    }


def main() -> int:
    if OUTPUT_ROOT.exists():
        raise RuntimeError(f"refusing to overwrite T33 evidence: {OUTPUT_ROOT}")
    OUTPUT_ROOT.mkdir(parents=True)
    summary = {
        "schema_version": 1,
        "t32_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("main")),
        "t33_library_sha256": diagnosis.t28.sha256(diagnosis.t28.library_for("current")),
        "off": run_level("OFF"),
        "summary": run_level("SUMMARY"),
    }
    summary["exact"] = summary["off"]["exact"] and summary["summary"]["exact"]
    OUTPUT_ROOT.joinpath("summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["exact"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
