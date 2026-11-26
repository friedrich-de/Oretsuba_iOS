// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The contributors to friedrich-de/Oretsuba_iOS
//
// This file tests the dictionary popup of the Oretsuba port. It is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details: its text is the file LICENSE in engine/resources/mining.

// The popup page's own parts that need no browser (engine/resources/mining/page): what a card
// gets from a word, furigana, definitions as HTML, and the bridge to the host.
//
//   node --test engine/tests/page/*.test.mjs
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { furiganaOf } from '../../resources/mining/page/furigana.js';
import { definitionEntries, meaningHtml } from '../../resources/mining/page/glossary-html.js';
import { hostOf, KitError } from '../../resources/mining/page/host.js';
import { placeInSentence, wordOf } from '../../resources/mining/page/word.js';

function result(overrides = {}) {
  return {
    matched: '食べられなかった',
    deinflected: '食べる',
    trace: [{ name: '-た' }],
    term: { expression: '食べる', reading: 'たべる', rules: 'v1', glossaries: [], frequencies: [], pitches: [], ...overrides },
  };
}

function glossary(dictionary, entries) {
  return { dictionary, glossary: JSON.stringify(entries), definitionTags: '', termTags: '', tags: [] };
}

test('furigana goes over each run of kanji, the kana between them left as they are', () => {
  assert.equal(furiganaOf('食べる', 'たべる'), '食[た]べる');
  assert.equal(furiganaOf('食べ物', 'たべもの'), '食[た]べ 物[もの]');
  assert.equal(furiganaOf('お茶', 'おちゃ'), 'お 茶[ちゃ]');
  assert.equal(furiganaOf('昨日', 'きのう'), '昨日[きのう]');
});

test('furigana compares kana however each is written', () => {
  assert.equal(furiganaOf('使いコナす', 'つかいこなす'), '使[つか]いコナす');
  assert.equal(furiganaOf('焼きそば', 'ヤキソバ'), '焼[ヤ]きそば');
});

test('furigana goes over the whole word when the reading cannot be shared out', () => {
  assert.equal(furiganaOf('今日は', 'きょうわ'), '今日は[きょうわ]');
});

test('furigana is the word alone when it has no kanji or no other reading', () => {
  assert.equal(furiganaOf('ありがとう', 'ありがとう'), 'ありがとう');
  assert.equal(furiganaOf('テレビ', 'てれび'), 'テレビ');
  assert.equal(furiganaOf('猫', ''), '猫');
});

test('a definition keeps the structure, the dictionary marks and the styles of structured content', () => {
  const html = definitionEntries(
    JSON.stringify([
      {
        type: 'structured-content',
        content: [
          { tag: 'span', data: { content: 'part-of-speech' }, content: 'verb' },
          {
            tag: 'ul',
            style: { marginLeft: 1, fontWeight: 'bold', listStyleType: 'circle' },
            content: [{ tag: 'li', lang: 'en', content: ['to eat', { tag: 'br' }, 'to live on'] }],
          },
          { tag: 'table', content: { tag: 'tr', content: { tag: 'td', colSpan: 2, content: 'x' } } },
        ],
      },
    ]),
  );
  assert.deepEqual(html, [
    '<span data-sc-content="part-of-speech">verb</span>' +
      '<ul style="font-weight: bold; margin-left: 1em; list-style-type: circle"><li lang="en">to eat<br>to live on</li></ul>' +
      '<table><tr><td colspan="2">x</td></tr></table>',
  ]);
});

test('a definition leaves out pictures, links into the dictionary, unknown tags and unsafe styles', () => {
  const html = definitionEntries(
    JSON.stringify([
      {
        type: 'structured-content',
        content: [
          { tag: 'img', path: 'img/bowl.png' },
          { tag: 'a', href: '?query=食う', content: '食う' },
          { tag: 'a', href: 'https://example.org/"x', content: 'more' },
          { tag: 'script', content: 'alert(1)' },
          { tag: 'span', style: { background: 'url(https://example.org/x)', color: 'red;x' } },
          { tag: 'span', data: { 'x"y': 'z', note: '<b>"' }, content: '<b>' },
        ],
      },
      { type: 'image', path: 'img/bowl.png' },
      ['食う', ['v5']],
    ]),
  );
  assert.deepEqual(html, [
    '<span>食う</span><a href="https://example.org/&quot;x">more</a><span></span>' +
      '<span data-sc-note="&lt;b&gt;&quot;">&lt;b&gt;</span>',
  ]);
});

test('a definition reads plain text, the text type and text that is not JSON', () => {
  assert.deepEqual(definitionEntries('["to eat", {"type": "text", "text": "a\\nb"}, ""]'), ['to eat', 'a<br>b']);
  assert.deepEqual(definitionEntries('<not json>'), ['&lt;not json&gt;']);
});

test('the meaning on a card is the definitions of the first dictionary only', () => {
  const word = result({
    glossaries: [glossary('Words', ['to eat']), glossary('Other', ['to consume']), glossary('Words', ['to live on', 'to make a living'])],
  });
  assert.equal(
    meaningHtml(word),
    '<div class="yomitan-glossary" style="text-align: left"><ol><li>to eat</li>' +
      '<li><ul><li>to live on</li><li>to make a living</li></ul></li></ol></div>',
  );
  assert.equal(meaningHtml(result({ glossaries: [glossary('Words', ['to eat'])] })), '<div class="yomitan-glossary" style="text-align: left">to eat</div>');
  assert.equal(meaningHtml(result()), '');
});

