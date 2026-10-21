# Changelog

## 0.1.0

First release.

- Native arm64 CUPS filter for the Brother DCP-L2520D, built from brlaser
  `2a49e328` with one local patch, replacing Brother's Intel-only filter so
  printing no longer requires Rosetta.
- Paper-size detection from page geometry when the raster carries no recognised
  size name. Uses a 2 pt tolerance against a table of point dimensions, tries
  `cupsPageSize` then `PageSize`, and falls back to A4.
- `install.sh` creates the queue if it is missing, or preserves and repoints an
  existing one. Refuses to install a non-arm64 filter.
- `uninstall.sh` deletes a created queue or restores a repointed one.
- `migrate.sh` removes Brother's Intel driver only after a queue job completes
  without filter or transport errors, archiving it first.
- `test.sh` covers the encode/decode round-trip, exact pixel placement, the full
  paper-size table, tolerance boundaries, and the integer fallback.
