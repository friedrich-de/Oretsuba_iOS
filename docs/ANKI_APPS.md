# Cards straight into the Anki app

The game's dictionary popup makes Anki cards. Where they go is a choice in the mining settings, **Send cards to**:

| Send cards to | |
| --- | --- |
| AnkiConnect | An Anki with the AnkiConnect add-on, reached over the network: on a computer or a server. |
| AnkiMobile | The Anki app on the same iPhone or iPad. |
| AnkiDroid | The Anki app on the same Android device. |

The choice offers only what the device has: AnkiMobile on iPhone and iPad, AnkiDroid on Android, AnkiConnect
everywhere. The deck, the note type and the fields are chosen in the same settings, whichever it is.

## AnkiMobile (iPhone, iPad)

1. Install [AnkiMobile](https://apps.apple.com/app/ankimobile-flashcards/id373493387) and open it once.
2. In the mining settings, set **Send cards to** to AnkiMobile.
3. Press **Fetch decks and note types from AnkiMobile**. AnkiMobile comes to the front for a moment and the game
   comes back. iOS then asks whether Oretsuba may paste from AnkiMobile: allow it. That is how AnkiMobile hands
   its lists over. (Settings › Oretsuba › Paste from Other Apps › Allow stops the question for good.)
4. Choose the deck, the note type and what goes into which field.

From then on the + in the popup hands the card to AnkiMobile: AnkiMobile comes to the front, adds the note, and
the game comes back and says "AnkiMobile added the card."

AnkiMobile can be reached only by links, which is why it works differently from the other two:

* **Every card switches to AnkiMobile and back.** There is no way to add a note to it in the background.
* **A picture and a sound each need a field of their own.** AnkiMobile fetches a file only for a field that holds
  nothing else. With a field that holds the sentence and the picture, the sentence arrives and the picture does
  not; the popup then says what the card went without.
* **No "already in Anki" mark.** AnkiMobile cannot be asked what it holds. When it refuses a word it has already,
  it stays in front with its own message. Go back to the game; the popup then says that AnkiMobile did not
  confirm the card.
* **Cards go to the profile that is open in AnkiMobile.**
* **After adding a deck or a note type in AnkiMobile, fetch the lists again.**

## AnkiDroid (Android)

1. Install [AnkiDroid](https://play.google.com/store/apps/details?id=com.ichi2.anki) and open it once.
2. In the mining settings, set **Send cards to** to AnkiDroid. Android asks once whether Oretsuba may read and
   write AnkiDroid's cards: allow it.
3. Choose the deck, the note type and what goes into which field.

AnkiDroid is asked directly, so the game stays in front: the + adds the card at once, words AnkiDroid has already
are marked, and a second card of the same word is refused unless duplicates are allowed. Pictures and sounds go
into AnkiDroid's media folder and may share a field with text.

If AnkiDroid was installed after Oretsuba, Android does not offer the permission: install Oretsuba again over
itself (the game files and saves stay), or allow it under Settings › Apps › Oretsuba › Permissions.

## For developers

The code is in `engine/src/mining/`:

* `anki_app.h`, `anki_app.cpp`: the part that knows neither the engine nor the system. AnkiMobile's links and
  the lists it answers with, `MediaServer` (a web server on `localhost` that hands a card's picture and sound to
  AnkiMobile, each behind a path nobody can guess), `AnkiMobileHandover` and `AnkiMobile` (one card on its way and
  what became of it), `AppReturns` (the links the app is opened with, and its coming to the front again), and the
  JSON of the requests to AnkiDroid.
* `anki_app_target.h`, `anki_app_target.cpp`: `AnkiMobileTarget` and `AnkiDroidTarget`, which fit the two apps
  to the kit's `CardTarget`, and `add_anki_apps`, which gives them to the kit together with the calls
  `anki.targets`, `ankimobile.fetch` and `ankidroid.status`. What AnkiMobile tells later reaches the popup as the
  events `anki.lists`, `card.added` and `card.unconfirmed`, each with a sentence for the player.
* What a host has to give is `host_anki_app()` (`mining/kit_host.h`). The Apple app's is
  `engine/src/platform/apple/apple_anki.mm`: it opens links, keeps the app running while AnkiMobile fetches the
  files, and reads AnkiMobile's answer from the clipboard. The app's own link scheme is `oretsuba`
  (`app/Resources/Info.plist`); the links it is opened with go to `ore_app_opened_with`. The Android app's is in
  `android/app/src/main/cpp/android_platform.cpp` and goes through `Native.ankiDroid` to `AnkiDroid.kt`, which
  speaks to AnkiDroid's content provider; `AnkiMediaProvider.kt` hands a card's files over. AnkiDroid names each
  file it stores after its type, so a voice in Ogg arrives as `.oga`.

The setting is `anki.target`: `connect`, `ankimobile` or `ankidroid`.

Two tests need neither an Anki app nor game files; a made-up AnkiMobile and a made-up AnkiDroid stand in:

```sh
make -C engine tests && build/host/anki_app_test && build/host/anki_target_test engine
```

Two checks run against the apps themselves, in GitHub's workflow "Anki apps" and on any machine that has the
tools:

```sh
# iPhone Simulator: a stand-in app takes AnkiMobile's links (the real one cannot be installed in a Simulator),
# the debug build sends it the request for the lists and three cards, and a UI test allows the paste
tools/apple/ankimobile-standin/run.sh

# Android emulator: the real AnkiDroid from its release page; the debug build's own check adds cards and
# reads them back
(cd android && ./gradlew :app:assembleDebug) && tools/android/ankidroid_check.sh
```

The second one takes a device instead of an emulator with `ORE_ADB_SERIAL`. It adds notes to that device's
AnkiDroid, so use only one whose collection does not matter.
