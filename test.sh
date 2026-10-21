#!/bin/bash
# SPDX-License-Identifier: MIT
# Round-trip and page-size tests for the native arm64 filter.
# Exercises the encoder without touching hardware.
set -euo pipefail

cd "$(dirname "$0")"
export LC_ALL=C
export PYTHONWARNINGS=ignore
PPD="$PWD/out/brl2520d.ppd"
FILTER="$PWD/out/rastertobrlaser"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -x "$FILTER" ] || { echo "No filter. Run ./build.sh first." >&2; exit 1; }

mkdir -p out/tools
clang -O2 -Wall -Wextra -o out/tools/mkraster tools/mkraster.c -lcups
[ -d out/build ] || { echo "No build dir. Run ./build.sh first." >&2; exit 1; }
cmake --build out/build --target brdecode -j4 >/dev/null 2>&1

pjl_paper() { grep -a -o 'SET PAPER = [A-Z0-9]*' | sed 's/SET PAPER = //' | head -1; }

encode() {
  PPD="$PPD" "$FILTER" 1 test "$1" 1 "" "$2" > "$3" 2>"$WORK/enc.log"
  grep -q '^ERROR' "$WORK/enc.log" && { cat "$WORK/enc.log"; return 1; }
  return 0
}

cat > "$WORK/pbm.py" <<'PY'
import sys

def parse(path):
    raw = open(path, 'rb').read()
    if not raw.startswith(b'P4'):
        raise SystemExit(f"{path}: not a P4 PBM")
    i, toks = 2, []
    while len(toks) < 2:
        while i < len(raw) and raw[i:i + 1].isspace():
            i += 1
        s = i
        while i < len(raw) and not raw[i:i + 1].isspace():
            i += 1
        if i == s:
            raise SystemExit("truncated PBM header")
        toks.append(raw[s:i])
    i += 1
    w, h = int(toks[0]), int(toks[1])
    stride = (w + 7) // 8
    data = raw[i:]
    if len(data) < stride * h:
        raise SystemExit(f"truncated bitmap: {len(data)} < {stride * h}")
    return w, h, stride, data

if __name__ == "__main__":
    w, h, stride, data = parse(sys.argv[1])
    rows = [y for y in range(h) if any(data[y * stride:(y + 1) * stride])]
    first_col = next((x for x in range(w)
                      if any(data[y * stride + x // 8] & (0x80 >> (x % 8)) for y in rows)), None)
    last_col = next((x for x in range(w - 1, -1, -1)
                     if any(data[y * stride + x // 8] & (0x80 >> (x % 8)) for y in rows)), None)
    print(w, h, (rows[0] if rows else -1), (rows[-1] if rows else -1),
          (first_col if first_col is not None else -1),
          (last_col if last_col is not None else -1))
PY

echo "0. Rebuild filter, tests and tools"
cmake --build out/build -j"$(sysctl -n hw.ncpu)" >/dev/null
ctest --test-dir out/build --output-on-failure 2>&1 | tail -3

echo "1. Pipeline: text -> CUPS raster -> native filter -> decode"
printf 'FRIDAY native driver round-trip test\n' > "$WORK/page.txt"
cupsfilter -p "$PPD" -m application/vnd.cups-raster "$WORK/page.txt" > "$WORK/page.ras" 2>/dev/null
encode pipeline "$WORK/page.ras" "$WORK/page.bin"
echo "   encoded: $(stat -f%z "$WORK/page.bin") bytes"

python3 - "$WORK/page.bin" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
assert data[:128] == b'\0' * 128, "expected 128-byte null prologue"
assert data[128:144].startswith(b'\x1b%-12345X'), "PJL header missing"
assert b'@PJL ENTER LANGUAGE = PCL' in data, "PCL entry missing"
print("   prologue: 128 nulls + PJL + PCL")
PY

"$PWD/out/build/brdecode" "$WORK/page.bin" >/dev/null 2>"$WORK/dec.log"
read -r w h first last c0 c1 < <(python3 "$WORK/pbm.py" "$WORK/page.bin-1.pbm")
[ "$w" -gt 0 ] && [ "$h" -gt 0 ] || { echo "   FAIL: empty bitmap"; exit 1; }
[ "$first" -ge 0 ] || { echo "   FAIL: page decoded blank"; exit 1; }
echo "   bitmap ${w}x${h}, ink rows ${first}..${last}"

echo "2. Exact round-trip: deterministic band must survive encoding"
# 595x842 pt at 600 dpi -> 4958x7016 px. mkraster draws rows >3508 and <3548.
./out/tools/mkraster "$WORK/band.ras" A4 595 842
encode band "$WORK/band.ras" "$WORK/band.bin"
"$PWD/out/build/brdecode" "$WORK/band.bin" >/dev/null 2>/dev/null
read -r bw bh bfirst blast bc0 bc1 < <(python3 "$WORK/pbm.py" "$WORK/band.bin-1.pbm")
exp_h=7016; exp_first=3509; exp_last=3547
if [ "$bh" -ne "$exp_h" ] || [ "$bfirst" -ne "$exp_first" ] || [ "$blast" -ne "$exp_last" ]; then
  echo "   FAIL: expected ${exp_h} rows with ink ${exp_first}..${exp_last}, got ${bh} with ${bfirst}..${blast}"
  exit 1
fi
# The band spans the full page width, so ink must reach both edges.
[ "$bc0" -eq 0 ] || { echo "   FAIL: left edge uninked (col $bc0)"; exit 1; }
[ "$bc1" -ge $((bw - 8)) ] || { echo "   FAIL: right edge uninked (col $bc1 of $bw)"; exit 1; }
echo "   ${bh} rows, ink ${bfirst}..${blast}, cols ${bc0}..${bc1} of ${bw}"

echo "3. Page-size detection from geometry alone"
fail=0
check() {
  local expect="$1" w="$2" h="$3"; shift 3
  ./out/tools/mkraster "$WORK/sz.ras" "" "$w" "$h" "$@"
  local got
  got=$(encode ps "$WORK/sz.ras" "$WORK/sz.bin" && cat "$WORK/sz.bin" | pjl_paper)
  if [ "$got" = "$expect" ]; then
    printf "   ok   %-10s -> %s\n" "${w}x${h}" "$got"
  else
    printf "   FAIL %-10s -> %s (expected %s)\n" "${w}x${h}" "$got" "$expect"
    fail=1
  fi
}

check A4        595  842
check A5        420  595
check A6        297  420
check B5        516  729
check B6        363  516
check C5        459  649
check DL        312  624
check MONARCH   279  540
check EXECUTIVE 522  756
check LEGAL     612  1008
check LETTER    612  792
echo "   -- orientation and tolerance boundary --"
check A4        842  595          # landscape A4
check A6        299  422          # 2 pt off, inside tolerance
check A4        300  423          # 3 pt off, outside tolerance
check A4        288  432          # 4x6in, must not snap onto A6
echo "   -- integer PageSize fallback (cupsPageSize zeroed) --"
check LETTER    612  792 --int-only
check A5        420  595 --int-only
[ "$fail" -eq 0 ] || exit 1

echo
echo "PASS: encoder round-trip and page-size detection are correct."
