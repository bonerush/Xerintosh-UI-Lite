---
id: dead-code-removal-check-tests
trigger: "when removing unused functions or APIs"
confidence: 0.9
domain: "refactoring"
source: "session-observation"
scope: project
---

# Dead Code Removal: Always Check Tests First

## Action
Before removing any function/macro identified as "dead code" (unused in src/), ALWAYS check if it's used in test/ files. Test files exercise APIs that may not be called by production code but are still part of the public API contract.

## Evidence
- Removed `kern_log_get_level()` and `kern_log_stats()` as "never called" — but `test_kernel_init.cpp` used both. Build failed with 7 errors. Had to restore both functions.
- `kern_mq_*` functions were correctly identified as dead (only tested, never used by app) — removed along with their tests. This was the right call because the entire subsystem was unused.
- `kern_clear_panic()` was initially flagged as dead but was used in test SetUp() — correctly kept.

## Rule
- If a function is used ONLY in tests → consider removing both the function AND its tests (if the feature is废弃)
- If a function is used in tests as part of a public API → keep it
- Always run `grep -r "function_name" test/` before removing any symbol
