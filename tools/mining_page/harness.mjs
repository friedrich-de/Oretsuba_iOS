// A stand-in for the app around the mining popup's page (docs/MINING.md), for tests and for
// looking at the popup in a browser. It runs the test host with its mining kit (orehost --kit
// --control), serves the page as the app does, and carries the page's calls to the kit.
//
//   node tools/mining_page/harness.mjs --kit FOLDER [--data SET_UP] [--save DIR] [--port 8750]
//
// Then http://127.0.0.1:8750/ shows the game's picture with the popup's page over it: a click on
// the picture is a finger's tap. Without --data there is no game, only the kit and a line made up
// for the popup to stand on (the kit's line.set changes it).
//
//   /                     the stage: the picture and the page over it
//   /page/…               the page's files (engine/resources/mining/page)
//   /page/kit/call/NAME   POST: a method of the kit, or popup.ready, popup.found, popup.close
//   /page/kit/data/NAME   a file of the kit's
//   /test/command?line=   one command for the test host, answered with its JSON
//   /test/frame.png       the game's picture as it stands
//   /test/events?after=N  what the page told the host and the kit told the page, from the Nth on
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '../..');
const PAGE = join(ROOT, 'engine/resources/mining/page');
const HOSTS = ['build/host/orehost', 'build/host-linux/orehost'];

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
  '.png': 'image/png',
};

/** The line the popup stands on when there is no game. */
const MADE_UP_LINE = '昨日は友達と晩ご飯を食べました。';

export async function startHarness(options = {}) {
  const hostPath = options.host ?? HOSTS.map((host) => join(ROOT, host)).find((host) => existsSync(host));
  if (!hostPath) throw new Error('Build the test host first (make -C engine -f Makefile.linux).');
  const out = options.out ?? mkdtempSync(join(tmpdir(), 'ore-stage-'));
  const args = ['--kit', options.kit, '--kit-resources', join(ROOT, 'engine/resources/mining'), '--control', '--out', out];
  if (options.data) args.push('--data', options.data, '--save', options.save ?? mkdtempSync(join(tmpdir(), 'ore-save-')));
  if (options.debug) args.push('--debug');
  const child = spawn(hostPath, args, { stdio: ['pipe', 'pipe', options.debug ? 'inherit' : 'ignore'] });

  // The host answers each command with one line; what the kit tells by itself comes between.
  const waiting = [];
  const events = [];
  let ended = false;
  createInterface({ input: child.stdout }).on('line', (line) => {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      return;
    }
    if (typeof message.event === 'string') events.push({ from: 'kit', name: message.event, payload: message.payload });
    else waiting.shift()?.(message);
  });
  child.on('exit', () => {
    ended = true;
    for (const done of waiting.splice(0)) done({ ok: false, error: 'The test host ended.', kind: 'internal' });
  });
  const command = (line) =>
    new Promise((done) => {
      if (ended) return done({ ok: false, error: 'The test host ended.', kind: 'internal' });
      waiting.push(done);
      child.stdin.write(`${line.replace(/[\r\n]+/g, ' ')}\n`);
    });

  let madeUp = { sentence: MADE_UP_LINE, speaker: '' };
  // Without a game the kit is told the line a word is tapped in.
  if (!options.data) await command(`call line.set ${JSON.stringify(madeUp)}`);

  async function call(method, body) {
    let params = {};
    try {
      if (body.trim() !== '') params = JSON.parse(body);
    } catch {
      return { ok: false, error: 'The call is not JSON.', kind: 'internal' };
    }
    if (method.startsWith('popup.')) {
      events.push({ from: 'page', name: method, payload: params });
      if (method === 'popup.close' && options.data) await command('hold 0');
      return { ok: true, result: null };
    }
    if (method === 'line.set' && !options.data) madeUp = { sentence: params.sentence ?? '', speaker: params.speaker ?? '' };
    return command(`call ${method} ${JSON.stringify(params)}`);
  }

  const server = createServer(async (request, answer) => {
    const url = new URL(request.url, 'http://stage');
    const send = (status, type, body) => {
      answer.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
      answer.end(body);
    };
    const json = (value) => send(200, 'application/json', JSON.stringify(value));
    try {
      if (url.pathname === '/') return send(200, TYPES['.html'], readFileSync(join(HERE, 'stage.html')));
      if (url.pathname === '/stage.js') return send(200, TYPES['.js'], readFileSync(join(HERE, 'stage.js')));
      if (url.pathname.startsWith('/page/kit/call/')) {
        let body = '';
        for await (const part of request) body += part;
        return json(await call(decodeURIComponent(url.pathname.slice('/page/kit/call/'.length)), body));
      }
      if (url.pathname.startsWith('/page/kit/data/')) {
        const method = decodeURIComponent(url.pathname.slice('/page/kit/data/'.length));
        const found = await command(`data ${method} ${JSON.stringify(JSON.parse(url.searchParams.get('p') ?? '{}'))}`);
        if (found.ok !== true) return send(404, 'text/plain', 'none');
        return send(200, found.type, Buffer.from(found.base64, 'base64'));
      }
      if (url.pathname.startsWith('/page/')) {
        const file = normalize(join(PAGE, decodeURIComponent(url.pathname.slice('/page/'.length))));
        if (!file.startsWith(PAGE) || !existsSync(file)) return send(404, 'text/plain', 'none');
        return send(200, TYPES[extname(file)] ?? 'application/octet-stream', readFileSync(file));
      }
      if (url.pathname === '/test/command') return json(await command(url.searchParams.get('line') ?? ''));
      if (url.pathname === '/test/line') return json({ game: Boolean(options.data), ...madeUp });
      if (url.pathname === '/test/frame.png') {
        if (!options.data) return send(404, 'text/plain', 'none');
        await command('shot stage');
        return send(200, 'image/png', readFileSync(join(out, 'stage.png')));
      }
      if (url.pathname === '/test/events') {
        const after = Number(url.searchParams.get('after') ?? 0);
        return json({ events: events.slice(after), count: events.length });
      }
      send(404, 'text/plain', 'none');
    } catch (error) {
      send(500, 'text/plain', String(error));
    }
  });
  await new Promise((done) => server.listen(options.port ?? 0, '127.0.0.1', done));
  return {
    url: `http://127.0.0.1:${server.address().port}/`,
    command,
    /** A method of the kit, as the launcher's settings would call it: the result, or an error with the kit's sentence. */
    async kit(method, params = {}) {
      const answer = await command(`call ${method} ${JSON.stringify(params)}`);
      if (answer.ok !== true) throw Object.assign(new Error(answer.error ?? 'The kit did not answer.'), { kind: answer.kind });
      return answer.result;
    },
    events,
    out,
    async stop() {
      if (!ended) child.stdin.write('quit\n');
      await new Promise((done) => server.close(done));
      server.closeAllConnections?.();
    },
  };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const options = { port: 8750 };
  for (let i = 2; i < process.argv.length; i += 2) options[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
  if (!options.kit) {
    console.error('usage: node tools/mining_page/harness.mjs --kit FOLDER [--data SET_UP] [--save DIR] [--port 8750]');
    process.exit(2);
  }
  options.port = Number(options.port);
  const harness = await startHarness(options);
  console.log(`The stage is at ${harness.url}`);
}
