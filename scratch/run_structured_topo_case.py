#!/usr/bin/env python3
"""Run a previously prepared structure-aware simulation directory."""

import argparse
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _parse_args(argv):
    arguments = list(sys.argv[1:] if argv is None else argv)
    if "--" in arguments:
        separator = arguments.index("--")
        launcher_arguments = arguments[:separator]
        simulator_arguments = arguments[separator + 1:]
    else:
        launcher_arguments = arguments
        simulator_arguments = []

    parser = argparse.ArgumentParser(
        description=(
            "Run a case directory prepared by ns-3-ub-tools with structured_topology.txt."
        )
    )
    parser.add_argument("--case-path", required=True, help="Prepared simulation case directory")
    args = parser.parse_args(launcher_arguments)
    args.simulator_args = simulator_arguments
    return args


def main(argv=None):
    args = _parse_args(argv)
    try:
        case_path = Path(args.case_path).expanduser().resolve()
        if not case_path.is_dir():
            raise ValueError(f"case path is not a directory: {case_path}")
        topology_path = case_path / "structured_topology.txt"
        if not topology_path.is_file():
            raise ValueError(f"missing prepared topology: {topology_path}")

        python = shutil.which("python3.12") or sys.executable
        program = [
            "scratch/ub-quick-example",
            f"--case-path={case_path}",
            f"--structured-topology-file={topology_path}",
            *(str(argument) for argument in args.simulator_args),
        ]
        command = [
            python,
            str(REPO_ROOT / "ns3"),
            "run",
            "--no-build",
            " ".join(shlex.quote(argument) for argument in program),
        ]
        return subprocess.run(command, cwd=REPO_ROOT, check=False).returncode
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
