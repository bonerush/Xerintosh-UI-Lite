# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

M5Stick-P1 is an embedded UI firmware for the M5Stick-C (ESP32-PICO + 80x160 TFT + MPU6886). It is a **port of `reference/oled-ui-astra-lite`** (a 128x64 OLED menu framework) to the M5Stick-C platform.

- **Language**: C core + C++ HAL bridge (M5GFX). All exposed headers must be C-compatible with `extern "C"`.
- **Framework**: Arduino (ESP32) via PlatformIO.
- **UI Framework name**: Xerintosh (renamed from AstraUI).
- **Kernel name**: Xeros — cooperative microkernel (VFS / scheduler / shell / IPC).
- **Build system**: PlatformIO (`platformio.ini`).
- **Target resolution**: 80x160 (portrait default, landscape via `setRotation(1)`).

## Common Commands

> The user primarily uses **VS Code + PlatformIO IDE extension** for build and upload. Terminal `pio` may not be in PATH; add it if needed:
> ```bash
> export PATH="$HOME/.platformio/penv/bin:$PATH"
> ```

### Build and Upload

```bash
# Build for M5Stick-C (hardware)
pio run -e m5stick-c

# Upload to device
pio run -e m5stick-c --target upload

# Serial monitor (115200 baud, RTS/DTR=0)
pio device monitor -e m5stick-c
```

### Testing (Native/Desktop)

```bash
# Run all native tests (GoogleTest)
pio test -e native

# Or build and run the native test binary directly
pio run -e native
./.pio/build/native/program

# Run a single test suite
./.pio/build/native/program --gtest_filter=AnimationTest.*
```

### Clean Build Cache

PlatformIO's `.pio/build/` cache is stubborn. After changing `platformio.ini`, always clean:
```bash
rm -rf .pio/build/
```

## Architecture

The project uses a **four-layer architecture** with strict module prefixes:

