# Validation record

What was checked, on what, and how far it reaches. The engine follows `ORE_TUBA.EXE` 1.01. Every check with the game
uses files the player supplies; the repository and the packages contain none of them.

## This fork: the Japanese original, 2026-11-25

Run on a Mac with macOS 26.6 and Xcode 27.2, with the `SET_UP` folder of the game's disc.

**The engine's checks** all pass (the commands are in the [README](../README.md#checks-and-packages), what each one
covers is under [Automatic checks](#automatic-checks)):

- `save_roundtrip`, `quick_save`, `command_input`, `image_blend`.
- `native_coverage`: 412 executable scripts refer to 341 different natives, and each has a registered handler.
- `movie_flow`: both opening films play to their own end and return to the script, with 3744 and 5308 rendered frames.
- `tools/test_save.py` (30300 frames), `tools/test_quick_save.py` (27000 frames) and `tools/test_history_toggle.py`
  (26000 frames): from the title into the story, then saving and loading, quick save and quick load, and opening and
  closing the history, each ending back in the text.
- `elg_all`: all 2851 ELG pictures decode.
- `mining_test`: 106 checks. `FolderImportTests`: 4 scenarios.

**Reading with mining on.** `engine/tests/inputs/mining_read.input` plays 31970 frames from the title into the second
script, next to a VN Mining Server 0.1.0. 634 lines run through the message window and the 61 that were read at
reading speed arrive at the server in order, each with its screenshot, and the 28 voiced ones with a voice file that
is byte for byte the game's own. [MINING.md](MINING.md) describes the run.

**The build before and after the Chinese patch support was removed.** The test host played the same two inputs with
both builds and wrote a picture every 100 frames. All 321 pictures of the reading run are the same files. Of the 311
pictures of the save flow 290 are the same files, and the other 21 differ only in the minute of the clock that the
save menu shows for the save.

**The app in the iPhone Simulator** (iPhone 17 Pro, iOS 27.2). The launcher finds the imported game folder and says
where the mined lines go, the game starts, the opening plays, and the first lines arrive at a VN Mining Server on the
same Mac with voice and screenshot. The Release build for iOS, the unsigned IPA, compiles.

**Not checked by this fork:** a real iPhone, iPad or Vision Pro, the visionOS and Mac builds of the current code, and
the story beyond its second script.

## Auto save and continue, 2026-11-25

[AUTOSAVE.md](AUTOSAVE.md) describes the feature. Run with the `SET_UP` folder of the game's disc, with the Linux
build of the test host (without `VOICE.LPK`) and on a Mac with macOS 26.6 and Xcode 27.2.

- `tools/test_autosave.py`, on both: an unbroken run of the story saves at every fifth line; a run ended in the middle
  of a line saves that line and writes no file but `DATA1200.SAV`; the next start goes back to that line in the same
  scene and the lines after it are the ones the unbroken run shows; with an empty save folder the logo and the title
  come.
- Pictures of the unbroken run and of the run after the start, taken at the same eleven lines, show the same scene,
  name, text and buttons; the history after the start holds the lines read before it.
- **With the switch off nothing changes.** The build before and the build after played
  `engine/tests/inputs/mining_read.input` and `save.input` with a picture every 100 frames: all 321 pictures of the
  first are the same files, and of the 318 of the second 297 are the same files and 21 differ only in the minute of
  the clock that the save menu shows.
- All of the engine's checks pass on the Mac, `movie_flow` and the three other flow tests among them. `mining_test`
  has 108 checks, two of them for the line the game starts at.
- **Next to a VN Mining Server**, with the test host: twelve lines read and sent, the app left at the twelfth, the
  game started again and two lines read. The server got those two and not the twelfth again.
- **The app in the iPad Simulator** (iPad Pro 11-inch, iOS 27.2). The launcher's switch turns on and stays on, the
  button reads "Continue" once there is a save, and four seconds after pressing it the saved line stands on the
  screen, with no logo and no title. `DATA1200.SAV` is written anew after five lines and again when the app goes to
  the home screen. After closing and starting the app the line that was on the screen is back, and the mining server
  has it once.
- A save file now has its sizes where the original game has them, 8 bytes later than before; saves this port wrote
  earlier are still read, and a save of the original game now loads (its text history does not come along yet).

**Not checked:** a real iPhone, iPad or Vision Pro. On an iPhone in landscape the switch lies below the start
button and is reached by scrolling.

## Next to the original game, 2026-11-25

`ORE_TUBA.EXE` 1.01 and this port's test host for Windows (`engine/Makefile.win`) ran side by side on one Windows
PC, with the same settings file and the same saves, and their pictures were compared pixel by pixel. The original
was driven by an agent inside its process that reads the script's variables and sends clicks to its window; its
picture was read with `BitBlt` from a thread that is not DPI aware. (`PrintWindow` on a scaled screen returns the
picture moved by a third of a pixel and made an early comparison look worse than it was.)

**Same in both without any change:** the title, complete at the same frame; 40 opening lines with the same letter
positions, line breaks and rows; the background above the text box on those lines, pixel for pixel; the list of
saves.

**Found and changed to what the original does.** The rules are written down in [docs/re](re/).

| What | Before | Now |
|---|---|---|
| Sizes and checksum in a save file | 8 bytes too early, so a save of the original did not load | Where the original has them; both places are read |
| Text history and RETURN in a save | A layout of the port's own; after loading an original save both were gone | The original's layout: a save the port writes has the same state bytes as one the original writes at the same line, and each game loads the other's saves with the same pictures |
| Memo mark in the save list | On every save | Only on a save with a memo |
| Speaker's name in the name box | 2 px too far right | The same pixels for four names |
| Letter cells at odd sizes | Half a cell rounded down, ascent from a rule of thumb | GDI's numbers; text at the sizes the script's size tags reach sits where the original's does |
| Name box background | The built-in 50 percent | The settings' window shade, as for the text window |
| Shade of the text, name and choice windows | Opacity rounded down, the frame laid over it with the letters' formula | Rounded to the nearest, the frame laid over it pair by pair as the original does |
| Choice window | Drawn from its y downwards, 37 px too low with two options | Centred on its y |
| Scroll bar arrows | Always lit, and in the text history 3 px too far right | Grey at the end of the range they point to, at the bar's own place |
| Small picture in a save | The page composed anew, one bilinear sample per pixel | The page as last shown, reduced with the original's filter |
| Opaque pixels of pictures with alpha | Mixed like the soft ones: white letters 254, opaque sprite pixels one short | Copied where the original copies them |

**Measured after the changes,** original against port at the same line:

- A line with a character on screen and a speaker's name: 54 of the 480,000 pixels differ, each by 1 (before the
  changes of this round 74,247 differed). The scene above the text box has no differing pixel on any of 16 lines.
  The white of the letters is the same on both sides, 406 pixels at 255 and 22 at 254.
