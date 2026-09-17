# Tickets

1. `01-integrate-component-history.md` - completed
2. `02-focused-correctness-validation.md` - completed
3. `03-combined-runtime-matrix.md` - completed
4. `04-large-scale-performance-validation.md` - completed
5. `05-final-compatibility-review.md` - completed
6. `../allocator-event-ownership-fastpath/tickets.md` - blocking scope completed; Ticket 07 remains
   a non-blocking follow-up

The branch is ready to merge into `next` under the representative gates in `design.md`. Exhaustive
cross-product testing and complete residual-cost attribution remain visible follow-up work rather
than merge blockers.

Ticket 06 includes both the allocator ownership fastpath and the typed TP reclaim assessment. Its
production, correctness, performance, and independent-review scope is complete. Other-ledger
attribution in subticket 07 is explicitly outside the production change and continues separately.
