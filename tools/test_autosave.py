#!/usr/bin/env python3
"""Exercise auto save and continue (docs/AUTOSAVE.md) using player-owned files.

Reads into the story, stops in the middle of a line, starts again and checks
that the same line comes back and that the lines after it are the ones an
unbroken run shows.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HOSTS = ("build/host/orehost", "build/host-linux/orehost", "build/host-win/orehost.exe")

# START, clicks through the opening film (the first line appears near frame 19200),
# then one click every 2.5 seconds.
FIRST_READING_CLICK = 19350
STEP = 150
OPENING = ["900 click 730 384"] + ["%d click 400 500" % f for f in range(1400, 19100, STEP)]


def reading(first, count):
    return ["%d click 400 500" % (first + i * STEP) for i in range(count)]


def run(host, data, saves, out, name, lines, frames, *options):
    input_file = out / (name + ".input")
    input_file.write_text("\n".join(lines) + "\n")
    command = [str(host), "--data", str(data), "--save", str(saves), "--frames", str(frames),
               "--input", str(input_file), "--out", str(out / name), "--debug", *options]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=600)
    log = result.stdout + result.stderr
    if result.returncode or any(s in log for s in ("engine stopped", "unimplemented native", "script error")):
        raise RuntimeError(name + ":\n" + log[-4000:])
    return log


def begun(log):
    return [int(n) for n in re.findall(r"autosave: line (\d+) begun", log)]


def written(log):
    return [(scene, int(n)) for scene, n in re.findall(r"autosave: written in (\S+) at line (\d+)", log)]


def scene_at_end(log):
    found = re.findall(r"ran \d+ frames, script (\S+)", log)
    return found[-1] if found else ""


def check(condition, message, log=""):
    if not condition:
        raise RuntimeError(message + ("\n" + log[-3000:] if log else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path)
    args = parser.parse_args()
    host = next((ROOT / h for h in HOSTS if (ROOT / h).is_file()), None)
    check(host is not None, "Build the test host first (make -C engine)")
    data = args.data.resolve()
    with tempfile.TemporaryDirectory(prefix="ore-autosave-") as temporary:
        out = Path(temporary)

        # An unbroken run of 24 lines.
        log = run(host, data, out / "saves-unbroken", out, "unbroken", OPENING + reading(FIRST_READING_CLICK, 24),
                  FIRST_READING_CLICK + 24 * STEP, "--autosave", "5")
        unbroken = begun(log)
        scene = scene_at_end(log)
        check(len(unbroken) >= 20 and scene.startswith("S"), "The unbroken run did not read into the story", log)
        saves = written(log)
        every_five = [unbroken[i] for i in range(4, len(unbroken), 5)]
        check([n for _, n in saves[:len(every_five)]] == every_five,
              "A save was expected at every fifth line, got %s" % saves, log)

        # The same run, ended in the middle of the 12th line after the first click.
        stop = FIRST_READING_CLICK + 11 * STEP + 60
        saves_dir = out / "saves"
        log = run(host, data, saves_dir, out, "first-part",
                  OPENING + reading(FIRST_READING_CLICK, 12) + ["%d quit" % stop], stop + 10, "--autosave", "5")
        first_part = begun(log)
        check(first_part == unbroken[:len(first_part)] and len(first_part) < len(unbroken),
              "The first part does not match the unbroken run", log)
        check(written(log) and written(log)[-1][1] == first_part[-1],
              "Leaving did not save the line on the screen (line %d)" % first_part[-1], log)
        names = sorted(p.name for p in saves_dir.glob("DATA*.SAV"))
        check(names == ["DATA1200.SAV"], "Only the slot of its own may be written, found %s" % names)

        # Starting again: the same line, then the lines the unbroken run shows after it.
        log = run(host, data, saves_dir, out, "continued", reading(600, 8), 600 + 8 * STEP, "--autosave", "5", "--resume")
        check("autosave: going back to the saved line" in log, "The start did not go back to the save", log)
        continued = begun(log)
        expected = unbroken[len(first_part) - 1:][:len(continued)]
        check(len(continued) >= 8 and continued == expected,
              "After the start the lines are %s, expected %s" % (continued, expected), log)
        check(scene_at_end(log) == scene, "The story did not go on in %s" % scene, log)

        # Without a save the logo and the title come as always.
        log = run(host, data, out / "saves-empty", out, "nothing-saved", [], 600, "--autosave", "5", "--resume")
        check(scene_at_end(log) in ("LOGO", "TITLE", "TITLE2") and not begun(log),
              "Without a save the game must start at the title", log)
        print("Auto save and continue passed: saved every 5 lines and on leaving at line %d, "
              "continued there for %d lines" % (first_part[-1], len(continued)))


if __name__ == "__main__":
    main()
