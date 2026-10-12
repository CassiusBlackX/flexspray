#!/usr/bin/env python3
"""Run one isolated case and bind its exit status, log and artifacts to exact inputs."""
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    case = Path(sys.argv[1]).resolve()
    relative = case.relative_to(ROOT)
    command = ["python3.12", "./ns3", "run", "--no-build",
               f"scratch/ub-quick-example --case-path={relative}"]
    output = case / "output"
    output.mkdir(exist_ok=True)
    # Place the running log outside runner-managed output directories, then preserve it there.
    temporary_log = case / "execution.log"
    def input_hashes():
        inputs = list(case.glob("*.csv")) + [case / "network_attribute.txt"]
        return {p.name: digest(p) for p in sorted(inputs)}

    def source_state():
        scripts = list(case.glob("*.py")) + [Path(__file__).resolve(),
            case.parent / "2nodes_reliable-rerouting/check_results.py", case.parent / "validate_routes.py"]
        return {
            "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "working_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)).hexdigest(),
            "scripts_sha256": {str(p.relative_to(ROOT)): digest(p) for p in scripts},
            "binary_sha256": {str(p.relative_to(ROOT)): digest(p) for p in (
                ROOT / "build/scratch/ns3.44-ub-quick-example", ROOT / "build/lib/libns3.44-unified-bus.so")},
        }

    before_inputs = input_hashes()
    before_source = source_state()
    started = time.time()
    with temporary_log.open("w") as handle:
        result = subprocess.run(command, cwd=ROOT, stdout=handle, stderr=subprocess.STDOUT)
    log = output / "execution.log"
    temporary_log.replace(log)
    unchanged = before_inputs == input_hashes() and before_source == source_state()
    manifest = {
        "command": command, "exit_code": result.returncode, "wall_seconds": time.time() - started,
        **before_source, "inputs_unchanged": unchanged,
        "inputs_sha256": before_inputs,
        "outputs_sha256": {p.name: digest(p) for p in output.iterdir() if p.is_file() and p.name != "run_result.json"},
    }
    (output / "run_result.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"case": str(relative), "exit_code": result.returncode,
                      "wall_seconds": manifest["wall_seconds"]}))
    return result.returncode if unchanged else 1


if __name__ == "__main__":
    raise SystemExit(main())
