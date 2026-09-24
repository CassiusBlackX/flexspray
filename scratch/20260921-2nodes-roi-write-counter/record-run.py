#!/usr/bin/env python3
"""Record one fixed manifest simulation command, preserving earlier evidence."""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("control", "treatment"), required=True)
    parser.add_argument("--stage", default="matrix", help="Evidence label, not a simulation input")
    args = parser.parse_args()
    package = Path(__file__).resolve().parent
    repo = package.parent.parent
    relative = package.relative_to(repo)
    out = package / "cases" / args.variant / "output"
    binary = repo / "build" / relative / "ns3.44-ub-roi-write-counter"
    source = package / "ub-roi-write-counter.cc"
    if not binary.is_file():
        parser.error("build ub-roi-write-counter before recording a run")
    # Exactly the simulator argv in command-manifest.yaml; this wrapper only captures evidence.
    command = ["python3.12", "./ns3", "run", "--no-build",
               f"{relative}/ub-roi-write-counter --variant={args.variant} "
               f"--output-dir={relative}/cases/{args.variant}/output"]
    archive = None
    if out.exists():
        archive = out.parent / "archives" / datetime.datetime.now(
            datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        archive.parent.mkdir(parents=True, exist_ok=True)
        out.rename(archive)
    out.mkdir(parents=True)
    record = dict(stage=args.stage, case_id=args.variant, command=command, cwd=str(repo),
                  archived=str(archive.relative_to(repo)) if archive else None,
                  archive_reason="preserve previous evidence before explicit rerun" if archive else None,
                  head=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
                  dirty=subprocess.check_output(["git", "status", "--short"], cwd=repo, text=True),
                  source_sha256=digest(source), binary_sha256=digest(binary),
                  parameter_catalog="output/runtime-catalog (ub-quick-example queries); "
                                    "runtime_values queried in driver process",
                  started=utc(), status="running")
    record_path = out / "run-record.json"
    record_path.write_text(json.dumps(record, indent=2) + "\n")
    with (out / "console.log").open("w") as log:
        try:
            result = subprocess.run(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT)
            code = result.returncode
        except OSError as exc:
            log.write(str(exc) + "\n")
            code = 127
    record.update(return_code=code, ended=utc(), status="success" if code == 0 else "failed",
                  failure_category=None if code == 0 else "execution_or_structural_failure")
    # Save the actual process outcome before touching potentially truncated output.
    record_path.write_text(json.dumps(record, indent=2) + "\n")
    record["evidence_errors"] = []
    try:
        params = json.loads((out / "resolved-parameters.json").read_text())
        if not isinstance(params, dict):
            raise ValueError("resolved parameters must be a JSON object")
        params["program_identity"] = {key: record[key] for key in
                                      ("head", "source_sha256", "binary_sha256")}
        (out / "resolved-parameters.json").write_text(json.dumps(params, indent=2) + "\n")
    except (OSError, ValueError) as exc:
        record["evidence_errors"].append(f"parameter provenance: {exc}")
    record["recorder_return_code"] = code if code != 0 else (2 if record["evidence_errors"] else 0)
    record["artifacts"] = sorted(p.name for p in out.iterdir())
    record_path.write_text(json.dumps(record, indent=2) + "\n")
    history = package / "output/run-history.jsonl"
    history.parent.mkdir(parents=True, exist_ok=True)
    with history.open("a") as stream:
        stream.write(json.dumps(record) + "\n")
    print(json.dumps(record, indent=2))
    return record["recorder_return_code"]


if __name__ == "__main__":
    raise SystemExit(main())
