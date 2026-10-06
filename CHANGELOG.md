# Changelog

## Unreleased

### Highlights

- Moves the SDK and Pocket runtime (`os20` bitstream, `os.bin`) to the
  openfpgaOS API 0.9.0 generation shipped with Diablo 1.1.0. The application
  only uses the video, audio and input APIs, which are unchanged; the new
  bitstream and kernel still need a physical Pocket regression pass.
- Fetches Diablo as a pinned shallow, sparse Git checkout instead of a
  GitHub-generated tarball, so builds no longer depend on tarball checksums
  staying stable. The three runtime files remain SHA-256 pinned.
- Backports two newer z8lua fixes: `chr()` accepts up to 32767 values like
  PICO-8 v0.2.5+, and the obsolete `atan2(x, 0x8000)` bug emulation is gone.
- `peek(addr, n)` now allows up to 32767 values and reserves Lua stack space
  first. Previously any `n` above ~20 wrote past the C function's stack slots
  and could corrupt the heap.
- The 2× presentation pass resolves the palette and pixel doubling through a
  per-frame lookup table and writes two doubled source pixels per 32-bit store,
  halving framebuffer stores and removing the per-pixel palette branch.
- Host runtime tests build with GCC as well as Clang.
- Unflipped `spr()` calls share `map()`'s unpacked 8×8 tile blitter instead
  of the general per-pixel sprite path, and fully visible `print()` glyphs
  write their pixels directly. String arguments to `print()` skip the
  `tostring` conversion. Rendered frames are byte-identical.
- Lua's many small allocations are served from size-class free lists instead
  of musl's `malloc`.
- The Lua `debug` library is no longer visible to cartridges. PICO-8 has no
  such global, and Tetyis printed its table address on screen because of it.
  Existing save states remain compatible.
- Clip bounds poked into draw-state RAM are clamped to the screen, so they
  can no longer make renderers write past the framebuffer.

- The z8lua interpreter reads and writes existing entries of plain tables
  (string or integer keys) directly in its dispatch loop instead of calling
  `luaV_gettable`/`luaV_settable`. Globals, fields, array elements and method
  lookups all take this path; metatables, missing keys, string indexing and
  the `_ENV` sandbox fallback keep the original code.
