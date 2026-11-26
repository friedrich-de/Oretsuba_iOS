// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The contributors to friedrich-de/Oretsuba_iOS
//
// This file is part of the dictionary popup of the Oretsuba port. It is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details: its text is the file LICENSE in engine/resources/mining.

// How the page reaches the mining kit (docs/MINING.md). A host carries two things:
//
//   call(method, params)     a method of the kit by its name, answered with the kit's JSON:
//                            {"ok":true,"result":…} or {"ok":false,"error":"a sentence","kind":"…"}.
//                            Methods that start with "popup." are for the host itself: popup.ready
//                            (the page can show words), popup.close (hide the page and let the game
//                            go on) and popup.found {length} (how many characters the word takes).
//   kit/data/<method>?p=…    a file of the kit's, fetched like any file beside the page; p is the
//                            method's parameters as JSON.
//
// An app whose web view can answer a message (WKWebView) is found by its message handler "oreKit".
// Any other host sets window.oreHost = { call(method, paramsJson) } before the page loads; its call
// returns the answer's JSON, at once or as a promise (Android's WebView). Without either, the page
// is served by a web server that takes POST kit/call/<method> (the test harness).

export class KitError extends Error {
  constructor(message, kind) {
    super(message);
    this.kind = kind;
  }
}

function carrier(win) {
  if (win.oreHost && typeof win.oreHost.call === 'function') {
    return (method, params) => Promise.resolve(win.oreHost.call(method, JSON.stringify(params)));
  }
  const apple = win.webkit?.messageHandlers?.oreKit;
  if (apple) return (method, params) => apple.postMessage({ method, params: JSON.stringify(params) });
  return async (method, params) => {
    const answer = await win.fetch(`kit/call/${encodeURIComponent(method)}`, { method: 'POST', body: JSON.stringify(params) });
    return answer.text();
  };
}

export function hostOf(win = window) {
  const carry = carrier(win);
  return {
    /** The result of a method, or a KitError with the kit's sentence. */
    async call(method, params = {}) {
      let answer;
      try {
        answer = await carry(method, params);
        if (typeof answer === 'string') answer = JSON.parse(answer);
      } catch (error) {
        throw new KitError(error instanceof Error ? error.message : String(error), 'internal');
      }
      if (answer === null || typeof answer !== 'object' || answer.ok !== true) {
        throw new KitError(answer?.error ?? 'The game did not answer.', answer?.kind ?? 'internal');
      }
      return answer.result;
    },
    /** Where a file of the kit's is fetched from. */
    dataUrl(method, params = {}) {
      return `kit/data/${encodeURIComponent(method)}?p=${encodeURIComponent(JSON.stringify(params))}`;
    },
  };
}
