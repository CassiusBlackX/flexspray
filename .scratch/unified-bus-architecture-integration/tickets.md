# Tickets

1. `01-integrate-component-history.md` - completed
2. `02-focused-correctness-validation.md` - completed
3. `03-combined-runtime-matrix.md` - pending, blocked by experiment planning
4. `04-large-scale-performance-validation.md` - pending, blocked by 03
5. `05-final-compatibility-review.md` - pending, blocked by 03, 04, and 06
6. `../allocator-event-ownership-fastpath/tickets.md` - pending, blocked by 02

The branch and Draft PR may be published after ticket 02. Merging into `next` requires all
remaining tickets to complete with reproducible artifacts.

Ticket 06 includes both the allocator ownership fastpath and the typed TP reclaim assessment. Its
internal blocker graph is authoritative for that work; the parent ticket is complete only after
subticket 08 finishes.