```
App Layer (src/app/) — Each app in its own subdirectory
├── app_init.c/h              — Menu tree construction, input dispatch
├── ui_task.c                 — Xeros kernel task wrapper (input→render→yield)
├── ui_service.c/h            — UI service layer (shared UI utilities)
├── svc_mgr_helper.c/h        — WiFi/BT shared toggle abstraction
├── boot/
│   └── boot_screen.c/h       — Macintosh 128K boot screen
├── settings/
│   └── settings.c/h          — Brightness/anim/rotation/baud config (levels → hw values)
├── storage/
│   └── storage.cpp/h         — NVS persistence wrapper
├── wifi/
│   └── wifi_manager.cpp/h    — WiFi state machine (async scan/connect)
├── bluetooth/
│   ├── bt_manager.cpp/h      — Bluetooth (NimBLE) manager
│   └── bt_uart_service.cpp/h — BT UART serial service
├── serial_input/
│   └── serial_input.cpp/h    — Serial CLI for WiFi/BT passcode input
├── serial_monitor/
│   ├── serial_monitor.h      — Serial monitor user_item lifecycle API
│   ├── sm_app.cpp/h          — Serial monitor state machine & main loop
│   ├── sm_buffer.c/h         — Terminal ring buffer (data layer)
│   └── sm_ui.c/h             — Serial monitor rendering (info bar + terminal)
├── taskmgr/
│   ├── taskmgr.h             — Task manager data API
│   ├── taskmgr_app.c         — Task list collection & process control
│   └── taskmgr_ui.c/h        — Task manager UI rendering (animated row list)
├── about/
│   └── about.c/h             — About page (version/Logo/developer info)
└── shutdown/
    ├── shutdown_screen.c/h   — Shutdown screen
    └── power_key_popup.c/h   — Power key long-press popup

Kernel Layer (src/kernel/) — Xeros cooperative microkernel ("everything is a file")
├── kern_types.h              — Error codes, constants, logging macros
├── kern_task.c/h             — Cooperative Round-Robin scheduler + dynamic stack
├── kern_vfs.c/h              — Virtual File System (inode/dentry/file)
├── kern_devfs.c/h            — /dev/ device registration
├── kern_procfs.c/h           — /proc virtual filesystem
├── kern_sysfs.c/h            — /sys virtual filesystem
├── kern_gpiofs.c/h           — /sys/gpio pin state mapping
├── kern_ipc.c/h              — IPC: anonymous pipe + named message queue
├── kern_syscall.c/h          — Unified syscall dispatcher & user-mode wrappers
├── kern_init.c/h             — Kernel initialization sequence
├── kern_version.h            — Version & developer info management
├── kern_port.c/h             — Portability layer (FreeRTOS / native backends)
├── kern_port_native.c        — Native (desktop) portability backend
├── kern_ctx_esp32.h          — ESP32-specific context
├── kern_shell.c/h            — Interactive serial shell (30+ commands)
├── kern_shell_cmds.c/h       — Shell command implementations
├── kern_shell_cmds_internal.h — Shell command internal helpers
├── kern_shell_parser.c/h     — Shell input parser (quote support, arg splitting)
├── debug_serial.cpp/h        — Debug serial output helpers
└── devices/
    ├── kern_devices.c/h      — Device registry (/dev/fb0, /dev/input0, /dev/ttyS0)
    ├── dev_fb0.c/h           — Framebuffer device
    ├── dev_input0.c/h        — Input device
    ├── dev_ttyS0.cpp/h       — Serial UART device
    └── dev_pwrkey.c/h        — Power key device

UI Core Layer (src/ui/) — Pure C, do not modify framework logic lightly
├── ui_item.h                 — Menu tree data model (5 item types), public API
├── ui_item_core.h            — Item internals & base struct
├── ui_item_base.c            — Base item creation & lifecycle
├── ui_item_list.c            — list_item type implementation
├── ui_item_selector.c        — Selector highlight (XOR inverted)
├── ui_item_popup.c           — Pop-up widget rendering
├── ui_item_camera.c          — Camera (scroll position) logic
├── ui_types.h                — Shared UI type definitions
├── ui_core.c/h               — Animation engine, main loop dispatcher
├── ui_context.c/h            — Singleton state container, backward-compat macros
├── ui_dispatch.c             — Function pointer O(1) type dispatch (replaces inline switch)
├── ui_drawer.h               — Renderer public API
├── ui_draw_list.c            — List appearance drawing
├── ui_draw_anim.c            — Animation rendering helpers
├── ui_draw_widgets.c         — Pop-up / info bar rendering
├── ui_draw_icons.c           — Icon bitmap rendering
├── ui_anim_row.c/h           — Reusable row-list animation (slide-in + highlight)
├── ui_widget.h               — Widget type definitions
├── ui_selector.h             — Selector type definitions
└── ui_camera.h               — Camera type definitions

HAL Layer (src/hal/) — Hardware abstraction
├── hal_display.cpp/h         — TFT double-buffer (M5Canvas) / native memory framebuffer
├── hal_input.cpp/h           — Button state machine (debounce, short/long press)
├── hal_input_double_click.c/h — Double-click detection extension
├── hal_system.cpp/h          — Tick and delay wrappers
├── hal_power_key.cpp/h       — Power key hardware interface
├── hal_power_off.cpp         — Power-off sequence
└── hal_layout.h              — Screen layout constants & helpers

Entry Points
├── src/main.cpp        — Arduino setup()/loop() for hardware
└── src/native_main.cpp — GoogleTest entry for desktop (guarded by #ifdef NATIVE_TEST)
```

### Architecture Layer Diagram

```
┌─────────────────────────────────────────────┐
│ App Layer  (user_item, settings, wifi, bt…) │  User code
├─────────────────────────────────────────────┤
│ UI Core Layer (item / core / drawer / ctx)  │  Menu framework
├─────────────────────────────────────────────┤
│ Xeros Kernel Layer                          │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │Sched  │ │ VFS  │ │devfs │ │ IPC  │      │  Cooperative microkernel
│  │coop   │ │(vrtl)│ │(dev) │ │(pipe)│      │
│  └──────┘ └──────┘ └──────┘ └──────┘      │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │sysfs  │ │procfs│ │Shell │ │Syscll│      │  "Everything is a file"
│  └──────┘ └──────┘ └──────┘ └──────┘      │
├─────────────────────────────────────────────┤
│ HAL Layer (display / input / system)        │  Hardware abstraction
├─────────────────────────────────────────────┤
│ FreeRTOS + Arduino (WiFi / BT protocol)     │  Runtime
└─────────────────────────────────────────────┘
```

FreeRTOS serves WiFi/BT at the bottom; Xeros runs inside Arduino `loop()` as a "logical process layer" — no conflict.

### Rendering Pipeline (per frame)

1. `hal_display_clear()` — clear back buffer
2. Draw background layer (list items, controls)
3. `xerintosh_draw_selector()` — XOR inverted highlight
4. `xerintosh_draw_widget()` — pop-up / info bar
5. `hal_display_flush()` — DMA pushSprite to screen + buffer swap

Full-frame redraw, target 60fps.

### Key Design Patterns

