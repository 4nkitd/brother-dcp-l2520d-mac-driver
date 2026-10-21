# Native Brother DCP-L2520D driver for Apple silicon

A CUPS driver for the Brother DCP-L2520D that runs entirely as native arm64, so
printing no longer depends on Rosetta.

## The problem

Brother's macOS driver package contains no arm64 code. On an Apple silicon Mac
the per-job filter is an x86_64 binary that must be translated by Rosetta 2:

```
execv of .../rastertobrother2300 failed. err:86, Bad CPU type in executable
STATE: +com.apple.badarch-error
```

That failure appears when Rosetta is missing, and it returns whenever Rosetta is
unavailable. Apple has said Rosetta's scope narrows after macOS 27, so the vendor
driver has a limited future on Apple silicon regardless of the current state.

The DCP-L2520D is a host-based printer: no PCL, no PostScript, no AirPrint, and
no network interface. It only accepts Brother's proprietary raster command set,
so some filter has to encode that format.

## What this does

It builds [brlaser](https://github.com/pdewacht/brlaser) as a native arm64 CUPS
filter, with one local patch for paper-size detection, and points the print queue
at it. brlaser is an independent open source implementation of Brother's raster
protocol and already has a verified entry for this model.

No Brother code is used, wrapped, or emulated.

## Status

`0.1.0` — early. Read this before installing.

**Verified:**

- The filter builds as Mach-O arm64 with no Intel slices, linking only `libcups`,
  `libc++` and `libSystem`.
- Encode/decode round-trips are exact: a deterministic page decodes back to the
  same rows and columns.
- The encoded output printed correctly on a physical DCP-L2520D, sent directly to
  the USB device endpoint.
- Paper-size detection is covered for every size in the table, plus landscape,
  tolerance boundaries, and the integer-field fallback.

**Not verified:**

- The full queue path (application → `cupsd` → PPD → filter → backend) has not
  been exercised end to end on a fresh install. The hardware test above bypassed
  `cupsd`.
- Only one printer model and one host have been tested.

Treat this as beta software and keep a way back.

## Supported

| | |
| --- | --- |
| Printer | Brother DCP-L2520D series, **USB only** |
| Host | Apple silicon (M1 and later) |
| macOS | 13 or later; developed against macOS 27 |
| Not supported | Other Brother models, network queues, Intel Macs (use Brother's driver) |

The generated PPD set also contains entries for other brlaser-supported models,
but only the DCP-L2520D has been tested here. Nothing else is installed by
default.

## Requirements

- Xcode Command Line Tools (`xcode-select --install`)
- CMake
- The printer connected by USB and switched on

## Build

```sh
./build.sh
```

Produces `out/rastertobrlaser` (arm64) and `out/brl2520d.ppd`.

## Test

```sh
./test.sh
```

Runs brlaser's own unit tests, a text→raster→filter→bitmap round-trip, an exact
pixel-position check, and the paper-size table. No paper is used.

## Install

```sh
sudo ./install.sh
```

If a queue named `Brother_DCP_L2520D_series` exists, it is preserved and
repointed. If not, one is created against the printer found on USB.

Before changing anything the script stores the existing PPD in
`/Library/Printers/brlaser/backup/`. If a step fails after the queue is changed,
it puts the queue back.

Use a different queue name with:

```sh
sudo QUEUE=MyPrinter ./install.sh
```

The variable must come after `sudo`. `QUEUE=... sudo ./install.sh` will not work,
because sudo drops it from the environment.

## Verify

```sh
lpstat -p Brother_DCP_L2520D_series
lipo -archs /Library/Printers/brlaser/filter/rastertobrlaser
```

The second command must print `arm64`. Then print a page and watch the queue:

```sh
lpstat -o
```

## Uninstall

```sh
sudo ./uninstall.sh
```

Deletes the queue if `install.sh` created it, or restores the preserved PPD if it
repointed one. Leaves `/Library/Printers/brlaser/backup/` in place; add `--purge`
to remove that too.

## Removing Brother's driver (optional)

Once the native driver is printing reliably, the Intel driver can go. This is
destructive and separate from installing:

```sh
sudo ./migrate.sh
```

The script is ordered so that nothing is deleted until a real job has been
submitted through the queue, reached completed state, and the CUPS log shows no
filter or transport errors. It then archives `/Library/Printers/Brother`, verifies
the archive, and only after that removes it. If the print test fails, the vendor
driver stays where it is.

It asks for confirmation; use `--yes` for non-interactive runs.

Restoring the vendor driver afterwards means extracting the archive and
reinstalling Brother's package — package receipts are not files and cannot be
archived.

## Troubleshooting

**`No queue named ... and no DCP-L2520D found on USB`**
The printer is off, unplugged, or on a different USB port. The error lists every
device CUPS can currently see.

**Job sits in the queue, nothing prints**
Check `lpstat -p` for a stopped queue and `sudo cupsenable <queue>`. Then look at
`/var/log/cups/error_log` for the filter line.

**"The printer software is not compatible with this device"**
macOS is reporting on Brother's Intel driver, not this one. If it appears after
installing here, the queue is still pointing at the old PPD; re-run `install.sh`.

**Pages print on the wrong size**
The driver uses the document's own page geometry when the print job carries no
recognised size name. It cannot read what is physically in the tray — this model
has no paper-width sensor. Set the tray size on the printer's panel.

## Limitations

- No toner or paper status UI. Brother's `BrStatusMonitor` is not replaced.
- No secure print, humidity level, or sleep-mode settings.
- Resolution is exposed as 600 and 1200 dpi.
- Single-model support, USB only.

## License

**GNU General Public License, version 3 or later.** See `LICENSE`.

Every file here carries an `SPDX-License-Identifier: GPL-3.0-or-later` header,
except the vendored brlaser tree, which keeps its own upstream GPL-2.0-or-later
notices.

The driver cannot be permissively licensed: it contains brlaser code, which is
GPL-2.0-or-later, and GPL-3.0 is reachable through brlaser's "or later" grant.

See `NOTICE.md` for attribution, the vendored commit, and the local patch.
Redistributing a built binary requires making the corresponding source
available.
