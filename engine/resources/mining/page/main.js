// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The contributors to friedrich-de/Oretsuba_iOS
//
// This file is part of the dictionary popup of the Oretsuba port. It is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details: its text is the file LICENSE in engine/resources/mining.

// The dictionary popup over the game (docs/MINING.md). The game tells the page which character
// of the line was tapped; the page asks the mining kit for the word that starts there, shows it,
// and makes a card of it when asked. While the page is shown the game stands still.
//
// The game calls:
//   window.oreMining.show(what)    a character was tapped. `what` is what the engine's tap gives:
//                                  {line, text, index, rects, sentence, speaker, box, screen}, and
//                                  the host may add picture: [x, y, w, h], where the game's picture
//                                  is on this page in CSS pixels (without it: fitted and centred).
//   window.oreMining.hide()        the popup goes away without a word to the host
//   window.oreMining.event(name, payload)   something the kit tells without having been asked
// The page calls the host (host.js): the kit's methods, and popup.ready, popup.found, popup.close.
import { hostOf, KitError } from './host.js';
import { placeInSentence, wordOf } from './word.js';

const host = hostOf(window);

const panel = document.getElementById('panel');
const holder = document.getElementById('holder');
const note = document.getElementById('note');
const status = document.getElementById('status');
const marks = document.getElementById('marks');

/** A tap this close to a character (in the game's pixels) counts for it. */
const NEAR = 6;

/**
 * The tags left out when the player hides them: the popup's own (★, the dictionary's name, a
 * word's tags) and those dictionaries draw in their definitions, as Jitendex's parts of speech.
 * A word's frequencies and pitch stay.
 */
const HIDDEN_TAGS = `
.gsm-hoshidicts-definition-tags,
.definition-tag-list,
.gsm-hoshidicts-tag-term,
[data-sc-class="tag"],
[data-sc-content="part-of-speech-info"],
[data-sc-content="misc-info"],
[data-sc-content="field-info"],
[data-sc-content="dialect-info"] { display: none !important; }
ul[data-sc-content="sense-groups"] { list-style: none !important; padding-left: 0 !important; }`;

/** What the kit says of itself (page.init): how the popup looks, the dictionaries, whether cards can be made. */
let init = null;
let popup = null;
/** What the popup was made for: it is made again when the dictionaries or its look change. */
let madeFor = '';
let popupModule = null;

/** What is shown: the tap, the characters of the page of text, and the character looked up from. */
let shown = null;
/** Counts lookups, so that an answer for a word left behind is dropped. */
let round = 0;
/** The words found in the line, as opposed to those looked up in a definition: where each stands. */
let inLine = new WeakMap();
/** The sound chosen for each word from the audio button's list. */
const chosenSounds = new Map();
let playing = null;
let soundRound = 0;
/** The word last handed to an Anki app, which answers later (card.added, card.unconfirmed). */
let handedOver = null;
/** The selection when the + button was pressed: a press on a button may take it away before its click is heard. */
let pressedWith = null;

function say(text, error = false) {
  status.textContent = text ?? '';
  status.hidden = text === null || text === '';
  status.classList.toggle('error', error);
  status.setAttribute('role', error ? 'alert' : 'status');
}

function messageOf(error) {
  return error instanceof Error ? error.message : String(error);
}

function wordKey(result) {
  return `${result.term.expression}\u0000${result.term.reading ?? ''}`;
}

// ---------------------------------------------------------------- where things are

/** The game's picture on the page, and how many CSS pixels one of its pixels takes. */
function pictureOf(what) {
  const [width, height] = what.screen ?? [800, 600];
  if (Array.isArray(what.picture) && what.picture.length === 4 && what.picture[2] > 0) {
    const [x, y, w] = what.picture;
    return { x, y, scale: w / width };
  }
  const scale = Math.min(window.innerWidth / width, window.innerHeight / height);
  return { x: (window.innerWidth - width * scale) / 2, y: (window.innerHeight - height * scale) / 2, scale };
}

