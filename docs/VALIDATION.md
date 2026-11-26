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
- `fx_test` checks the arithmetic of the effects, and `elg_all` decodes every ELG picture.
- `mining_test`: the mining settings file, the text of a record, which lines are sent, the request and the queue.
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
