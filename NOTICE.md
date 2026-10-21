# Third-party components and attribution

## brlaser

This project vendors and patches **brlaser**, an open source CUPS driver for
Brother laser printers.

- Upstream: https://github.com/pdewacht/brlaser
- Copyright: 2013 Peter De Wachter and contributors
- License: GNU General Public License, version 2 or (at your option) any later version
- Vendored commit: `2a49e3287c70c254e7e3ac9dabe9d6a07218c3fa` (see `vendor/brlaser.commit`)
- Location: `vendor/brlaser/`

brlaser implements Brother's proprietary raster command set. This project does
not reimplement that protocol; it builds brlaser as a native arm64 binary and
adds one patch.

## Local modifications to brlaser

`vendor/patches/0001-page-size-detection.patch` — derived from brlaser and
therefore under the same license. It changes how `build_page_params()` resolves
the paper size in `src/main.cc`:

- Upstream used only `cupsPageSizeName` and fell back to a hardcoded `A4`.
- The patch falls back to the page geometry (`cupsPageSize`, then `PageSize`)
  against a table of paper dimensions in points, with a 2 pt tolerance.

## Apple and CUPS

The filter links against the system CUPS libraries (`libcups`, `libc++`,
`libSystem`) and is built with Apple clang. No Apple code is redistributed here.

## License of this project

Because the filter binary is a derivative work of brlaser, the project as a whole
is distributed under the **GNU General Public License, version 2 or later**. See
`LICENSE`.

In practice this means: if you redistribute a built binary, you must also make
the corresponding source available, including the patch above. Shipping this
repository alongside the binary satisfies that.
