#!/bin/bash
# Install the native arm64 filter for the Brother DCP-L2520D.
#
#   sudo ./install.sh
#
# If the queue already exists it is snapshotted and repointed. If it does not,
# it is created against the printer found on USB. Any failure after the queue is
# changed restores the previous state.
set -euo pipefail

cd "$(dirname "$0")"
VERSION=$(cat VERSION 2>/dev/null || echo dev)

QUEUE="${QUEUE:-Brother_DCP_L2520D_series}"
MODEL_MATCH="${MODEL_MATCH:-direct usb://Brother/DCP-L2520D}"
DEST=/Library/Printers/brlaser
FILTER="$DEST/filter/rastertobrlaser"
PPD_SRC="out/brl2520d.ppd"
PPD_DEST="$DEST/ppd/brl2520d.ppd"
BACKUP_DIR="$DEST/backup"
STATE_FILE="$BACKUP_DIR/queue-$QUEUE.state"
STAMP=$(date +%Y%m%d-%H%M%S)

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo." >&2; exit 1; }

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo; echo "== $* =="; }

say "Preflight"

[ -x out/rastertobrlaser ] || die "No filter built. Run ./build.sh first."
[ -f "$PPD_SRC" ]          || die "No PPD built. Run ./build.sh first."

archs=$(lipo -archs out/rastertobrlaser)
[ "$archs" = "arm64" ] || die "Filter is '$archs', expected arm64 only."

grep -Fq "$FILTER" "$PPD_SRC" \
  || die "PPD does not reference $FILTER (rebuild with ./build.sh)."

echo "   filter: $archs"
echo "   ppd:    $(grep -m1 cupsFilter "$PPD_SRC" | sed 's/.*: *//')"

say "Preparing queue '$QUEUE'"
mkdir -p "$BACKUP_DIR"

created=0
if lpstat -p "$QUEUE" >/dev/null 2>&1; then
  echo "   queue exists, will be repointed"

  ORIGINAL="$BACKUP_DIR/$QUEUE.ppd.original"
  if [ ! -f "$ORIGINAL" ]; then
    cp "/etc/cups/ppd/$QUEUE.ppd" "$ORIGINAL" \
      || die "Could not preserve the original PPD. Refusing to continue."
    chmod 644 "$ORIGINAL"
    echo "   preserved original PPD -> $ORIGINAL"
  else
    echo "   original PPD already preserved"
  fi

  SNAPSHOT="$BACKUP_DIR/$QUEUE.ppd.$STAMP"
  cp "/etc/cups/ppd/$QUEUE.ppd" "$SNAPSHOT" \
    || die "Could not snapshot the current PPD. Refusing to continue."

  was_disabled=no
  lpstat -p "$QUEUE" 2>/dev/null | head -1 | grep -q "disabled" && was_disabled=yes
else
  uri=$(lpinfo -v 2>/dev/null | awk -v m="$MODEL_MATCH" 'index($0, m) { print $2; exit }')
  [ -n "$uri" ] || die "No queue named '$QUEUE' and no DCP-L2520D found on USB.
Connect the printer, switch it on, then retry. Devices seen:
$(lpinfo -v 2>/dev/null | sed 's/^/  /')"
  echo "   creating queue at $uri"
  created=1
fi

# ---- rollback --------------------------------------------------------------

changed=no
rollback() {
  if [ "$changed" = yes ]; then
    echo "Rolling back queue '$QUEUE'" >&2
    if [ "$created" = 1 ]; then
      lpadmin -x "$QUEUE" 2>/dev/null || echo "  rollback: could not delete queue" >&2
    else
      lpadmin -p "$QUEUE" -P "$SNAPSHOT" 2>/dev/null || echo "  rollback: could not restore PPD" >&2
      [ "$was_disabled" = yes ] && cupsdisable "$QUEUE" 2>/dev/null || true
    fi
  fi
}
trap 'rc=$?; if [ $rc -ne 0 ]; then rollback; fi; exit $rc' EXIT

# ---- install ---------------------------------------------------------------

say "Installing filter"
mkdir -p "$DEST/filter" "$DEST/ppd"
install -m 755 out/rastertobrlaser "$FILTER"
install -m 644 "$PPD_SRC" "$PPD_DEST"
echo "   $FILTER"

say "Configuring queue"
if [ "$created" = 1 ]; then
  lpadmin -p "$QUEUE" -E -v "$uri" -P "$PPD_DEST"
  changed=yes
  printf 'STATE=created\nVERSION=%s\n' "$VERSION" > "$STATE_FILE"
else
  lpadmin -p "$QUEUE" -P "$PPD_DEST"
  changed=yes
  printf 'STATE=repointed\nVERSION=%s\n' "$VERSION" > "$STATE_FILE"
fi

cupsenable "$QUEUE" || die "cupsenable failed; the queue may be stopped."
launchctl kickstart -k system/org.cups.cupsd || die "Could not restart cupsd."

trap - EXIT

say "Done"
lpstat -p "$QUEUE"
echo
echo "Filter arch: $(lipo -archs "$FILTER")"
echo "State:       $(grep -m1 STATE "$STATE_FILE" | cut -d= -f2)"
echo "Remove:      sudo ./uninstall.sh"
