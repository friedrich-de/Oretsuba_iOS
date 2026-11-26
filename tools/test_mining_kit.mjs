#!/usr/bin/env node
// Mining in the game without a browser (docs/MINING.md): the engine and the mining kit on the
// test host with the game, a made-up AnkiConnect and audio source, and a small dictionary. It
// does by hand what the popup's page does: a tap on the text, the lookup, the card.
//
//   node tools/test_mining_kit.mjs SET_UP
//
// Checks which character a tap hits, the line's picture and voice, the card Anki gets, and that
// the game stands still while the popup would be open. With VOICE.LPK in the folder the card
// carries the line's voice: as AAC from a Mac's host, as the game's Ogg from the Linux one.
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { placeInSentence, wordOf } from '../engine/resources/mining/page/word.js';
import { fakeAnki, fakeAudioSource } from './mining_page/fakes.mjs';
import { startHarness } from './mining_page/harness.mjs';
import { firstJapanese, firstLine, importDictionary, setUpKit } from './mining_page/setup.mjs';

if (!process.argv[2]) {
  console.error('usage: node tools/test_mining_kit.mjs SET_UP');
  process.exit(2);
}
const game = resolve(process.argv[2]);
const voiced = existsSync(join(game, 'VOICE.LPK'));

let checks = 0;
function check(condition, what) {
  checks += 1;
  assert.ok(condition, what);
}

