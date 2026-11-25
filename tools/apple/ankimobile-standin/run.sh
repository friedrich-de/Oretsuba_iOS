#!/bin/sh
# Checks the cards for AnkiMobile in the iPhone Simulator, where the real
# AnkiMobile cannot be installed: a stand-in app takes AnkiMobile's links
# (StandIn/StandInApp.swift), the Oretsuba app runs its own check against it
# (ore_debug_ankimobile_check, debug builds only), and a UI test plays the
# player (UITests/RoundTripTests.swift). On a Mac with Xcode:
#
#   tools/apple/ankimobile-standin/run.sh
#
# It builds both apps for the Simulator, runs the round trip on the first
# iPhone the Simulator offers (or the one named by ORE_SIMULATOR, a device id)
# and compares what the app reports with what the stand-in received
# (verify.py). Everything it makes is under build/ankimobile-standin; what it
# found is in build/ankimobile-standin/results. Needs no game files.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/tools/apple/ankimobile-standin"
OUT="$ROOT/build/ankimobile-standin"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
mkdir -p "$OUT/results"

device="${ORE_SIMULATOR:-}"
if [ -z "$device" ]; then
	device="$(xcrun simctl list devices available -j | python3 -c '
import json, sys
found = [d for runtime, devices in sorted(json.load(sys.stdin)["devices"].items()) if "iOS" in runtime
         for d in devices if d["name"].startswith("iPhone")]
print(found[-1]["udid"] if found else "")')"
fi
[ -n "$device" ] || { echo "The Simulator offers no iPhone." >&2; exit 2; }
echo "Simulator: $device"

# The app, as a debug build: only that holds the check.
"$ROOT/tools/apple/gen_project.sh"
xcodebuild -project "$ROOT/app/Oretsuba.xcodeproj" -scheme Oretsuba -configuration Debug \
	-destination "platform=iOS Simulator,id=$device" -derivedDataPath "$OUT/app" \
	build > "$OUT/results/build-app.log" 2>&1 || { tail -60 "$OUT/results/build-app.log"; exit 1; }
app="$OUT/app/Build/Products/Debug-iphonesimulator/Oretsuba.app"
bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Info.plist")"
echo "Built $bundle"

XG="$(command -v xcodegen || true)"
[ -n "$XG" ] || XG="$ROOT/build/tools/xcodegen/bin/xcodegen"
"$XG" generate --spec "$HERE/project.yml" --project "$HERE"

xcrun simctl boot "$device" 2>/dev/null || true
xcrun simctl bootstatus "$device" -b > /dev/null
xcrun simctl uninstall "$device" "$bundle" 2>/dev/null || true
xcrun simctl uninstall "$device" org.local.ankistandin 2>/dev/null || true
xcrun simctl install "$device" "$app"

# The UI test installs the stand-in, starts the app with the check and plays the player.
rm -rf "$OUT/result.xcresult"
status=0
ORE_APP_BUNDLE="$bundle" TEST_RUNNER_ORE_APP_BUNDLE="$bundle" xcodebuild test -project "$HERE/AnkiStandIn.xcodeproj" -scheme AnkiStandIn \
	-destination "platform=iOS Simulator,id=$device" -derivedDataPath "$OUT/standin" -resultBundlePath "$OUT/result.xcresult" \
	> "$OUT/results/test.log" 2>&1 || status=$?
grep -E "Test Case|error:|passed|failed|XCTAssert" "$OUT/results/test.log" | tail -30 || true

xcrun simctl io "$device" screenshot "$OUT/results/end.png" > /dev/null 2>&1 || true
data="$(xcrun simctl get_app_container "$device" "$bundle" data 2>/dev/null || true)"
other="$(xcrun simctl get_app_container "$device" org.local.ankistandin data 2>/dev/null || true)"
[ -n "$data" ] && cp "$data/Documents/anki-check.txt" "$OUT/results/app-report.txt" 2>/dev/null || true
[ -n "$other" ] && cp "$other/Documents/standin.log" "$OUT/results/standin.log" 2>/dev/null || true
echo "---- the app's report"
cat "$OUT/results/app-report.txt" 2>/dev/null || echo "(none)"
echo "---- what the stand-in did"
cat "$OUT/results/standin.log" 2>/dev/null || echo "(none)"
echo "----"
python3 "$HERE/verify.py" "$OUT/results/app-report.txt" "$OUT/results/standin.log" || status=1
[ "$status" = 0 ] && echo "AnkiMobile stand-in check passed" || echo "AnkiMobile stand-in check FAILED"
exit "$status"
