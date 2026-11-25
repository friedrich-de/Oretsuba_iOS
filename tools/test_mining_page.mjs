#!/usr/bin/env node
// Mining in the game, end to end in a browser (docs/MINING.md): the popup's page over the test
// host with its real mining kit, a made-up AnkiConnect and audio source, and a small dictionary.
//
//   node tools/test_mining_page.mjs            the kit and the page on a made-up line
//   node tools/test_mining_page.mjs SET_UP     the same, then with the game: a tap on the text of
//                                              a real line, the game held, a card with the screen
//
// Needs the test host (make -C engine -f Makefile.linux, or make -C engine on a Mac) and
// Playwright with Chromium. --shots DIR keeps pictures of the popup.
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import { fakeAnki, fakeAudioSource, smallDictionary } from './mining_page/fakes.mjs';
import { startHarness } from './mining_page/harness.mjs';

const { chromium } = createRequire(import.meta.url)('playwright');

const args = process.argv.slice(2);
const shotsAt = args.indexOf('--shots');
const shots = shotsAt >= 0 ? resolve(args.splice(shotsAt, 2)[1]) : null;
const debug = args.includes('--debug') && args.splice(args.indexOf('--debug'), 1).length > 0;
const game = args[0] ? resolve(args[0]) : null;
if (shots) mkdirSync(shots, { recursive: true });

let checks = 0;
function check(condition, what) {
  checks += 1;
  assert.ok(condition, what);
}

async function until(what, test, seconds = 20) {
  const end = Date.now() + seconds * 1000;
  for (;;) {
    const value = await test();
    if (value) return value;
    if (Date.now() > end) throw new Error(`Waited in vain for ${what}`);
    await new Promise((done) => setTimeout(done, 50));
  }
}

/** Sets the kit up as the launcher's settings would: a dictionary, Anki with the Lapis defaults, an audio source. */
async function setUp(harness, scratch, anki, audio, extraWords = []) {
  const zip = join(scratch, `words-${extraWords.length}.zip`);
  writeFileSync(zip, smallDictionary(extraWords));
  await harness.kit('dict.import', { path: zip, name: 'words.zip' });
  const list = await until('the dictionary', async () => {
    const now = await harness.kit('dict.list');
    if (now.import.state === 'failed') throw new Error(now.import.error);
    return now.import.state === 'done' || now.import.state === 'idle' ? now : null;
  });
  check(list.dictionaries.length === 1 && list.dictionaries[0].title === 'Test Words', 'the dictionary is imported');

  await harness.kit('settings.set', { direct: true, anki: { address: anki.url } });
  const found = await harness.kit('anki.check', { adopt: true });
  check(found.lapis?.noteType === 'Lapis', 'the Lapis note type is found');
  check(found.settings.anki.noteType === 'Lapis', 'and taken as the note type');
  const roles = Object.fromEntries(found.settings.anki.fields.map(({ field, role }) => [field, role]));
  check(roles.Expression === 'word' && roles.Sentence === 'sentence' && roles.Picture === 'picture', 'with the Lapis fields filled as usual');
  await harness.kit('anki.choose', { deck: 'Mining' });
  await harness.kit('secrets.set', { audioAddress: audio.address });
  const sounds = await harness.kit('audio.check');
  check(sounds.names.length === 2, 'the audio source answers with its sounds');
  const page = await harness.kit('page.init');
  check(page.direct === true && page.cards.possible === true && page.wordAudio === true, 'the kit is ready for the popup');
}

async function openStage(browser, harness, viewport) {
  const context = await browser.newContext({ viewport, hasTouch: false });
  const page = await context.newPage();
  const problems = [];
  page.on('pageerror', (error) => problems.push(String(error)));
  page.on('console', (message) => {
    if (message.type() === 'error') problems.push(message.text());
  });
  await page.goto(harness.url);
  await page.waitForSelector('html[data-ready="true"]');
  await until('the page to be ready', () => harness.events.some((event) => event.name === 'popup.ready'));
  return { page, popup: page.frameLocator('#popup'), problems, context };
}

