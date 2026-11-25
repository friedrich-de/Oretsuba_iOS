#!/bin/sh
# Checks the cards for AnkiDroid against the real AnkiDroid in an Android
# emulator: installs AnkiDroid and the app's debug build, opens AnkiDroid
# once, and runs the check the debug build carries (AnkiDroidCheck.kt), which
# adds cards and looks at what arrived. On a machine with the Android SDK
# (ANDROID_HOME) and the debug build made (./gradlew :app:assembleDebug):
#
#   tools/android/ankidroid_check.sh
#
# Without ORE_ADB_SERIAL it makes an emulator of its own (Android 14, x86_64;
# needs hardware virtualisation), starts it without a window and shuts it down
# at the end. With ORE_ADB_SERIAL it uses that device instead: use only one
# whose AnkiDroid collection does not matter, the check adds notes to it.
# ORE_ANKIDROID_APK names an AnkiDroid package to install; without it the
# newest release is downloaded from AnkiDroid's page on GitHub. What it found
# is in build/ankidroid-check.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/ankidroid-check"
APP="$ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
PACKAGE=io.github.friedrich_de.oretsuba
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$SDK" ] || { echo "ANDROID_HOME is not set." >&2; exit 2; }
[ -f "$APP" ] || { echo "Build the debug app first: (cd android && ./gradlew :app:assembleDebug)" >&2; exit 2; }
mkdir -p "$OUT"
PATH="$SDK/platform-tools:$SDK/emulator:$SDK/cmdline-tools/latest/bin:$PATH"

apk="${ORE_ANKIDROID_APK:-}"
if [ -z "$apk" ]; then
	apk="$OUT/AnkiDroid.apk"
	if [ ! -f "$apk" ]; then
		link="$(curl -fsSL ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} https://api.github.com/repos/ankidroid/Anki-Android/releases/latest |
			python3 -c '
import json, sys
assets = json.load(sys.stdin)["assets"]
for ending in ("-full-universal.apk", "-full-x86_64.apk", ".apk"):
    found = [a["browser_download_url"] for a in assets if a["name"].endswith(ending)]
    if found:
        print(found[0])
        break')"
		[ -n "$link" ] || { echo "AnkiDroid's newest release names no package." >&2; exit 2; }
		echo "AnkiDroid: $link"
		curl -fsSL -o "$apk" "$link"
	fi
fi

serial="${ORE_ADB_SERIAL:-}"
started=""
if [ -z "$serial" ]; then
	image="system-images;android-34;default;x86_64"
	# The SDK's licences are the machine's owner's to accept (sdkmanager --licenses); this only installs.
	sdkmanager --install "$image" "emulator" "platform-tools" < /dev/null > "$OUT/sdkmanager.log" 2>&1 || { tail -20 "$OUT/sdkmanager.log"; exit 1; }
	echo no | avdmanager create avd --force --name ore-ankidroid-check --package "$image" --device pixel_5 > "$OUT/avdmanager.log" 2>&1
	port="${ORE_EMULATOR_PORT:-5570}"
	serial="emulator-$port"
	emulator -avd ore-ankidroid-check -port "$port" -no-window -no-audio -no-boot-anim -no-snapshot -gpu swiftshader_indirect \
		-memory 2048 > "$OUT/emulator.log" 2>&1 &
	started=yes
fi
adb=adb
run() { "$adb" -s "$serial" "$@"; }
finish() {
	run logcat -d > "$OUT/logcat.txt" 2>/dev/null || true
	[ -z "$started" ] || run emu kill > /dev/null 2>&1 || true
}
trap finish EXIT

run wait-for-device
i=0
until [ "$(run shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do
	i=$((i + 1))
	[ "$i" -lt 180 ] || { echo "The emulator did not start." >&2; tail -20 "$OUT/emulator.log" 2>/dev/null; exit 1; }
	sleep 2
done
echo "Device: $serial, Android $(run shell getprop ro.build.version.release | tr -d '\r')"

# AnkiDroid first: Android gives an app a permission of another app's only when that app is there.
run install -r -g "$apk" > /dev/null
run install -r "$APP" > /dev/null
echo "AnkiDroid $(run shell dumpsys package com.ichi2.anki | grep -m1 versionName | tr -d ' \r')"
# Opened once, so that its collection is there.
run shell monkey -p com.ichi2.anki -c android.intent.category.LAUNCHER 1 > /dev/null 2>&1 || true
sleep 15
run exec-out screencap -p > "$OUT/ankidroid.png" 2>/dev/null || true

status=0
run shell am instrument -w ${ORE_CHECK_DECK:+-e deck "$ORE_CHECK_DECK"} "$PACKAGE/.AnkiDroidCheck" > "$OUT/check.txt" 2>&1 || status=$?
cat "$OUT/check.txt"
grep -q "AnkiDroid check passed" "$OUT/check.txt" || status=1
[ "$status" = 0 ] || echo "AnkiDroid check FAILED"
exit "$status"
