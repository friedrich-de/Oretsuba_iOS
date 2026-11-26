#!/bin/sh
# Build local test packages without game files or signing credentials.
# Usage: tools/apple/package.sh ios|visionos|macos
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
platform="${1:?usage: package.sh ios|visionos|macos}"
case "$platform" in
    ios) destination='generic/platform=iOS'; product=Release-iphoneos; name=Oretsuba-iOS-unsigned.ipa ;;
    visionos) destination='generic/platform=visionOS'; product=Release-xros; name=Oretsuba-visionOS-unsigned.ipa ;;
    macos) destination='generic/platform=macOS,variant=Mac Catalyst'; product=Release-maccatalyst; name=Oretsuba-macOS.zip ;;
    *) echo 'Expected ios, visionos or macos' >&2; exit 2 ;;
esac
"$ROOT/tools/apple/gen_project.sh"
derived="$ROOT/build/package-$platform"
out="$ROOT/build/packages"
mkdir -p "$out"
# ORE_BUILD_NUMBER, when set, becomes the package's build number and the last part of its version (the
# build on GitHub passes its run number): 0.1.0 in app/project.yml becomes 0.1.<number>, so that every
# published build is newer than the one before, also for a sideloading app that compares versions.
version=""
if [ -n "${ORE_BUILD_NUMBER:-}" ]; then
    case "$ORE_BUILD_NUMBER" in
        *[!0-9]*) echo 'ORE_BUILD_NUMBER has to be a number' >&2; exit 2 ;;
    esac
    base="$(sed -n 's/^ *MARKETING_VERSION: *"\([0-9][0-9]*\.[0-9][0-9]*\)\.[0-9][0-9]*" *$/\1/p' "$ROOT/app/project.yml")"
    [ -n "$base" ] || { echo 'app/project.yml: MARKETING_VERSION is not of the form "0.1.0"' >&2; exit 1; }
    version="$base.$ORE_BUILD_NUMBER"
fi
xcodebuild -project "$ROOT/app/Oretsuba.xcodeproj" -scheme Oretsuba \
    -configuration Release -destination "$destination" -derivedDataPath "$derived" \
    CODE_SIGNING_ALLOWED=NO ${ORE_BUILD_NUMBER:+"CURRENT_PROJECT_VERSION=$ORE_BUILD_NUMBER"} \
    ${version:+"MARKETING_VERSION=$version"} build
app="$derived/Build/Products/$product/Oretsuba.app"
# Packaging only the built application prevents inclusion of local game folders.
if [ "$platform" = macos ]; then
    codesign --force --deep --sign - "$app"
    codesign --verify --deep --strict "$app"
    ditto -c -k --sequesterRsrc --keepParent "$app" "$out/$name"
else
    staging="$(mktemp -d "$derived/payload.XXXXXX")"
    trap 'rm -rf "$staging"' EXIT HUP INT TERM
    mkdir "$staging/Payload"
    ditto "$app" "$staging/Payload/Oretsuba.app"
    ditto -c -k --keepParent "$staging/Payload" "$out/$name"
fi
(cd "$out" && shasum -a 256 "$name" > "$name.sha256")
echo "Created $out/$name (local test package; device validation is still required)"
