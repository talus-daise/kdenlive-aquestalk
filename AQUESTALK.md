# AquesTalk Dock (fork feature)

Adds an **AquesTalk** dock panel to Kdenlive that generates Japanese speech
with [aquestalk-server](https://github.com/Lqm1/aquestalk-server)
(AquesTalk1, OpenAI-compatible HTTP API) and imports the generated WAV
files into the project bin.

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

## Accent notation

AquesTalk accent marks can be written directly in the text, e.g.
`こんに'ちは`. The `'` is passed to the server as-is; it is stripped
from the subtitle text so it does not show on screen.
(Note: whether the marks survive depends on aquestalk-server's
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
