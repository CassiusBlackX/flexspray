# Use Packet Boundaries for Routed LastAck Recovery

Status: Accepted and implemented on 2026-09-24 (ticket 01).

Reliable rerouting keeps whole-packet backups and idealized ordinary link acknowledgements, while
LastAck becomes a serialized network message carried over configured healthy alternate routes.
Ordinary packets retain their existing header layouts: link-local transmission identity reuses the
existing attempt token and link-delivery metadata, and the receiving port maintains the next expected
whole-packet sequence. LastAck carries that boundary in its packet-level `RcvPtr_Packet` field.

This deliberately narrows UB Base Specification 2.1.0 §5.3.9: the reported position denotes a packet
sequence rather than a flit-buffer address. The model introduces neither logical flit accounting nor
partial-packet reception or physical retry-buffer wraparound, and assumes no bit corruption instead
of adding CRC computation. Finite-width message fields still require explicit wraparound validation.
This boundary lets experiments exercise actual LastAck routing, queueing, delivery, duplication and
loss without expanding the simulator into a flit-level retry implementation.

Local detection disables the failed output; retained traffic may migrate only after a valid LastAck
arrives. Each endpoint sends three copies and starts recovery on its first valid received copy.
The synthetic LastAck timer and its configuration are removed without compatibility behavior;
the configurable receive timeout defaults to one second and terminates the simulation with an
explicit recovery failure when it expires.

Validation: concentrated production-path tests cover zero/all-confirmed boundaries, 16-bit
wraparound, more than 65,536 unconfirmed attempts, three-copy loss/duplicates and independent
send/receive deadlines. The preserved small case completes four tasks and 382 recovery transfers.
Capacity-release and delayed-processing closure remain ticket 02; Clos detours remain ticket 03.
