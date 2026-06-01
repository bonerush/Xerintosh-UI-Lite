---
id: embedded-pointer-binding-constraint
trigger: "when encapsulating global variables in embedded/C projects with UI frameworks"
confidence: 0.8
domain: "refactoring"
source: "session-observation"
scope: project
---

# Embedded UI Framework: Pointer Binding Prevents Full Encapsulation

## Action
When a UI framework uses pointer binding (slider/switch items take `int16_t*`/`bool*`), you CANNOT fully encapsulate the bound variables behind getter/setter functions. The framework reads/writes through the pointer directly.

## Evidence
- `g_brightness_level` is bound to a slider via `xerintosh_new_slider_item("亮度", &g_brightness_level, ...)`
- `g_is_landscape` is bound to a switch via `xerintosh_new_switch_item("横屏/竖屏", &g_is_landscape, ...)`
- The slider/switch framework dereferences these pointers on every frame — getter/setter would break this

## Solution
- Add getter/setter functions for API consistency
- Keep `extern` declarations for pointer-bound variables
- Use getter/setter for non-pointer-bound access (callbacks, sysfs, etc.)
- Document which variables are pointer-bound vs API-accessed

## Rule
- Before encapsulating any variable, check if its address (`&var`) is taken anywhere
- If `&var` appears in a UI constructor call → keep extern, add getter/setter alongside
- If `&var` never appears → safe to fully encapsulate
