---
name: pico8pocket-release
description: Build, package and hand Pico8Pocket test builds to the user for Analogue Pocket testing, including side-by-side A/B cores, the SD-card layout, the platform banner and the PR/CodeRabbit review flow. Use whenever producing a zip for the user, changing packaging or core metadata, or opening and merging PRs in this repo.
---

# Pico8Pocket builds, device testing and PRs

## Building a test zip

- Run `make test`, then `make package USE_SDK_CONTAINER=0`. The second writes
  `releases/pico8pocket-v<VERSION>.zip` and must print "APF package
  validation passed: zenibako.pico8 on pico8".
- Run `rm -rf .obj/pico8pocket` first if `include/p8p/vm_fasttext.h` changed.
- Copy the zip to the repo root under a descriptive name, for example
  `pico8-zenibako-v0.0.33-<topic>.zip` (`*.zip` is gitignored), and send it
  with SendUserFile. The user tests every change on a real Pocket.
- Public zips never contain carts. `make package-local` bundles local carts
  and is for private use only.

## SD-card layout (core zenibako.pico8, platform pico8)

```text
Cores/zenibako.pico8/              core.json, data.json, os20.rbf_r, loader.bin, ...
Assets/pico8/common/               pico8pocket.elf, pico8pocket.ini, os.bin,
                                   p8p_pad5/6/7, cards/ (user carts)
Assets/pico8/zenibako.pico8/       pico8pocket.json (the game entry, slot 0)
Platforms/pico8.json
Platforms/_images/pico8.bin        platform banner
```

- The core ID is `author.shortname` from `dist/core/core.json`. The asset and
  platform folders come from `platform_ids[0]`.
  - `scripts/assemble-pocket.sh` reads both.
  - `scripts/validate-pocket.sh` checks the slot layout os20 needs.
- Users must extract with folder **merge**. macOS Finder's Replace deletes
  existing folders; Option-drag and choose Merge.
- Upgrading from `Askent.pico8pocket` means moving carts and saves from the
  `pico8pocket` folders to `pico8`.

## Side-by-side A/B testing

```sh
scripts/variant-core.sh releases/pico8pocket-v<VER>.zip b "Test build B" b.zip
```

- This produces core `zenibako.pico8b` on the same platform, sharing the
  cards.
- Every file it owns has a unique name: game entry, elf, ini, cfg and saves.
  Same-named game entries in two core folders made one disappear when the
  user copied files, so keep names unique.
- Name the variants A and B, say exactly what differs, and ask for the same
  scene's overlay from each.

## Platform banner

- Format: 521x165 grayscale, stored rotated 90 degrees counter-clockwise as
  16-bit pixels. The first byte is the inverted brightness and the second is
  0, matching the SDK's reference `openfpgaos.bin`.
- Banners can only be monochrome.
- To regenerate after changing `assets/branding/pico8-platform.png`, run
  `scripts/platform-image.py PNG dist/platforms/_images/pico8.bin`. Decode
  the result back to an image to check it before shipping.

## Branches, pushes and PRs

- Develop on the session's designated branch. After its PR merges, restart
  the branch from `origin/main`. The user may also ask for a new branch per
  change.
- Pushes to GitHub sometimes fail with "Internal Server Error". Retry in a
  loop with a pause; never force-push.
- Commit messages end with the Co-Authored-By and Claude-Session lines. No
  model identifiers in commits or PRs.
- CodeRabbit reviews PRs against `main` automatically:
  - The free plan allows about one review per hour; a rate-limited PR says
    when the next is available.
  - It never reviews closed or merged PRs.
  - For a PR whose base isn't `main`, comment `@coderabbitai full review`.
  - To review already-merged work, push a branch at the old base and open a
    review-only PR from `main` into it.
- The user wants PRs merged only after CodeRabbit has finished. For each
  finding:
  1. Check it against the code.
  2. Fix the real ones, with a test where possible.
  3. Reply on the thread with the commit and resolve it.
  4. For findings not worth their code, explain why and leave the call to
     the user.