function onPage(rect, picture) {
  return {
    left: picture.x + rect[0] * picture.scale,
    top: picture.y + rect[1] * picture.scale,
    width: rect[2] * picture.scale,
    height: rect[3] * picture.scale,
  };
}

/** The character of the page of text nearest to a point of the page, or -1 when none is near. */
function characterAt(x, y) {
  if (shown === null) return -1;
  const picture = pictureOf(shown.what);
  const gx = (x - picture.x) / picture.scale;
  const gy = (y - picture.y) / picture.scale;
  let hit = -1;
  let best = NEAR + 1;
  shown.what.rects.forEach((rect, index) => {
    const dx = gx < rect[0] ? rect[0] - gx : gx >= rect[0] + rect[2] ? gx - (rect[0] + rect[2] - 1) : 0;
    const dy = gy < rect[1] ? rect[1] - gy : gy >= rect[1] + rect[3] ? gy - (rect[1] + rect[3] - 1) : 0;
    const distance = Math.max(dx, dy);
    if (distance < best) {
      best = distance;
      hit = index;
    }
  });
  return hit;
}

/** Marks the characters of the word found, and returns the box of its first line. */
function mark(index, count) {
  marks.replaceChildren();
  const picture = pictureOf(shown.what);
  const rects = shown.what.rects.slice(index, index + Math.max(count, 1)).map((rect) => onPage(rect, picture));
  let box = null;
  for (const rect of rects) {
    if (count > 0) {
      const element = document.createElement('div');
      element.className = 'mark';
      Object.assign(element.style, {
        left: `${rect.left}px`,
        top: `${rect.top}px`,
        width: `${rect.width}px`,
        height: `${rect.height}px`,
      });
      marks.append(element);
    }
    // The popup stands by the line the word starts in.
    if (box === null) box = { left: rect.left, top: rect.top, right: rect.left + rect.width, bottom: rect.top + rect.height };
    else if (Math.abs(rect.top - box.top) < rect.height / 2) box.right = Math.max(box.right, rect.left + rect.width);
  }
  return box;
}

/** Below the word where there is room, else above it, inside the page. */
function place(box) {
  const view = { width: window.innerWidth, height: window.innerHeight };
  const width = Math.min(520, view.width - 32);
  const left = Math.min(Math.max(16, box.left - 24), view.width - width - 16);
  const below = view.height - box.bottom - 16;
  const above = box.top - 16;
  const style = panel.style;
  style.left = `${left}px`;
  style.width = `${width}px`;
  if (below >= 280 || below >= above) {
    style.top = `${box.bottom + 8}px`;
    style.bottom = 'auto';
    style.maxHeight = `${Math.max(120, below - 8)}px`;
  } else {
    style.top = 'auto';
    style.bottom = `${view.height - box.top + 8}px`;
    style.maxHeight = `${Math.max(120, above - 8)}px`;
  }
}

// ---------------------------------------------------------------- the popup

function lookOf() {
  const look = init?.popup ?? {};
  return {
    scale: look.textSize ?? 100,
    display: { glossaryLayoutMode: look.compact === true ? 'compact' : 'default' },
    // The definitions scroll in the popup, not the page behind it.
    css: ['.gsm-hoshidicts-content-scroll { overscroll-behavior: contain; }', look.hideTags === true ? HIDDEN_TAGS : ''].join('\n'),
  };
}

function plainState() {
  return init?.cards?.possible === true ? 'add' : 'unavailable';
}

function stopSound() {
  soundRound += 1;
  if (playing !== null) {
    playing.audio.pause();
    URL.revokeObjectURL(playing.url);
    playing = null;
  }
}

