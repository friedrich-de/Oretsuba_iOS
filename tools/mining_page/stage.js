// What the app does around the popup's page, in a browser: it shows the game's picture, asks the
// engine what a tap is, shows the page over the picture for a tapped word and hides it again when
// the page says so. Without a game it draws a made-up line and answers the tap itself.
const picture = document.getElementById('picture');
const line = document.getElementById('line');
const popup = document.getElementById('popup');
const SCREEN = [800, 600];

const state = await (await fetch('test/line')).json();
let seen = 0;
let open = false;
/** The made-up line's characters, each with its cell in the game's pixels. */
let cells = [];

/** Where the 800x600 picture is on the page: fitted and centred, as the app shows it. */
function fitted() {
  const scale = Math.min(window.innerWidth / SCREEN[0], window.innerHeight / SCREEN[1]);
  const w = SCREEN[0] * scale;
  const h = SCREEN[1] * scale;
  return { x: (window.innerWidth - w) / 2, y: (window.innerHeight - h) / 2, w, h, scale };
}

async function command(text) {
  return (await fetch(`test/command?line=${encodeURIComponent(text)}`)).json();
}

async function refresh() {
  const at = fitted();
  const element = state.game ? picture : line;
  Object.assign(element.style, { left: `${at.x}px`, top: `${at.y}px`, width: `${at.w}px`, height: `${at.h}px` });
  element.hidden = false;
  if (state.game) {
    const loaded = new Promise((done) => picture.addEventListener('load', done, { once: true }));
    picture.src = `test/frame.png?${Date.now()}`;
    await loaded;
    return;
  }
  const { sentence } = await (await fetch('test/line')).json();
  cells = [...sentence].map((character, index) => ({ character, rect: [60 + (index % 24) * 28, 440 + Math.floor(index / 24) * 34, 28, 30] }));
  line.replaceChildren(
    ...cells.map(({ character, rect }) => {
      const span = document.createElement('span');
      span.textContent = character;
      Object.assign(span.style, {
        left: `${rect[0] * at.scale}px`,
        top: `${rect[1] * at.scale}px`,
        width: `${rect[2] * at.scale}px`,
        height: `${rect[3] * at.scale}px`,
        fontSize: `${26 * at.scale}px`,
        lineHeight: `${rect[3] * at.scale}px`,
      });
      return span;
    }),
  );
}

function show(what) {
  const at = fitted();
  open = true;
  popup.style.visibility = 'visible';
  popup.contentWindow.oreMining.show({ ...what, picture: [at.x, at.y, at.w, at.h] });
}

/** A tap at a place of the game's screen, as the app handles it. */
async function tap(x, y) {
  if (state.game) {
    const answer = await command(`tap ${Math.round(x)} ${Math.round(y)}`);
    if (answer.tap === 1) show(answer.popup);
    else await refresh();
    return answer.tap;
  }
  const index = cells.findIndex(({ rect }) => x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3]);
  if (index < 0) return 0;
  const { sentence, speaker } = await (await fetch('test/line')).json();
  show({ line: 1, text: sentence, index, rects: cells.map(({ rect }) => rect), sentence, speaker, box: [40, 420, 720, 160], screen: SCREEN });
  return 1;
}

document.addEventListener('click', (event) => {
  if (open) return;
  const at = fitted();
  void tap((event.clientX - at.x) / at.scale, (event.clientY - at.y) / at.scale);
});

window.addEventListener('resize', () => void refresh());

// What the page told the host, and what the kit tells the page.
async function listen() {
  for (;;) {
    try {
      const { events, count } = await (await fetch(`test/events?after=${seen}`)).json();
      seen = count;
      for (const event of events) {
        if (event.from === 'kit') popup.contentWindow.oreMining?.event(event.name, event.payload);
        else if (event.name === 'popup.close') {
          open = false;
          popup.style.visibility = 'hidden';
          await refresh();
        }
      }
    } catch {
      // The harness has gone.
    }
    await new Promise((done) => setTimeout(done, 100));
  }
}

// For a test: the stage's own doings by name.
window.stage = { tap, refresh, command, isOpen: () => open };
await refresh();
void listen();
document.documentElement.dataset.ready = 'true';
