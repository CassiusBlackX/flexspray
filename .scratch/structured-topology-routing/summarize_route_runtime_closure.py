#!/usr/bin/env python3
"""Aggregate the final 1024-NPU route runtime benchmark samples."""

import json
import argparse
import hashlib
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MODES = {
    "main-full-table": "closure-main-run*.json",
    "compressed-table-lookup": "closure-compressed-run*.json",
    "structured-rule-compute": "closure-structured-run*.json",
    "text-table-to-compressed-lookup": "closure-text-run*.json",
}


def median(values):
    return statistics.median(values)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def summarize_signatures(directory):
    groups = {
        "allNewSelectors": ("compressed-full-*.bin", "structured-full-*.bin"),
        "mainCommonSelectors": (
            "main-common-*.bin",
            "compressed-common-*.bin",
            "structured-common-*.bin",
        ),
    }
    result = {}
    for name, patterns in groups.items():
        paths = [path for pattern in patterns for path in sorted(directory.glob(pattern))]
        expected = 6 if name == "allNewSelectors" else 9
        if len(paths) != expected:
            raise ValueError(f"expected {expected} {name} signatures, found {len(paths)}")
        digests = {sha256(path) for path in paths}
        if len(digests) != 1:
            raise ValueError(f"byte signature mismatch for {name}")
        result[name] = {
            "sha256": digests.pop(),
            "fileCount": len(paths),
            "queryCount": 100000,
            "coverage": (
                "candidate plus HASH64, CRC32, TOEPLITZ, ROUND_ROBIN, ADAPTIVE, "
                "INGRESS_PORT_STRIPE"
                if name == "allNewSelectors"
                else "candidate plus HASH and ADAPTIVE across main and new runtimes"
            ),
        }
    return result


def measurement(samples, name):
    matches = []
    for sample in samples:
        matches.append(next(value for value in sample["measurements"] if value["name"] == name))
    if len({value["checksum"] for value in matches}) != 1:
        raise ValueError(f"checksum mismatch for {name}")
    if any(value["missing"] for value in matches):
        raise ValueError(f"missing query for {name}")
    return {
        "meanNs": median(value["meanNs"] for value in matches),
        "p50BatchMeanNs": median(value["p50BatchMeanNs"] for value in matches),
        "p99BatchMeanNs": median(value["p99BatchMeanNs"] for value in matches),
        "checksum": matches[0]["checksum"],
        "missing": 0,
    }


def summarize(label, pattern):
    paths = sorted(ROOT.glob(pattern))
    if len(paths) != 3:
        raise ValueError(f"expected three {label} samples, found {len(paths)}")
    samples = [json.loads(path.read_text(encoding="utf-8")) for path in paths]
    query_count = {sample["queryCount"] for sample in samples}
    iterations = {sample["iterations"] for sample in samples}
    if len(query_count) != 1 or len(iterations) != 1:
        raise ValueError(f"inconsistent corpus for {label}")
    parse_key = "topologyInstallSeconds" if label == "main-full-table" else "parseSeconds"
    load_key = "routingLoadSeconds" if label == "main-full-table" else "installSeconds"
    names = [value["name"] for value in samples[0]["measurements"]]
    return {
        "samples": [path.name for path in paths],
        "queryCount": query_count.pop(),
        "iterationsPerMeasurement": iterations.pop(),
        "topologyParseOrInstallSeconds": median(sample[parse_key] for sample in samples),
        "routeLoadOrInstallSeconds": median(sample[load_key] for sample in samples),
        "rssAfterInstallBytes": median(sample["rssAfterInstallBytes"] for sample in samples),
        "heapInUseAfterInstallBytes": median(
            sample["heapInUseAfterInstallBytes"] for sample in samples
        ),
        "peakRssBytes": median(sample["peakRssBytes"] for sample in samples),
        "measurements": {name: measurement(samples, name) for name in names},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--signature-dir", type=Path, required=True)
    args = parser.parse_args()
    summary = {
        "benchmark": "1dpod-1024npu-route-runtime-closure",
        "mainCommit": "0ca0e95",
        "sampleCount": 3,
        "querySeed": 20260722,
        "byteSignatures": summarize_signatures(args.signature_dir),
        "modes": {label: summarize(label, pattern) for label, pattern in MODES.items()},
    }
    output = ROOT / "route-runtime-closure-summary.json"
    output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
