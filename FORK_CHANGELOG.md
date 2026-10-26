# Fork Changelog

All notable fork-specific changes to `daz-script-server` are documented here.
This fork's version scheme is `<upstream-version>+bb.<n>` — see
[README.md#about-this-fork](README.md#about-this-fork). Upstream changes from
[bluemoonfoundry/daz-script-server](https://github.com/bluemoonfoundry/daz-script-server)
are tracked in [CHANGELOG.md](CHANGELOG.md) (mirrored from upstream, not
duplicated here) — only fork-specific fixes are listed below.

## Unreleased

### Added

`POST /geometry/frames` takes channel overrides: `"channels"` (bone/node
property, morph, or figure property) plus `"values"`, one row per override
set. Every frame is evaluated once per set with the values written at that
frame's time, then each channel is restored without a trace — an existing key
gets its value back, a key the override created is deleted, a keyless channel
loses its keys again (on Studio 6 any `setValue()` keys a channel; keyless it
sits at its default). With sets the payload is DZGF version 2
(`verts[frames][sets][verts][3]`, header carries the set count and the number
of restores that could not be verified); without them the response stays
byte-identical version 1. Measured on a dressed G8F: identical to the
DazScript `setValue` + `forceCacheUpdate` loop, ~20 ms per evaluation in one
call instead of ~234 ms. Requested by pinkcharakter (bug 48).

## 2.9.2+bb.2 — 2026-10-20

### Added

`POST /geometry/frames` — deformed figure vertices (base resolution by
default, optionally a vertex/surface subset) plus world-space bone origins for
a list of frames, returned as one binary float32 payload
(`application/octet-stream`, format in `include/GeometryExport.h`). Only the
figure is evaluated per frame (`setFrame` + `forceCacheUpdate`); the frame is
restored afterwards, followed by one Scene update (`"sceneUpdate": false`
skips it, ~130 ms). The Resolution Level is switched to Base only when the
cached mesh is not already the base cage — at "High Resolution" it normally
is, and the toggle alone cost ~1 s per call. Measured on a dressed G8F
(16,556 vertices): ~17 ms/frame and ~5 ms for a single frame without update,
vs. ~234 ms/frame for the equivalent DazScript loop; values bit-identical.
Requested by pinkcharakter.

### Fixed

Plugin routes (`registerPluginRoute`) set the response body via `strlen`,
truncating any body containing a NUL byte; the length is now passed
explicitly.

## 2.9.2+bb.1 — 2026-08-30

Fork baseline, based on `bluemoonfoundry/daz-script-server` at `2.9.2`
(merge-base `c4f001e`).

### Fixed

DazScript `renderMgr["renderFinished(bool)"].connect()` throws on DAZ Studio 6
(`Cannot call method 'connect' of undefined`) before `doRender()` runs, so
`POST /render` / `daz_render_async` always failed. Bind the signal only when
it exists; otherwise treat a completed `doRender()` as success. (`0a68db0`)

### Changed

- Git history for the two fork commits was rewritten to correct a
  misattributed author (recorded as "GSH", actually Black-Byte): `fef3ba6` →
  `0a68db0`, `e6d503e` → `9a58456`.
