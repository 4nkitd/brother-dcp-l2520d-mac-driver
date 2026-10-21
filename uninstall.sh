#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ankit Dagar
# Remove the native driver and put the queue back the way it was.
#
#   sudo ./uninstall.sh
#
# If install.sh created the queue, it is deleted. If it repointed an existing
# queue, the preserved original PPD is restored. Brother's Intel driver is not
# touched; see NOTICE and the README for restoring it from the archive.
set -euo pipefail

cd "$(dirname "$0")"

QUEUE="${QUEUE:-Brother_DCP_L2520D_series}"
DEST=/Library/Printers/brlaser
BACKUP_DIR="$DEST/backup"
STATE_FILE="$BACKUP_DIR/queue-$QUEUE.state"
ORIGINAL="$BACKUP_DIR/$QUEUE.ppd.original"

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo." >&2; exit 1; }

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo; echo "== $* =="; }

state=unknown
[ -f "$STATE_FILE" ] && state=$(grep -m1 '^STATE=' "$STATE_FILE" | cut -d= -f2)

say "Removing native driver"

case "$state" in
  created)
    echo "   deleting queue '$QUEUE' (created by install.sh)"
    lpadmin -x "$QUEUE" 2>/dev/null || echo "   queue already gone"
    ;;
  repointed)
    [ -f "$ORIGINAL" ] || die "No preserved original PPD at $ORIGINAL.
Refusing to guess. Restore the PPD manually if you need to keep the queue."
    echo "   restoring original PPD for '$QUEUE'"
    lpadmin -p "$QUEUE" -P "$ORIGINAL"
    cupsenable "$QUEUE" 2>/dev/null || true
    ;;
  *)
    echo "   no state file; leaving queue '$QUEUE' alone"
    ;;
esac

say "Removing installed files"
rm -rf "$DEST/filter" "$DEST/ppd"
echo "   removed $DEST/filter and $DEST/ppd"

if [ "${1:-}" = "--purge" ]; then
  rm -rf "$DEST"
  echo "   --purge: removed $DEST entirely, including backups"
else
  echo "   backups kept in $BACKUP_DIR"
fi

launchctl kickstart -k system/org.cups.cupsd
sleep 1

say "Done"
lpstat -p 2>&1 | sed 's/^/   /' || true
echo
if [ "${1:-}" = "--purge" ]; then
  echo "Backups were purged. If the Intel driver had been archived there, it is gone."
else
  echo "Archived vendor driver, if any, is still in $BACKUP_DIR."
fi
