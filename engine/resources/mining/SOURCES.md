# What this folder is

The mining kit's files that are not code of the engine (docs/MINING.md). An app copies the folder as it is and
gives its place to `ore_kit_open`.

- `transforms.json`: the rules for undoing Japanese inflections, see below.
- `page/`: the page of the dictionary popup that a web view shows over the game. `popup.html`, `popup.css`,
  `main.js`, `host.js`, `word.js`, `furigana.js` and `glossary-html.js` were written for this project (`word.js`,
  `furigana.js` and `glossary-html.js` follow what [VN Mining Server](https://github.com/friedrich-de/VN-Mining-Server)'s
  page does, so that both make the same cards). `page/vendor/dictionary-popup/` is Hachidori's dictionary popup,
  GPL-3.0-or-later; its own `SOURCES.md` says where it comes from and what was changed.

# Where the Japanese rules come from

`transforms.json` is Yomitan's set of rules for undoing Japanese inflections, written as data by the
[Hoshidicts](https://github.com/bee-san/hoshidicts) Japanese language pack (`languages/ja/transforms.json` at
`088ac5d`), copied unchanged.

- The rules and their descriptions are Yomitan's
  ([`ext/js/language/ja/japanese-transforms.js`](https://github.com/yomidevs/yomitan/blob/e7334a96106a6c74e6fa75bd33a5ea1219d92712/ext/js/language/ja/japanese-transforms.js)),
  GPL-3.0-or-later, Copyright (C) 2024-2026 Yomitan Authors.
- The transforms marked `"source": "own"` (such as `-ください` and `-ながら`) were written for Hoshidicts,
  GPL-3.0-or-later.

The tests in `engine/tests/japanese/` come from the same pack: `yomitan.tsv` is Yomitan's Japanese conjugation test
corpus (GPL-3.0-or-later, Copyright (C) 2023-2026 Yomitan Authors and 2020-2022 Yomichan Authors) and `cases.tsv` was
written for Hoshidicts (GPL-3.0-or-later).

To update: copy the pack's `languages/ja/transforms.json` and `languages/ja/tests/*.tsv` from a newer Hoshidicts,
note its commit here, and run `engine/tests/dict_test.cpp`.
