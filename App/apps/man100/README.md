# Man100 — 是男人就下100层

Version: **0.1**

A compact port of the classic Wenquxing "是男人就下100层" ("It takes a man to
descend 100 floors") for the 128x64 monochrome LCD, built on the overlay-app
framework. The whole shaft rises toward a spiked ceiling while the man falls
under gravity — steer him left and right to drop from one floor to the next.

## Goal

Descend **100 floors** to win. It is one life: touch the spiked ceiling, land
on a spiked floor, or fall out the bottom of the screen and the run ends
immediately.

The floor generator always leaves a way down — each safe floor plans the next
one a bounded distance away, and spiked floors are kept out of the fall
corridor between them, so a skilled run is always survivable.

## Keys

| Key | Action |
|---|---|
| `←` / `→` (or `4` / `0`) | Move left / right |
| `F` | Pause / resume |
| `MENU` | Start / restart after GAME OVER or YOU WIN |
| `EXIT` | Quit |

The status bar shows `F <floor>` on the left.

## Build

Built as an overlay-app blob (`.app`) by `compile-app.sh` inside the
`uvk1-uvk5v3` Docker image — the CI workflow `.github/workflows/build-apps.yml`
compiles every app under `App/apps/` on a push to `main00`/`main` and uploads
`build/Apps/*.app` as an artifact. It links at the pinned overlay VMA
(`0x20000280`) and must stay within the 4 KiB overlay budget; sprites, texts
and the title art live in the separate flash asset area (API level 2,
`asset_read`).
