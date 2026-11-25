// Made-up neighbours for the mining kit's tests: an AnkiConnect, an audio source and a small
// dictionary in Yomitan's format. None of it holds anything of a real server.
import { createServer } from 'node:http';
import { crc32 } from 'node:zlib';

/** The fields of the Lapis note type, in its order. */
export const LAPIS_FIELDS = [
  'Expression', 'ExpressionFurigana', 'ExpressionReading', 'ExpressionAudio', 'SelectionText', 'MainDefinition',
  'DefinitionPicture', 'Sentence', 'SentenceFurigana', 'SentenceAudio', 'Picture', 'Glossary', 'Hint',
  'IsWordAndSentenceCard', 'IsClickCard', 'IsSentenceCard', 'IsAudioCard', 'PitchPosition', 'PitchCategories',
  'Frequency', 'FreqSort', 'MiscInfo',
];

function listen(handler) {
  const server = createServer(handler);
  return new Promise((done) =>
    server.listen(0, '127.0.0.1', () =>
      done({
        url: `http://127.0.0.1:${server.address().port}`,
        stop: () => new Promise((closed) => (server.closeAllConnections?.(), server.close(closed))),
      }),
    ),
  );
}

async function bodyOf(request) {
  const parts = [];
  for await (const part of request) parts.push(part);
  return Buffer.concat(parts);
}

/** An AnkiConnect with a Lapis note type and two decks. What it was asked and what it holds can be read. */
export async function fakeAnki() {
  const anki = { actions: [], notes: [], media: new Map(), decks: ['Default', 'Mining'], models: { Basic: ['Front', 'Back'], Lapis: LAPIS_FIELDS } };
  let nextId = 1700000000000;
  const firstField = (note) => String(note.fields?.[anki.models[note.modelName]?.[0]] ?? '').trim();
  const refusal = (note) => {
    if (!anki.models[note.modelName]) return `model was not found: ${note.modelName}`;
    if (!anki.decks.includes(note.deckName)) return `deck was not found: ${note.deckName}`;
    if (firstField(note) === '') return 'cannot create note because it is empty';
    if (anki.notes.some((known) => known.note.deckName === note.deckName && firstField(known.note) === firstField(note))) {
      return 'cannot create note because it is a duplicate';
    }
    return '';
  };
  const server = await listen(async (request, answer) => {
    let message = {};
    try {
      message = JSON.parse((await bodyOf(request)).toString('utf8'));
    } catch {
      // answered below as an action it does not know
    }
    const { action, params = {} } = message;
    anki.actions.push(action);
    let result = null;
    let error = null;
    if (action === 'version') result = 6;
    else if (action === 'deckNames') result = anki.decks;
    else if (action === 'modelNames') result = Object.keys(anki.models);
    else if (action === 'modelFieldNames') {
      if (anki.models[params.modelName]) result = anki.models[params.modelName];
      else error = `model was not found: ${params.modelName}`;
    } else if (action === 'findNotes') {
      result = anki.notes.filter((known) => params.query === `note:"${known.note.modelName}"`).map((known) => known.id);
    } else if (action === 'notesInfo') result = (params.notes ?? []).map((id) => ({ noteId: id, cards: [id + 1] }));
    else if (action === 'cardsInfo') {
      result = (params.cards ?? []).flatMap((card) => anki.notes.filter((known) => known.id + 1 === card).map((known) => ({ deckName: known.note.deckName })));
    } else if (action === 'canAddNotesWithErrorDetail') {
      result = (params.notes ?? []).map((note) => {
        const why = refusal(note);
        return why === '' ? { canAdd: true } : { canAdd: false, error: why };
      });
    } else if (action === 'storeMediaFile') {
      anki.media.set(params.filename, Buffer.from(params.data ?? '', 'base64'));
      result = params.filename;
    } else if (action === 'addNote') {
      const why = refusal(params.note ?? {});
      if (why !== '') error = why;
      else {
        anki.notes.push({ id: nextId, note: params.note });
        result = nextId;
        nextId += 1000;
      }
    } else error = 'unsupported action';
    answer.writeHead(200, { 'Content-Type': 'application/json' });
    answer.end(JSON.stringify({ result, error }));
  });
  return Object.assign(anki, server);
}