test('a word for a card has the word, its reading, furigana, frequency, pitch and place in the sentence', () => {
  const word = wordOf(
    result({
      glossaries: [glossary('Words', ['to eat'])],
      frequencies: [
        { dictionary: 'Empty', frequencies: [] },
        { dictionary: 'Freq', frequencies: [{ value: 512.4, displayValue: '512㋕' }, { value: 9 }] },
      ],
      pitches: [{ dictionary: 'Pitch', pitches: [{ position: 2 }, { position: 0 }, { position: 2 }] }],
    }),
    { start: 3, length: 8 },
    'to eat',
  );
  assert.deepEqual(word, {
    expression: '食べる',
    reading: 'たべる',
    furigana: '食[た]べる',
    meaning: '<div class="yomitan-glossary" style="text-align: left">to eat</div>',
    frequency: '512',
    pitch: '2, 0',
    pitchCategories: 'kifuku,heiban',
    selection: 'to eat',
    at: { start: 3, length: 8 },
  });
  const bare = wordOf(result());
  assert.deepEqual([bare.frequency, bare.pitch, bare.pitchCategories, bare.at], ['', '', '', { start: 0, length: 0 }]);
});

test('pitch patterns are named as Yomitan names them, for Lapis to colour the word by', () => {
  const categories = (expression, reading, rules, positions) =>
    wordOf(result({ expression, reading, rules, pitches: [{ dictionary: 'Pitch', pitches: positions.map((position) => ({ position })) }] }))
      .pitchCategories;
  assert.equal(categories('箸', 'はし', '', [1]), 'atamadaka');
  assert.equal(categories('橋', 'はし', '', [2]), 'odaka');
  assert.equal(categories('お菓子', 'おかし', '', [2]), 'nakadaka');
  assert.equal(categories('桜', 'さくら', '', [0]), 'heiban');
  // A small ゃ is part of the mora before it, a small っ a mora of its own.
  assert.equal(categories('ちゃっかり', 'ちゃっかり', '', [3, 4]), 'nakadaka,odaka');
  // A verb or an adjective whose pitch falls is kifuku, wherever it falls.
  assert.equal(categories('食べる', 'たべる', 'v1', [2]), 'kifuku');
  assert.equal(categories('高い', 'たかい', 'adj-i', [2, 0]), 'kifuku,heiban');
  // A noun that takes する is named as a noun.
  assert.equal(categories('勉強', 'べんきょう', 'vs n', [0]), 'heiban');
  assert.equal(categories('料理', 'りょうり', 'vs n', [1]), 'atamadaka');
});

test('a word of the page of text is found in the sentence the card gets', () => {
  // The same text: the word is where it was tapped.
  assert.deepEqual(placeInSentence('昨日は晩ご飯を食べた。', '昨日は晩ご飯を食べた。', 7, '食べた'), { start: 7, length: 3 });
  // The box holds brackets the sentence does not: the word is found one earlier.
  assert.deepEqual(placeInSentence('昨日は晩ご飯を食べた。', '「昨日は晩ご飯を食べた。」', 8, '食べた'), { start: 7, length: 3 });
  // A word said twice: the one nearest to where it was tapped.
  assert.deepEqual(placeInSentence('食べたら食べた。', '食べたら食べた。', 4, '食べた'), { start: 4, length: 3 });
  assert.deepEqual(placeInSentence('食べたら食べた。', '食べたら食べた。', 0, '食べた'), { start: 0, length: 3 });
  // A character outside the basic plane takes two units.
  assert.deepEqual(placeInSentence('𠮷野家で食べた。', '𠮷野家で食べた。', 5, '食べた'), { start: 5, length: 3 });
  // A word that is not in the sentence is not marked.
  assert.deepEqual(placeInSentence('別の文。', '食べた。', 0, '食べた'), { start: 0, length: 0 });
  assert.deepEqual(placeInSentence('', '食べた。', 0, '食べた'), { start: 0, length: 0 });
});

test('the page reaches the kit through whatever the host offers', async () => {
  // An app's web view that answers a message.
  const asked = [];
  const apple = hostOf({
    webkit: { messageHandlers: { oreKit: { postMessage: async (message) => (asked.push(message), '{"ok":true,"result":{"direct":true}}') } } },
  });
  assert.deepEqual(await apple.call('page.init'), { direct: true });
  assert.deepEqual(asked, [{ method: 'page.init', params: '{}' }]);

  // A host that set its own bridge, answering at once.
  const own = hostOf({ oreHost: { call: (method, params) => JSON.stringify({ ok: true, result: [method, JSON.parse(params)] }) } });
  assert.deepEqual(await own.call('lookup', { text: '食べる' }), ['lookup', { text: '食べる' }]);

  // A web server.
  const fetched = [];
  const web = hostOf({
    fetch: async (url, options) => (fetched.push([url, options.method, options.body]), { text: async () => '{"ok":false,"error":"Anki is not running.","kind":"unreachable"}' }),
  });
  await assert.rejects(web.call('card.add', { word: {} }), (error) => error instanceof KitError && error.kind === 'unreachable' && error.message === 'Anki is not running.');
  assert.deepEqual(fetched, [['kit/call/card.add', 'POST', '{"word":{}}']]);
  assert.equal(web.dataUrl('dict.media', { dictionary: 'Words', path: 'img/a b.png' }), 'kit/data/dict.media?p=%7B%22dictionary%22%3A%22Words%22%2C%22path%22%3A%22img%2Fa%20b.png%22%7D');

  // A host that fails says so in the kit's way.
  const broken = hostOf({ oreHost: { call: () => 'not json' } });
  await assert.rejects(broken.call('page.init'), (error) => error instanceof KitError && error.kind === 'internal');
});
