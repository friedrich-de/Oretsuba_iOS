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

**Not compared:** a choice with three or more options, and the menus other than save, load and the text history.
Effects and transitions followed a day later, see the next section.

## Frame by frame next to the original, 2026-11-26

For effects both programs were given the same stepped clock: 16 ms per shown frame, in the original through its
time imports, in the port's test host with `--frame-ms 16`. A click goes in at a chosen frame, and every frame after
it is kept on both sides (`--shot-range`). Over 200 frames the original's clock stepped by exactly 16 and its script
ran once per frame; on a line that stands still all 227 frames were the same.

**Same in both:** the letters of a line appear one every second frame in both, on the same frames; a cross fade
has the same strength on every frame; character moves, the inset picture and the bounce match on every frame (two
moves within one colour step); the pattern transition number 86 is the same pixel for pixel in both directions.

**Found and changed:**

| What | Before | Now |
|---|---|---|
| The end of a transition | The port ended a transition one frame after its last step. A screen that fades away and leaves the change of pages to the script, as the save, load and history screens do, showed once more for one frame after its fade; two transitions in a row had one still frame more between them than in the original | A transition ends in the frame that shows its last step, as the original's loop does (render, then finish) |
| Right edge of the horizontal blur | The last column was repeated beyond the edge; the last columns were off by up to 17 steps | The column before the last is repeated and the last one is not read, the rule that fits all 31 frames measured on the original. Measured again with the changed code: the same pixel for pixel |
| How the save, load and history screens fade | They appeared at once. When they closed, their controls vanished first and only the background faded | They fade in over the game's picture in 300 ms and fade away whole. The original's script copies the game's picture into the menu's page and starts a transition of a kind that composes nothing while it runs; the port composed the page anyway, so both ends of the fade were the menu. On the test host the strength of the fade is now 13, 27, … 244 on the frames 3 to 20 after the click and the screen is complete at frame 21; closing runs over the frames 1 to 18 and is complete at 19. Those are the frames measured on the original |
| The text window during a scene change | Window and name box faded away with the old picture and came back when the change was over | They stay above the change, and the text goes on writing while the picture changes. The original draws the page's overlay list, which holds the window and the name box, over every frame of a transition |
| When a click and an effect take hold | A click on the text started the next line one frame earlier than in the original, and a layer effect showed its first change one frame later. On screen the two cancelled each other for a plain line | The engine runs the script before the tasks, the order logged on the original. The next line's commands start in the frame after the click, and a layer effect takes the frame of its call as its start |
| The small button that waits for a click | It was drawn only when its picture changed, so after a click it came back up to 4 frames after the next line was complete; the first time it came one frame late; its round of six pictures took 37 and 38 frames in turn | It is on screen in the frame the line is complete, and its round takes 38 frames. The original draws it again when it is shown and sets the animation's start anew at the end of each 600 ms, which drops what was over. On the test host with eight clicks it came in the frame the text was complete every time, and with a line that is complete one frame after the click it was hidden for that one frame only, as measured on the original |
| Who takes a plain click | The click region as large as the screen took it: the script saw a hit without a number (`N_2ba` = 1, `N_2bb` = -1) and left out its key routine in that frame | The script makes that region with -1 as its fifth argument, and the original makes such a region switched off: it never takes a click, and the text window's own controller advances the text. Now the script sees `N_2ba` = 0 in the frame of a click and runs its key routine, the calls logged on the original. The next line starts in the same frame as before |

**Measured after the changes,** port against original on the same stepped clock, the click held for two frames on
both sides, every frame compared. Frames are counted from the click.

- The save, load and history screens: no frame of the opening or of the closing differs by more than 1 in any of
  the three. Both programs change from frame 3 to 21 when the screen opens and from frame 1 to 19 when it closes,
  and neither shows the screen again after that. What differs by 1 is letter edges in the game's picture below (at
  most 50 pixels) and one spot of 6 pixels in the menu.
- Loading a save from the title: the original starts a transition by a rule picture (750 ms) 127 frames after the
  click on the slot and a cross fade (500 ms) at frame 175. Both run on the same frames in the port. The cross
  fade's 33 frames are the same pixel for pixel; the rule transition's differ by 2 to 4 at most.
