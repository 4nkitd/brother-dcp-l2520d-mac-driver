<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Making the driver report printer status

Research notes on what this printer can be made to report, and what macOS will
actually display. Findings are grounded in PJL status readback against the real
device, the CUPS filter API, and Apple's CUPS notes on macOS restrictions.

## What the printer already tells us

Queried over USB from user space with `tools/pjlquery`:

```
@PJL INFO STATUS
CODE=10001
DISPLAY="Ready"
ONLINE=TRUE

@PJL INFO STATUS          (idle, a few minutes later)
CODE=40000
DISPLAY="Sleep"
ONLINE=TRUE

@PJL DINQUIRE PAPER
A4
```

`@PJL INFO VARIABLES` also enumerates `COPIES`, `MANUALFEED`, `RESOLUTION`,
`AUTOCONT`, `PASSWORD`, `MEDIATYPE`, `ECONOMODE`, `IMAGEADAPT`, `INTRAY2` and
`PRINTQUALITY`.

Two things follow. The device answers status readback reliably, and the bus is
genuinely bidirectional — not write-only as the current filter assumes.

## What macOS will display

Filters and backends talk to the scheduler by writing prefixed lines to stderr:

| Prefix | Effect |
| --- | --- |
| `STATE: +media-jam` / `STATE: -media-jam` | Sets or clears `printer-state-reasons` for the queue |
| `ATTR: marker-levels=...` | Sets `marker-levels`, `marker-message`, `marker-names`, `marker-types` |
| `INFO:` / `WARNING:` | Sets `printer-state-message` |
| `PAGE:` | Counts sheets |

Standard reason keywords from the IANA IPP registry include `media-jam`,
`media-empty`, `media-low`, `media-needed`, `cover-open`, `toner-low`,
`toner-empty`, `marker-supply-low`, `marker-supply-empty`, `offline` and
`paused`. Vendor-specific reasons must be prefixed, e.g.
`com.brother.paper-mismatch`.

The CUPS documentation is explicit about the visible payoff: on macOS, a reason
ending in `-error` or `-warning` makes the printer's Dock item bounce, provided
the reason is localized with a `cupsIPPReason` entry in the PPD. Brother's own
driver did exactly this — the vendor PPD contains `cupsIPPReason` entries for
`com.brother.toner-low-report` and `com.brother.marker-supply-empty-warning`.

## PJL status codes worth mapping

The PJL specification says to key off `CODE=`, not `DISPLAY=`, because the
display string is localized on the printer.

| Code | Meaning | Standard reason |
| --- | --- | --- |
| 10001 | Ready | clear conditions |
| 10006 | Toner low | `toner-low` |
| 10023 | Processing job | — |
| 40000 | Sleep / standby | — |
| 40021 | Cover open / close cover | `cover-open` |
| 40022 | Paper jam | `media-jam` |
| 40038 | Low toner | `toner-low` |
| 40079 | Offline / service mode | `offline` |
| 40096 | Unexpected paper size, check tray | `com.brother.media-size-mismatch` |
| 40119 | Paper misfeed | `media-jam` |

Brother extends the standard set. Community SNMP templates for Brother laser
printers map vendor codes including 41213 (no paper), 40010 (no toner) and 62121
(replace toner). The exact set this model emits must be confirmed on real
hardware — several of these can only be triggered by a genuine fault.

## The honest limit on paper detection

The question "is A5 actually loaded?" cannot be answered by this printer.

`@PJL DINQUIRE PAPER` returns the size **configured on the control panel**, not a
measurement. The DCP-L2520D has a single tray and no media-width sensor. If you
load A5 and leave the panel on A4, the printer reports A4 and will warn — that is
precisely the `com.brother.tray1-size-warning` state the vendor filter used to
raise.

What is achievable, and still worth having: compare the job's media size against
the printer's configured size and warn **before** the first sheet. Printing 200
pages of A5 onto an A4-configured tray is the expensive failure this prevents.

## Constraint: no custom backend on macOS

The obvious design — a backend that queries the printer, emits `STATE:` lines,
then hands off to the stock USB backend — does not work here.

