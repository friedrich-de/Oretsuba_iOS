# Cards straight into the Anki app

The game's dictionary popup makes Anki cards. Where they go is a choice in the mining settings, **Send cards to**:

| Send cards to | |
| --- | --- |
| AnkiConnect | An Anki with the AnkiConnect add-on, reached over the network: on a computer or a server. |
| AnkiMobile | The Anki app on the same iPhone or iPad. |
| AnkiDroid | The Anki app on the same Android device. |

The choice offers only what the device has: AnkiMobile on iPhone and iPad, AnkiDroid on Android, AnkiConnect
everywhere (a Mac has AnkiConnect alone, and the choice is not shown there). The deck, the note type and the
fields are chosen in the same settings, whichever it is. They are kept once, not for each place: after changing
where cards go, look at the deck and the note type again.

## AnkiMobile (iPhone, iPad)

1. Install [AnkiMobile](https://apps.apple.com/app/ankimobile-flashcards/id373493387) and open it once.
2. In the launcher, under "Mining in the game", press **Set up…** and set **Send cards to** to AnkiMobile.
3. Press **Fetch decks and note types from AnkiMobile**. AnkiMobile comes to the front for a moment and the game
   comes back. iOS then asks whether Oretsuba may paste from AnkiMobile: allow it. That is how AnkiMobile hands
   its lists over. (Settings › Oretsuba › Paste from Other Apps › Allow stops the question for good.)
4. Choose the deck, the note type and what goes into which field. With the
   [Lapis](https://github.com/donkuri/lapis) note type there is nothing to choose: it is taken together with its
   deck, and its picture and sounds have fields of their own.

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
* **After adding a deck or a note type in AnkiMobile, fetch the lists again.** A card of a note type AnkiMobile
  did not name is not sent; the popup says so.

## AnkiDroid (Android)

1. Install [AnkiDroid](https://play.google.com/store/apps/details?id=com.ichi2.anki), open it once and go
   through its first screens, so that it has its decks.
2. In the launcher, under "Mining in the game", press **Set up…** and set **Send cards to** to AnkiDroid. Android
   asks whether Oretsuba may read and write AnkiDroid's cards: allow it.
3. Choose the deck, the note type and what goes into which field. With the
   [Lapis](https://github.com/donkuri/lapis) note type there is nothing to choose: it is taken together with its
   deck.

AnkiDroid is asked directly, so the game stays in front: the + adds the card at once, words AnkiDroid has already
are marked, and a second card of the same word is refused. Pictures and sounds go into AnkiDroid's media folder
and may share a field with text.

* **If the answer to Android's question was no,** the settings say so and their button asks again. After a
  second no Android stops asking; the button then opens the system's settings for Oretsuba, where the permission
  can be given under Permissions, Additional permissions.
* **A deck or a note type added in AnkiDroid** is in the lists when you come back to the settings; **Fetch decks
  and note types again** asks at once.
* **A deck or a note type that was chosen for an AnkiConnect** stays chosen when AnkiDroid has one of the same
  name. Otherwise the settings name what AnkiDroid does not have: choose one of its own.
* It does not matter which of the two apps was installed first (checked on Android 14).

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

* The settings sheet of the Apple app (`app/Sources/App/MiningSettings.swift`, `MiningSettingsView.swift`) asks
  `anki.targets` for the choice, sets `anki.target`, and for AnkiMobile calls `ankimobile.fetch` and waits for
  the event `anki.lists`.
* The settings screen of the Android app (`MiningSettingsActivity.kt`, `res/layout/mining_settings.xml`) does the
  same with `anki.targets` and `anki.target`. For AnkiDroid it asks `ankidroid.status`, has Android put its
  question (`AnkiDroid.ask`, answered in `onRequestPermissionsResult`), and then calls `anki.check`, which for an
  Anki app needs no address. It asks again whenever the screen comes back to the front.

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

# Android emulator: the real AnkiDroid from its release page. The debug build carries two checks: one plays the
# player on the mining settings screen (Send cards to AnkiDroid, Android's question, deck, note type, fields, a
# card through the mining kit), the other adds cards of every kind and reads them back
(cd android && ./gradlew :app:assembleDebug) && tools/android/ankidroid_check.sh
```

The second one takes a device instead of an emulator with `ORE_ADB_SERIAL`. It adds notes and a deck to that
device's AnkiDroid and changes the app's mining settings, so use only one where neither matters; with
`ORE_ANKIDROID_NEW=yes` it also sets up an AnkiDroid that was never opened there. In its own
emulator it sets a new AnkiDroid up as a player does and runs a second time with the apps installed in the other
order, Oretsuba first; that time Android's question is refused twice, and the way through the system's settings
is taken. The pictures it takes of the screen are in `build/ankidroid-check`.

What these checks do not reach is the real AnkiMobile, which costs money and which no Simulator takes: that iOS
asks before the paste and the lists then arrive, that AnkiMobile fetches the picture and the sounds while Oretsuba
is in the back, and that it plays the `.m4a` voice. The first card on a real iPhone shows all three.
