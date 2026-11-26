# What this folder is

The mining kit's files that are not code of the engine (docs/MINING.md). An app copies the folder as it is and
gives its place to `ore_kit_open`.

- `transforms.json`: the rules for undoing Japanese inflections, see below.
- `page/`: the page of the dictionary popup that a web view shows over the game. `popup.html`, `popup.css`,
  `main.js`, `host.js`, `word.js`, `furigana.js` and `glossary-html.js` were written for this project (`word.js`,
  `furigana.js` and `glossary-html.js` follow what [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server)'s
  page does, so that both make the same cards). `page/vendor/dictionary-popup/` is Hachidori's dictionary popup,
  GPL-3.0-or-later; its own `SOURCES.md` says where it comes from and what was changed.
- `LICENSE`: the text of the GNU General Public License, version 3.

# Licence

Everything in this folder is under the GNU General Public License, version 3 or later (GPL-3.0-or-later); `LICENSE`
is its text. The page's own files say so at their head. `transforms.json` is data and cannot carry such a head: this
note stands for it. `page/vendor/dictionary-popup/` has its own copy of the licence and names its authors; its icons
are under the MIT licence (`page/vendor/dictionary-popup/hachidori/LICENSE.fluent-icons`).

This statement is about this folder (and `engine/tests/japanese` and `engine/tests/page`, see below), not about the
rest of the repository.

# Where the Japanese rules come from

`transforms.json` is Yomitan's set of rules for undoing Japanese inflections, written as data by the
[Hoshidicts](https://github.com/bee-san/hoshidicts) Japanese language pack (`languages/ja/transforms.json` at
`088ac5d`), copied unchanged.

- The rules and their descriptions are Yomitan's
  ([`ext/js/language/ja/japanese-transforms.js`](https://github.com/yomidevs/yomitan/blob/e7334a96106a6c74e6fa75bd33a5ea1219d92712/ext/js/language/ja/japanese-transforms.js)),
  GPL-3.0-or-later, Copyright (C) 2024-2026 Yomitan Authors.
- The transforms marked `"source": "own"` (such as `-ください` and `-ながら`) were written for Hoshidicts,
  GPL-3.0-or-later.

The page's own tests in `engine/tests/page/` are under the same licence as the page. The tests in
`engine/tests/japanese/` come from the same pack as the rules: `yomitan.tsv` is Yomitan's Japanese conjugation test
corpus (GPL-3.0-or-later, Copyright (C) 2023-2026 Yomitan Authors and 2020-2022 Yomichan Authors) and `cases.tsv` was
written for Hoshidicts (GPL-3.0-or-later).

To update: copy the pack's `languages/ja/transforms.json` and `languages/ja/tests/*.tsv` from a newer Hoshidicts,
note its commit here, and run `engine/tests/dict_test.cpp`.
