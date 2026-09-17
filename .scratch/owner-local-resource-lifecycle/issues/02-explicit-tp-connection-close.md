# 02 — Complete explicit TP connection close across execution owners

**What to build:** Make explicit TP connection destroy close new upper-layer admission at both
endpoints, continue all valid work already accepted, and retire the reservation only after both
execution owners have completed their owner-local transport close and installed quarantine.

**Blocked by:** 01 — Resolve live, closing, retired, and invalid TP ingress owner-locally.

**Status:** complete

- [x] Repeated destroy requests for the same connection generation are idempotent and preserve all
      completion callbacks.
- [x] Destroy reports completed, pending, or rejected without exposing internal close-fence state to
      packet, transaction, allocator, or application callers.
- [x] Both endpoints stop accepting new upper-layer transport work while continuing valid data,
      ACK, CNP, retransmission, timer, allocator, queue, and response work already accepted.
- [x] An unmaterialized endpoint slot is not declared closed solely because it lacks backing; its
      owner proves no accepted obligation remains or materializes drain-only handling when required.
- [x] Reservation views remain available for ingress until both endpoint owners acknowledge their
      owner-local close and quarantine installation.
- [x] Completion callbacks run only after both owner acknowledgements and final reservation
      retirement.
- [x] Close coordination carries immutable identities and causal control, never remote mutable
      objects or packet-terminal bookkeeping.
- [x] The delayed-destroy treatment completes in local, MTP, MPI, and hybrid execution.
- [x] Frozen canonical network outputs are byte-identical to the corresponding no-destroy control
      and baseline where a successful baseline exists.
