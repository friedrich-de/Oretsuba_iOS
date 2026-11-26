#!/usr/bin/env node
// Made-up neighbours for trying mining in the app by hand (docs/MINING.md): an AnkiConnect with
// the Lapis note type, an audio source and a small dictionary to download, all on this computer.
// The iOS Simulator reaches them at the addresses printed; none of it holds anything real.
//
//   node tools/mining_page/neighbours.mjs [EXPRESSION:READING[:RULES] ...]
//
// The words named are added to the dictionary (RULES as Yomitan has them: v1, v5, adj-i, n).
// Every card the AnkiConnect is given is printed with its fields and the sizes of its files.
import { createServer } from 'node:http';

import { fakeAnki, fakeAudioSource, smallDictionary } from './fakes.mjs';

const words = process.argv.slice(2).map((word) => {
  const [expression, reading = '', rules = 'n'] = word.split(':');
  return { expression, reading, rules, meanings: [`a made-up meaning of ${expression}`] };
});

const anki = await fakeAnki(8765);
const audio = await fakeAudioSource(8766);
const zip = smallDictionary(words);
const dictionary = createServer((request, answer) => {
  answer.writeHead(200, { 'Content-Type': 'application/zip', 'Content-Length': zip.length });
  answer.end(zip);
});
await new Promise((done) => dictionary.listen(8767, '127.0.0.1', done));

console.log(`AnkiConnect:  ${anki.url}`);
console.log(`word audio:   ${audio.address}`);
console.log(`a dictionary: http://127.0.0.1:8767/test-words.zip`);

let told = 0;
setInterval(() => {
  for (; told < anki.notes.length; told++) {
    const { note } = anki.notes[told];
    console.log(`card in ${note.deckName} (${note.modelName}), tags ${JSON.stringify(note.tags ?? [])}`);
    for (const [field, value] of Object.entries(note.fields)) {
      if (value === '') continue;
      const file = /(?:src="|sound:)([^"\]]+)/.exec(value)?.[1];
      const size = file && anki.media.has(file) ? ` (${anki.media.get(file).length} bytes)` : '';
      console.log(`  ${field}: ${value.length > 160 ? `${value.slice(0, 160)}…` : value}${size}`);
    }
  }
}, 500);
