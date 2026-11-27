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
3. Set Server URL / Voice / Speed, write text (one line = one utterance),
   and press **Generate and Add to Bin**.
4. WAV files are saved to `<project folder>/aquestalk/` with batch-stamped
   unique names (`20261127_222530_001_f1.wav`, … — existing files are never
   overwritten) and added to the project bin.
5. Options (both on by default):
   - **Place on timeline**: inserts the generated clips, in order, on the
     first audio track at the playhead position.
   - **Write subtitles (.srt)**: writes `<batch>_subtitles.srt` next to the
     WAVs, timed from the WAV durations, starting at 0 (relative timing —
     offset it to the playhead position when importing).

Settings (server URL, voice, speed) persist via KdenliveSettings
(group `aquestalk` in `kdenlivesettings.kcfg`).

## Implementation notes

- `src/aquestalk/aquestalkwidget.{hpp,cpp}` — the dock widget.
  Requests are sent sequentially (the server serializes per voice anyway).
  Payload: `POST /v1/audio/speech` with
  `{model: "tts-1", input, voice, response_format: "wav", speed}`.
- Registered in `MainWindow` via the standard `addDock()` helper, next to
  the Speech Editor dock, so it gets the usual View-menu toggle action.
- Generated files are imported with `Bin::slotAddClipToProject()`.

## Not yet done

- Per-line voice/speed and character presets (currently one voice+speed
  for the whole batch).
- Regeneration / preview of a single line, reading-correction dictionary.
- Subtitles as a real Kdenlive subtitle track (currently an .srt file
  with relative timing, written next to the WAVs).
- Build verification on a machine with the full KDE development stack
  (Qt6, KF6, MLT 7, KDDockWidgets). This VM has none of those, so the
  code is written against the current master APIs but not compiled yet.
