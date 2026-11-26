// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The contributors to friedrich-de/Oretsuba_iOS-Android-Mining
//
// This file is part of the dictionary popup of the Oretsuba port. It is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details: its text is the file LICENSE in engine/resources/mining.

// What a card gets from a word looked up in the popup.
import { furiganaOf } from './furigana.js';
import { meaningHtml } from './glossary-html.js';

/** Small kana that join the kana before them into one mora. っ is a mora of its own. */
const SMALL_KANA = new Set('ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ');

function moraCount(reading) {
  let count = 0;
  for (const character of reading) {
    if (!(SMALL_KANA.has(character) && count > 0)) count += 1;
  }
  return count;
}

/**
 * Whether a word of these parts of speech is a verb or an adjective, whose pitch Yomitan calls
 * kifuku when it falls at all: a noun that takes する is not.
 */
function isVerbOrAdjective(rules) {
  const parts = new Set(rules.split(/\s+/));
  const verbOrAdjective = ['v1', 'v5', 'vk', 'vz', 'adj-i', 'vs'].some((part) => parts.has(part));
  return verbOrAdjective && !(parts.has('vs') && parts.has('n'));
}

/** The name of a pitch pattern, as Yomitan's getPitchCategory gives it. */
export function pitchCategory(reading, position, verbOrAdjective) {
  if (position === 0) return 'heiban';
  if (verbOrAdjective) return position > 0 ? 'kifuku' : undefined;
  if (position === 1) return 'atamadaka';
  if (position > 1) return position >= moraCount(reading) ? 'odaka' : 'nakadaka';
  return undefined;
}

/**
 * The word of a lookup result, as the kit's card.add takes it:
 *   expression, reading   the word as the dictionary writes it, and how it is read
 *   furigana              in Anki's furigana: 食[た]べる
 *   meaning               its definitions in its first dictionary, as HTML
 *   frequency             in the first dictionary that has one, as a number; "" without one
 *   pitch                 where its pitch falls: "0" or "2, 3"; "" without one
 *   pitchCategories       as Yomitan's {pitch-accent-categories}: "heiban" or "atamadaka,nakadaka"
 *   selection             the part of its definitions selected in the popup
 *   at                    where the word is in the sentence, in UTF-16 units; length 0 when it is not in it
 */
export function wordOf(result, at = { start: 0, length: 0 }, selection = '') {
  const { expression } = result.term;
  const reading = result.term.reading ?? '';
  const frequency = (result.term.frequencies ?? []).find((group) => (group.frequencies ?? []).length > 0)
    ?.frequencies[0]?.value;
  const pitches = (result.term.pitches ?? []).find((group) => (group.pitches ?? []).length > 0)?.pitches ?? [];
  const verbOrAdjective = isVerbOrAdjective(result.term.rules ?? '');
  return {
    expression,
    reading,
    furigana: furiganaOf(expression, reading),
    meaning: meaningHtml(result),
    frequency: frequency === undefined || !Number.isFinite(frequency) ? '' : String(Math.round(frequency)),
    pitch: [...new Set(pitches.map((pitch) => pitch.position))].join(', '),
    pitchCategories: [
      ...new Set(pitches.flatMap((pitch) => pitchCategory(reading, pitch.position, verbOrAdjective) ?? [])),
    ].join(','),
    selection,
    at,
  };
}

/**
 * Where a word found from a character of the page of text is in the line's sentence, in UTF-16
 * units. The page of text is what stands in the text box, the sentence what the card gets: they
 * mostly are the same, but the box may hold a name, brackets or marks the sentence does not. So
 * the word is looked for in the sentence, nearest to where it stands on the page.
 */
export function placeInSentence(sentence, pageText, start, matched) {
  if (matched === '' || sentence === '') return { start: 0, length: 0 };
  const expected = sentence.length - (pageText.length - start);
  let best = -1;
  for (let at = sentence.indexOf(matched); at !== -1; at = sentence.indexOf(matched, at + 1)) {
    if (best === -1 || Math.abs(at - start) + Math.abs(at - expected) < Math.abs(best - start) + Math.abs(best - expected)) best = at;
  }
  return best === -1 ? { start: 0, length: 0 } : { start: best, length: matched.length };
}
