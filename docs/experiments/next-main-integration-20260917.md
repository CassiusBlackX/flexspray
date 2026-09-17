# GitCode next/main integration validation

The candidate combines GitCode `next` at `61eea8e` with `main` at `7f558d9`.
It does not include GitHub `feat/ub2.1-atomic`.

## Merge resolution

- Preserve next's descriptor-based GBN retransmission and its adapted TPNAK wire format.
- Carry main's CAQM timeout rollback through `UbRetransTimeoutResult::retransmitBytes`.
  Compute the retry payload using next's existing descriptor accounting, without
  restoring main's obsolete segment-progress rewind helpers.
- Carry main's reserved transport opcode drop accounting and rejection test.
- Carry main's current contact QR code and round-robin knowledge card.
- Guard MTP-only references in an existing test so a non-MTP test build compiles.
- Register the concurrent receiver-materialization test only with MTP, which enables
  atomic ns-3 reference counts. Sequential idempotence coverage remains enabled.

## Environment and commands

macOS arm64, AppleClang, Python 3.12, Ninja, release profile. MPI and MTP are disabled.

```sh
python3.12 ./ns3 configure --enable-modules=unified-bus --enable-tests \
  --disable-examples --disable-mpi --disable-mtp --disable-werror \
  -d release -G Ninja -- -DNS3_FAST_LINKERS=OFF
ninja -C cmake-cache -j6
./build/utils/ns3.44-test-runner --test-name=<suite> --verbose
python3.12 -m unittest discover -s tests/openusim_skills -p test_skill_docs.py
git diff --check
```

The build completes for all targets in this configuration. The first build exposed
unguarded `MtpInterface` references in `UbExplicitConnectionDestroyDrainsAdmittedWriteTest`;
the compile guards correct this test-only issue.

## Results

| Suite | Result |
| --- | --- |
| unified-bus | 234 cases passed |
| unified-bus-transport-retrans | 40 cases passed |
| unified-bus-congestion-control | 28 cases passed |
| unified-bus-retrans-cc-regression | 5 cases passed |
| unified-bus-lifecycle | 89 cases passed |
| unified-bus-lifecycle-trace | 3 cases passed |
| unified-bus-identity-connection-destroy | 1 case passed |
| unified-bus-switch-voq | 32 cases passed |
| unified-bus-create-node-delay-mapping | 4 cases passed |
| unified-bus-examples-smoke | 6 cases passed |
| OpenUSim skill documentation | 20 tests passed |
| unified-bus-tp-reservation | 30 consecutive runs passed, 15 cases per run |

Suite counts overlap because focused suites reuse cases from broader suites.
The new GBN timeout case checks outstanding payload rollback, unchanged new-data PSN,
retry cursor placement, restored byte accounting after retry, final ACK convergence,
and an empty retry window.

The first reservation-suite runs exposed an intermittent crash in the 16-thread
receiver-materialization fixture (one failure in five diagnostic runs). The fixture
copies shared `Ptr` objects concurrently, whereas `SimpleRefCount` uses a plain
`uint32_t` without `NS3_MTP`. Restricting that concurrent fixture to MTP matches its
reference-counting prerequisite. After rebuilding, 30 sequential suite runs passed;
the 234-case main suite and five-case retransmission/CC suite also passed again.
Exit-code files and stdout/stderr logs were checked under
`/tmp/next-main-reservation-repeat/`. Temporary per-test diagnostic logging was removed.

The historical main/next performance matrix has not been rerun. This verification
does not establish MPI/MTP or Windows compatibility. LLDB changes fork-test behavior;
debugger runs that skip functions are diagnostic only, never passing test evidence.

## Sources

- Main commit `3f6ab72` and GitCode PR 55 define the GBN timeout accounting intent.
- Main `f1c6a82` and next `9402d4c` implement the same TPNAK correction against different
  retransmission implementations.
- `docs/experiments/main-next-controlled-comparison-20260802.md` supplies historical
  comparison evidence, not validation of this candidate.