/** A tenth of a second of silence as a WAV file. */
export function silence() {
  const samples = 4410;
  const wav = Buffer.alloc(44 + samples * 2);
  wav.write('RIFF', 0);
  wav.writeUInt32LE(36 + samples * 2, 4);
  wav.write('WAVEfmt ', 8);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(44100, 24);
  wav.writeUInt32LE(88200, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write('data', 36);
  wav.writeUInt32LE(samples * 2, 40);
  return wav;
}

/**
 * An audio source as Yomitan's custom ones: /list?term=…&reading=… answers with a list of two
 * sounds for every word but 友達, which it has none for. Its address for the kit is `address`.
 */
export async function fakeAudioSource() {
  const source = { asked: [], fetched: [] };
  const server = await listen((request, answer) => {
    const url = new URL(request.url, 'http://audio');
    if (url.pathname === '/list') {
      const term = url.searchParams.get('term') ?? '';
      source.asked.push(term);
      const sounds = term === '友達' ? [] : [
        { name: 'First voice', url: `/sound/one.wav?term=${encodeURIComponent(term)}` },
        { name: 'Second voice', url: `/sound/two.wav?term=${encodeURIComponent(term)}` },
      ];
      answer.writeHead(200, { 'Content-Type': 'application/json' });
      answer.end(JSON.stringify({ type: 'audioSourceList', audioSources: sounds }));
    } else if (url.pathname.startsWith('/sound/')) {
      source.fetched.push(url.pathname.slice('/sound/'.length));
      answer.writeHead(200, { 'Content-Type': 'audio/wav' });
      answer.end(silence());
    } else {
      answer.writeHead(404);
      answer.end();
    }
  });
  return Object.assign(source, server, { address: `${server.url}/list?term={term}&reading={reading}` });
}

/** A zip file of files stored as they are. */
export function zipOf(files) {
  const locals = [];
  const central = [];
  let offset = 0;
  for (const [name, content] of Object.entries(files)) {
    const data = Buffer.isBuffer(content) ? content : Buffer.from(content, 'utf8');
    const file = Buffer.from(name, 'utf8');
    const sum = crc32(data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0x0800, 6);
    local.writeUInt32LE(sum, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(file.length, 26);
    const entry = Buffer.alloc(46);
    entry.writeUInt32LE(0x02014b50, 0);
    entry.writeUInt16LE(20, 4);
    entry.writeUInt16LE(20, 6);
    entry.writeUInt16LE(0x0800, 8);
    entry.writeUInt32LE(sum, 16);
    entry.writeUInt32LE(data.length, 20);
    entry.writeUInt32LE(data.length, 24);
    entry.writeUInt16LE(file.length, 28);
    entry.writeUInt32LE(offset, 42);
    locals.push(local, file, data);
    central.push(entry, file);
    offset += 30 + file.length + data.length;
  }
  const directory = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(central.length / 2, 8);
  end.writeUInt16LE(central.length / 2, 10);
  end.writeUInt32LE(directory.length, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, directory, end]);
}

/**
 * A small dictionary in Yomitan's format: a few words with meanings, one with structured content
 * and a link to another entry, and a frequency and a pitch for 食べる.
 */
export function smallDictionary(extraWords = []) {
  const term = (expression, reading, rules, meanings, score = 0) => [expression, reading, '', rules, score, meanings, 0, ''];
  const terms = [
    term('食べる', 'たべる', 'v1', ['to eat', 'to live on'], 10),
    term('友達', 'ともだち', 'n', ['friend', 'companion']),
    term('晩ご飯', 'ばんごはん', 'n', ['dinner', 'evening meal']),
    term('昨日', 'きのう', 'n', ['yesterday']),
    term('飲む', 'のむ', 'v5', ['to drink']),
    term('ご飯', 'ごはん', 'n', [
      {
        type: 'structured-content',
        content: [
          { tag: 'div', data: { content: 'glossary' }, content: 'cooked rice; meal' },
          { tag: 'div', content: ['See also ', { tag: 'a', href: '?query=%E6%99%A9%E3%81%94%E9%A3%AF', content: '晩ご飯' }] },
        ],
      },
    ]),
    ...extraWords.map((word) => term(word.expression, word.reading, word.rules ?? 'n', word.meanings)),
  ];
  const metas = [
    ['食べる', 'freq', { reading: 'たべる', frequency: 512 }],
    ['食べる', 'pitch', { reading: 'たべる', pitches: [{ position: 2 }] }],
  ];
  return zipOf({
    'index.json': JSON.stringify({ title: 'Test Words', revision: '1', format: 3, sequenced: false }),
    'term_bank_1.json': JSON.stringify(terms),
    'term_meta_bank_1.json': JSON.stringify(metas),
    'styles.css': 'div[data-sc-content="glossary"] { font-weight: bold; }',
  });
}