- Scene changes on 14 lines of three scripts, 61 to 381 frames each: the scene above the text box is the same pixel
  for pixel on every frame of 10 lines (cross fade, the numbered pattern transition, the horizontal blur with its
  right edge, character moves, background moves). On the other 4 it differs by at most 2, and only while a
  transition by a rule picture runs. The text and name boxes differ by 1 at letter edges.
- The text window above a scene change: at four moments of one scene change the scene has no differing pixel, the name
  box 4 and the text box 3 to 10, each by 1.
- Timing: the text window reports the click to the script one frame after it in both (`N_300` = 3), and the
  transition or layer effect of the next line is called in that frame in both. On a long line the count of letters
  shown is the same in 200 of 200 frames. On five lines with two changes in a row the picture changes on the same
  frames in both, for example on frames 2 to 33 and 35 to 92.
- The original never starts a transition of mode 2 on the 169 lines logged; scene changes use mode 1, the menus
  and the loading of a save mode 0.

The last two rows were changed after that comparison, from what it found in the original. They were measured on the
test host with the same clock and the same clicks, not again next to the original.

**What still differs:**

- A transition by a rule picture (`SYS\RULE%03d.ELG`) rounds differently, by 2 at most in a scene change and by 4
  at most while a save loads. Not traced.
- The pointer. The original moves it onto the first save slot and onto a dialog's default button, which lights them
  at once; the port leaves it where it is (see "Left as it is" above).

**Read in the original's code for these, 2026-11-26:** the routines that draw a running transition, one per mode
(`docs/re/30_effects.md` 2.2), what `N_1a3` hands out and what the menus do with it, and the order of a frame with
every native call logged around a click (`docs/re/10_lifecycle_system.md` 3.2).

## Mining in the game, 2026-11-26

[MINING.md](MINING.md) describes the feature. Run with the `SET_UP` folder of the game's disc, with the Linux build
of the test host (without `VOICE.LPK`) and on a Mac with macOS 26.6 and Xcode 27.2. Every AnkiConnect, audio source
and dictionary in these runs was made up for the test or a throwaway.

- `dict_test` (1,666 checks) and `kit_test` (195), on both: Yomitan's own tests for Japanese inflections, the
  import of a dictionary and what a lookup finds, and the kit against a made-up AnkiConnect and audio source. The
  page's arithmetic has 12 tests of its own (`node --test engine/tests/page/*.test.mjs`).
- Importing Jitendex takes about 2 seconds on the four-core machine these checks ran on and leaves a file of 94 MB.
- `tools/test_mining_page.mjs`, the popup's page in Chromium: 32 checks over a made-up line and 58 over the game.
  The marks lie on the tapped word, the popup stands beside it and inside the picture, a card is made once, the
  game's line is the same when the popup closes, and a meaning's pictures (an SVG and a PNG) are drawn.
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
- **Dictionaries in the iPad Simulator**, after "Import from Files…" did nothing on a real iPhone (the picked file
  was only lent to the app, and the reason for a failure stood out of sight at the top of the page): the picker
  now hands the app a copy of its own. A zip picked in "On My iPad" is imported and no copy is left behind;
  closing the picker and opening it again works; a file that is no zip is refused with the reason under the
  buttons; "Add Jitendex" downloads the newest release from its makers and imports it (437,367 words), and the
  button is gone once the dictionary is there. The Simulator lets an app read any file, so the failure itself
  does not show there: on a real iPhone this is still to be tried.
- **Two real dictionaries** that failed on a player's iPhone, run through the importer outside the app with
  `dict_probe` on a Linux PC, before and after the fix in `zip_reader`. A Japanese-Japanese dictionary (334,748
  words, 2,854 pictures) showed most of its pictures as failed: 24 of its files, the label pictures such as
  漢-default.svg, are named in Shift-JIS in the zip with the UTF-8 name in an extra field, and 40,986 of the
  44,429 picture references in its meanings ask for one of them. Before, the library found 3,443 of the
  references; now it finds all 44,429, and no picture is kept under a name that is not UTF-8. A pitch accent
  dictionary (88,089 rows) was refused with "term_meta_bank_1.json in that dictionary is damaged.": the zip
  states a wrong checksum for all nine of its banks, which unpack whole. It now imports with 88,089 pitch
  accents, and 昨日, 丈夫 and 食べる come with theirs. Three imports and eight lookups took 1.4 seconds.
