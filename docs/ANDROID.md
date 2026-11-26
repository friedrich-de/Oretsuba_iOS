# Android

The Android app is the same engine as on Apple devices behind a launcher and a game screen written in Kotlin. It
needs Android 8.0 or later on a 64-bit device. Like every build of this project it contains nothing of the game.

## Installing

1. Download `Oretsuba-Android.apk` from the [latest build](../../../releases/latest) on the phone or tablet.
2. Open the file. The first time, Android asks whether your browser or file manager may install apps: allow it.
   Play Protect may offer to scan the app, because it does not come from the Play Store.
3. Open Oretsuba and bring the game files in, as described below.

**Updating.** Install a newer `Oretsuba-Android.apk` over the app. The game files, the saves and `mining.txt` stay.
Do not uninstall the app first: the game files and the saves are in the app's own folder, and Android removes that
folder with the app (from Android 10 on the system asks whether to keep the data).

Every published build is signed with the same key, which is why one build installs over another. A build you make
yourself is signed with another key and does not install over a published one, see
[The signing key](#the-signing-key).

## Bringing the game files in

The app keeps everything in one folder of the device's storage:

```text
Android/data/io.github.friedrich_de.oretsuba/files/
```

It needs the `SET_UP` folder of the game's disc there (the folder with `SCRIPT.LPK`). The launcher says whether it
found the files, and "Check again" looks once more.

* **With a cable.** Connect the device to a computer, choose "File transfer" on the device, and copy the `SET_UP`
  folder into the folder above. On a Mac this needs a tool for Android file transfer. Some devices do not show
  `Android/data` to a computer: use the other way there.
* **On the device.** Have the folder on the device first (in Downloads, on a memory card, on a USB stick), press
  **Import a folder…** in the launcher and choose the folder. The app makes its own copy and shows how far it is, so
  the original can be deleted afterwards. Keep the app open while it copies. If the copy is interrupted, nothing
  half-copied stays behind, and a game folder that was already there is only replaced once the new one is complete.

## Mining

[MINING.md](MINING.md) describes the two ways of mining. Both are in the Android app.

### In the game

In the launcher, under "Mining in the game", press **Set up…**. The screen asks for what
[MINING.md](MINING.md#setting-it-up) describes: the address of your AnkiConnect, the dictionaries, the word audio,
and the switch **Look words up in the game**. From then on a tap on a word of the text shows the dictionary, and
the text box no longer reads on: tap the picture above it.

* **Send cards to** chooses between an AnkiConnect and AnkiDroid on the same device. With AnkiDroid, Android asks
  once whether Oretsuba may read and write AnkiDroid's cards; after that the decks and note types are AnkiDroid's
  and no address is needed. [ANKI_APPS.md](ANKI_APPS.md#ankidroid-android) has the steps.
* **Dictionaries** come in with **Import a file…**, which opens the system's file picker (have the zip file on the
  device first), or with **Download from an address…**. The app keeps its own copy inside the app, not in the
  folder above.
* **The dictionary is shown by Android System WebView**, the part of Android that shows web pages inside apps and
  that the Play Store keeps up to date. It has to be version 88 or newer. Where it is older, the launcher says so
  and the game plays as it does without mining; update "Android System WebView" in the Play Store.
* **Back** closes the dictionary. While it is open the game waits.
* The AnkiConnect's key and the address of the audio source are kept on the device only, sealed with a key that
  the device's Keystore holds.
* Addresses follow the rule of MINING.md, and the app holds to it: `https://` for anything in the internet, plain
  `http://` only for a server in your own network (an IP address, a name without dots, or one ending in `.local`).

### Through a mining server

`mining.txt` gets into the game folder in one of two ways:

* Download `mining.txt` from your mining server's page on the device, press **Import mining.txt…** in the launcher
  and choose the file. **Remove mining.txt** turns mining off again.
* Or copy the file with a cable into the folder above, next to the `SET_UP` folder or into it.

The launcher then says where the lines go and whether the server takes the key. The rule for addresses is the one of
MINING.md: `https://` for a server in the internet, plain `http://` only for a server in your own network. The app
does not send to an address that breaks it, and the launcher says so.

## Controls

The game plays in landscape. The picture keeps its 4:3 shape, with black bars at the sides, and the system's bars
are hidden: swipe in from the edge of the screen to see them.

| Action | Touch | Mouse / keyboard |
|---|---|---|
| Advance / confirm | Tap | Left button, Enter |
| Look a word up, with [mining in the game](#in-the-game) on | Tap the word | Click the word |
| Menu / hide the text window | Tap with two fingers, or Back (the button or the gesture) | Right button |
| History (text log) | Swipe down | Wheel up |
| Scroll the other way | Swipe up | Wheel down |
| Skip | Hold a finger down, for as long as it should skip | Hold Ctrl (or Shift, a setting of the game) |
| End a film early | Back, or tap with two fingers | Escape |

A mouse and a keyboard work as on the PC. Game controllers work too: A confirms, B opens the menu (and ends a film),
the right trigger skips, up and down on the D-pad scroll.

On the PC only Escape ends one of the game's films early. A phone has no such key, so while a film plays Back, the
tap with two fingers and a controller's B send Escape in place of the right button.

Back does not close the game, because the original uses the right button for its menu. Leave the game through its
own menu, which leads back to the launcher, or with the Home gesture.

## Auto save and continue

The launcher has the switch "Auto save and continue", off by default. It works as on the other devices:
[AUTOSAVE.md](AUTOSAVE.md). Leaving the app (Home, another app, the screen lock) saves at once.

## Saves

The saves are in `Saves/jpn/` inside the app's folder. They stay over updates. To keep a copy, copy that folder with
a cable.

## Letters

The game's letters are drawn by Android with the device's own Japanese fonts, as the Apple app draws them with the
fonts of iOS. A device normally has a Japanese gothic font; the game's mincho setting uses the system's serif font
where it has Japanese letters. The letters therefore look a little different from Windows, and may differ between
makers of devices.

## Building from source

You need a JDK (17 or newer) and the Android SDK with its licences accepted. Gradle fetches what else it needs: the
SDK platform 36, the NDK 27.3.13750724 and CMake 3.31.6. No Mac is needed.

```sh
cd android
./gradlew :app:assembleRelease   # app/build/outputs/apk/release/app-release.apk
./gradlew :app:lintRelease       # the lint check, warnings count as errors
adb install -r app/build/outputs/apk/release/app-release.apk
```

The package holds the engine for `arm64-v8a` (phones and tablets) and `x86_64` (emulators, Chromebooks). The app uses
no library besides Android itself and SQLite, which the dictionaries of mining are kept in. Android has no SQLite for
native code, so the build fetches SQLite's source from sqlite.org the first time, in the one file its makers publish
(version 3.54.1), checks it against its hash and compiles it in. For a build without the network, put `sqlite3.c` and
`sqlite3.h` of that version into `engine/third_party/sqlite`.

`ORE_BUILD_NUMBER` in the environment sets the build number (`versionCode`), which Android wants to grow from one
update to the next, and the last part of the version (`versionName`, 0.1.N). The build on GitHub passes the number of
its run, the same for the iPhone package; a build that was given none is 0.1.0.

### The signing key

Android installs an update only over a package that was signed with the same key.

* Without any setting, the release build is signed with the debug key of the machine that builds it. Such a package
  installs and runs, but not over a published build, and not over a build from another machine.
* The builds GitHub publishes are signed with one fixed key. The workflow takes it from two secrets of the
  repository, `ANDROID_KEYSTORE_BASE64` (the keystore file, base64) and `ANDROID_KEYSTORE_PASSWORD`, and hands it to
  Gradle through `ORE_KEYSTORE`, `ORE_KEYSTORE_PASSWORD`, `ORE_KEY_ALIAS` and `ORE_KEY_PASSWORD`. The key itself is
  not in the repository. Where the secrets are not to be had, as in a pull request from a fork, the package is built
  and checked but not published. The certificate of the published builds has the SHA-256 fingerprint
  `FB:7B:16:8A:C9:5B:14:5C:25:3B:AF:B2:4A:05:E9:33:4D:5E:A3:ED:E8:30:68:5E:4C:46:D6:AC:5D:AB:BF:62`
  (`apksigner verify --print-certs Oretsuba-Android.apk` shows it).

A fork that publishes its own builds makes its own key once and keeps it safe, because a lost key means that users
have to uninstall before the next update:

```sh
keytool -genkeypair -keystore release.jks -storetype PKCS12 -alias oretsuba \
  -keyalg RSA -keysize 4096 -validity 36500 -dname "CN=Oretsuba"
base64 -w0 release.jks | gh secret set ANDROID_KEYSTORE_BASE64
gh secret set ANDROID_KEYSTORE_PASSWORD
```

The same four variables sign a build on your own machine:

```sh
ORE_KEYSTORE=/path/to/release.jks ORE_KEYSTORE_PASSWORD=... ORE_KEY_ALIAS=oretsuba ORE_KEY_PASSWORD=... \
  ./gradlew :app:assembleRelease
```

## Checks

Without game files, as the workflow runs them on Linux:

```sh
python3 tools/android/make_cp932.py build/cp932_table.inc   # the Shift-JIS table, written anew
cmp build/cp932_table.inc android/app/src/main/cpp/cp932_table.inc
g++ -std=c++17 -Wall -Wextra -Iandroid/app/src/main/cpp android/tests/cp932_test.cpp \
  android/app/src/main/cpp/cp932.cpp -o build/cp932_test
build/cp932_test    # every two-byte code against the system's iconv, both ways
(cd android && ./gradlew :app:lintRelease :app:assembleRelease)
```

What was checked with the game, on which devices, is in [VALIDATION.md](VALIDATION.md).

## For developers

The engine under `engine/src` is compiled as it is; nothing in it knows Android. `engine/src/api/ore_api.mm`, the C
interface the Apple app uses, is compiled as plain C++ here and takes `AndroidPlatform` in place of `ApplePlatform`.

`android/app/src/main/cpp/`:

* `android_platform.*`: the engine's `Platform`. Shift-JIS through `cp932.*`, letters and HTTP through Kotlin.
* `cp932.*`, `cp932_table.inc`: Shift-JIS (code page 932) to and from Unicode, as Windows converts it. Android's C
  library has no such conversion. `tools/android/make_cp932.py` writes the table.
* `audio_out.*`: sound through AAudio. Its callback asks the engine's mixer for float stereo at 44,100 Hz. When the
  output device changes (headphones pulled), the stream is opened anew.
* `jni_main.cpp`: the functions Kotlin calls, registered by name at load. A session holds the engine, the sound and
  the window. The picture, 800x600, is copied into the window's buffer of that size and the system scales it; it is
  copied only when the engine drew a new one.
* `jni_bridge.*`: calls from the engine into Kotlin, from any thread. Texts cross as UTF-8 bytes.
* `android_kit_host.cpp`: what the mining kit asks of a system ([MINING.md](MINING.md#the-mining-kit)): its HTTP
  requests and the voice of a line as AAC, both done in Kotlin, and the way to AnkiDroid.

`android/app/src/main/kotlin/`:

* `LauncherActivity`: the launcher. `GamePaths` knows the folders, `FolderImport` copies a chosen folder. "Licences"
  shows `app/Resources/ThirdPartyNotices.txt`, which the build puts into the package.
* `GameActivity`: the game screen. The engine runs on a thread of its own with a large stack, one step per screen
  refresh, and every call into it goes to that thread. Touch, mouse, keyboard and controller become the mouse and
  keys the game knows from Windows.
* `Glyphs`: one letter as a coverage bitmap, drawn by `android.graphics.Paint`.
* `Http`: the mining sender's requests, through `HttpURLConnection`.
* `MiningKit`: the mining kit for the app, one for its whole life, and `Secrets`, which seals the two secrets with a
  key of the device's Keystore. `KitHttp` does the kit's requests (a dictionary is downloaded into a file),
  `VoiceEncoder` turns a line's voice into AAC in an `.m4a` file with `MediaCodec`.
* `MiningSettingsActivity`: the mining settings. It holds nothing itself: it asks the kit and shows what it answers.
  For AnkiDroid it also has Android ask for AnkiDroid's permission (`AnkiDroid.kt`, which speaks to AnkiDroid's
  content provider; `AnkiMediaProvider.kt` hands a card's files over).
* `MiningPopup`: the dictionary over the game, a web view that shows the kit's page from the package
  (`assets/mining`, which the build copies from `engine/resources/mining`). `GameActivity` asks the engine what a tap
  on the picture hit before the tap becomes a click of the game.
* `Native`: the list of the native functions.

`tools/android/make_icons.py` makes the launcher icon's two layers from the project's icon, `icon-1024.png`.