/** Plays how a word sounds, from the audio source: the sound chosen, if one was. */
async function playSound(result, chosen) {
  stopSound();
  const mine = soundRound;
  if (chosen !== undefined) chosenSounds.set(wordKey(result), chosen);
  const source = chosenSounds.get(wordKey(result));
  const { expression } = result.term;
  try {
    const answer = await fetch(host.dataUrl('audio.word', { term: expression, reading: result.term.reading ?? '', source: source ?? '' }));
    if (!answer.ok) throw new Error('The audio source has none.');
    const blob = await answer.blob();
    if (mine !== soundRound) return;
    const url = URL.createObjectURL(blob);
    const audio = new Audio(url);
    playing = { audio, url };
    audio.addEventListener('ended', () => {
      if (playing?.audio === audio) stopSound();
    });
    await audio.play();
  } catch (error) {
    if (mine !== soundRound) return;
    say(`No sound of ${expression}${source === undefined ? '' : ` (${source})`}: ${messageOf(error)}`, true);
  }
}

/** The text selected in the popup's definitions, as Yomitan's {selection-text}: "" without one. */
function selectedText() {
  const element = popup?.element;
  const root = element?.shadowRoot;
  if (!element || !root) return '';
  // Chrome tells a selection in a shadow root only from there; the others from the document.
  const selection = (typeof root.getSelection === 'function' ? root.getSelection() : null) ?? document.getSelection();
  const node = selection?.anchorNode ?? null;
  if (selection === null || node === null || !(root.contains(node) || element.contains(node))) return '';
  return selection.toString().trim();
}

/** Makes a card of a result the popup shows. Its button changes by the result, not by its place. */
async function makeCard(result) {
  const current = popup;
  const selection = pressedWith ?? selectedText();
  pressedWith = null;
  if (current === null || init?.cards?.possible !== true) return;
  current.setResultState(result, 'adding');
  say(`Making a card of ${result.term.expression}…`);
  try {
    const made = await host.call('card.add', {
      word: wordOf(result, inLine.get(result) ?? { start: 0, length: 0 }, selection),
      audioChoice: chosenSounds.get(wordKey(result)) ?? '',
    });
    if (made.handedOver === true) {
      // An Anki app on the device took it and says later what became of it.
      handedOver = { result, popup: current };
      current.setResultState(result, 'adding', made.message);
    } else {
      current.setResultState(result, 'in-anki', made.message);
    }
    say(made.message);
  } catch (error) {
    const duplicate = error instanceof KitError && error.kind === 'duplicate';
    current.setResultState(result, duplicate ? 'in-anki' : 'error', messageOf(error));
    say(messageOf(error), !duplicate);
  }
}

async function made() {
  const presentation = JSON.stringify([init?.dictionaries ?? [], init?.popup ?? {}, init?.wordAudio === true]);
  if (popup !== null && madeFor !== presentation) {
    popup.destroy();
    popup = null;
  }
  if (popup !== null) return popup;
  // The popup's code is most of the page's size: it is loaded with the first word.
  popupModule ??= await import('./vendor/dictionary-popup/index.js');
  if (popup !== null) return popup;
  madeFor = presentation;
  popup = popupModule.createDictionaryPopup(holder, {
    theme: 'dark',
    opacity: 100,
    dictionaries: (init?.dictionaries ?? []).map((dictionary) => ({
      title: dictionary.title,
      frequencyMode: dictionary.frequencyMode ?? null,
    })),
    ...lookOf(),
    onAddCard: (result) => void makeCard(result),
    onClose: () => close(),
    onPlayAudio: (result, _index, chosen) => void playSound(result, chosen),
    hasAudio: () => init?.wordAudio === true,
    audioSources: async (result) =>
      (await host.call('audio.sources', { term: result.term.expression, reading: result.term.reading ?? '' })).names ?? [],
    chosenAudio: (result) => chosenSounds.get(wordKey(result)),
    lookup: async (text) => {
      const found = await host.call('lookup', { text });
      return { results: found.results, styles: init?.styles ?? [], states: found.results.map(plainState) };
    },
    media: (dictionary, path) => host.dataUrl('dict.media', { dictionary, path }),
    // A link out of a definition would leave the game.
    onExternalLink: () => undefined,
  });
  return popup;
}