const scratch = mkdtempSync(join(tmpdir(), 'ore-mining-kit-'));
const anki = await fakeAnki();
const audio = await fakeAudioSource();
const harness = await startHarness({ kit: join(scratch, 'kit'), data: game, save: join(scratch, 'save') });
const standingNow = async () => (await harness.command('text')).standing;
const fileOf = async (method) => {
  const found = await harness.command(`data ${method} {}`);
  return found.ok === true ? { type: found.type, bytes: Buffer.from(found.base64, 'base64') } : null;
};
try {
  // Before the kit is set up a tap on the text is a click like any other.
  await setUpKit(harness, scratch, anki, audio, check);
  await harness.kit('settings.set', { direct: false });
  const line = await firstLine(harness);
  console.log(`  line: ${line.speaker ? `【${line.speaker}】` : ''}${line.sentence}`);
  const characters = [...line.text];
  check(characters.length === line.rects.length && characters.length > 3, 'every character of the page of text has its cell');
  check(line.rects.every(([x, y, w, h]) => x >= line.box[0] && y >= line.box[1] && x + w <= line.box[0] + line.box[2] && y + h <= line.box[1] + line.box[3]), 'all cells lie in the text box');
  check(line.index === -1 && line.line === 0, 'reading the line hands nothing to the kit');

  const start = firstJapanese(line.text);
  const word = characters.slice(start, start + 2).join('');
  await importDictionary(harness, scratch, [{ expression: word, reading: '', meanings: ['a word of this line'] }]);
  const [x, y, w, h] = line.rects[start];
  const centre = `${Math.round(x + w / 2)} ${Math.round(y + h / 2)}`;

  // The setting "direct" decides whether the text takes taps.
  check((await harness.kit('page.init')).direct === false, 'mining in the game can be off');
  await harness.kit('settings.set', { direct: true });

  const tapped = await harness.command(`tap ${centre}`);
  check(tapped.tap === 1, 'a tap on a character of the line is a word, not a click');
  check(tapped.popup.index === start && tapped.popup.line > 0, 'it names the character and the line');
  check(tapped.popup.sentence === line.sentence && tapped.popup.speaker === line.speaker, 'with the sentence and who says it');
  check((await harness.command('state')).hold === true, 'and the game is held');

  // A tap a few pixels beside a character still counts for it.
  await harness.command('hold 0');
  const near = await harness.command(`tap ${x + w + 2} ${y + h + 4}`);
  check(near.tap === 1 && Math.abs(near.popup.index - start) <= 1, 'a finger a few pixels off still hits');
  check(near.popup.line === tapped.popup.line, 'the same line tapped again is the same line to the kit');

  // What the kit holds of the line.
  const picture = await fileOf('line.picture');
  check(picture && picture.type === 'image/jpeg' && picture.bytes[0] === 0xff && picture.bytes[1] === 0xd8 && picture.bytes.length > 10000, 'the kit holds the screen as a JPEG');
  const voice = await fileOf('line.voice');
  if (voiced) check(voice && voice.bytes.subarray(0, 4).toString('latin1') === 'OggS', 'and the line\'s voice as the game has it');
  else console.log('  no VOICE.LPK: the line is silent');
  const held = await harness.kit('line.get');
  check(held.sentence === line.sentence, 'and its sentence');

  // The lookup the page would make, and the card.
  const found = await harness.kit('lookup', { text: characters.slice(start, start + 40).join('') });
  check(found.results[0]?.matched === word && found.results[0].term.expression === word, 'the word that starts at the character is found');
  const result = found.results[0];
  const before = characters.slice(0, start).join('').length;
  const forCard = wordOf(result, placeInSentence(line.sentence, line.text, before, result.matched));
  check(line.sentence.slice(forCard.at.start, forCard.at.start + forCard.at.length) === word, 'and placed in the sentence');
  check((await harness.kit('card.known', { words: [forCard] })).known[0] === false, 'Anki has no card of it');

  const made = await harness.kit('card.add', { word: forCard, audioChoice: '' });
  check(made.handedOver === false && made.noteId > 0, 'the card is made');
  const note = anki.notes.at(-1).note;
  check(note.fields.Expression === word, 'with the word');
  const sentence = line.sentence;
  check(note.fields.Sentence === `${sentence.slice(0, forCard.at.start)}<b>${word}</b>${sentence.slice(forCard.at.start + word.length)}`, `the sentence with the word in bold (${note.fields.Sentence})`);
  const pictureName = /^<img src="(oretsuba_[0-9a-f]+_[0-9a-f]+\.jpg)">$/.exec(note.fields.Picture ?? '')?.[1];
  check(pictureName && anki.media.get(pictureName)?.equals(picture.bytes), `the screen (${note.fields.Picture})`);
  check((note.fields.MiscInfo ?? '').includes('俺たちに翼はない'), `the game's title as the source (${note.fields.MiscInfo})`);
  check(/^\[sound:vn-miner_word_.+\.wav\]$/.test(note.fields.ExpressionAudio), 'the word\'s sound');
  if (voiced) {
    const voiceName = /^\[sound:(oretsuba_[0-9a-f]+_[0-9a-f]+\.(m4a|ogg))\]$/.exec(note.fields.SentenceAudio ?? '');
    check(voiceName !== null, `the line's voice (${note.fields.SentenceAudio})`);
    const stored = anki.media.get(voiceName[1]);
    if (voiceName[2] === 'm4a') check(stored.subarray(4, 8).toString('latin1') === 'ftyp' && stored.length > 2000, 'as AAC in an MPEG-4 file, which every Anki plays');
    else check(stored.equals(voice.bytes), 'as the game\'s Ogg file');
    console.log(`  voice: ${voiceName[1]}, ${stored.length} bytes`);
  } else {
    check((note.fields.SentenceAudio ?? '') === '', 'no voice where the line has none');
  }
  check((await harness.kit('card.known', { words: [forCard] })).known[0] === true, 'Anki knows the word now');
  await assert.rejects(harness.kit('card.add', { word: forCard }), (error) => error.kind === 'duplicate');
  check(true, 'and a second card of it is refused as a duplicate');

  // While the popup is open nothing reaches the game.
  await harness.command(`tap ${centre}`);
  for (const press of ['click 400 200', 'key 13', 'key 17 down', 'wheel -120', 'command 5', 'rclick 400 200']) {
    await harness.command(press);
    await harness.command('run 60');
  }
  await harness.command('key 17 up');
  await harness.command('hold 0');
  await harness.command('run 240');
  const after = await standingNow();
  check(after !== null && after.sentence === line.sentence, 'clicks, keys, the wheel and commands during the hold do not reach the game');

  // In the text box beside the characters nothing happens; outside it the game reads on.
  const [bx, by, bw, bh] = line.box;
  const beside = await harness.command(`tap ${bx + Math.round(bw / 2)} ${by + bh - 30}`);
  if (beside.tap === 2) {
    await harness.command('run 120');
    check((await standingNow())?.sentence === line.sentence, 'a tap in the text box beside the characters does nothing');
  } else {
    console.log(`  the place beside the text was taken as ${beside.tap}`);
  }
  const outside = await harness.command('tap 400 200');
  check(outside.tap === 0, 'a tap outside the text box is the game\'s');
  let next = null;
  for (let i = 0; i < 60 && next === null; i++) {
    await harness.command('run 30');
    const now = await standingNow();
    if (now && now.sentence !== line.sentence && now.text.length > 0) next = now;
    else if (i % 6 === 5) await harness.command('click 400 200');
  }
  check(next !== null, 'and the game reads on');
  console.log(`  next: ${next.sentence}`);
  const nextStart = Math.max(0, firstJapanese(next.text));
  const [nx, ny, nw, nh] = next.rects[nextStart];
  const again = await harness.command(`tap ${Math.round(nx + nw / 2)} ${Math.round(ny + nh / 2)}`);
  check(again.tap === 1 && again.popup.line > tapped.popup.line, 'the next line is a new line to the kit');
  const nextPicture = await fileOf('line.picture');
  check(nextPicture && !nextPicture.bytes.equals(picture.bytes), 'with a picture of its own');
  await harness.command('hold 0');

  // With mining in the game off, the text takes clicks again.
  await harness.kit('settings.set', { direct: false });
  check((await harness.command(`tap ${Math.round(nx + nw / 2)} ${Math.round(ny + nh / 2)}`)).tap === 0, 'with the setting off a tap on the text is a click again');

  console.log(`ok: ${checks} checks`);
} finally {
  await harness.stop();
  await anki.stop();
  await audio.stop();
  rmSync(scratch, { recursive: true, force: true });
}