- The choice window, reached by playing and through RETURN: the same frame lines, text rows and highlight; 230 of
  its 54,225 pixels differ by 1, along the frame's edge.
- The text history on its newest, a middle and its oldest page: the whole picture is the same, pixel for pixel.
- A save made at the same line in the same way: the scene part of the small picture is the same; the text box part
  differed by a few steps in 644 of 7,500 pixels before the shade was corrected and was not measured again after.
- Saves: a player's own saves of the original load in the port with RETURN and LOG lit and 51 entries in the text
  history, and RETURN leads to the same choice.
- All of the engine's checks pass with these changes on a Mac with macOS 26.6 and Xcode 27.2, the four flow tests
  with the game among them.

**What still differs, each by one step of 255:** a few half-covered edge pixels of letters over the window shade
(14 on the line above), the edge pixels of a choice's text under the pointer, the choice window's frame edge, and
the small blue button that waits for a click (40 pixels). The likely cause is the same in all four, rounding in the
drawing of soft pixels onto a picture that has alpha of its own; it was not traced.


**Left as it is:** the original moves the mouse pointer onto the default button of a dialog and onto the first
option of a choice, a setting that is on when the game is installed. The port keeps that off, since on a touch
screen it would only light a button nobody pointed at. The original packs `GAMEDATA.DAT` and `TEXT_LOG.DAT`; the
port writes them unpacked, and the original reads those.

**Not compared:** effects and transitions between pictures, a choice with three or more options, and the menus
other than save, load and the text history.