/** Looks up the word that starts at a character of the page of text. */
async function lookUp(index) {
  const mine = ++round;
  const { what, characters } = shown;
  shown.index = index;
  say(null);
  note.hidden = true;
  place(mark(index, 0));
  panel.hidden = false;
  const text = characters.slice(index, index + 40).join('');
  try {
    const [, found] = await Promise.all([
      host.call('page.init').then((answer) => {
        init = answer;
      }),
      host.call('lookup', { text }),
    ]);
    if (mine !== round) return;
    const current = await made();
    if (mine !== round) return;
    const results = found.results ?? [];
    const matched = results[0]?.matched ?? '';
    const count = [...matched].length;
    void host.call('popup.found', { length: count }).catch(() => undefined);
    place(mark(index, count));
    if (results.length === 0) {
      current.clear();
      holder.hidden = true;
      note.textContent =
        (init?.dictionaries ?? []).length === 0
          ? 'No dictionary is installed. Add one in the settings before the game starts.'
          : 'No word in the dictionaries starts here.';
      note.hidden = false;
      return;
    }
    // Where each word stands in the sentence the card gets.
    const before = characters.slice(0, index).join('').length;
    inLine = new WeakMap();
    for (const result of results) {
      inLine.set(result, placeInSentence(what.sentence ?? '', what.text ?? '', before, result.matched ?? ''));
    }
    const possible = init?.cards?.possible === true;
    current.render(results, { styles: init?.styles ?? [], states: results.map(() => (possible ? 'waiting' : plainState())) });
    holder.hidden = false;
    if (!possible) {
      if (init?.cards?.problem) say(init.cards.problem);
      return;
    }
    let known;
    try {
      known = (await host.call('card.known', { words: results.map((result) => wordOf(result, inLine.get(result))) })).known;
    } catch {
      known = [];
    }
    // A word in a definition may be shown by now: these are the line's words.
    results.forEach((result, at) => current.setResultState(result, known[at] === true ? 'in-anki' : plainState()));
  } catch (error) {
    if (mine !== round) return;
    holder.hidden = true;
    say(messageOf(error), true);
  }
}

function hide() {
  round += 1;
  stopSound();
  shown = null;
  panel.hidden = true;
  marks.replaceChildren();
  popup?.clear();
}

/** Closes the popup and tells the host, which lets the game go on. */
function close() {
  if (shown === null) return;
  hide();
  void host.call('popup.close').catch(() => undefined);
}

// ---------------------------------------------------------------- what the game and the player do

window.oreMining = {
  show(what) {
    if (what === null || typeof what !== 'object' || !Array.isArray(what.rects)) return;
    stopSound();
    shown = { what, characters: [...(what.text ?? '')], index: 0 };
    void lookUp(Math.min(Math.max(0, what.index ?? 0), Math.max(0, what.rects.length - 1)));
  },
  hide,
  event(name, payload) {
    if (handedOver === null) return;
    const { result, popup: from } = handedOver;
    if (name === 'card.added') {
      handedOver = null;
      const text = payload?.message ?? `Made a card of ${result.term.expression}.`;
      from.setResultState(result, 'in-anki', text);
      say(text);
    } else if (name === 'card.unconfirmed') {
      handedOver = null;
      const text = payload?.message ?? payload?.reason ?? 'Anki did not say whether it made the card.';
      from.setResultState(result, 'error', text);
      say(text, true);
    }
  },
};

window.addEventListener(
  'pointerdown',
  (event) => {
    if (shown === null || !(event.target instanceof Node)) return;
    if (panel.contains(event.target)) return;
    // A tap on another character of the line looks that one up; a tap anywhere else closes.
    const index = characterAt(event.clientX, event.clientY);
    if (index >= 0) void lookUp(index);
    else close();
  },
  true,
);

holder.addEventListener('pointerdown', () => (pressedWith = selectedText()), true);
holder.addEventListener('keydown', () => (pressedWith = null), true);

window.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape' || shown === null) return;
  if (popup?.back() === true) return;
  close();
});

window.addEventListener('resize', () => {
  if (shown === null) return;
  const count = marks.childElementCount;
  place(mark(shown.index, count));
});

void host.call('popup.ready').catch(() => undefined);
