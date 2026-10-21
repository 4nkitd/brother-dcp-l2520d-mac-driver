#!/bin/bash
# SPDX-License-Identifier: MIT
# Cutover: install the native filter, prove a real queue print works, and only
# then remove Brother's Intel (Rosetta) driver.
#
#   sudo ./migrate.sh
#
# If the print test fails, nothing is deleted and the Intel driver stays.
set -euo pipefail

cd "$(dirname "$0")"

QUEUE="${QUEUE:-Brother_DCP_L2520D_series}"
BROTHER_PKG=com.Brother.Brotherdriver.Brother_PrinterDrivers_MonochromeLaser
BROTHER_DIR=/Library/Printers/Brother
BACKUP_DIR=/Library/Printers/brlaser/backup
STAMP=$(date +%Y%m%d-%H%M%S)
LOG=/var/log/cups/error_log

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo." >&2; exit 1; }

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo; echo "== $* =="; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

ASSUME_YES=0
[ "${1:-}" = "--yes" ] && ASSUME_YES=1
if [ "$ASSUME_YES" -ne 1 ]; then
  if [ -t 0 ]; then
    echo "This will install the native driver, print a test page through the queue,"
    echo "and then permanently remove Brother's Intel driver from $BROTHER_DIR."
    printf 'Continue? [y/N] '
    read -r reply
    case "$reply" in
      y|Y|yes) ;;
      *) echo "Aborted. Nothing changed."; exit 1 ;;
    esac
  else
    die "Not running on a terminal. Re-run with --yes to confirm."
  fi
fi

say "1/7  Install the native filter and repoint the queue"
./install.sh

say "2/7  Verify the queue is off the Intel filter"
grep -qi "$BROTHER_DIR" "/etc/cups/ppd/$QUEUE.ppd" \
  && die "Queue still references $BROTHER_DIR. Aborting before any removal."
grep -m1 cupsFilter "/etc/cups/ppd/$QUEUE.ppd" | sed 's/^/   /'
lipo -archs /Library/Printers/brlaser/filter/rastertobrlaser | sed 's/^/   filter arch: /'

say "3/7  Print a test page through the queue"
cat > "$WORK/cutover.txt" <<'EOF'
F.R.I.D.A.Y.  CUTOVER TEST

Driver: native arm64 (brlaser 6), no Rosetta.
This page went through cupsd and the queue.

If you can read this, the queue path works.
EOF

before=0
[ -f "$LOG" ] && before=$(wc -c < "$LOG" | tr -d ' ')

lp_out=$(lp -d "$QUEUE" "$WORK/cutover.txt") || die "lp could not submit the job."
echo "   $lp_out"
job=$(printf '%s\n' "$lp_out" | sed -n 's/.*request id is \([^ )]*\).*/\1/p')
[ -n "$job" ] || die "Could not parse the job id from lp output."

echo "   waiting for $job"
deadline=$(( $(date +%s) + 120 ))
while [ "$(date +%s)" -lt "$deadline" ]; do
  if ! lpstat -W not-completed -o "$QUEUE" 2>/dev/null | grep -q -- "$job"; then
    break
  fi
  sleep 2
done

say "4/7  Verify the job actually succeeded"
lpstat -W completed -o "$QUEUE" 2>/dev/null | grep -q -- "$job" \
  || die "Job $job did not complete. Intel driver left in place."

if [ -f "$LOG" ]; then
  size=$(wc -c < "$LOG" | tr -d ' ')
  if [ "$size" -gt "$before" ]; then
    tail -c +$((before + 1)) "$LOG" > "$WORK/logsegment"
    if grep -qE "Bad CPU type|filter failed|Unable to send data|Unable to write print data|Stopped with status" "$WORK/logsegment"; then
      echo "   cupsd reported a failure for this job:" >&2
      grep -E "Bad CPU type|filter failed|Unable to send data|Unable to write print data|Stopped with status" "$WORK/logsegment" | tail -3 | sed 's/^/     /' >&2
      die "Print verification failed. Intel driver left in place."
    fi
  fi
fi
echo "   job completed, no filter or transport errors in the log"

say "5/7  Check nothing else depends on the Intel driver"
dependents=$(grep -l "$BROTHER_DIR" /etc/cups/ppd/*.ppd 2>/dev/null | grep -v "/$QUEUE.ppd$" || true)
if [ -n "$dependents" ]; then
  echo "   these queues still reference $BROTHER_DIR:" >&2
  printf '     %s\n' $dependents >&2
  [ "${FORCE:-0}" = "1" ] || die "Refusing to remove the driver. Set FORCE=1 to override."
  echo "   FORCE=1 set, continuing"
else
  echo "   no other queue depends on it"
fi

say "6/7  Archive the Intel driver"
mkdir -p "$BACKUP_DIR"
ARCHIVE="$BACKUP_DIR/brother-intel-$STAMP.tar.gz"
tar -czf "$ARCHIVE.partial" -C /Library/Printers Brother \
  || die "Archive failed. Nothing removed."
[ -s "$ARCHIVE.partial" ] || die "Archive is empty. Nothing removed."
tar -tzf "$ARCHIVE.partial" >/dev/null 2>&1 \
  || die "Archive is unreadable. Nothing removed."
mv "$ARCHIVE.partial" "$ARCHIVE"
echo "   $(du -h "$ARCHIVE" | cut -f1) -> $ARCHIVE"

# Receipts are not files on disk, so record them alongside the archive.
{
  pkgutil --pkg-info "$BROTHER_PKG" 2>/dev/null || echo "receipt already absent"
  pkgutil --files "$BROTHER_PKG" 2>/dev/null | head -50
} > "$BACKUP_DIR/brother-intel-$STAMP.receipt.txt" || true
cp "/etc/cups/ppd/$QUEUE.ppd" "$BACKUP_DIR/$QUEUE.ppd.native-$STAMP" || true

say "7/7  Remove the Intel driver"
pkgutil --forget "$BROTHER_PKG" >/dev/null 2>&1 || echo "   (receipt already gone)"
rm -rf "$BROTHER_DIR"
launchctl kickstart -k system/org.cups.cupsd
sleep 2

[ -d "$BROTHER_DIR" ] && die "Directory still present after removal."
echo "   removed; remaining arch in print path:"
lipo -archs /Library/Printers/brlaser/filter/rastertobrlaser | sed 's/^/     /'

echo
echo "Done. Test page printed from job $job."
echo "Rollback archive: $ARCHIVE"
