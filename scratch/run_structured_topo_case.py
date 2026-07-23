#!/usr/bin/env python3
"""Run a previously prepared structure-aware simulation directory."""

import argparse
import json
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
    parser.add_argument(
        "--route-runtime-mode",
        choices=("compressed-table-lookup", "structured-rule-compute"),
        default="compressed-table-lookup",
        help="Runtime route lookup method (default: compressed-table-lookup)",
    )
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
        compressed_routing_path = case_path / "compressed_routing.bin"
        summary_path = case_path / "structured_summary.json"
        route_generation = None
        if summary_path.is_file():
            with summary_path.open(encoding="utf-8") as file:
                route_generation = json.load(file).get("routeGeneration")
        if (
            args.route_runtime_mode == "structured-rule-compute"
            and route_generation == "full"
        ):
            raise ValueError(
                "structured-rule-compute requires a case prepared with "
                "--route-generation=structured or mixed"
            )
        python = shutil.which("python3.12") or sys.executable
        program = [
            "scratch/ub-quick-example",
            f"--case-path={case_path}",
            f"--structured-topology-file={topology_path}",
            f"--route-runtime-mode={args.route_runtime_mode}",
            *(
                [f"--compressed-routing-file={compressed_routing_path}"]
                if (
                    args.route_runtime_mode == "compressed-table-lookup"
                    and compressed_routing_path.is_file()
                )
                else []
            ),
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