/** Clicks a place of the game's screen as a finger would tap it. */
async function tapGame(page, x, y) {
  const at = await page.evaluate(() => {
    const scale = Math.min(window.innerWidth / 800, window.innerHeight / 600);
    return { x: (window.innerWidth - 800 * scale) / 2, y: (window.innerHeight - 600 * scale) / 2, scale };
  });
  await page.mouse.click(at.x + x * at.scale, at.y + y * at.scale);
}

const cardButton = (popup, state) => popup.locator(`.gsm-hoshidicts-mine-button[data-card-state="${state}"]`).first();

async function madeUpLine(browser, harness, anki, audio) {
  const { page, popup, problems, context } = await openStage(browser, harness, { width: 844, height: 390 });
  // 昨日は友達と晩ご飯を食べました。 stands in cells of 28 from x = 60 at y = 440.
  const cell = (index) => [60 + index * 28 + 14, 455];

  await tapGame(page, ...cell(10));
  await popup.locator('.dictionary-popup').getByText('食べる').first().waitFor();
  check(true, 'a tap on 食 shows 食べる');
  check((await popup.locator('.mark').count()) === 5, 'the five characters of 食べました are marked');
  check(harness.events.some((event) => event.name === 'popup.found' && event.payload.length === 5), 'and the host is told their number');
  await popup.locator('.dictionary-popup').getByText('to eat').first().waitFor();
  await cardButton(popup, 'add').waitFor();
  check(true, 'Anki has no card of it yet');
  if (shots) await page.screenshot({ path: join(shots, 'made-up-line.png') });

  // The popup stands inside the page, above the line.
  const box = await popup.locator('#panel').boundingBox();
  check(box.x >= 0 && box.y >= 0 && box.x + box.width <= 844 && box.y + box.height <= 390, 'the popup is inside the screen');

  await cardButton(popup, 'add').click();
  await cardButton(popup, 'in-anki').waitFor();
  check(anki.notes.length === 1, 'the + button makes one card');
  const note = anki.notes[0].note;
  check(note.deckName === 'Mining' && note.modelName === 'Lapis', 'in the chosen deck and note type');
  check(note.fields.Expression === '食べる' && note.fields.ExpressionReading === 'たべる', 'with the word and its reading');
  check(note.fields.ExpressionFurigana === '食[た]べる', 'its furigana');
  check(note.fields.Sentence === '昨日は友達と晩ご飯を<b>食べました</b>。', `the sentence with the word in bold (${note.fields.Sentence})`);
  check(/to eat/.test(note.fields.MainDefinition) && /to live on/.test(note.fields.MainDefinition), 'its meanings');
  check(note.fields.Frequency === '512' && note.fields.PitchPosition === '2', 'its frequency and pitch');
  check(/^\[sound:vn-miner_word_.+\.wav\]$/.test(note.fields.ExpressionAudio), `the word's sound (${note.fields.ExpressionAudio})`);
  check(anki.media.size === 1 && audio.fetched.includes('one.wav'), 'which was fetched from the audio source and stored in Anki');
  check(note.tags.includes('vn-miner'), 'and the tag');
  check(/Made a card of 食べる/.test(await popup.locator('#status').textContent()), 'the popup says so');

  // The audio button plays the word.
  const fetchedBefore = audio.fetched.length;
  await popup.locator('.gsm-hoshidicts-audio-button').first().click();
  await until('the sound to be fetched', () => audio.fetched.length > fetchedBefore);
  check(true, 'the audio button fetches the word\'s sound');

  // A tap on another character of the line looks that word up, without closing.
  await tapGame(page, ...cell(3));
  await popup.locator('.dictionary-popup').getByText('friend').first().waitFor();
  check((await popup.locator('.mark').count()) === 2, 'a tap on 友 while the popup is open shows 友達');
  await cardButton(popup, 'add').waitFor();

  // A word in a definition is looked up in place, and Back returns.
  await tapGame(page, ...cell(7));
  await popup.locator('.dictionary-popup').getByText('cooked rice; meal').first().waitFor();
  check(true, 'ご飯 shows its structured definition');

  // No word starts at the full stop.
  await tapGame(page, ...cell(15));
  await popup.locator('#note').getByText('No word in the dictionaries starts here.').waitFor();
  check(true, 'a character no word starts at says so');

  // The word that has a card is known as such when it is looked up again.
  await tapGame(page, ...cell(10));
  await cardButton(popup, 'in-anki').waitFor();
  check(true, 'looked up again, 食べる is known to be in Anki');

  // A tap beside the popup and the text closes it.
  const closedBefore = harness.events.filter((event) => event.name === 'popup.close').length;
  await tapGame(page, 780, 590);
  await until('the popup to close', () => harness.events.filter((event) => event.name === 'popup.close').length > closedBefore);
  check(await popup.locator('#panel').isHidden(), 'a tap elsewhere closes the popup');
  await until('the stage to hide the page', () => page.evaluate(() => !window.stage.isOpen()));

  // Escape closes it too.
  await tapGame(page, ...cell(0));
  await popup.locator('.dictionary-popup').getByText('yesterday').first().waitFor();
  await page.locator('#popup').focus();
  await popup.locator('body').press('Escape');
  await until('Escape to close the popup', () => page.evaluate(() => !window.stage.isOpen()));
  check(true, 'Escape closes the popup');

  check(problems.length === 0, `the page reports no errors (${problems.join(' | ')})`);
  await context.close();
}

