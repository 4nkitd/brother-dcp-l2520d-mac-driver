# Licensing and third-party attribution

This repository mixes original code with a vendored, patched upstream project.
The two carry different licenses, so read this before redistributing.

## Summary

| Part | License |
| --- | --- |
| `build.sh`, `install.sh`, `uninstall.sh`, `migrate.sh`, `test.sh` | MIT |
| `tools/mkraster.c`, `tools/pjlquery.c` | MIT |
| `vendor/brlaser/` | GPL-2.0-or-later, © 2013 Peter De Wachter and contributors |
| `vendor/patches/` | GPL-2.0-or-later (derivative of brlaser) |
| Generated PPD (`brl2520d.ppd`) | GPL-2.0-or-later (derived from `brlaser.drv.in`) |
| Built `rastertobrlaser` binary | GPL-2.0-or-later (contains brlaser code) |
| The repository as a whole | GPL-3.0-or-later |

Original files carry an `SPDX-License-Identifier: MIT` header. Vendored files keep
their upstream headers unchanged.

## Why the whole project is GPL-3.0-or-later

The printed output is produced by a filter binary that contains brlaser code. That
makes the binary a derivative work, so it cannot be distributed under MIT. There
is no way to ship a working driver under a permissive license without replacing
brlaser entirely.

GPL-3.0 is available because brlaser grants "version 2 of the License, or (at your
option) any later version". Upgrading to version 3 is therefore permitted by the
upstream grant itself. `vendor/brlaser/` keeps its original GPL-2.0-or-later
notices; nothing upstream was relicensed.

The MIT-licensed scripts and tools are GPL-compatible, so the combined
distribution can be conveyed under GPL-3.0-or-later without any conflict.

## brlaser

- Upstream: https://github.com/pdewacht/brlaser
- Copyright: 2013 Peter De Wachter and contributors
- License: GNU General Public License, version 2 or later
- Vendored commit: `2a49e3287c70c254e7e3ac9dabe9d6a07218c3fa`
- Location: `vendor/brlaser/`

brlaser implements Brother's proprietary raster command set. This project does
not reimplement that protocol.

## Local patch

`vendor/patches/0001-page-size-detection.patch` modifies `build_page_params()` in
`src/main.cc`:

- Upstream resolves paper size only from `cupsPageSizeName` and falls back to a
  hardcoded `A4`.
- The patch falls back to page geometry (`cupsPageSize`, then `PageSize`) against
  a table of dimensions in points, with a 2 pt tolerance.

Applying the patch to upstream `2a49e328` reproduces `vendor/brlaser/` exactly.

## Compliance

If you distribute a built binary, GPL-3.0-or-later requires that you also make the
corresponding source available, including the patch above. Shipping this
repository alongside the binary satisfies that.

## Apple and CUPS

The filter links against system CUPS libraries (`libcups`, `libc++`, `libSystem`)
and is built with Apple clang. No Apple code is redistributed here.
