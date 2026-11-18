#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app="$HOME/Applications/ANNA 通话助手.app"
identity=${ANNA_SIGNING_IDENTITY:-}
if [ -z "$identity" ]; then
    identity=$(security find-identity -v -p codesigning | awk '
        /"Apple Development:/ { identity = $2; count++ }
        END { if (count == 1) print identity; else print "-" }
    ')
fi
mkdir -p "$app/Contents/MacOS"
xcrun swiftc -swift-version 5 "$src/main.swift" "$src/ChromeCall.swift" "$src/ChromeText.swift" -o "$app/Contents/MacOS/ANNACallHelper"
cp "$src/Info.plist" "$app/Contents/Info.plist"
codesign --force --sign "$identity" "$app"
codesign --verify --strict "$app"
