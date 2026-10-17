# Palette fast paths: draft experiment

This change passes correctness checks but fails the requirement that neither
Celeste 2 nor Moss Moss regress. Keep it as a draft until the performance
regressions are resolved. The earlier machine-code diagnostic's gains did not
transfer to this source implementation on current main.

## Implementation

An argument-free `pal()` returns immediately when both palettes and transparency
already have their default values and API profiling is disabled. The complete
implementation handles mutations, resets with changed state, return values and
profiling. Its small wrapper lives in BRAM.

The tile renderer also recognizes an identity draw palette when a screen-palette
change has cleared the combined `palettes_default` flag. Four aligned, alias-safe
word comparisons inspect the current draw palette. This avoids introducing a
cache that would need invalidation for `pal`, `poke`, `memcpy` and state loading.
Custom transparency still uses the general path. Runtime layout and save format
remain unchanged.

## Measured result

Baseline: main `1ef6525b9bd875ee7bb3ea81162637b53f12aa7b`.
Candidate: `faa9b61ba0e696d020b9bf2e561163784ebd3ef8`.
The candidate excludes the separate unpublished audio batching experiment.

These are fixed-seed (`0x5eed1234`) simulated os20 / 90 MHz runtime measurements,
with 12 measured frames per window. They are selected windows rather than full
game or physical Pocket FPS measurements. Positive changes mean slower frames.

| Window | Frames | Baseline ms | Candidate ms | Quiet change | Scanout change |
| --- | --- | ---: | ---: | ---: | ---: |
| Celeste 2 tower (`intro2`) | 100–111 | 26.745094 | 26.346743 | -1.489439% | -1.552526% |
| Celeste 2 Trailhead | 100–111 | 43.293020 | 43.604199 | +0.718773% | +0.760238% |
| Moss Moss steady | 180–191 | 49.397436 | 49.464786 | +0.136343% | +0.155413% |
| Moss Moss GC | 234–245 | 50.100072 | 50.101220 | +0.002292% | +0.015433% |

The quiet runtime capacity is 37.39 → 37.96 FPS for the tower window, 23.10 →
22.93 for Trailhead, 20.24 → 20.22 for Moss steady, and 19.960 → 19.960 for Moss
GC. The tower cart requests 30 FPS, so both tower results have headroom above its
cap. Capacity is not an observed device frame rate.

The result establishes a source-level tradeoff, not its isolated cause: the
portable implementation changes instructions, placement and layout, and the
baseline also differs from the earlier diagnostic. Further attribution needs
matched component and layout controls. The small Moss changes should not be
presented as meaningful improvements.

## Verification

- Native core and runtime suites pass on the exact candidate source, including
  palette API return values, custom transparency, profiling counts, all 16 draw
  palette entries, sprite/map output, RAM mutation and save/load transitions.
- A separate 48-case tile oracle passes natively and with all 35 translation
  units instrumented by ASan/UBSan. It checks literal framebuffer pixels and
  packed-screen checksums. An instrumented lookup oracle confirms screen-only
  remapping avoids per-pixel draw-palette reads; the baseline fails those two
  optimization assertions while retaining correct pixels.
- Removing the wrapper's profiling guard makes the public runtime suite fail.
  Omitting the last palette-word comparison also makes it fail.
- All eight baseline/candidate benchmark cases pass repeated native and QEMU
  checks, quiet and scanout RTL memory checks, and deliberately failing negative
  controls. Each scene's 600-frame indexed pixels, screen palettes and audio are
  byte-identical between baseline and candidate.
- The production ELF links with GCC 13.2: `.app_fasttext` uses 14,302 of 14,336
  bytes, leaving 34 bytes. Baseline uses 14,182 bytes. Disassembly confirms four
  word comparisons without a `memcpy` call and a 50-byte `pal` wrapper.

Production candidate ELF SHA-256:
`d3a63d02eefc2365e90bcef6f6b54d4a416b5ca73a858cedb4cb70e1bfc98bab`.
No installable package was produced for this draft.

The private evidence is retained in the companion `openfpga-os25-harness`
checkout under `experiments/pico8-sources/palette-pr-20261016-r1` (source,
production and tests) and `palette-pr-20261016-r2` (complete timing matrix).
The public companion summary is `pico8/PALETTE-SOURCE-OS20.md` with machine-readable
results in `pico8/PALETTE-SOURCE-OS20.csv`.

The initial source implementation exceeded BRAM by 14 bytes because the compiler
emitted a `memcpy` call. Four separate aligned four-byte copies eliminated that
call and produced the fitting version above. The first baseline timing attempt
also exposed pending writes when the simulator stopped immediately at its final
HAL marker. The complete matrix was rerun with the same untimed 4,096-NOP drain
after all measured frames in both variants. Memory checks remain strict; the
frozen CPU/simulator inputs were not changed. Results from the failed attempt
are preserved and excluded from the table.
