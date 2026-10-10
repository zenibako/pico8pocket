---
name: pico8pocket-performance
description: Measure and optimize Pico8Pocket's frame time on the Analogue Pocket (RV32 openfpgaOS) - reading the profiler overlay, reproducing a slow scene, counting RV32 instructions, BRAM placement and the frame-skip scheduler. Use when a cart runs slowly on the Pocket, when the user sends overlay screenshots, or before changing the interpreter, drawing or audio hot paths.
---

# Pico8Pocket performance

## The hardware, and what that implies

- VexiiRiscv rv32imafc at about 100 MHz: 8 KB instruction cache, 32 KB data
  cache, single-precision FPU only. `double` arithmetic is software-emulated
  at hundreds of instructions per operation, so keep it off per-call paths
  (`time()` used to divide in double; see `api_time`).
- 14,336 bytes of zero-wait BRAM for code (`APP_BRAM` in
  `.deps/diablo/src/sdk/app.ld`), in section `.app_fasttext`. The z8lua
  dispatch loop and a few helpers live there:
  - `include/p8p/vm_fasttext.h` (`OF_FASTTEXT` declarations, force-included
    into z8lua)
  - `P8P_FASTTEXT` in `src/runtime/runtime.cpp`
  Check the size with
  `riscv64-unknown-elf-size -A .obj/pico8pocket/app.elf | grep fast`.
  An overflow fails the link ("region APP_BRAM overflowed").
- Changing `vm_fasttext.h` does not trigger a rebuild. Run
  `rm -rf .obj/pico8pocket` before `make package`.
- Measured: the I-cache is not the bottleneck. Three quarters of fetched
  instructions come from BRAM, and simulated misses cost about 0.2 ms per
  frame. Frame time tracks RV32 instruction count.

## Reading the overlay (Select + X)

- **Left column:**
  - L: logical FPS. V: visible FPS.
  - U, D: the cart's `_update` and `_draw` ms.
  - A: audio pump ms. P: present ms.
  - R: render divisor (R02 = every other frame drawn).
  - K: Lua activity.
  - F: the whole runtime step that the scheduler times. It includes audio
    serviced mid-frame, GC and frame setup, so F can exceed U + D.
- **Right column:** API call counts per frame by group.
  - S: sprites. G: graphics. M: memory. C: draw state (`pal`/`palt`/`clip`...).
  - T: text. B: buttons. Q: helpers.
- **Scheduler** (`src/app/scheduler.c`): it keeps R1 while F + A + P fits the
  budget, 32 ms at 30 FPS or 15 ms at 60 FPS. Update cost is only measured on
  skipped frames. At R1 and over budget, one draw in 15 is skipped to
  re-measure it.
- A screenshot showing R02 with U + D + A + P under budget means F is larger.
  Ask for F before optimizing anything.

## Reproducing a slow scene

1. Read the cart: `build/tools/pico8pocket_compat_scan CART --source > cart.lua`
   (needs `make compat-scan`). Find a function that jumps to the scene, such
   as `goto_level(n)`.
2. Host profile of that scene:
   ```sh
   valgrind --tool=callgrind build/tools/pico8pocket_compat_scan CART \
       --bench "(function() goto_level(2) level_intro=0 return 1 end)()" 200 1 draw
   ```
   Subtract a 0-frame run to remove load cost. For update-only cost, use
   `nodraw` instead of `draw`.
3. **Pocket-accurate counts:**
   ```sh
   tools/rv32-profile/profile.sh CART "<setup lua>" 30 20
   ```
   It builds the runtime with the Pocket's flags (picolibc, QEMU user mode)
   and prints RV32 instructions per frame, the top functions and I-cache
   misses. It takes about a minute.
   - x86 counts can mislead. x86 spills registers that RV32 keeps, so
     dispatch-loop changes may be invisible on x86 and real on RV32, or the
     reverse.
   - `memset`/`memcpy` come from picolibc there and look slower than the
     SDK's.
   - To attribute cost to source lines or opcodes, build with debug info
     (`-g`) and read `build/rv32-profile/report.txt`. For per-address counts,
     extend `tools/rv32-profile/icsim.c`; it already supports a PC range in
     arguments 5 and 6 that writes `pcnt.txt`.

## Priority regression check

- `make celeste` compares Celeste carts against `build/celeste-baseline`:
  frame hash plus callgrind instructions per frame.
  `make celeste-baseline` records the baseline.
- Carts live only in `assets/cards/`, which is gitignored; never commit them.
- The cost figure moves by about 1.5% with binary layout alone, because Lua's
  string hash seed depends on addresses. Do not chase changes smaller than
  that.

## Where the time goes (Celeste 2 tower room, about 1.6M RV32 instructions per frame)

- **Interpreter loop:** about half the frame. Dispatch is 11 instructions per
  Lua instruction. Constant-key table access (`GETTABUP`/`GETTABLE`) uses
  per-instruction inline caches (`patches/z8lua-inline-cache.patch`,
  `z8lua-vm-tuning.patch`).
- **API calls:** they go through `luaD_callcfast`. C API functions read their
  arguments with `arg_int`/`arg_value`, not `lua_gettop`/`lua_tonumberx`.
- **Drawing:**
  - `blit_tile` (in BRAM) handles 8x8 sprites.
  - Patterned fills write 4 pixels at a time (`draw_rect_pattern`,
    `fill_pattern_span`).
- **Audio:** synthesized at 24 kHz and interpolated to 48 kHz. When muted,
  synthesis is skipped (`p8p_audio_skip`) but timing still advances.

## Before calling an optimization done

- Run `make test`, then `make package USE_SDK_CONTAINER=0`; the package must
  validate and BRAM must still fit.
- Compare frames on every local cart. Take one scanner from the base and one
  from your change (`scripts/compare-frames.sh build`), then
  `scripts/compare-frames.sh diff old new carts...`.
  - Expect identical frames unless the change is meant to alter output.
  - Kiloman and Grippy change with binary layout alone.
  - Fuzz anything with edge cases (fill patterns with camera, clip and
    palette, for example) using a small generated cart run through both
    scanners.
- For interpreter changes, also check that runtime error messages keep their
  line numbers, and that save states still replay (`make test` covers both).
- Ship a test zip and ask the user for overlay screenshots of the same scene.
  Host numbers are estimates.
