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

Run with the Linux build of the test host and the `SET_UP` folder of the game's disc, without `VOICE.LPK`.
[AUTOSAVE.md](AUTOSAVE.md) describes the feature.

- `tools/test_autosave.py`: an unbroken run of the story saves at every fifth line; a run ended in the middle of line
  32 saves that line and writes no file but `DATA1200.SAV`; the next start goes back to line 32 in the same scene and
  the nine lines after it are the ones the unbroken run shows; with an empty save folder the logo and the title come.
- Pictures of the unbroken run and of the run after the start, taken at the same eleven lines, show the same scene,
  name, text and buttons; the history after the start holds the lines read before it.
- **With the switch off nothing changes.** The build before and the build after played
  `engine/tests/inputs/mining_read.input` and `save.input` with a picture every 100 frames: all 321 pictures of the
  first are the same files, and of the 318 of the second 297 are the same files and 21 differ only in the minute of
  the clock that the save menu shows.
- `save_roundtrip`, `quick_save`, `command_input`, `native_coverage`, `tools/test_save.py`, `tools/test_quick_save.py`
  and `tools/test_history_toggle.py` pass with this build. `mining_test` has 108 checks, two of them for the line the
  game starts at, which is not sent to a mining server a second time.
- A save file now has its sizes where the original game has them, 8 bytes later than before; saves this port wrote
  earlier are still read, and a save of the original game now loads (its text history does not come along yet).

**Not checked:** a real iPhone, iPad or Vision Pro; the music after the start, which the test host does not play;
the mining server not getting the line twice, beyond the unit check.

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

- `save_roundtrip`: a save of a normal scene, the global progress and the read history are written and read again.
- `quick_save`: a negative number stands for the newest quick save, the ring overwrites its oldest entry, the state is
  restored, reading does not move the index, and an empty ring is handled.
- `command_input`: native menu commands are taken once, following the game's own mapping; History toggles; confirm,
  cancel and the four directions are held and released.
- `image_blend`: a transparent menu panel, a half-transparent overlay, a transparent source, extra opacity, and the
  blending of sprites as before.
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
