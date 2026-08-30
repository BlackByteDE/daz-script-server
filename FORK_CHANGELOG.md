# Fork Changelog

All notable fork-specific changes to `daz-script-server` are documented here.
This fork's version scheme is `<upstream-version>+bb.<n>` — see
[README.md#about-this-fork](README.md#about-this-fork). Upstream changes from
[bluemoonfoundry/daz-script-server](https://github.com/bluemoonfoundry/daz-script-server)
are tracked in [CHANGELOG.md](CHANGELOG.md) (mirrored from upstream, not
duplicated here) — only fork-specific fixes are listed below.

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
