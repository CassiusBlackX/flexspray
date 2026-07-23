#!/usr/bin/env python3
"""Exercise legacy prepared-case fallback and version-4 artifact enforcement."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_CASE = REPO_ROOT / "scratch" / "2nodes_single-tp"
RUNNER = REPO_ROOT / "scratch" / "run_structured_topo_case.py"


def _protocol(version):
    fingerprint = "" if version < 4 else f"ROUTING_FINGERPRINT {'0' * 64}\n"
    return (
        f"NS3_UB_STRUCTURED {version}\n"
        "NODES 2\n"
        "0 DEVICE 1 1ns 0ns 0\n"
        "1 DEVICE 1 1ns 0ns 0\n"
        "LINKS 1\n"
        "0 0 1 0 400Gbps 10ns\n"
        f"{fingerprint}"
        "TABLE 2\n"
        "0 1 0 1 0 0\n"
        "1 0 0 1 0 0\n"
        "PORT_GROUPS 0\n"
        "RULES 0\n"
        "RULE_RANGES 0\n"
        "TOPOLOGY_RULES 0\n"
        "END\n"
    )


def _run(case):
    return subprocess.run(
        [
            sys.executable,
            str(RUNNER),
            "--case-path",
            str(case),
            "--",
            "--stop-ms=5",
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        legacy = root / "legacy-v3"
        shutil.copytree(SOURCE_CASE, legacy)
        (legacy / "structured_topology.txt").write_text(_protocol(3), encoding="utf-8")
        legacy_result = _run(legacy)
        if legacy_result.returncode != 0:
            raise RuntimeError(
                "version 3 table-only prepared case failed:\n"
                + legacy_result.stdout
                + legacy_result.stderr
            )

        current = root / "current-v4"
        shutil.copytree(SOURCE_CASE, current)
        (current / "structured_topology.txt").write_text(_protocol(4), encoding="utf-8")
        current_result = _run(current)
        if current_result.returncode == 0:
            raise RuntimeError("version 4 case unexpectedly ran without compressed_routing.bin")
        failure_output = current_result.stdout + current_result.stderr
        if "compressed-table-lookup requires --compressed-routing-file" not in failure_output:
            raise RuntimeError("version 4 case failed without the expected artifact diagnostic")

    print("legacy-v3=passed v4-missing-binary=rejected")


if __name__ == "__main__":
    main()