- **A dictionary's pictures in the iPad Simulator**: the SVG and the PNG of a made-up dictionary's meaning are
  drawn in the popup over the game.
- The Release packages for iOS, Mac and visionOS compile with the mining kit and carry the folder `mining`.

**Not checked:** a real iPhone, iPad or Vision Pro; Anki on a computer with the AnkiConnect add-on itself; the Mac
and visionOS apps beyond compiling; a keyboard's Escape and a controller's B in the app; and the two real
dictionaries above inside the app (the importer is the same code there, and the test dictionary's pictures show).

## The Android app, 2026-11-26

[ANDROID.md](ANDROID.md) describes the app. Run with the `SET_UP` folder of the game's disc on three devices: an
emulator with Android 15 (x86_64, 1080x2400), an emulator with Android 8.0, the lowest version the app takes, and a
phone with Android 14 (arm64, 1080x2412, a screen that can refresh 120 times a second). On the emulators and on the
phone every input was sent through `adb`; nobody held the phone.

**Without game files**, as the workflow runs it for every change: `cp932_test` converts all 9,604 two-byte codes of
Shift-JIS to Unicode and back and agrees with the system's iconv in each, the lint check passes with warnings counted
as errors, and the package builds for `arm64-v8a` and `x86_64`.

**The game, Android 15 emulator.**

- The launcher finds the game folder, says so, and starts the game. Its column follows the screen when it is turned.
- Logo, title menu, START, the poem and the first opening film (800x600, watched for 87 seconds). The first twelve
  lines stand as they should: line breaks, brackets, dashes, full-width digits, the name plate.
- Controls: a tap advances; Back opens the game's menu once and closes it again; a swipe down opens the text history
  and Back leaves it; a long press skips and stops when the finger lifts; skip stops at the first choice and one tap
  on an option picks it. While a film plays, Back ends it, and the first line follows with no menu open.
- Saving into a slot with the game's own question, a comment typed into Android's dialog with the keyboard, loading,
  and the question before overwriting. The files are in `Saves/jpn`.
- Leaving with Home stops the sound within seconds and keeps the app; coming back shows the same line and the sound
  goes on. The same with the screen switched off and on. The game's own exit returns to the launcher. After a forced
  stop the app starts anew in 0.4 seconds.
- **Auto save and continue**: the switch stays on over a restart of the app; `DATA1200.SAV` appears within the first
  five lines read and is written anew at once on Home; after a forced stop the button reads "Continue" and the saved
  line stands on the screen a second later, with no logo and no title, and the lines that follow are the story's
  next. With the switch off the logo and the title come and the file is left alone.
- **Mining**, next to a VN Mining Server 0.2.0 on the same PC: the launcher names the server and its version, says
  so when the key is wrong and when the server is down, and asks again on "Check again". `mining.txt` was brought in
  by copying and through the system's file picker, and removed with the launcher's button. 38 lines read at a normal
  pace arrived in order, each with its text, the speaker where there is one, and a screenshot (JPEG, 800x600, about
  90 KB); voiced lines came with a valid Ogg Vorbis file. Of the lines the skip ran over, none arrived. The line
  before the choice arrived when an option was picked, the options did not. A line left with Home arrived half a
  second later, with the app in the background.
- **Importing the folder** through the system's folder picker: the question before replacing the folder that is
  there, progress that moves, then "Game files found". An import cut off by a forced stop left the old game folder
  whole and nothing half-copied behind after the next start. Turning the screen during an import did not stop it.

**Android 8.0 emulator.** The app installs and starts, the title appears, the sound stream opens and runs (through
AAudio, which was new in that version), the letters are drawn with the system's Japanese font, Back reaches the app
by the older route and does the same, the system's bars are hidden.

**The phone.** A build signed with the key of the published builds installed, and six newer builds installed over
it with the game files kept. The picture stands in the middle of the screen at exactly 4:3 (1440x1080), clear of
the camera hole. The game was played from the title through the film into the first twelve lines; the lines laid
next to the emulator's have the same text, line breaks and positions. The sound stream runs (float, stereo, 44,100 Hz, no underruns) and stops on
Home. The screen runs at 60 Hz while the game is in front. A save with a comment: the keyboard comes up with the
dialog. Mining to the server on the PC (reached through `adb reverse`): four lines with voice and screenshot.

**How much of the processor it takes.** A text line that stands still costs about 2 % of one processor core in the
emulator, a screen where nothing moves none. The moving star background of the prologue took three quarters of a
core on the phone and reached only 30 pictures a second: a profile showed that its two background effects asked for
every pixel on its own. They now copy whole spans, checked to give the same picture on 4,000 random cases, and the
phone shows 60 pictures a second there at half of one of its small cores.

**The build on GitHub** carries the same certificate as the builds made by hand (the fingerprint is in ANDROID.md).

**Not checked:** playing by hand on a phone, and with it the tap with two fingers, which `adb` cannot send (in the
code it takes the same path as Back). The sound by ear: the emulators ran silent, and on the phone only the state of
the stream was read. Copying the game folder with a cable, and the folder picker on a real phone. A mouse, a
keyboard or a game controller on a device. A tablet. The versions between Android 8 and 14. The story beyond the
first choice of the prologue.

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
- `menu_fade`: the load screen is opened from the title and closed again, and every frame is looked at: the screen
  fades in over the title's picture, it fades away with all its controls, and once it has faded away it does not
  show again.
- `wait_cursor`, without the game: the animation of the small button that waits for a click takes 38 frames for its
  six pictures at 16 ms a frame, draws in the frame it is shown again and keeps its phase over a hide; a click
  region made with -1 as its fifth argument is switched off.
- `fx_test` checks the arithmetic of the effects, and `elg_all` decodes every ELG picture.
- `mining_test`: the mining settings file, the text of a record, which lines are sent, the request and the queue.
- `dict_test`: undoing inflections against Yomitan's test corpus, reading a zip, importing a dictionary in Yomitan's
  format, lookups, frequencies and pitch, pictures, and the library of installed dictionaries.
- `kit_test`: the mining kit's methods against a made-up AnkiConnect and audio source: settings, the Lapis defaults,
  what a card's note holds, duplicates, wrong keys and addresses, imports and downloads, and the dictionaries
  suggested for one tap.
- `engine/tests/page/*.test.mjs` (Node): where a word stands in its sentence, furigana, and a meaning as a card's HTML.
- `tools/test_mining_page.mjs` (Node and Playwright's Chromium): the popup's page over a made-up line, and over the
  game when given `SET_UP`.
- `tools/test_mining_kit.mjs` (Node): the engine and the kit together with the game.
- `FolderImportTests`: importing the folder that is already there, a name that is taken, a copy that fails, and an
  import that succeeds.

The tests with the game need the player's own game files.

## Limits

The Android package is signed with this project's own key and installs as it is; Android asks before it installs a
package that does not come from a store. The iOS and visionOS IPAs are unsigned and need the user's own Apple account. For a native visionOS app, installing
with Xcode is the checked way; whether Sideloadly handles such a package was not checked. The Mac package carries a
local ad-hoc signature and is not notarised. The packages include the third-party licences and no game content, no
provisioning profile and no Debug film entry.

Nobody has played every route to its end, taken every choice, or compared every visual effect with the original
pixel by pixel. Some of the effects' arithmetic and the `bg_anime` format still have
[open questions](re/30_effects.md#open-questions); a static check found no scene that refers to `L+dec8` in FUNC_LIB,
where `bg_anime` lives. That every native is registered and that the opening runs does not mean the whole game was
tested, which is why this is a pre-release. A problem report is most useful with the platform, the version, the place
in the script and the steps that lead to it.
