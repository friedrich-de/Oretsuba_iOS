# Auto save and continue

A switch in the launcher, off by default. With it on, the port keeps the reader's place by itself, and the
game starts where it was left, without the logo, the title menu and the load screen.

## What it does

* **It saves without a sound** every 5 lines, and at once when the app leaves the screen (the home button, another
  app, the lock) or the game is closed. There is no message, no sound and no pause.
* **It saves into a slot of its own**, the file `DATA1200.SAV` in the save folder. None of the game's menus shows
  that slot, so it cannot overwrite a save or a quick save of the player's, and the player's saves do not touch it.
* **The game starts at that line.** The launcher's button reads "Continue" when there is such a save. The scene is
  rebuilt as after any load: the picture, the music, the text history, and the line is shown again with its voice.
  With [mining](MINING.md) on, that line is not sent a second time.
* The title menu is still there: Menu › Title in the game leads to it, for a new game, another save, the gallery or
  the settings. Whatever is read after that becomes the new place after five lines.

It does not save while a scene is replayed from the gallery, where the game itself takes no save either, and not
while a menu, the history or the settings are open: there the last save, at most five lines old, stays.

With the switch off nothing changes and an existing `DATA1200.SAV` is left alone.

## How it works

Nothing asks the game's script to save or to load, which is why nothing is heard or seen.

* A quick save of the game is one call of the save function while the script waits inside a line. The engine writes
  the same kind of file itself at that moment: after the step of the script that began a line, and only while the
  scene is the running script and the game would take a quick save (`engine/src/core/autosave.cpp`).
* A load from the title menu ends with the title reporting "a game was loaded", and the game's main script does
  the rest. At the start the engine loads `DATA1200.SAV` and answers the main script's call of the title with that
  result; the logo before it is left out. Without a save, or with one that cannot be read, the logo and the title
  come as always.

## Trying it without the app

The test host takes the same two settings: `orehost --autosave 5` saves, `orehost --autosave 5 --resume` starts at
the save. `python3 tools/test_autosave.py /path/to/SET_UP` reads into the story, stops, starts again and checks that
the same line comes back and that the lines after it are the ones an unbroken run shows.
