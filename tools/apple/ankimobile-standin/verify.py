#!/usr/bin/env python3
"""Compares the app's report of its AnkiMobile check with what the stand-in
received (run.sh). Usage: verify.py APP_REPORT STANDIN_LOG"""
import json
import sys

failed = 0


def check(ok, what):
    global failed
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        failed += 1


def read(path):
    try:
        with open(path, encoding="utf-8") as file:
            return [line.rstrip("\n") for line in file if line.strip()]
    except OSError:
        return []


report = read(sys.argv[1])
log = [json.loads(line) for line in read(sys.argv[2])]


def told(start):
    """The JSON the app was told in the report line that starts with `start`."""
    for line in report:
        if line.startswith(start):
            return json.loads(line[len(start):])
    return {}


check(bool(report) and report[-1] == "done: three cards", "the app's check ran to its end")

lists = told("anki.lists ")
check(lists.get("ok") is True and lists.get("message") == "AnkiMobile has 2 decks and 2 note types.",
      "the app read the stand-in's decks and note types off the clipboard")
check("decks Default, 日本語::Mining" in report and "note types Basic, Check" in report, "with their names, Japanese too")

files = next((line.split() for line in report if line.startswith("files ")), [])
sent = {"Picture": (int(files[2]), int(files[3])), "Sound": (int(files[5]), int(files[6]))} if len(files) == 7 else {}

first = told("first card: card.added ")
check(first.get("message") == "AnkiMobile added the card." and first.get("missing") == [],
      "the first card came back as added, with nothing missing")
again = told("same word again: card.unconfirmed ")
check("did not say that it added the card" in again.get("message", ""),
      "the same word again, which the stand-in keeps to itself, came back as not confirmed")
third = told("same word, duplicates allowed: card.added ")
check(third.get("message") == "AnkiMobile added the card.", "with duplicates allowed it was added again")

did = [entry.get("did") for entry in log]
check(did == ["lists", "added", "refused", "added"], "the stand-in was asked for its lists once, added two notes and refused one: %s" % did)
notes = [entry for entry in log if entry.get("did") == "added"]
for number, note in enumerate(notes, 1):
    fields = note.get("fields", {})
    check(note.get("type") == "Check" and note.get("deck") == "日本語::Mining" and note.get("tags") == "oretsuba-check second-tag",
          "note %d names the note type, the deck and the tags" % number)
    check(fields.get("Word") == "食べる" and fields.get("Sentence") == 'ご飯を<b>食べる</b>。 & "more" + 100%',
          "note %d has its text as it was written, with its HTML and its marks" % number)
    got = {file.get("field"): file for file in note.get("files", [])}
    for field, kind in (("Picture", "image/jpeg"), ("Sound", "audio/mp4")):
        file = got.get(field, {})
        check(file.get("status") == 200 and (file.get("bytes"), file.get("sum")) == sent.get(field) and file.get("type") == kind,
              "note %d: the stand-in fetched the %s whole, as %s (%s bytes)" % (number, field.lower(), kind, file.get("bytes")))
    check(note.get("back", "").startswith("oretsuba://anki/added?card="), "note %d names the way back into the app" % number)
if len(notes) == 2:
    check(notes[0].get("dupes") == "" and notes[1].get("dupes") == "1", "only the last note asks for duplicates")

sys.exit(1 if failed else 0)