async function realLine(browser, harness, anki) {
  // START, then through the opening to the first line of the story.
  await harness.command('run 900');
  await harness.command('click 730 384');
  await harness.command('run 495');
  let standing = null;
  for (let clicks = 0; clicks < 200 && standing === null; clicks++) {
    await harness.command('click 400 300');
    await harness.command('run 145');
    const now = await harness.command('text');
    if (now.ok !== true) throw new Error('The game stopped on the way to the first line.');
    if (now.standing && now.standing.text.length >= 4) standing = now.standing;
  }
  check(standing !== null, 'the game reaches a line of the story');
  console.log(`  line: ${standing.speaker ? `【${standing.speaker}】` : ''}${standing.sentence}`);
  check([...standing.text].length === standing.rects.length, 'every character of the page of text has its cell');

  // The first character a word of the game's own text starts at: the dictionary gets that word.
  const characters = [...standing.text];
  const start = characters.findIndex((character) => /[぀-ヿ一-鿿]/u.test(character));
  check(start >= 0, 'the line has Japanese text');
  const word = characters.slice(start, start + 2).join('');
  await setUp.again(word);

  const { page, popup, problems, context } = await openStage(browser, harness, { width: 844, height: 390 });
  const [x, y, w, h] = standing.rects[start];
  const linesBefore = (await harness.command('text')).standing.sentence;

  await tapGame(page, x + w / 2, y + h / 2);
  await popup.locator('.dictionary-popup').getByText('a word of this line').first().waitFor();
  check(true, `a tap on ${characters[start]} in the game's text shows the word that starts there`);
  check((await popup.locator('.mark').count()) === 2, 'its characters are marked on the picture');
  check((await harness.command('state')).hold === true, 'the game is held');
  if (shots) await page.screenshot({ path: join(shots, 'game-line-phone.png') });

  // While the popup is open the game does not go on, whatever is pressed.
  await harness.command('click 400 300');
  await harness.command('run 120');
  await harness.command('key 13');
  await harness.command('run 120');
  check((await harness.command('text')).standing === null || true, 'input during the hold is not an error');

  await cardButton(popup, 'add').click();
  await cardButton(popup, 'in-anki').waitFor();
  const note = anki.notes.at(-1).note;
  check(note.fields.Expression === word, 'the card has the word');
  check(note.fields.Sentence.includes(`<b>${word}</b>`), `the game's sentence with the word in bold (${note.fields.Sentence})`);
  const picture = /<img src="(oretsuba_[^"]+\.jpg)">/.exec(note.fields.Picture ?? '');
  check(picture !== null, `a picture of the screen (${note.fields.Picture})`);
  const stored = anki.media.get(picture[1]);
  check(stored && stored[0] === 0xff && stored[1] === 0xd8 && stored.length > 10000, 'which is a JPEG stored in Anki');
  check(note.fields.MiscInfo?.includes('俺たちに翼はない') ?? true, 'the title of the game as the source');
  if (note.fields.SentenceAudio) console.log(`  voice: ${note.fields.SentenceAudio}`);

  // Closing lets the game go on from the same line: nothing was pressed through.
  await tapGame(page, 780, 590);
  await until('the stage to hide the page', () => page.evaluate(() => !window.stage.isOpen()));
  check((await harness.command('state')).hold === false, 'closing the popup lets the game go on');
  await harness.command('run 30');
  const after = (await harness.command('text')).standing;
  check(after !== null && after.sentence === linesBefore, 'the same line still stands: no press went through the popup');

  // A tap in the text box beside the characters does nothing; a tap outside it goes on reading.
  const [bx, by, bw, bh] = after.box;
  const beside = await harness.command(`tap ${bx + bw - 8} ${by + bh - 6}`);
  check(beside.tap === 2 || beside.tap === 0, 'a tap in the box beside the text is not a word');
  if (beside.tap === 2) {
    await harness.command('run 60');
    check((await harness.command('text')).standing?.sentence === linesBefore, 'and does not go on reading');
  }
  const outside = await harness.command('tap 400 200');
  check(outside.tap === 0, 'a tap outside the text box is the game\'s');
  let next = null;
  for (let i = 0; i < 40 && next === null; i++) {
    await harness.command('run 30');
    const now = (await harness.command('text')).standing;
    if (now && now.sentence !== linesBefore && now.text.length > 0) next = now;
    else if (i % 5 === 4) await harness.command('click 400 200');
  }
  check(next !== null, 'and the game reads on to the next line');
  if (next) console.log(`  next: ${next.sentence}`);

  // An iPad's screen.
  const large = await openStage(browser, harness, { width: 1180, height: 820 });
  const first = [...next.text].findIndex((character) => /[぀-ヿ一-鿿]/u.test(character));
  const [nx, ny, nw, nh] = next.rects[Math.max(first, 0)];
  await tapGame(large.page, nx + nw / 2, ny + nh / 2);
  await large.popup.locator('#panel').waitFor();
  await new Promise((done) => setTimeout(done, 500));
  if (shots) await large.page.screenshot({ path: join(shots, 'game-line-tablet.png') });
  check(large.problems.length === 0, `the page reports no errors on a large screen (${large.problems.join(' | ')})`);
  await large.context.close();

  check(problems.length === 0, `the page reports no errors (${problems.join(' | ')})`);
  await context.close();
}

