# AquesTalk Dock (fork feature)

Adds an **AquesTalk** dock panel to Kdenlive that generates Japanese speech
with [aquestalk-server](https://github.com/Lqm1/aquestalk-server)
(AquesTalk1, OpenAI-compatible HTTP API) and imports the generated WAV
files into the project bin.

## Building

This is a fork of Kdenlive master; the full upstream build guide is
[`dev-docs/build.md`](dev-docs/build.md) in this repository. In short:

Requirements are recent: Qt >= 6.10, KF6 >= 6.21, MLT >= 7.42
( upstream lists Ubuntu 26.04+ or Arch as supported). On older distros
you will also have to build MLT and KDDockWidgets from source first —
both are covered in `dev-docs/build.md`.

```bash
# Dependencies (Debian/Ubuntu; enable deb-src first)
sudo apt install build-essential git cmake extra-cmake-modules ninja-build
sudo apt build-dep mlt kdenlive

# This fork, the feature branch
git clone https://github.com/talus-daise/kdenlive-aquestalk.git
cd kdenlive-aquestalk
git checkout feat/aquestalk-dock

# Configure, build, install
mkdir build && cd build
cmake .. -GNinja -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DKDE_INSTALL_USE_QT_SYS_PATHS=ON -DRELEASE_BUILD=OFF -DBUILD_TESTING=OFF
ninja -j$(nproc)
sudo ninja install   # required: effects etc. are only complete after install

# Run (when installed outside a system path)
. prefix.sh
kdenlive
```

The AquesTalk dock adds no new dependencies beyond Kdenlive's own
(Qt Network and Qt Multimedia are already part of the build).

## Usage

1. Start aquestalk-server (listens on port 8080 by default).
   The widget only talks HTTP to it; how/where the server runs is up to you.
2. In Kdenlive, open the dock: View menu → **AquesTalk**
   (it starts closed, tabbed on the right side like the Speech Editor).
3. Set Server URL / Voice / Speed, write text (one line = one utterance):
   - **Generate All** — every non-empty line, in order
   - **Generate Line** — only the line at the text cursor
   - **Preview Line** — generate the cursor line and play it, without
     adding anything to the project
4. WAV files are saved to `<project folder>/aquestalk/` with batch-stamped
   unique names (`20261127_222530_001_f1.wav`, … — existing files are never
   overwritten) and added to the project bin.
5. Options (both on by default):
   - **Place on timeline**: inserts the generated clips, in order, on the
     first audio track at the playhead position.
   - **Add subtitles**: writes an SRT timed from the WAV durations and,
     when the clips were placed, imports it into the timeline's subtitle
     track at the placement position. Otherwise the .srt file is simply
     left next to the WAVs.

## Per-line voice / speed

- Character preset: start the line with `name>` —
  e.g. `reimu> ゆっくりしていってね`
- Inline override: `[f2] text` or `[f2@1.2] text`
- Otherwise the panel's Voice / Speed apply.

## Characters / Dictionary dialog

- Characters: one preset per line, `name:voice:speed:image`
  (speed and image optional), e.g. `reimu:f1:1.0:/home/user/reimu.png`
- Dictionary: reading corrections, one per line, `before=after`,
  e.g. `明日=あす`. Applied to the spoken text only — subtitles keep the
  original text.

## Phonetic notation (accents and symbols)

The text may contain full AquesTalk phonetic notation (see A-Quest's
"音声記号列仕様書"): accent `'`, phrase separators `/` `;` `+` and
halfwidth `,`, devoicing `_` before a katakana syllable, nasal `゜`,
and the reading tags `<NUM VAL=...>`, `<NUMK VAL=... COUNTER=...>`
and `<ALPHA VAL=...>`. The spoken text is sent to the server as-is.

The subtitle/display text is derived with these rules:

- A line with no phonetic notation is shown untouched (a literal `+`
  or `/` in normal prose is never eaten).
- Tags are replaced by their VAL contents, so numbers and codes stay
  visible: `<NUM VAL=01-2345>` shows as `01-2345`.
- `'` is removed; `/` `;` `+` between kana (or next to a number) are
  removed; halfwidth `,` becomes `、`; `_` is removed; `゜` is kept.

(Note: whether the notation survives depends on aquestalk-server's
text conversion — please verify with the actual server.)

## Standing images (tachie)

If a character preset has an image, timeline placement also puts that
image on the first video track, one segment per line, resized to the
exact length of the line's audio. No mouth/blink animation yet, and
segment boundaries use durations rounded to frames, so a sub-frame
drift against the audio is possible.

Settings (server URL, voice, speed, presets, dictionary) persist via
KdenliveSettings (group `aquestalk` in `kdenlivesettings.kcfg`).

## Implementation notes

- `src/aquestalk/aquestalkwidget.{hpp,cpp}` — the dock widget.
  Requests are sent sequentially (the server serializes per voice anyway).
  Payload: `POST /v1/audio/speech` with
  `{model: "tts-1", input, voice, response_format: "wav", speed}`.
- Registered in `MainWindow` via the standard `addDock()` helper, next to
  the Speech Editor dock, so it gets the usual View-menu toggle action.
- Generated files are imported with `Bin::slotAddClipToProject()`;
  timeline placement uses `TimelineController::insertClips()`;
  subtitle import uses `SubtitleModel::importSubtitle()` (the same
  pattern as the speech-to-text dialog).

## Not yet done

- Mouth (lip-sync) and blink animation for standing images.
- A GUI accent editor — accent is written as text notation instead
  (see above).
- Build verification on a machine with the full KDE development stack
  (Qt6, KF6, MLT 7, KDDockWidgets). The development VM has none of those,
  so the code is written against the current master APIs but not
  compiled yet.