## Mining in the game, 2026-11-26

[MINING.md](MINING.md) describes the feature. Run with the `SET_UP` folder of the game's disc, with the Linux build
of the test host (without `VOICE.LPK`) and on a Mac with macOS 26.6 and Xcode 27.2. Every AnkiConnect, audio source
and dictionary in these runs was made up for the test or a throwaway.

- `dict_test` (1,652 checks) and `kit_test` (193), on both: Yomitan's own tests for Japanese inflections, the
  import of a dictionary and what a lookup finds, and the kit against a made-up AnkiConnect and audio source. The
  page's arithmetic has 12 tests of its own (`node --test engine/tests/page/*.test.mjs`).
- Importing Jitendex takes about 2 seconds on the four-core machine these checks ran on and leaves a file of 94 MB.
- `tools/test_mining_page.mjs`, the popup's page in Chromium: 31 checks over a made-up line and 57 over the game.
  The marks lie on the tapped word, the popup stands beside it and inside the picture, a card is made once, and the
  game's line is the same when the popup closes.
- `tools/test_mining_kit.mjs`, the engine and the kit without a browser: 38 checks with the Linux host and 40 on the
  Mac, where the card carries the line's voice as an M4A file of 112,642 bytes. A tap on a character names that
  character and holds the game; clicks, keys, the wheel and commands during the hold do not reach it; a tap in the
  text box beside the characters does nothing and one outside it reads on.
- With the Linux host and the game: in the game's menu, in the history and with the text box hidden, a tap where
  the text was is the game's own click and no word is looked up; the LOG button in the text box still opens the
  history.
