# Licensing and third-party attribution

## License

This repository is distributed under the **GNU General Public License, version 3
or later**. See `LICENSE`. Every file in this repository that is not vendored
carries an `SPDX-License-Identifier: GPL-3.0-or-later` header.

## Why GPL-3.0 and not a permissive license

The printed output is produced by a filter binary that contains brlaser code, so
the binary is a derivative work of a GPL project and cannot be released under
MIT or any other permissive license. There is no way to ship a working driver
permissively without replacing brlaser entirely.

GPL-3.0 is available because brlaser grants "version 2 of the License, or (at
your option) any later version". Upgrading to version 3 is therefore permitted
by the upstream grant itself.

## brlaser

- Upstream: https://github.com/pdewacht/brlaser
- Copyright: 2013 Peter De Wachter and contributors
- License: GNU General Public License, version 2 or later
- Vendored commit: `2a49e3287c70c254e7e3ac9dabe9d6a07218c3fa`
- Location: `vendor/brlaser/`

The vendored tree keeps its upstream notices unchanged; nothing there was
relicensed. brlaser implements Brother's proprietary raster command set. This
project does not reimplement that protocol.

## Local patch

`vendor/patches/0001-page-size-detection.patch` modifies `build_page_params()` in
`src/main.cc`:

- Upstream resolves paper size only from `cupsPageSizeName` and falls back to a
  hardcoded `A4`.
- The patch falls back to page geometry (`cupsPageSize`, then `PageSize`) against
  a table of dimensions in points, with a 2 pt tolerance.

Applying the patch to upstream `2a49e328` reproduces `vendor/brlaser/` exactly.

## Generated output

The PPD produced by `ppdc` from `vendor/brlaser/brlaser.drv.in` is derived from
brlaser and is GPL-2.0-or-later, as is the built `rastertobrlaser` binary.

## Compliance

If you distribute a built binary, GPL-3.0-or-later requires that you also make
the corresponding source available, including the patch above. Shipping this
repository alongside the binary satisfies that.

## Apple and CUPS

The filter links against system CUPS libraries (`libcups`, `libc++`, `libSystem`)
and is built with Apple clang. No Apple code is redistributed here.
