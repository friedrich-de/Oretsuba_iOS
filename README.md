# 俺たちに翼はない on Apple devices and Android

A native port of 俺たちに翼はない (Navel, 2009) for iPhone, iPad, Apple Vision Pro, Mac and Android.

This is a fork of [galhack/Oretsuba_iOS](https://github.com/galhack/Oretsuba_iOS). It adds mining (see below) and an
Android app, plays the Japanese original only, and has its apps and documents in English.

> **Pre-release 0.1.1.** The opening and the first chapters run with text, voices, saving and loading, and both opening
> films play to their end. Not every route has been played through, and some visual effects still have open questions
> from the reverse engineering. What was checked, on which devices and how, is in
> [docs/VALIDATION.md](docs/VALIDATION.md).

The Lucifen engine the game uses was written anew for this project (script virtual machine, layer compositing,
transitions and effects, text, interface, audio, opening films, saves), so the game runs natively on Apple devices
and on Android.

**This repository contains nothing of the game.** You have to buy the game yourself and use your own game files.

> **Mining.** This fork makes Anki cards from what you read, in two ways. In the game (on Apple devices so far):
> a tap on a word of the text shows a dictionary over the game, and a button makes the card, with the sentence,
> its voice, a picture and the word's sound, through an AnkiConnect you name in the launcher. Or through a
> [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server), to which the game sends every line you
> read once a `mining.txt` lies in the game folder. Both are off until you set them up:
> see [docs/MINING.md](docs/MINING.md).

## Supported devices

| Device | How | Lowest system the build supports |
|---|---|---|
| iPhone, iPad | Sideloading (Sideloadly or Xcode) | iOS / iPadOS 17 |
| Apple Vision Pro | A native visionOS app, signed and installed with Xcode | visionOS 1 |
| Mac (Apple silicon or Intel) | Runs directly | macOS 14 |
| Android phone or tablet (64-bit) | Install the APK | Android 8.0 |

## The game files

You need the **`SET_UP` folder** from the game's disc (or from an image of the disc). It holds `SCRIPT.LPK`,
`SYS.LPK`, `CHR.LPK`, `PIC.LPK`, `BGM.LPK`, `SE.LPK`, `VOICE.LPK` and `DATA.LPK`. The game does not have to be
installed on Windows.

## Installing

GitHub builds the unsigned iPhone and iPad package and the Android package from the code of `main` after every
change and publishes them as the release [Latest build](../../releases/latest), so those packages always come from
the code in this repository ([the workflow](.github/workflows/build.yml)). The Vision Pro and Mac packages are built
on a Mac, see
[Checks and packages](#checks-and-packages). The builds of the
[original project](https://github.com/galhack/Oretsuba_iOS/releases) come without mining.

### iPhone / iPad (a source for Feather, AltStore or SideStore)

Apps that install from sources can take the newest build straight from this address. Add it as a source (in Feather
under Sources) and the app appears there with its build number and date:

```text
https://github.com/friedrich-de/Oretsuba_iOS/releases/latest/download/source.json
```

Every build of `main` writes that file anew, so it always points to the newest package. The package is unsigned; the
app that installs it signs it with your own certificate.

### iPhone / iPad (sideloading)

1. Download `Oretsuba-iOS-unsigned.ipa` from the [latest build](../../releases/latest), or build it yourself with
   `tools/apple/package.sh ios`.
2. Connect the device with a cable, open [Sideloadly](https://sideloadly.io/), choose the device and the IPA, enter
   your Apple ID and press Start. An app signed with a free Apple ID has to be signed again after 7 days, and a device
   takes at most 3 apps signed this way at a time.
3. Before the first start, trust your developer certificate on the device under Settings › General › VPN & Device
   Management.
4. If the system asks for Developer Mode, turn it on under Settings › Privacy & Security › Developer Mode, restart and
   confirm.

Apple describes the limits of a free account under
[About your developer account](https://developer.apple.com/help/account/basics/about-your-developer-account).
To update or to sign again, install over the app with the same Apple ID and bundle ID, which keeps the app and its
saves; do not uninstall it first. The [Sideloadly FAQ](https://sideloadly.io/faq.html) describes installing over an app
and signing again automatically.

### Apple Vision Pro

On the first Vision Pro the project runs as a native visionOS target. To install it:

1. Install Xcode with the visionOS platform on a Mac and sign in with your own Apple Account under Settings › Accounts.
2. Generate and open the project as described under [Building from source](#building-from-source).
3. Pair the Vision Pro with the Mac in Xcode's device management (Device Hub in Xcode 27). Keep the headset unlocked
   and turn on Developer Mode when the system asks.
4. Under Signing & Capabilities choose your own team and leave Automatically manage signing on.
5. Choose the paired Vision Pro as the run destination and press Run. Once the app is installed, bring the game files
   in as described below.

See Apple's pages on
[running an app on a device](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices)
and on [Developer Mode](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device).
`Oretsuba-visionOS-unsigned.ipa` has to be signed separately and cannot be installed as it is. Installing with Xcode is
the way that was checked; whether Sideloadly handles a native visionOS package was not.

### Android

1. Download `Oretsuba-Android.apk` from the [latest build](../../releases/latest) on the device and open it. The
   first time, Android asks whether your browser or file manager may install apps: allow it.
2. To update, install a newer `Oretsuba-Android.apk` over the app. The game files and saves stay; do not uninstall
   the app first.

[docs/ANDROID.md](docs/ANDROID.md) has everything about the Android app.

### Mac

Build `Oretsuba-macOS.zip` with `tools/apple/package.sh macos`, unpack it and drag `Oretsuba.app` to Applications. If
the system blocks the first start, press Open Anyway under System Settings › Privacy & Security.

## Bringing the game files in

* **iPhone / iPad**: connect the device to a Mac with a cable, select it in the Finder's sidebar, open the Files tab
  and drag the `SET_UP` folder onto Oretsuba. Or save the folder to the Files app first and press "Import a folder
  from Files…" in the launcher.
* **Vision Pro**: send the folder to the Vision Pro with AirDrop, save it to Files and import it in the launcher.
* **Mac**: choose the game folder in the launcher. Nothing is copied.
* **Android**: have the `SET_UP` folder on the device (in Downloads, on a memory card, on a USB stick) and press
  "Import a folder…" in the launcher. Or connect the device to a computer with a cable and copy the folder into
  `Android/data/io.github.friedrich_de.oretsuba/files`.

Once the launcher has found the files, the game can be started.

## Controls

**Vision Pro**: the buttons below the game window (Save, Load, Quick save, Quick load, History, Menu). Pressing
History again closes the text log. The text advances when you look at it and pinch.

| Action | Touch | Mouse / keyboard |
|---|---|---|
| Advance / confirm | Tap | Left button, Enter |
| Menu / hide the text window | Tap with two fingers (on Android also Back) | Right button |
| History (text log) | Swipe down | Wheel up |
| Skip | Long press | Hold Ctrl (or Shift, a setting of the game) |

Game controllers work too: A confirms, B opens the menu, the right trigger skips, up and down on the D-pad scroll.

With [mining in the game](docs/MINING.md#mining-in-the-game) turned on, a tap or click on a word of the text shows
the dictionary and the text box no longer advances the text: tap the picture above it, or use Enter, the wheel or a
controller.

## Auto save and continue

The launcher has a switch "Auto save and continue", off by default. With it on, the port saves without a sound every
5 lines and whenever you leave the app, into a slot of its own that the game's menus do not show, and the game starts
at that line: no logo, no title menu, no load screen. Your own saves and quick saves stay as they are, and the title
menu is still reached through Menu › Title in the game. [docs/AUTOSAVE.md](docs/AUTOSAVE.md) has the details.

## Building from source

The checks used Xcode 27.1 and 27.2. If the system points at the Command Line Tools, set this in the terminal first:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
```

```sh
ORE_TEAM=YOUR_TEAM_ID tools/apple/gen_project.sh   # YOUR_TEAM_ID: the ID of your own development team
open app/Oretsuba.xcodeproj
```

Xcode's signing settings show the team ID. To build for the Mac only, run `tools/apple/gen_project.sh` without a team.
The script uses the XcodeGen in PATH; if there is none, it downloads a fixed version into `build/tools`, which git
ignores.

The test host for the command line (macOS, runs without a window and writes screenshots):

```sh
make -C engine
build/host/orehost --data <SET_UP folder> --frames 1200 --shot 600,1200 --out /tmp/ore
```

The same test host builds for Linux, with letters from FreeType and a Japanese font file, and for Windows, with
letters drawn by GDI as the original draws them, so that the port can run next to the original game
(`tools/reference/README.md`):

```sh
make -C engine -f Makefile.linux   # build/host-linux/orehost, needs the FreeType headers
make -C engine -f Makefile.win     # build/host-win/orehost.exe, with MinGW-w64
```

The Android app builds on Linux, macOS or Windows with a JDK and the Android SDK, and needs no Mac
([docs/ANDROID.md](docs/ANDROID.md#building-from-source)):

```sh
cd android && ./gradlew :app:assembleRelease   # app/build/outputs/apk/release/app-release.apk
```

`docs/ENGINE_GUIDE.md` and `docs/re/` describe the engine and hold the notes from the reverse engineering.

## Credits and licences

* The game 俺たちに翼はない © Navel. This project has nothing to do with Navel and is meant only for players who
  bought the game.
* The port is the work of galhack: [galhack/Oretsuba_iOS](https://github.com/galhack/Oretsuba_iOS). Mining, auto
  save and the Android app were added in this fork.
* Libraries: stb_vorbis, stb_image, stb_image_write (public domain / MIT), pl_mpeg (MIT), minimp3 (CC0). The LPK and
  ELG formats were read with the help of GARbro (MIT). Licences and attributions are in
  [ThirdPartyNotices.txt](app/Resources/ThirdPartyNotices.txt), which is also inside the apps.
* Mining in the game uses two works under the GPL-3.0-or-later, which the Apple app carries as files of their own: the
  dictionary popup of [Hachidori](https://github.com/bee-san/hachidori)
  ([dictionary-popup](https://github.com/bee-san/dictionary-popup), in
  `engine/resources/mining/page/vendor/dictionary-popup` with its licence and sources), and
  [Yomitan](https://github.com/yomidevs/yomitan)'s rules for Japanese inflections as the Hoshidicts language pack
  writes them (`engine/resources/mining/transforms.json`, see `SOURCES.md` beside it). Dictionaries are not part of
  the app; you import your own.

## Checks and packages

```sh
make -C engine -j8 all tests
build/host/save_roundtrip /path/to/SET_UP
build/host/quick_save /path/to/SET_UP
build/host/command_input /path/to/SET_UP
build/host/native_coverage /path/to/SET_UP
build/host/image_blend
build/host/mining_test
build/host/dict_test engine
build/host/kit_test engine
build/host/movie_flow /path/to/SET_UP
python3 tools/test_save.py /path/to/SET_UP
python3 tools/test_quick_save.py /path/to/SET_UP
python3 tools/test_history_toggle.py /path/to/SET_UP
python3 tools/test_autosave.py /path/to/SET_UP
xcrun swiftc -module-cache-path build/swift-module-cache \
  app/Sources/App/FolderImport.swift app/Tests/FolderImportTests.swift \
  -o build/folder-import-tests
build/folder-import-tests
tools/apple/package.sh ios       # unsigned IPA, to be signed by a sideloading tool
tools/apple/package.sh visionos  # unsigned native visionOS IPA
tools/apple/package.sh macos     # Mac Catalyst package with a local ad-hoc signature
(cd android && ./gradlew :app:lintRelease :app:assembleRelease)   # the Android package and its lint check
```

The Apple packages land in `build/packages/`, the Android package in `android/app/build/outputs/apk/release/`. None
contains game data. An unsigned IPA cannot be installed by a device tool
as it is; it has to be signed with the user's own account. The Mac package is not notarised.