- **Next to the code of a real AnkiConnect server** ([anki-connect-server](https://github.com/friedrich-de/anki-connect-server)
  0.3.0 with Anki 26.9.4 and a throwaway collection), with the Linux host and the game: a wrong key is told in a
  sentence, the Lapis note type and its deck are found, the card arrives with word, reading, meaning, the sentence
  with the word in bold, the picture (60 KB) and the word's sound, both files lie in the collection, and a second
  card of the word is refused as a duplicate.
- **The app in the iPad Simulator** (iPad Pro 11-inch, iOS 27.2). In the launcher's settings: Connect finds Lapis
  and its deck, the list of fields shows what each gets, a dictionary is downloaded from an address and imported,
  the audio source is checked and kept, and the launcher then says "On: 1 dictionary, cards go to the deck
  Mining." In the game: a tap on 自分 marks it and shows its entry; + makes the card, which arrives with the
  line's voice (M4A), the picture, the word's sound and the game's title; a tap on 気付いた finds 気付く; a tap
  beside the popup closes it and the line stays; a tap in the text box beside the text does nothing; a tap on
  the picture reads on. After closing and starting the app the settings are as they were, the key and the audio
  address among them: from the Keychain in a build signed to run locally, and from the file beside the settings
  in an unsigned one, where the Keychain refuses.
- **The app in the iPhone Simulator** (iPhone 17 Pro, iOS 27.2, landscape): the popup stands above the line and
  inside the screen, and a word Anki has already shows as known.
- The Release packages for iOS, Mac and visionOS compile with the mining kit and carry the folder `mining`.

**Not checked:** a real iPhone, iPad or Vision Pro; Anki on a computer with the AnkiConnect add-on itself; the Mac
and visionOS apps beyond compiling; a keyboard's Escape and a controller's B in the app; and a dictionary's
pictures inside the app (`dict_test` and `kit_test` cover reading them).

## The original project's checks on devices, 2026-10-18

The original project checked its versions 0.1.0 and 0.1.1 on real devices. It played with a Chinese translation
patch, which this fork no longer supports; the engine those checks ran is otherwise the one in this repository.

| Platform | Tested on | Confirmed |
|---|---|---|
| iPhone | iPhone 16 Pro Max, iOS 26.6.2 | Text, sound, advancing by tap. The first opening film plays without gaps, with its music, to its own end and returns. |
| iPad | iPad Pro M5, iPadOS 26.4.2 | Text, sound, advancing by tap, proportions and touch positions in landscape and portrait. Saving and loading, quick save and quick load. The second opening film plays without gaps, with its music, to its own end and returns. |
| visionOS | First Apple Vision Pro, visionOS 27.2 | Text, sound, advancing by look and pinch. The large native buttons, saving and loading, History closing on a second press, the chapter captions. The first opening film plays without gaps, with its music, to its own end and returns. |
| Mac | macOS 26.6.2, Mac Catalyst | The Release build's folder chooser finds the game, the game starts to its title, START by mouse, the question about jumping to a chapter and, after NO, the opening. |

The files on the iPad and the Vision Pro were compared with the local ones by path and size after the import. The
film check uses a temporary save folder of its own; afterwards iPhone, iPad and Vision Pro started normally again
without the diagnostic environment variable.

Those builds were made with Xcode 27.1. The README names the lowest systems the build supports; the results on
devices do not mean that every one of those systems was tested. The Mac package holds arm64 and x86_64 code and ran
on an Apple silicon Mac only.

0.1.1 added a check at the boundary of a logic frame: a short press and a native command that arrive in a frame in
which no script has run yet are kept for the next logic frame.

## Automatic checks

- `save_roundtrip`: a save of a normal scene, the global progress, the read history and the text history with a
  choice are written and read again, and the text history lies in the file the way the original game writes it.
- `quick_save`: a negative number stands for the newest quick save, the ring overwrites its oldest entry, the state is
  restored, reading does not move the index, and an empty ring is handled.
- `command_input`: native menu commands are taken once, following the game's own mapping; History toggles; confirm,
  cancel and the four directions are held and released.
- `image_blend`: a transparent menu panel, a half-transparent overlay, a transparent source, extra opacity, the
  blending of sprites with opaque pixels copied pair by pair, and the reduction for a save's small picture.
- `native_coverage`: every native a script refers to has a registered handler. This checks registration only, not
  what a handler does.
- `tools/test_save.py`: saves and loads through the game's menus and is back in the text at the saved sentence.
- `tools/test_quick_save.py`: quick save, its confirmation and quick load through the native commands.
- `tools/test_history_toggle.py`: text, then HISTORY, then text again.
- `tools/test_autosave.py`: auto save and continue: a save every fifth line and on leaving, in a file of its own, and
  a start that goes back to that line and reads on as an unbroken run does.
- `movie_flow`: both opening films of the game's MoviePlay script, with no input to skip them, end by themselves and
  return to the script.
- `fx_test` checks the arithmetic of the effects, and `elg_all` decodes every ELG picture.
- `mining_test`: the mining settings file, the text of a record, which lines are sent, the request and the queue.
- `dict_test`: undoing inflections against Yomitan's test corpus, reading a zip, importing a dictionary in Yomitan's
  format, lookups, frequencies and pitch, pictures, and the library of installed dictionaries.
- `kit_test`: the mining kit's methods against a made-up AnkiConnect and audio source: settings, the Lapis defaults,
  what a card's note holds, duplicates, wrong keys and addresses, imports and downloads.
- `engine/tests/page/*.test.mjs` (Node): where a word stands in its sentence, furigana, and a meaning as a card's HTML.
- `tools/test_mining_page.mjs` (Node and Playwright's Chromium): the popup's page over a made-up line, and over the
  game when given `SET_UP`.
- `tools/test_mining_kit.mjs` (Node): the engine and the kit together with the game.
- `FolderImportTests`: importing the folder that is already there, a name that is taken, a copy that fails, and an
  import that succeeds.

The tests with the game need the player's own game files.

## Limits

The iOS and visionOS IPAs are unsigned and need the user's own Apple account. For a native visionOS app, installing
with Xcode is the checked way; whether Sideloadly handles such a package was not checked. The Mac package carries a
local ad-hoc signature and is not notarised. The packages include the third-party licences and no game content, no
provisioning profile and no Debug film entry.

Nobody has played every route to its end, taken every choice, or compared every visual effect with the original
pixel by pixel. Some of the effects' arithmetic and the `bg_anime` format still have
[open questions](re/30_effects.md#open-questions); a static check found no scene that refers to `L+dec8` in FUNC_LIB,
where `bg_anime` lives. That every native is registered and that the opening runs does not mean the whole game was
tested, which is why this is a pre-release. A problem report is most useful with the platform, the version, the place
in the script and the steps that lead to it.
