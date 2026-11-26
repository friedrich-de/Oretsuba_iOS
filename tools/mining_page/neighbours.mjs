#!/usr/bin/env node
// Made-up neighbours for trying mining in the app by hand (docs/MINING.md): an AnkiConnect with
// the Lapis note type, an audio source and a small dictionary to download, all on this computer.
// The iOS Simulator reaches them at the addresses printed, an Android emulator with 10.0.2.2 in
// place of 127.0.0.1; none of it holds anything real.
//
//   node tools/mining_page/neighbours.mjs [EXPRESSION:READING[:RULES] ...]
//
// The words named are added to the dictionary (RULES as Yomitan has them: v1, v5, adj-i, n).
// Every card the AnkiConnect is given is printed with its fields and the sizes of its files.
// ORE_NEIGHBOUR_PORT is the first of the three ports (8765 without it, which is also where a real
// Anki's AnkiConnect listens); ORE_NEIGHBOUR_MEDIA is a folder that gets the cards' files;
// ORE_NEIGHBOUR_KEY is a key the AnkiConnect then asks for, as the add-on does with an apiKey.
import { mkdirSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { basename, join } from 'node:path';

import { fakeAnki, fakeAudioSource, smallDictionary } from './fakes.mjs';

const words = process.argv.slice(2).map((word) => {
  const [expression, reading = '', rules = 'n'] = word.split(':');
  return { expression, reading, rules, meanings: [`a made-up meaning of ${expression}`] };
});

const port = Number(process.env.ORE_NEIGHBOUR_PORT ?? 8765);
const mediaFolder = process.env.ORE_NEIGHBOUR_MEDIA ?? '';
if (mediaFolder !== '') mkdirSync(mediaFolder, { recursive: true });

const anki = await fakeAnki(port, process.env.ORE_NEIGHBOUR_KEY ?? '');
const audio = await fakeAudioSource(port + 1);
const zip = smallDictionary(words);
const dictionary = createServer((request, answer) => {
  answer.writeHead(200, { 'Content-Type': 'application/zip', 'Content-Length': zip.length });
  answer.end(zip);
});
await new Promise((done) => dictionary.listen(port + 2, '127.0.0.1', done));

console.log(`AnkiConnect:  ${anki.url}`);
console.log(`word audio:   ${audio.address}`);
console.log(`a dictionary: http://127.0.0.1:${port + 2}/test-words.zip`);

let told = 0;
const kept = new Set();
setInterval(() => {
  if (mediaFolder !== '') {
    for (const [name, data] of anki.media) {
      if (kept.has(name)) continue;
      kept.add(name);
      writeFileSync(join(mediaFolder, basename(name)), data);
    }
  }
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
