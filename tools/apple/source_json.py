#!/usr/bin/env python3
"""Write the source file from which sideloading apps (Feather, AltStore, SideStore) offer the package.

Bundle ID, version, build, lowest system and size are read from the package itself, so the file cannot disagree
with what it points to.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import plistlib
import zipfile

ICON = "app/Resources/Assets.xcassets/AppIcon.appiconset/icon-1024.png"
TINT = "4c73b2"
DESCRIPTION = (
    "A native port of the visual novel 俺たちに翼はない (Navel, 2009) for iPhone and iPad.\n\n"
    "The app contains nothing of the game: copy the SET_UP folder of your own copy into the app's folder in Files, "
    "or import it from the app. With a mining.txt next to the game files it sends every line you read, with its "
    "voice and a screenshot, to a VN Mining Server.\n\n"
    "The package is unsigned. Sign it with your own certificate."
)


def app_info(ipa):
    with zipfile.ZipFile(ipa) as package:
        plists = [name for name in package.namelist()
                  if name.startswith("Payload/") and name.count("/") == 2 and name.endswith(".app/Info.plist")]
        if len(plists) != 1:
            raise SystemExit(f"{ipa}: expected one app in Payload, found {len(plists)}")
        return plistlib.loads(package.read(plists[0]))


def identifier(repository):
    """The name a sideloading app knows the source by. It must stay the same for good, or the app takes the next
    build's file for another source: so it is made of the owner alone and not of the repository's name, which has
    changed once (it was Oretsuba_iOS)."""
    return "io.github." + repository.split("/")[0].lower() + ".oretsuba-ios"


def source(ipa, repository, tag, commit, date):
    info = app_info(ipa)
    site = f"https://github.com/{repository}"
    icon = f"https://raw.githubusercontent.com/{repository}/main/{ICON}"
    download = f"{site}/releases/download/{tag}/{ipa.name}"
    size = ipa.stat().st_size
    version = info["CFBundleShortVersionString"]
    build = str(info["CFBundleVersion"])
    notes = f"Build {build}, made by GitHub from commit {commit[:7]} of {repository}."
    return {
        "name": "Oretsuba",
        "identifier": identifier(repository),
        "subtitle": "The newest build of the port of 俺たちに翼はない",
        "website": site,
        "iconURL": icon,
        "tintColor": TINT,
        "apps": [{
            "name": info.get("CFBundleDisplayName") or info["CFBundleName"],
            "bundleIdentifier": info["CFBundleIdentifier"],
            "developerName": repository,
            "subtitle": "Plays 俺たちに翼はない from your own game files",
            "localizedDescription": DESCRIPTION,
            "iconURL": icon,
            "tintColor": TINT,
            "versions": [{
                "version": version,
                "buildVersion": build,
                "date": date,
                "localizedDescription": notes,
                "downloadURL": download,
                "size": size,
                "minOSVersion": info["MinimumOSVersion"],
            }],
            "appPermissions": {"entitlements": [], "privacy": []},
            # Readers of the older format take the newest version from these.
            "version": version,
            "versionDate": date,
            "versionDescription": notes,
            "downloadURL": download,
            "size": size,
        }],
        "news": [],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ipa", type=Path, required=True)
    parser.add_argument("--repository", required=True, help="owner/name on GitHub")
    parser.add_argument("--tag", default="latest-build", help="the release that holds the package")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--date", help="ISO 8601; now if left out")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    date = args.date or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    document = source(args.ipa, args.repository, args.tag, args.commit, date)
    args.out.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    newest = document["apps"][0]["versions"][0]
    print(f"Wrote {args.out}: {newest['version']} ({newest['buildVersion']}), {newest['size']} bytes")


if __name__ == "__main__":
    main()
