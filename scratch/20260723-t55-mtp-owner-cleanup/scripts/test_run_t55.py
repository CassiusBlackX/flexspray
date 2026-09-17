#!/usr/bin/env python3
"""Focused process-cleanup tests for the T55 runner."""

from __future__ import annotations

import unittest
import os
import signal
import subprocess
import sys

from run_t55 import (
    RSS_LIMIT_BYTES,
    monitor_process,
    process_group_rss_bytes,
    terminate_group,
    validate_manifest_contract,
)


class FakeProcess:
    def __init__(self) -> None:
        self.pid = 4242
        self.reaped = False
        self.wait_count = 0

    def poll(self) -> int | None:
        return None if not self.reaped else -15

    def wait(self, timeout: float | None = None) -> int:
        self.wait_count += 1
        self.reaped = True
        return -15


class RunnerCleanupTest(unittest.TestCase):
    def spawn_group(self) -> subprocess.Popen[str]:
        script = (
            "import subprocess,sys,time; "
            "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); "
            "print(child.pid, flush=True); time.sleep(60)"
        )
        process = subprocess.Popen(
            [sys.executable, "-c", script],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        self.assertIsNotNone(process.stdout)
        descendant_pid = int(process.stdout.readline().strip())
        self.assertGreater(descendant_pid, 0)
        return process

    def cleanup_group(self, process: subprocess.Popen[str]) -> None:
        if process_group_rss_bytes(process.pid)[1] > 0:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            pass
        if process.stdout is not None:
            process.stdout.close()
        if process.stderr is not None:
            process.stderr.close()

    def assert_cleanup(self, *, sample_group, record_sample, error: type[BaseException]) -> None:
        process = FakeProcess()
        terminated_pgids: list[int] = []

        def terminate(fake: FakeProcess) -> str:
            terminated_pgids.append(fake.pid)
            fake.reaped = True
            return "sigterm_reaped"

        with self.assertRaises(error):
            monitor_process(
                process,
                sample_group=sample_group,
                record_sample=record_sample,
                terminate=terminate,
            )

        self.assertEqual(terminated_pgids, [process.pid])
        self.assertTrue(process.reaped)
        self.assertGreaterEqual(process.wait_count, 1)

    def test_sampling_failure_terminates_only_child_group_and_reaps_process(self) -> None:
        def fail_sampling(_pgid: int) -> tuple[int, int]:
            raise OSError("sampling failed")

        self.assert_cleanup(
            sample_group=fail_sampling,
            record_sample=lambda _elapsed, _rss, _count: None,
            error=OSError,
        )

    def test_sample_recording_failure_terminates_and_reaps_process(self) -> None:
        def fail_recording(_elapsed: float, _rss: int, _count: int) -> None:
            raise OSError("disk failed")

        self.assert_cleanup(
            sample_group=lambda _pgid: (1, 1),
            record_sample=fail_recording,
            error=OSError,
        )

    def test_keyboard_interrupt_terminates_and_reaps_process(self) -> None:
        def interrupt(_pgid: int) -> tuple[int, int]:
            raise KeyboardInterrupt()

        self.assert_cleanup(
            sample_group=interrupt,
            record_sample=lambda _elapsed, _rss, _count: None,
            error=KeyboardInterrupt,
        )

    def test_rss_guard_terminates_once_waits_and_returns_actual_status(self) -> None:
        process = FakeProcess()
        terminations: list[int] = []

        def terminate(fake: FakeProcess) -> str:
            terminations.append(fake.pid)
            fake.reaped = True
            return "sigterm_reaped"

        outcome = monitor_process(
            process,
            sample_group=lambda _pgid: (RSS_LIMIT_BYTES + 1, 1),
            record_sample=lambda _elapsed, _rss, _count: None,
            terminate=terminate,
        )

        self.assertEqual(terminations, [process.pid])
        self.assertEqual(process.wait_count, 1)
        self.assertTrue(process.reaped)
        self.assertEqual(outcome["return_code"], -15)
        self.assertEqual(outcome["stop_reason"], "rss_limit")

    def test_manifest_rejects_second_run_command_source(self) -> None:
        manifest = {
            "run_order": ["a"],
            "commands": [{
                "id": "run-a", "case_id": "a", "phase": "run", "command": ["run"],
                "cwd": "{worktree}", "output_dir": "{run_dir}", "expected_artifacts": [],
                "argv_template": ["other"],
            }],
        }
        with self.assertRaisesRegex(RuntimeError, "unsupported command key"):
            validate_manifest_contract(manifest, ["a"], {"a"})

    def test_manifest_rejects_matrix_case_drift(self) -> None:
        manifest = {
            "run_order": ["a"],
            "commands": [{
                "id": "run-a", "case_id": "a", "phase": "run", "command": ["run"],
                "cwd": "{worktree}", "output_dir": "{run_dir}", "expected_artifacts": [],
            }],
        }
        with self.assertRaisesRegex(RuntimeError, "matrix case IDs differ from run order"):
            validate_manifest_contract(manifest, ["b"], {"b"})

    @unittest.skipIf(sys.platform == "win32", "POSIX process groups required")
    def test_terminate_group_reaps_real_leader_and_descendant_group(self) -> None:
        process = self.spawn_group()
        try:
            termination = terminate_group(process)
            self.assertIn(termination, {"sigterm_reaped", "sigkill_reaped"})
            self.assertIsNotNone(process.returncode)
            self.assertEqual(process_group_rss_bytes(process.pid)[1], 0)
        finally:
            self.cleanup_group(process)

    @unittest.skipIf(sys.platform == "win32", "POSIX process groups required")
    def test_rss_guard_reaps_real_leader_and_descendant_group(self) -> None:
        process = self.spawn_group()
        try:
            outcome = monitor_process(
                process,
                sample_group=lambda _pgid: (RSS_LIMIT_BYTES + 1, 2),
                record_sample=lambda _elapsed, _rss, _count: None,
            )
            self.assertEqual(outcome["stop_reason"], "rss_limit")
            self.assertIsInstance(outcome["return_code"], int)
            self.assertIsNotNone(process.returncode)
            self.assertEqual(process_group_rss_bytes(process.pid)[1], 0)
        finally:
            self.cleanup_group(process)


if __name__ == "__main__":
    unittest.main()