- The Lua garbage collector waits until the heap reaches 4× the live data
  (Lua's default is 2×) before starting a cycle.
- `blit_tile` replaces `draw_sprite` in the 14 KiB on-chip BRAM so the larger
  interpreter loop still fits.
- `make deps` records the applied z8lua patch set and reapplies all patches
  from a clean tree when it changes, instead of checking each patch alone.

- `.p8` text carts now load like PICO-8 itself: UTF-8 glyphs in the Lua
  source (control codes, button symbols, characters 128-255) are converted
  back to single P8SCII bytes. Kiloman previously crashed decoding its data
  strings because they kept their multi-byte UTF-8 form.
- `print()` is a port of Fake-08's PICO-8 text engine: the complete
  256-glyph default font (including the smaller uppercase letters and
  button/kana glyphs), custom fonts at `0x5600`, and P8SCII control codes
  such as `\f` colour, `\#` background, cursor moves, wide/tall/inverted
  modes, one-off glyphs and `\^!`/`\^@` memory pokes. `print(str, col)`
  sets the colour and `print()` returns the right-most x position.
- Drawing can be redirected with `0x5f55` (to the sprite sheet or upper
  memory) and back, which carts such as Kiloman use to compose sprites.
- `palt(bitfield)` sets transparency for all sixteen colours at once.

- Writes into screen memory (`poke`, `memset`, `memcpy`, `reload`, print
  pokes) update only the affected framebuffer pixels instead of packing and
  unpacking the whole screen. Kiloman `memset`s its health bars about 26
  times per frame, which previously cost over half a million pixel
  conversions per frame.
- Full-screen packing and unpacking use aligned word loads and stores.
- `split()` and other number conversions parse plain decimal integers up to
  32767 directly instead of through `strtod` (exactly equivalent; checked
  against `strtod` for every integer string from -99999 to 99999).

- `spr()` with a negative sprite number draws nothing. Kiloman uses `-1`
  as "no sprite", which previously painted a black box under the player
  while shooting.
- The audio queue adapts to the frame time: it keeps about 1.5x the time
  between top-ups queued (43-170 ms), so slow frames no longer drain the
  buffer and make music stutter, while fast carts keep the low latency.

- Input follows PICO-8's once-per-frame sampling. The mid-frame updates that
  keep busy-wait loops responsive may now only add buttons for `btn()`: a
  quick tap released during a long frame is no longer dropped before the cart
  reads it, and a press arriving after the cart's `btnp()` check fires on the
  next frame instead of never.
- `btnp()` honours the repeat delay and interval at `0x5f5c`/`0x5f5d`,
  including 255 to disable repeating, as Kiloman does.

Profiling Tetyis on the host showed its own `_draw` repeats the HUD, falling
piece and ghost piece ten times per frame (a misplaced `end`). Together, these
changes cut host instructions per gameplay frame by 26% with byte-identical
output.

## 0.0.32 — 2026-08-22

Local hardware-test build.

### Highlights

- Moves the Pocket hardware/runtime base from the July openfpgaOS API 0.7.0
  snapshot to the matched API 0.8.1 SDK and runtime shipped by the official
  Diablo port. The application, OS image and `os20` bitstream now come from a
  mutually compatible generation rather than mixing old SDK headers with new
  binaries.
- Uses the newer dual-issue VexiiRiscv `rv32imafc` configuration at a
  timing-closed 90 MHz, with 32 KiB instruction and 64 KiB data caches and the
  August openfpgaOS Pocket fixes.
- Pins and verifies the Diablo source archive and all three Pocket runtime
  files by checksum so local builds cannot silently drift with upstream.

### Expected impact

- This is primarily a hardware/runtime-base test. It may improve consistency,
  cache behaviour and input/runtime correctness, but does not remove z8lua's
  interpreter cost by itself; CPU-heavy cartridges still need real-device
  profiling before performance claims are made.
- Save-state persistence, cartridge selection, menu input and audio must be
  regression-tested on physical Pocket hardware because the kernel/SDK base
  changed together.

## 0.0.31 — 2026-07-25

Second public alpha release.

### Highlights

- Reworks adaptive rendering around separately measured update and draw costs.
  Overloaded 30/60 FPS carts now skip drawing only when it materially protects
  game speed, recover visual cadence faster after heavy effects, and retain a
  bounded amount of frame-time debt instead of introducing extra waits.
- Expands the opt-in Select+X profiler with labelled logical/visible FPS,
  update/draw/audio/presentation timings, render divisor, approximate Lua work
  and PICO-8 API call categories. This makes CPU, drawing and presentation
  bottlenecks distinguishable on real Pocket hardware.
- Adds faster ordinary `map()` tile rendering, cheaper `mget()`/`fget()` and
  Lua table iteration, no-op palette/transparency fast paths, and a unity-gain
  audio path. Effect- and tile-heavy cartridges spend less time in common hot
  loops, although CPU-bound carts can still miss their target frame rate.
- Removes the experimental `FULL SOFT` scaler after hardware testing showed a
  visibly blurry result. Crisp pixel-perfect `2X` remains the default and the
  only enlarged display mode in this release.
- Confirms on physical Pocket hardware that all ten save-state slots persist
  after leaving and reopening the core, cartridge selection works on every
  fresh launch, Select is the sole menu shortcut, and the profiler starts off.
- Keeps the public package cartridge-free. No PICO-8 games, ROMs or third-party
  sound banks are included.

### Known limitations

- This is still an alpha and does not provide complete PICO-8 compatibility.
- Multicart `load()` switching, ZIP extraction, native 11× display output and
  the PICO-8 pause menu are not implemented.
- CPU-heavy cartridges such as BAS Escape, UFO and later Celeste 2 scenes can
  still run substantially below their intended frame rate.

## 0.0.27 — 2026-07-23

First public alpha release.

### Highlights

- Runs single-cartridge `.p8` and `.p8.png` PICO-8 games on Analogue Pocket.
- Adds a Select-driven system menu with per-cartridge controls, ten compressed
  save-state slots with previews, restart and cartridge switching.
- Implements PICO-8 graphics, input, fixed-point Lua execution, four-channel
  audio, `cartdata`, `dget` and `dset` compatibility.
- Provides pixel-perfect `2X` and optional full-screen soft scaling.
- Adds an opt-in FPS/runtime/audio/presentation profiler with Select+X.
- Fixes persistent-state slot alignment for the `os20` Pocket runtime.
- Includes compatibility and performance work for minified carts, heavy
  effects, large off-screen fills and responsive input under load.

### Known limitations

- This is an alpha and does not provide complete PICO-8 compatibility.
- Multicart `load()` switching, ZIP extraction and the PICO-8 pause menu are
  not implemented.
- CPU-heavy cartridges may run below their intended frame rate.
- Save persistence changes require confirmation on physical Pocket hardware.
- No cartridges, ROMs or third-party sound banks are included. Users must
  provide cartridges they are legally entitled to use.