- **C-style OOP**: Base struct `xerintosh_list_item_t` must be the **first member** of every derived struct. Safe cast via type tag check.
- **Animation easing**: `current += (target - current) / (100.0f - speed)`.
- **TFT double-buffering**: `M5Canvas` sprite must have `setColorDepth(16)` **before** `createSprite()`.
- **XOR highlight**: TFT lacks OLED's `draw_color(2)`, so selector uses pixel-by-pixel `color ^ 0xFFFF`.
- **Type dispatch**: `ui_dispatch.c` uses function pointer arrays for O(1) type routing (replaces inline switch).
- **Cooperative kernel**: Round-Robin scheduling + dynamic stack + VFS "everything is a file" + pipe/mq IPC.

## Coding Conventions

All conventions are enforced in `doc/coding-style.md`. Critical rules:

- **Module prefixes**: `xerintosh_`, `hal_display_`, `wifi_mgr_`, `settings_`, `kern_`, etc.
- **Headers**: Every `.h` must have `extern "C"` guards and include guards.
- **No C++ in C interfaces**: Do not use `nullptr`, `&` references, or classes in headers exposed to C. Use `NULL`, pointers, and `extern "C"` wrappers in `.cpp` files.
- **Callbacks**: Uniform signature `void (*)(void *user_data)` with a context pointer.
- **Constants**: Use `#define` (not `static const`) for C compatibility.
- **File size**: Keep functions under 50 lines and files under 400 lines.

## Item Types

Five menu item types defined in `ui_item.h`:

| Type | Struct | Behavior on Confirm |
|------|--------|---------------------|
| `list_item` | `xerintosh_list_item_t` | Enter submenu |
| `switch_item` | `xerintosh_switch_item_t` | Toggle bound `bool*` |
| `slider_item` | `xerintosh_slider_item_t` | Enter/confirm edit mode for bound `int16_t*` |
| `button_item` | `xerintosh_button_item_t` | Fire callback once |
| `user_item` | `xerintosh_user_item_t` | Enter full-screen App (init → loop → exit) |

**Important**: `switch_item` and `slider_item` bind to **pointers** that must remain valid for the menu's entire lifetime (use `static` or global variables, never locals).

## Default Input Mapping

Defined in `app_init.c` (`app_input_process()`):

| Button | Short Press | Long Press |
|--------|-------------|------------|
| **BtnA** | Next item | Confirm / Enter |
| **BtnB** | Previous item | Return / Cancel |

## Known Pitfalls

- **Color depth order**: In `hal_display.cpp`, `setColorDepth(16)` must be called **before** `createSprite()`. Otherwise alpha=0 makes all draws invisible (black screen).
- **Backlight**: `M5.begin()` does not always set max brightness. Explicitly call `M5.Display.setBrightness(...)`.
- **M5.update()**: Must be called in `loop()` before reading button state. Without it, all buttons read as false.
- **build_src_filter is unreliable**: The project uses `#ifdef NATIVE_TEST` to exclude `main.cpp` in native builds, not `build_src_filter`.
- **PlatformIO cache**: Delete `.pio/build/` after `platformio.ini` changes.
- **NVS key changes**: When changing a default stored value, rename the NVS key so old deployed devices pick up the new default.
- **WiFi logs**: `WiFi.begin()` floods serial with WARNING logs. Suppress with `esp_log_level_set("wifi", ESP_LOG_ERROR)` during connection.

## Tools

- **`tools/ui-layout-designer/`**: Pure-static-file web UI layout designer. Open `index.html` in a browser. No build step. Maps design elements directly to HAL APIs and Xerintosh item types.
- **`tools/icon_converter.py`**: Converts images to XBM bitmap C headers for use with `hal_draw_xbitmap()`.

## Documentation

All technical docs live under `doc/` and are written in **Chinese**. They mirror `src/` structure.

- `doc/index.md` — Central knowledge map (must be kept in sync).
- `doc/coding-style.md` — C OOP naming, encapsulation, inheritance rules.
- `doc/developer-guide.md` — How to build menus, create `user_item` apps, and organize code.
- `doc/kernel/` — Xeros kernel subsystem docs (types, task, VFS, devfs, procfs, sysfs, GPIO, IPC, syscall, shell, devices, portability).
- `doc/ui/` — UI core layer docs (item, core, context, dispatch, drawer, draw-icons, draw-anim, draw-list, draw-widgets, anim-row).
- `doc/app/` — App layer docs (app-init, settings, taskmgr, serial-monitor, ui-task, svc-mgr-helper).
- `doc/hal/` — HAL layer docs (display, input, system).
- `doc/tutorials/your-first-app.md` — Beginner tutorial for creating a `user_item` App.

Every code snippet in docs must have a source link with line anchors. Complex logic must include Chinese pseudocode breakdown. See `.claude/rules/technical-documentation.md` for the full standard.
