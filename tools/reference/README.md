# Comparing the port with the original

The engine in this repository was written from the code of `ORE_TUBA.EXE`. The tools here put the original, running
on Windows, next to the port, so that what the code reading left open can be settled by looking. Both sides use the
player's own game; no picture or file of the game belongs in the repository.

## `orewin`: the original's window on Windows

`orewin.cs` photographs the client area of the game's window and sends it clicks and keys. Positions are game
coordinates (800x600), the same as in the input files of the port's test host. It needs nothing installed: the
compiler that ships with Windows builds it.

```bat
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize /out:orewin.exe /r:System.Drawing.dll orewin.cs
```

Start the game in a window, leave the window uncovered, then:

```bat
orewin info                       the game's window and the size of its client area
orewin mute on                    mute the game in the Windows volume mixer (off takes it back, show tells)
orewin shot title.png             one picture
orewin click 730 384              a left click at a game position
orewin still rest.png             wait until the picture stands still, then save it
orewin walk 200 run1              200 times: wait for the still picture, save it, click to go on
orewin burst 4 fade --click-x 400 --click-y 500    click, then 4 seconds of pictures, 30 a second
orewin run some.input run2 --shot-every 100        play an input file of the port's test host in real time
```

`walk` writes `line_00000.png` and so on with a `walk.csv` (when each picture was taken, how long the game took to
come to rest, and whether it did). A file named `stop` in the output folder ends a walk. The original runs on the real
clock, so two runs are never the same to the frame: compare pictures of the game at rest, and use `burst` for what
moves.

The port's side of a comparison is `orehost` (see the [README](../../README.md#checks-and-packages)), which writes
its pictures with `--shot-every` and `shot` commands.

## `compare.py`: the pictures of both

```sh
python3 tools/reference/compare.py pair original.png port.png --out both.png
python3 tools/reference/compare.py walk original-run port-run --out report
```

`pair` measures two pictures and writes them side by side with their difference. `walk` takes two folders of pictures
in reading order, pairs them up even where one side has a picture more or less, writes `pairs.csv` and the pairs that
differ most. The scene and the text box are measured apart, because Windows draws the letters with another font than
Apple's systems: the text box never matches to the pixel, the scene should. It needs Pillow and NumPy.
