# Mining

This port has two ways of making Anki cards from what you read. They do not depend on each other and can both be
on.

* **In the game** (iPhone, iPad, Vision Pro, Mac and Android). A tap on a word of the text shows a dictionary
  over the game, and a button in it makes the card, with the sentence, its voice, a picture
  of the scene and the word's sound. You set it up in the launcher: where your cards go, the dictionaries, and
  where the sound of a word comes from.
* **Through a mining server.** The game sends every line you read to a
  [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server), whose page shows the lines as they come,
  looks words up and makes the cards, on the same phone or on any other device. It is on once a file named
  `mining.txt` lies in the game folder.

## Mining in the game

### Setting it up

In the launcher, under "Mining in the game", press **Set up…**.

1. **Anki.** On an iPhone, an iPad or an Android device, **Send cards to** chooses where cards go: to an
   AnkiConnect, or straight into the Anki app on the same device (AnkiMobile, AnkiDroid), which
   [ANKI_APPS.md](ANKI_APPS.md) describes. For an AnkiConnect,
   enter its address and, if it asks for one, its key, then press Connect. That is the
   AnkiConnect add-on of Anki on your computer (`http://192.168.1.20:8765`; the add-on listens to its own
   computer only until its setting `webBindAddress` is `0.0.0.0`), or a server of your own that speaks
   AnkiConnect. The game is a client of it and nothing of Anki is in the app.
   * If the collection has the [Lapis](https://github.com/donkuri/lapis) note type, it is chosen together with
     the deck its newest card is in, and its fields are filled the way Lapis means them to be. There is nothing
     more to set.
   * Otherwise choose a deck and a note type, and under "What goes into the fields" what each field gets.
   * An address in the internet has to begin with `https://`. Plain `http://` is for a server in your own
     network, given by its IP address, a name without dots or one ending in `.local`; iOS then asks once for
     access to the local network.
   * The Android app sends its cards to an AnkiConnect.
2. **Dictionaries.** "Add Jitendex" downloads the newest release of [Jitendex](https://jitendex.org)
   (Japanese to English) from its makers and imports it; "Add JMnedict" does the same for the names of people
   and places. Any other dictionary in [Yomitan](https://yomitan.wiki/dictionaries/)'s format you import as the
   zip file it comes in: from the Files app (on Android: **Import a file…**, which opens the system's file
   picker), or by its address (`https://`). Dictionaries of words, of frequencies and of pitch accents all
   count. A word's meanings are shown in the order of the list, and a card gets the meaning of the first; Edit
   changes the order, and a swipe removes a dictionary (on Android each dictionary has the buttons Up, Down and
   Remove). Importing a large dictionary takes a few seconds and can be stopped. If a file or an address cannot
   be taken, the reason stands under the buttons. Importing a dictionary that is already there puts the new
   import in its place. If a dictionary's label pictures (漢, 文 and the like) fail to show, it was imported by
   a build that kept them under the wrong names (see `zip_reader` below): import it once more.
3. **Word audio** (optional). The address of an audio source as Yomitan takes a custom one, with `{term}` and
   `{reading}` where the word and its reading go. It may answer with a sound or with Yomitan's list of sounds.
   Check asks it for 食べる and keeps an address that answers.
4. Turn on **Look words up in the game**.

The launcher then says how things stand: "On: 2 dictionaries, cards go to the deck Mining." Looking words up works
without Anki; the button for a card then says what is missing.

### In the game

* **A tap on a word** of the line shows the dictionary for the longest word that starts at the character you
  tapped, with inflections undone (気付いた is found as 気付く). The word is marked in the line and the popup
  stands above or below it. With a mouse or trackpad a click does the same.
* **+** makes the card. It turns into a mark when Anki has the word already. The loudspeaker plays the word; a long
  press on it lists the sounds the source has, and the one you choose is the one a card gets.
* A tap on another character of the line looks that one up. Inside a meaning, a tap on a word looks that word
  up, and the popup's arrow goes back.
* **×**, a tap beside the popup, Escape or the controller's B closes it, and on Android Back does too.
* **While the popup is open the game stands still**: the script and the animations wait, no click, key, wheel
  or controller button reaches the game, and what was sounding goes on sounding.

Because the text now answers to taps, **the text box no longer reads on**. Tap the picture above it, or use
Enter, the wheel or a controller. The game's own buttons in the text box (SAVE, LOG, SKIP ...) work as ever, and
while a line is still appearing a tap on it completes it as before. In the history and in the game's menus
nothing is looked up.

### What a card gets

| Field of Lapis | |
| --- | --- |
| Expression, ExpressionReading, ExpressionFurigana | The word in its dictionary form, its reading, and both together (食[た]べる). |
| MainDefinition | Its meanings in your first dictionary. |
| SelectionText | What you had selected in the popup when you pressed +, if anything. |
| Sentence | The line, with the word in bold. |
| SentenceAudio | The voice of the line, as AAC in an `.m4a` file, which every Anki plays. Empty for a line without a voice. |
| Picture | The screen as it was when you first tapped the line, with the text box, as a JPEG. |
| ExpressionAudio | The sound of the word from the audio source. |
| Frequency, FreqSort, PitchPosition, PitchCategories | From your frequency and pitch dictionaries. |
| MiscInfo | The game's title. |

Cards get the tags you set (at first `vn-miner`). A word Anki has already in that deck is refused as a duplicate,
and the popup says so.

### Where it is kept

The settings and the dictionaries lie in the app's own folder (`Application Support/Mining`); the dictionaries
stay out of device backups, since they can be imported again. The AnkiConnect's key and the audio source's
address, which often holds a key, are kept in the device's Keychain and are not written into the settings. A
Mac build that is signed without a team has no Keychain of its own; there the two lie in a file beside the
settings that only your user can read.

The Android app keeps the settings and the dictionaries in its inner folder (`files/mining`), which no other app
and no file manager reads; it goes with the app when the app is uninstalled, unless you tell Android to keep the
app's data. The two secrets lie beside the app's own settings, sealed with a key that the device's Keystore
holds and does not give out.

## Sending lines to a mining server

The game can send every line you read to a [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server):
the text, who speaks, the voice and a screenshot. The server's page shows the lines as they come, looks words up
and makes Anki cards of them, on the same phone or on any other device.

Nothing is sent unless a file named `mining.txt` lies in the game folder.

### Setting it up

1. Open your mining server's page, go to its settings, and under "The game" press **Download mining.txt**.
2. Put the file into the game folder, next to `SCRIPT.LPK`. One of the two folders above it works as well.
   * **iPhone / iPad**: in the Files app, move `mining.txt` to "On My iPhone" › Oretsuba (or into the
     `SET_UP` folder inside it). With a cable, Finder's "Files" tab of the device does the same.
   * **Mac**: put it into the game directory you chose in the launcher.
   * **Android**: press "Import mining.txt…" in the launcher and choose the downloaded file
     ([ANDROID.md](ANDROID.md#mining)).
3. Open the app. The launcher says where the lines go and whether the server takes the key:
   "Mining: lines go to vn.example.org (VN Mining Server 0.1.0)."

Then read. Each line appears on the server's page a moment after its text stands complete on the screen.

### mining.txt

```text
server = https://vn.example.org
key = the-api-key
```

| Line | |
| --- | --- |
| `server` | The address of the mining server. Needed. |
| `key` | The server's API key. Needed. |
| `game` | The title the server shows and puts on cards. Without it: 俺たちに翼はない. |
| `voice` | `no` sends lines without their voice. |
| `screenshot` | `no` sends lines without a screenshot. |

Lines that begin with `#` are comments. The file is UTF-8. It holds the key to your server, so keep it to yourself;
the repository's `.gitignore` names it.

An address in the internet has to begin with `https://`. Plain `http://` is for a server in your own network,
given by its IP address, a name without dots or one ending in `.local` (`http://192.168.1.20:8733`); iOS then
asks once for access to the local network.

### What is sent, and when

* A line is sent once its whole text stands on the screen and waits for you, 0.3 seconds after it began at
  the earliest. Lines you click through faster than that, and lines the skip function runs over, are not sent.
  The question before a choice does not wait for a click; it is sent when it has ended. The choices themselves
  are not sent.
* Switching to another app in the middle of a line sends that line at once, since the next thing you do is most
  likely to look up a word of it. The app asks iOS for the time to finish sending before it is put to sleep.
  The text is the whole line; the screenshot shows the screen as it was, so a line you leave while it is
  still appearing has only its first words in the picture.
* The text goes without the engine's tags and line breaks. The voice is the game's own Ogg file, as it is. The
  screenshot is the 800x600 screen at that moment, with the text box, as a JPEG of about 100 KB.
* The history screen sends nothing. A line shown again after a menu or a load is not sent twice, and neither is
  the line the game starts at with [auto save and continue](AUTOSAVE.md).

Sending happens beside the game, which never waits for the network. Without a connection up to 64 lines are kept
and sent in their order once the server answers again; beyond that the oldest go. If the server does not take the
key, the game stops sending until it is started again, because repeated wrong keys get an address locked out
for a while.

### When no lines arrive

The launcher's line under the game files says what is wrong:

| The launcher says | |
| --- | --- |
| Sending lines to a mining server is off | No `mining.txt` was found in the game folder or the two folders above it. |
| mining.txt has no line "server = ..." (or "key = ...") | The file is there but incomplete. |
| ... could not be reached | The address is wrong, the server is down, or the device has no connection to it. |
| ... does not take the key in mining.txt | The key was changed on the server: download the file again. |

Press Check again in the launcher to ask again.

## For developers

### The sender

The code that sends lines to a server is in `engine/src/mining/`:

* `mining.h`, `mining.cpp`: the part without the engine. The settings file, the text of a record, `LineGate`
  (which lines count as read), the multipart request and `MiningQueue` (one thread that sends in order, tries
  again and drops what the server will never take).
* `mining_tap.h`, `mining_tap.cpp`: `MiningTap` listens to the engine. `MessageWindow::show` (native 0x2fa)
  tells it of every line the ADV loop begins, `Engine::frame` lets it look once per frame, `Engine::set_paused`
  tells it that the player leaves.
* `engine/src/platform/apple/apple_http.mm`: the requests, through `NSURLSession`. Another host gives its own
  through `Platform::http_sender`; the Android app's go through `HttpURLConnection` (`android/.../Http.kt`).

`engine/tests/mining_test.cpp` tests the first part and needs neither game files nor an Apple system:

```sh
make -C engine tests && build/host/mining_test

# anywhere else
cd engine && c++ -std=c++17 -Isrc -Ithird_party tests/mining_test.cpp src/mining/mining.cpp src/util/log.cpp -lpthread -o /tmp/mining_test && /tmp/mining_test

# and against a running VN Mining Server at a plain http:// address: three lines are sent and read back
/tmp/mining_test --server http://127.0.0.1:8733 --key THE-KEY --voice some.ogg
```

The test host mines too: with a `mining.txt` in the folder given to `orehost --data`, every line its scripted
input reads goes to the server, and the run ends by saying how many arrived. `--debug` prints each line the
ADV loop begins and each line that is sent, with the game's clock, and the input command `pause N` puts the
app into the background for N frames. `engine/tests/inputs/mining_read.input` reads the opening of the
Japanese original that way:

```sh
build/host/orehost --data SET_UP --save /tmp/ore-save --frames 31970 --debug \
    --input engine/tests/inputs/mining_read.input 2>&1 | grep mining:
```

Of the 634 lines it runs through, the 61 that are read at a normal pace are sent, 28 of them with a voice. The
lines clicked away after 0.2 seconds and the lines the skip key runs over are not. Line 11 of the first script
goes out 0.5 seconds after it began, when the app is paused.

### The mining kit

Mining in the game is the "mining kit": portable C++ in `engine/src/dict` and `engine/src/mining`, a web page
for the popup in `engine/resources/mining/page`, and a thin layer of each system's own code that carries calls
between them. The Swift side (`app/Sources/App/Mining*.swift`) holds no dictionary and no Anki logic, so another
system needs only that layer.

The folder `engine/resources/mining` (the page and the inflection rules) is under the GNU General Public License,
version 3 or later, as the works it is built on are; its `SOURCES.md` and `LICENSE` say more. An app packs the folder
whole, so that the licence's text goes with the page.

* `dict/`: dictionaries. `yomitan_import` reads a Yomitan zip into an SQLite file, `dictionary_db` reads it,
  `deinflector` undoes inflections with Yomitan's rules (`engine/resources/mining/transforms.json`), `lookup`
  finds the words that start a text, and `library` keeps the installed dictionaries, their order and one
  import at a time. `zip_reader` takes a zip as Yomitan's reader does, because real dictionaries count on it.
  A file's name that the packer wrote in its own encoding (Shift-JIS on a Japanese Windows) is read from the
  "Unicode Path" field beside it, or a picture named 漢-default.svg would be kept under bytes no meaning asks
  for. And a checksum that does not fit refuses nothing as long as the file unpacks to its end and to the size
  stated: some dictionaries are packed with wrong ones.
* `mining/anki_connect`, `anki_fields`, `card_note`, `word_audio`: the AnkiConnect client behind the `CardTarget`
  seam, the Lapis defaults and the guesses for other note types, the note of a card, and the audio source.
* `mining/anki_app`, `anki_app_target`: the Anki apps on the device as two more `CardTarget`s, AnkiMobile and
  AnkiDroid ([ANKI_APPS.md](ANKI_APPS.md)).
* `mining/mining_kit`: `MiningKit`, one per app. It keeps the settings (`settings.json`) and answers calls by
  the name of a method with JSON in and out. `mining/kit_tap`: `KitTap` ties it to the engine: which character a
  tap hits, the line's voice and picture, and the reading hold.
* `mining/kit_host.h`: the two things a system gives the kit, an HTTP fetcher and an encoder for the voice
  (`platform/apple/apple_kit_host.mm`: `NSURLSession` and AAC through AudioToolbox; the Android app's
  `android_kit_host.cpp`: `HttpURLConnection` and AAC through `MediaCodec`; the Linux test host has sockets and
  no encoder, so its cards carry the game's Ogg file).
* `dict/kit_config.h`: `ORE_MINING_KIT` is 0 on Windows, where the test host stands next to the original game
  and needs none of this; the functions below are then empty.

**The C interface** is `engine/src/api/ore_kit.h`: `ore_kit_open` and `ore_kit_close`, `ore_kit_call(kit,
method, params_json)` for every method, `ore_kit_data` for the files the page shows, `ore_kit_set_events`, and
with the engine `ore_set_kit`, `ore_text_tap` (what a tap is: the game's, a word with the JSON for the page, or
nothing) and `ore_set_reading_hold`. A call may wait for the network and belongs on a thread of its own.

**Methods.** Every answer is `{"ok":true,"result":…}` or `{"ok":false,"error":"a sentence for the
player","kind":"…"}` with a kind of `address`, `settings`, `anki`, `unreachable`, `duplicate`, `dictionary`,
`busy`, `stale`, `audio` or `internal`.

| Method | |
| --- | --- |
| `settings.get`, `settings.set {direct?, anki?, popup?}` | The settings: `direct` (the text takes taps), `anki {target, address, deck, noteType, fields:[{field, role}], tags}`, `popup {textSize, hideTags, compact}`, and whether the two secrets are set. |
| `secrets.set {ankiKey?, audioAddress?}` | The two secrets, which the kit does not write down: the host keeps them and tells them at each start. |
| `anki.check {address?, key?, adopt?}` | Asks the AnkiConnect, or the Anki app when `anki.target` names one, for its decks and note types and whether Lapis is there. With `adopt` the address and key become the kit's, and Lapis is chosen where nothing was. |
| `anki.targets`, `ankimobile.fetch`, `ankidroid.status` | The places for cards this device has, `[{name, label, detail}]`; the request to AnkiMobile for its decks and note types, which come later with the event `anki.lists {ok, message}`; and whether AnkiDroid is there and lets the app in, `{installed, allowed}`. |
| `anki.fields {noteType?}`, `anki.choose {deck?, noteType?}`, `anki.setRole {field, role}`, `anki.roleChoices` | The fields of a note type and what goes into them. |
| `audio.check {address?}`, `audio.sources {term, reading}` | The audio source: whether it answers, and the names of the sounds it has for a word. |
| `dict.list`, `dict.import {path, name?, remove?}`, `dict.download {url}`, `dict.cancel`, `dict.arrange {order, disabled}`, `dict.enable {id, enabled}`, `dict.remove {id}` | The dictionaries. An import runs beside the call; `dict.list` says how far it is (`import {state, source, name, progress, title, error}`). It also names the dictionaries a settings page can offer with one tap, `suggested [{name, text, url, installed}]`: the `url` goes to `dict.download`, and `installed` says the dictionary is already there. |
| `page.init` | What the popup needs to draw itself: its look, the dictionaries and their styles, whether a card can be made and why not. |
| `lookup {text}` | The words that start `text`, longest first, each with its entries in every dictionary. |
| `card.known {words}`, `card.add {word, audioChoice?}` | Whether Anki has the words, and a card of one. `word` is `{expression, reading, furigana, meaning, frequency, pitch, pitchCategories, selection, at:{start, length}}`, with `at` in UTF-16 units of the sentence. |
| `line.get`, `line.set {sentence, speaker}` | The line the next card is made of. The engine sets it at a tap. |

Files, through `ore_kit_data`: `dict.media {dictionary, path}`, `audio.word {term, reading, source?}`,
`line.picture`, `line.voice`.

**The popup's page** is shown in a web view over the game's picture and is the same on every system. Its host
gives it three things:

* A way to call. The page looks for `window.oreHost = { call(method, paramsJson) }`, which returns the answer's
  JSON as a string or a promise of one (the way for a web view whose bridge is an injected object); then for
  WKWebView's message handler `oreKit`, to which it posts `{method, params}` with `params` as JSON text and
  takes the answer as the reply; and without either it sends `POST kit/call/<method>` to where it was loaded
  from. Methods that begin with `popup.` are the host's own: `popup.ready` (the page can show words),
  `popup.found {length}` (how many characters the found word takes) and `popup.close` (hide the page and end the
  reading hold).
* The kit's files, fetched beside the page as `kit/data/<method>?p=<the parameters as JSON>`.
* The tap: `window.oreMining.show(what)` with the JSON of `ore_text_tap` (`{line, text, index, rects, sentence,
  speaker, box, screen}`, places in the game's 800x600 pixels) and `picture: [x, y, width, height]` added, the
  place of the game's picture on the page in CSS pixels. `window.oreMining.hide()` closes the popup from outside,
  and `window.oreMining.event(name, payload)` passes on what the kit tells without being asked.

In the app that is `MiningPopup.swift`: the page comes from the scheme `ore-mining://page/`, whose handler serves
the bundle's `mining/page` folder and `kit/data/`. `GameViewController` asks `ore_text_tap` before a tap or a
primary click becomes a click of the game, holds the game while the popup is open and ends the hold when the
page calls `popup.close`.

In the Android app it is `MiningPopup.kt` and `GameActivity`, which do the same with Android's web view: the
page comes from an address no server has (`https://mining.oretsuba.invalid/`), and every request to it is
answered from the package's `mining/page` folder or by the kit. A script served with the page sets
`window.oreHost`; its calls cross to Kotlin with a number each and come back as the answer to that number.
[ANDROID.md](ANDROID.md#for-developers) names the files.

**Tests.** None of them needs an Apple system.

```sh
make -C engine tests
build/host/dict_test engine      # dictionaries, the import, Yomitan's inflection tests
build/host/kit_test engine       # the kit against a made-up AnkiConnect and audio source
node --test engine/tests/page/*.test.mjs   # the page's own arithmetic

# the popup's page in a browser (Playwright's Chromium), over a made-up line or, with SET_UP, over the game
node tools/test_mining_page.mjs [SET_UP]
# the engine and the kit with the game: a tap, the line's picture and voice, the card, the hold
node tools/test_mining_kit.mjs SET_UP

# a dictionary of your own against the importer, outside the app: what the import says, what it counted,
# and for the words after "--" their pitch accents and whether every picture of their meanings is there
build/host/dict_probe FOLDER engine/resources/mining/transforms.json DICTIONARY.zip -- 昨日 丈夫
```

`dict_probe` is the tool for a dictionary that does not import or shows no pictures; `engine/tests/dict_probe.cpp`
has the one line that builds it without the engine, from the dictionary sources, SQLite and zlib.

The last two drive the test host, which opens the kit with `--kit FOLDER` and with `--control` takes commands
on its standard input (`run N`, `tap X Y`, `call METHOD JSON`, `data METHOD JSON`, `hold 0|1`, `state` ...; the
head of `engine/host/main.mm` lists them) and answers each with a line of JSON. `tools/mining_page/` holds the
made-up AnkiConnect, audio source and dictionary these tests use, and the stage that stands in for the app.
`node tools/mining_page/neighbours.mjs` runs the three at fixed addresses, for trying the app in the Simulator
by hand, or in an Android emulator, which reaches them at `10.0.2.2`. `ORE_NEIGHBOUR_PORT` moves them away from
8765, where a real Anki's AnkiConnect listens, and `ORE_NEIGHBOUR_MEDIA` names a folder that gets the pictures
and sounds of the cards they are given.
