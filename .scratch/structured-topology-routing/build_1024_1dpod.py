#!/usr/bin/env python3
"""Load the checked-in 1024-NPU 1DPod fixture for prepared-case tooling."""

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "scratch" / "ns-3-ub-tools"
CASE = REPO_ROOT / ".scratch" / "structured-topology-routing" / "1dpod-1024npu"
sys.path.insert(0, str(TOOLS_DIR))

from case_topology_loader import load_case_topology


def build_topology():
    return load_case_topology(CASE)
