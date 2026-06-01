---
id: callback-signature-change-order
trigger: "when changing function pointer signatures across a codebase"
confidence: 0.85
domain: "refactoring"
source: "session-observation"
scope: project
---

# Callback Signature Change: Correct Order of Operations

## Action
When changing function pointer signatures (e.g., `void (*)()` to `void (*)(void *ud)`), follow this exact order to minimize compilation errors:

1. **Define typedef** in the central header (e.g., `typedef void (*xerintosh_cb_t)(void *user_data)`)
2. **Update struct fields** in the header (replace `void (*func)()` with `xerintosh_cb_t func`)
3. **Update constructor declarations** in the header
4. **Update constructor implementations** in .c/.cpp files
5. **Update invocation sites** (pass `item->user_data` as argument)
6. **Update callback function definitions** (add `void *ud` parameter + `(void)ud;`)
7. **Update forward declarations** in .c/.cpp files
8. **Update extern declarations** in consuming files
9. **Update test stubs** (test files often define stub callbacks)
10. **Verify both native and hardware builds**

## Evidence
- Changed 8 struct fields + 4 constructors + 8 invocation sites + ~30 callback definitions across 21 files
- Missed test stubs in `test_exit_animation.cpp` — caused compilation failure
- Missed forward declarations in `wifi_manager.cpp` — had to do a second pass
- The typedef (`xerintosh_cb_t`) significantly reduced verbosity in all subsequent changes

## Rule
- Always define a typedef first — it makes all subsequent changes cleaner
- Test stubs are easy to forget — always search test/ for the old signature
- Forward declarations in .cpp files are easy to miss — grep for the old signature after changes
