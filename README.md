# Brother DCP-L2520D driver for macOS — Apple silicon (M1/M2/M3/M4)

If your Brother DCP-L2520D stopped printing on an M1, M2, M3 or M4 Mac — or macOS
says the printer software is not compatible — this fixes it. Free, and it does
not use Rosetta.

> **Beta.** This has been tested on one Mac with one printer. Please read
> [Before you start](#before-you-start) and keep the uninstall step handy.

Not affiliated with Brother. Brother's own driver is not modified or
redistributed here; this is an independent driver.

## Before you start

You need all of these:

- An Apple silicon Mac (M1 or newer — not an Intel Mac)
- macOS 13 or later
- A Brother **DCP-L2520D** printer
- A USB cable (this printer has no network port)

If any of those are not true, this will not help you.

## Install

The quickest way is Homebrew:

```sh
brew install 4nkitd/tap/brother-dcp-l2520d
sudo brother-dcp-l2520d-install
```

Or from source. Open **Terminal** (press `Cmd+Space`, type `Terminal`, press
Return) and paste these three lines:

```sh
git clone https://github.com/4nkitd/brother-dcp-l2520d-mac-driver.git
cd brother-dcp-l2520d-mac-driver
sudo ./install.sh
```

It will ask for your Mac password. That is normal — writing a printer driver
requires administrator access. Nothing is sent anywhere.

When it finishes, print anything you like.

## Check it worked

```sh
lipo -archs /Library/Printers/brlaser/filter/rastertobrlaser
```

If that says `arm64`, the new driver is installed and Rosetta is out of the
picture.

## Undo

```sh
sudo brother-dcp-l2520d-uninstall
```

Or if you installed from source:

```sh
cd brother-dcp-l2520d-mac-driver
sudo ./uninstall.sh
```

That removes the driver and puts your printer back the way it was.

## If it doesn't print

**"No queue named ... and no DCP-L2520D found on USB"**
The printer is off, unplugged, or on a different USB port. Plug it in, switch it
on, and run the install again.

**The job disappears but nothing comes out**
Check the queue:

```sh
lpstat -p
```

If it says `disabled` or `stopped`, run `sudo cupsenable Brother_DCP_L2520D_series`
and print again.

**It prints on the wrong paper size**
Set the paper size on the printer's own control panel. The driver follows the
document's page size, but this printer cannot sense what is physically in the
tray.

## What it actually does

Brother's macOS driver only contains Intel code. On an Apple silicon Mac it has
to run through Rosetta, and when Rosetta is missing you get
`Bad CPU type in executable` and the job dies.

This project builds a small, native Apple silicon driver from an open source
implementation of Brother's printer language, and points your print queue at it.

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE).

## Developers

Build instructions, architecture, the patch to upstream, and what has and has
not been verified: see [docs/TECHNICAL.md](docs/TECHNICAL.md).
