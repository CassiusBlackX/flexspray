# Use NLP4 for the CFG7 LDST bypass extension

Status: accepted and implemented; validated by the LDST CFG7 suite and four-task scratch case.

The UB Base Specification 2.0 English and 2.0.1 Chinese documents define CFG7 with CNA24 and
NLP0/CTPH or NLP2/RTPH, while the existing simulator LDST path uses CFG9 with CNA16 and compact
transaction headers. To add CNA24 addressing with a small change to that LDST path, this simulator
defines CFG7/NLP4 as TP bypass and retains the CFG9 compact request, memory-access extension,
response headers, and existing payload semantics. This is a simulator-local wire-format extension,
not a claim of compliance with a standardized CFG7 bypass format.

Reusing NLP0 would conflict with CTP dispatch. Changing to full transaction headers would expand
the requested work into a different transaction representation. A distinct NLP4 lets the existing
CTP interpretation remain intact while both LDST formats reuse the current operation and completion
paths. Responses retain their request's CFG even when the responder's default outgoing format differs.

Sources: UB Base Specification 2.0.1 (Chinese) and 2.0 (English), sections 5.2.1–5.2.2,
6.1–6.2, and appendices B.2.5–B.2.6.
