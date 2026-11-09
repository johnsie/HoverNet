# Music in HoverNet

Five tracks, all from [OpenGameArt.org](https://opengameart.org), all free to use, change and share under the licences below. *Hard Left* and *Asphalt* were converted from MP3 to
Ogg Vorbis (44.1 kHz, quality 3 and 4) for the game; the two *Fever Stadium* files are the author's own Ogg Vorbis files, unchanged; *Hyper Ultra-Racing* was converted from WAV to Ogg Vorbis
(44.1 kHz, quality 4). Nothing else was changed. Each page's licence list was read on 2026-11-09. Race tracks are taken in turn, never the same one twice in a row; the final lap has its own.

| File | Track | By | Where it plays | Licence | Source page |
| --- | --- | --- | --- | --- | --- |
| `HardLeft.ogg` | Hard Left | Dan Knoflicek (published by "Macro") | menus and results screens | choose any of CC-BY 3.0, CC-BY-SA 3.0, GPL 2.0, GPL 3.0; HoverNet uses it under **CC-BY 3.0** (credit: "Music by Dan Knoflicek") | https://opengameart.org/content/hard-left |
| `Asphalt.ogg` | Asphalt | Sygil (2025) | races | **GPL 3.0** | https://opengameart.org/content/asphalt-0 |
| `FeverStadium.ogg` | Fever Stadium (the 165 bpm loop) | MintoDog | races | **CC0** (public domain dedication; no credit needed, given anyway) | https://opengameart.org/content/fever-stadium |
| `FeverStadiumClimax.ogg` | Fever Stadium, "Climax" (the 180 bpm loop, described by its author as final-lap music) | MintoDog | the final lap of a race | **CC0** | https://opengameart.org/content/fever-stadium |
| `HyperUltraRacing.ogg` | Hyper Ultra-Racing (from `AugustUltraAmbience.wav`) | cynicmusic | races | **CC0** | https://opengameart.org/content/hyper-ultra-racing |

## What the licences ask of you

* **Hard Left (CC-BY 3.0):** keep the credit above and the licence link, <https://creativecommons.org/licenses/by/3.0/>. The game also credits each track on screen when it starts.
* **Asphalt (GPL 3.0):** the track may be copied, changed and redistributed under the GNU General Public License version 3, whose text is in `GPL-3.0.txt` next to it. It is
  distributed here as a separate data file, unmodified in content, and the game's code does not copy it into itself: the game only plays it. If you redistribute HoverNet with this
  file, you must keep this file and `GPL-3.0.txt` with it and pass the GPL's rights on for the track. The "source" of an audio track is the track (and the original MP3 at
  the source page above); the Ogg here is a re-encoding of it. This does not change the licence of HoverNet's own code (GrokkSoft HoverRace licence, see `LICENSE`): the track is
  an independent work that is aggregated with the game, not part of its code. If you are unsure whether that suits your own plans, take Asphalt out (the game plays
  without it, silent) or replace it with a track whose licence you prefer.

The three CC0 tracks ask nothing of you. SHA-256 of the files as shipped: `FeverStadium.ogg` 65c892eba361402e1519…, `FeverStadiumClimax.ogg` ba37e7877eee7361dbe0…, `HyperUltraRacing.ogg` f155aacd3c896ac3f34d… (first 20 hex digits).

To turn the music off: Settings > Sound > Music volume, or set `HOVERNET_NO_MUSIC=1`.
