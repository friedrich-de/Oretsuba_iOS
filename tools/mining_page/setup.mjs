// What the tests of mining in the game share: setting the kit up as the launcher's settings
// would, and reading the game up to its first line.
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

import { smallDictionary } from './fakes.mjs';

export async function until(what, test, seconds = 20) {
  const end = Date.now() + seconds * 1000;
  for (;;) {
    const value = await test();
    if (value) return value;
    if (Date.now() > end) throw new Error(`Waited in vain for ${what}`);
    await new Promise((done) => setTimeout(done, 50));
  }
}

/** Imports the small dictionary, with words of the test's own, in place of the one that is there. */
export async function importDictionary(harness, scratch, extraWords = []) {
  for (const dictionary of (await harness.kit('dict.list')).dictionaries) await harness.kit('dict.remove', { id: dictionary.id });
  const zip = join(scratch, `words-${Date.now()}.zip`);
  writeFileSync(zip, smallDictionary(extraWords));
  await harness.kit('dict.import', { path: zip, name: 'words.zip' });
  return until('the dictionary', async () => {
    const now = await harness.kit('dict.list');
    if (now.import.state === 'failed') throw new Error(now.import.error);
    return now.import.state !== 'importing' && now.import.state !== 'downloading' && now.dictionaries.length === 1 ? now : null;
  });
}

/** A dictionary, Anki with the Lapis defaults, an audio source, and taps on the text looking words up. */
export async function setUpKit(harness, scratch, anki, audio, check) {
  const list = await importDictionary(harness, scratch);
  check(list.dictionaries[0].title === 'Test Words', 'the dictionary is imported');
  await harness.kit('settings.set', { direct: true, anki: { address: anki.url } });
  const found = await harness.kit('anki.check', { adopt: true });
  check(found.lapis?.noteType === 'Lapis', 'the Lapis note type is found');
  check(found.settings.anki.noteType === 'Lapis', 'and taken as the note type');
  const roles = Object.fromEntries(found.settings.anki.fields.map(({ field, role }) => [field, role]));
  check(roles.Expression === 'word' && roles.Sentence === 'sentence' && roles.Picture === 'picture', 'with the Lapis fields filled as usual');
  check(roles.SentenceAudio === 'sound' && roles.ExpressionAudio === 'wordAudio', 'the line\'s voice and the word\'s sound among them');
  await harness.kit('anki.choose', { deck: 'Mining' });
  await harness.kit('secrets.set', { audioAddress: audio.address });
  const sounds = await harness.kit('audio.check');
  check(sounds.names.length === 2, 'the audio source answers with its sounds');
  const page = await harness.kit('page.init');
  check(page.direct === true && page.cards.possible === true && page.wordAudio === true, 'the kit is ready for the popup');
}

/** START, then through the opening to the first line of the story. The line as the engine tells it. */
export async function firstLine(harness) {
  await harness.command('run 900');
  await harness.command('click 730 384');
  await harness.command('run 495');
  for (let clicks = 0; clicks < 200; clicks++) {
    await harness.command('click 400 300');
    await harness.command('run 145');
    const now = await harness.command('text');
    if (now.ok !== true) throw new Error('The game stopped on the way to the first line.');
    if (now.standing && now.standing.text.length >= 4) return now.standing;
  }
  throw new Error('The game did not reach a line of the story.');
}

/** The first character of a line that a Japanese word can start at, or -1. */
export function firstJapanese(text) {
  return [...text].findIndex((character) => /[぀-ヿ一-鿿]/u.test(character));
}
