#!/usr/bin/env python3
"""Archive previous results and bind this run to its exact inputs."""
import hashlib
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4

CASE = Path(__file__).resolve().parent
INPUTS = ("node.csv", "topology.csv", "routing_table.csv", "traffic.csv", "network_attribute.txt")


def main():
    hashes = {name: hashlib.sha256((CASE / name).read_bytes()).hexdigest() for name in INPUTS}
    previous = [CASE / name for name in ("runlog", "output", "runner.log", "runner.exit", "run-start.json")]
    if any(path.exists() for path in previous):
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid4().hex[:8]
        history = CASE / "history" / stamp
        history.mkdir(parents=True)
        for path in previous:
            if path.exists():
                path.rename(history / path.name)
        print(f"Previous results preserved in {history}")
    (CASE / "run-start.json").write_text(json.dumps({
        "started_ns": time.time_ns(), "inputs_sha256": hashes,
    }, indent=2) + "\n")
    print("Prepared a fresh run; capture runner.log and runner.exit as documented in README.md")


if __name__ == "__main__":
    main()