CUPS only loads backends from its `ServerBin`, which is
`/usr/libexec/cups/backend` on macOS. Apple's own CUPS documentation states that
current macOS does **not** allow installation to `/usr` with default System
Integrity Protection. On this machine `csrutil status` reports `enabled`, and
`/usr/libexec/cups` carries the `sunlnk` flag.

That does not prove the directory rejects new files — it only proves the
directory itself cannot be removed — so the point needs an actual write test with
sudo before it is designed around. But a driver that asks every user to disable
SIP or write into `/usr` is not distributable, which makes a custom backend a
poor choice regardless.

Filters are unaffected: they are loaded by absolute path from the PPD, which is
why this driver already lives in `/Library/Printers`.

So the query has to happen either inside the filter, or in a separate privileged
helper.

## Feasibility

| Feature | Possible | Data source | Effort |
| --- | --- | --- | --- |
| Jam warning | Yes | `INFO STATUS` 40022 / 40119 | Small |
| Cover open | Yes | 40021 | Small |
| Out of paper | Yes | 41213-style vendor code | Small |
| Toner low / out | Yes, condition only | 10006 / 40038 / 40010 | Small |
| Toner percentage | No | proprietary bidi protocol | Large |
| Paper size mismatch | Yes, configured vs requested | `DINQUIRE PAPER` | Medium |
| Physically loaded paper | **No** | no sensor exists | — |
| Page counter | Probably | `INFO PAGECOUNT` | Small |
| Push notifications | Yes | `@PJL USTATUS DEVICE=ON` | Medium |

## Two candidate architectures

### A. Filter-side back-channel

The filter writes a PJL query into its own output stream, the backend forwards it
to the device, and the filter reads the reply with `cupsBackChannelRead()` on
file descriptor 3.

- Pros: one binary, no daemon, no extra install steps.
- Cons: relies on the stock USB backend relaying back-channel data, which is
  unproven here. A manual test writing to fd 3 returned zero bytes. Query and
  job data also interleave in one stream, which is awkward to reason about.
- Verdict: test before committing. Cheap to try once the printer is reconnected.

### B. Privileged status helper

A small LaunchDaemon, running as root, opens the USB interface directly with
IOKit (already proven by `tools/pjlquery`), polls `INFO STATUS` and
`DINQUIRE PAPER` while the printer is idle, and caches the result. The filter
reads the cache at job start and emits `STATE:` and `ATTR:` lines.

- Pros: device access is proven to work; no dependency on backend behaviour; can
  poll on its own schedule; can use `USTATUS` later for push.
- Cons: a second component to install and uninstall; must avoid contending with
  `cupsd` for the USB interface during a job.
- Verdict: the reliable path, and the one to build if the back-channel test
  fails.

## Recommended sequence

1. Reconnect the printer and run `tools/pjlquery` against `INFO STATUS` to
   capture the codes this model actually emits while idle, sleeping and
   printing.
2. Test the fd 3 back-channel hypothesis with a throwaway filter. If the stock
   backend relays replies, choose architecture A.
3. Otherwise build architecture B, gated on "no job is active" before it opens
   the device.
4. Start with paper-size mismatch and jam/cover/offline, since those are the
   conditions that waste paper. Toner percentage is not worth reverse
   engineering Brother's proprietary channel.

## Status

Implemented, not yet validated on hardware.

Built: `tools/brother-statusd.c` (the poller), the `emit_printer_state()` mapping
in the filter patch, `cupsIPPReason` localization in the generated PPD, and
launchd wiring in `install.sh` / `uninstall.sh`.

The mapping logic is covered by `test.sh` against synthetic status files, so the
code paths are exercised. What has **not** been demonstrated is that this printer
emits the codes above for a real jam, a real empty tray, or low toner. Those need
genuine faults to trigger, and the daemon has not yet run against the device.

The two assumptions still to confirm on hardware:

1. That `@PJL INFO STATUS` returns the expected codes while the printer is in
   each fault state, rather than a vendor-specific value outside this table.
2. That the daemon does not disturb jobs when it opens the interface between
   polls.
