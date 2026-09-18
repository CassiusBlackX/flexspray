from __future__ import annotations

from pathlib import Path


COMMON_REQUIRED_FILES = (
    "network_attribute.txt",
    "traffic.csv",
)

CSV_REQUIRED_FILES = (
    "node.csv",
    "topology.csv",
    "routing_table.csv",
)

STRUCTURED_REQUIRED_FILES = (
    "structured_topology.txt",
    "compressed_routing.bin",
    "structured_summary.json",
)

STRUCTURED_MARKERS = (
    "structured_topology.txt",
    "compressed_routing.bin",
)

CSV_MARKERS = (
    "node.csv",
    "topology.csv",
)

TRANSPORT_CHANNEL_MODE_ON_DEMAND = "on-demand"
TRANSPORT_CHANNEL_MODE_PRECOMPUTED = "precomputed"
TRANSPORT_CHANNEL_MODES = frozenset(
    {
        TRANSPORT_CHANNEL_MODE_ON_DEMAND,
        TRANSPORT_CHANNEL_MODE_PRECOMPUTED,
    }
)


def detect_case_format(case_dir: Path) -> str:
    case_dir = Path(case_dir)
    if any((case_dir / name).is_file() for name in STRUCTURED_MARKERS):
        return "structured"
    if any((case_dir / name).is_file() for name in CSV_MARKERS):
        return "csv"
    return "unknown"


def check_case_files(
    case_dir: Path, transport_channel_mode: str = TRANSPORT_CHANNEL_MODE_ON_DEMAND
) -> dict:
    if transport_channel_mode not in TRANSPORT_CHANNEL_MODES:
        raise ValueError(f"Unsupported transport_channel_mode: {transport_channel_mode}")

    case_dir = Path(case_dir)
    case_format = detect_case_format(case_dir)
    required_files = list(COMMON_REQUIRED_FILES)
    if case_format == "structured":
        required_files += STRUCTURED_REQUIRED_FILES
    else:
        # Unknown format is reported against the CSV requirements so the
        # missing-file list tells the user which artifacts to generate.
        required_files += CSV_REQUIRED_FILES
        if transport_channel_mode == TRANSPORT_CHANNEL_MODE_PRECOMPUTED:
            required_files.append("transport_channel.csv")

    missing_files = [name for name in required_files if not (case_dir / name).is_file()]
    if missing_files:
        return {
            "status": "missing_files",
            "case_format": case_format,
            "missing_files": missing_files,
            "next_action": "stop",
        }

    return {
        "status": "ok",
        "case_format": case_format,
        "missing_files": [],
        "next_action": "ready",
    }