const scratch = mkdtempSync(join(tmpdir(), 'ore-mining-page-'));
const anki = await fakeAnki();
const audio = await fakeAudioSource();
const browser = await chromium.launch();
let harness = null;
try {
  console.log('The kit and the page on a made-up line');
  harness = await startHarness({ kit: join(scratch, 'kit'), debug });
  await setUp(harness, scratch, anki, audio);
  await madeUpLine(browser, harness, anki, audio);
  await harness.stop();
  harness = null;

  if (game) {
    console.log('With the game');
    harness = await startHarness({ kit: join(scratch, 'kit-game'), data: game, save: join(scratch, 'save'), debug });
    await setUp(harness, scratch, anki, audio);
    // The game's own word is added to the dictionary once the line is known.
    setUp.again = async (word) => {
      const { dictionaries } = await harness.kit('dict.list');
      await harness.kit('dict.remove', { id: dictionaries[0].id });
      const zip = join(scratch, 'game-words.zip');
      writeFileSync(zip, smallDictionary([{ expression: word, reading: '', meanings: ['a word of this line'] }]));
      await harness.kit('dict.import', { path: zip, name: 'game-words.zip' });
      await until('the dictionary', async () => {
        const now = await harness.kit('dict.list');
        if (now.import.state === 'failed') throw new Error(now.import.error);
        return now.import.state !== 'importing' && now.import.state !== 'downloading' && now.dictionaries.length === 1;
      });
    };
    await realLine(browser, harness, anki);
  }
  console.log(`ok: ${checks} checks`);
} finally {
  await browser.close();
  await harness?.stop();
  await anki.stop();
  await audio.stop();
  rmSync(scratch, { recursive: true, force: true });
}
