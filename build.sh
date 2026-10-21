#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ankit Dagar
# Build the native arm64 Brother DCP-L2520D CUPS filter from vendored brlaser.
set -euo pipefail

cd "$(dirname "$0")"

ARCH="${ARCH:-arm64}"
DEPLOY_TARGET="${DEPLOY_TARGET:-13.0}"

rm -rf out/build
mkdir -p out

# Modern CMake rejects brlaser's cmake_minimum_required(VERSION 3.1).
cmake -S vendor/brlaser -B out/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build out/build --target rastertobrlaser -j"$(sysctl -n hw.ncpu)"

install -m 755 out/build/rastertobrlaser out/rastertobrlaser

# Generate the PPD for this model.
mkdir -p out/ppd
ppdc -d out/ppd out/build/brlaser.drv
install -m 644 out/ppd/brl2520d.ppd out/brl2520d.ppd

# CUPS resolves bare filter names against /usr/libexec/cups/filter.
# We install under /Library/Printers, so rewrite to an absolute path.
sed -i '' 's|rastertobrlaser|/Library/Printers/brlaser/filter/rastertobrlaser|' out/brl2520d.ppd

echo
echo "Built:"
lipo -archs out/rastertobrlaser | sed 's/^/  arch: /'
grep -m1 'cupsFilter' out/brl2520d.ppd | sed 's/^/  /'
