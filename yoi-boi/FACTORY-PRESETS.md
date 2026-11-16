# YOI-BOI 0.3.0 factory bank

YOI-BOI 0.3.0 ships **87 factory sounds** in eleven Library folders:

- **75 sounds on the new engine 4**, in ten folders. Twelve of them are reworked versions of the originals, marked "II", in the **Yoi** folder.
- **The twelve original sounds** in **Classic**, on their original engine, with the new backend FINISH voicing. Saved old sessions retain their original output.

All of them are hand-authored recipes, not saved randomizer results or copies of commercial presets. **Tape Bounce** and **Tape Bounce II** aim for a Tape B-inspired bounce. They are not official artist presets, endorsements or reconstructions of a track.

## Finding and playing them

Open **LIBRARY**. The folder list shows the new folders first, then **Classic**, then your own folders. Search matches names, folders, families and tags (for example "triplet", "wide" or "sub").

Start around MIDI notes **36–48**. Every fresh engine-4 sound uses the note-tracking **Body low-cut** at sqrt(2) × the lowest held note (35–350 Hz), and all but **Subterranean** (Driven) use the clean **Direct** sub. A few sounds are played by hand:

- **Back Talk** and **Snarl Engine** talk with the **mod wheel**.
- **Yap Trap** and **Howl Pack** talk with **aftertouch** (channel or key pressure).

Without the wheel or pressure, these sounds hold their first vowel. Sounds marked **legato-only glide** slide only between connected notes. **Dive Bomb** uses a ±24 semitone bend range, and **Riser Chirp** ±12.

Favorites and ids are unchanged: the originals are still `factory:0`–`factory:11`, and the new sounds are `factory:12`–`factory:86`.

## How the numbers were measured

The standard console phrase is stereo 48 kHz at 140 BPM, notes 43/46 at velocity 112 over 3 seconds, with wheel/pressure sweeps. The default Length/Sub player locks are retained. When locks change a factory recipe, the resolved sound receives an offline measured trim.

**All 87 sounds:** -10.020 to -9.982 BS.1770-4 gated integrated LUFS; maximum reconstructed true peak -1.144 dBTP. Output defaults to 0 dB. Per-preset FINISH depth/sub balance and the measured trim are baked by `YoiBoiTests --bake-finish`. `--preset-stats` uses a proper gated meter; `--measure-wavs` measures integrated LUFS and 4× true peak. The former ungated “LUFS-ish” levels are superseded.

FINISH processes the body with program-relative three-band upward/downward compression, sums the protected sub, then applies oversampled GLUE/ceiling and a DC blocker after clipping. Engine 4's sub is on the note, independent of its body pitch envelope. Classic's octave-down pitch and fixed 110 Hz body cut remain. New host-only controls are not added to the editor or randomization locks. Old states/XML files missing the new fields load FINISH off, with zero trim and their original sub/cut behaviour, bit-identically.

Vowel contrast median increases 1.13%; the largest individual loss is 9.42%. All 200 fresh-default NEW YOI seeds pass −10 ±2 LU and −1 dBTP. Full per-preset band, contrast, CPU and compatibility evidence: `../../outputs/yoi-boi-0.3.0/measurements.md`.

To repeat: `--render-demos <absolute-folder>` writes all demos and the tour; `--measure-wavs <absolute-folder>` meters the WAVs. Use the final Release console binary, not the no-arguments GUI-containing suite, in a sandbox.

## The sounds

### Yoi (16)

The core talking bass. The twelve originals reworked for engine 4 ("II"), plus four new ones.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Tape Bounce II | Rubber | OO → AH → EE, Y glide, K consonant, squash 25%, velocity | -2.02 | -10.01 |
| Bubble Trouble II | Rubber | OO → UH → EE, W glide, squash 20%, velocity | -1.97 | -10.00 |
| Elastic Yoi II | Rubber | OH → AH → IH, Y glide, diode, squash 25%, legato-only glide | -1.41 | -10.00 |
| Chrome Chew II | Metal | OO → AE → EE, T consonant, fold, squash 30%, velocity | -1.94 | -10.00 |
| Laser Reply II | Metal | OH → EH → IH, T consonant, drop 7 st, squash 30% | -2.23 | -10.00 |
| Steel Syllable II | Metal | AH → ER → EE, fold, squash 35%, velocity | -1.38 | -10.01 |
| Low Talker II | Throat | OO → UH → EE, W glide, diode, squash 20% | -1.71 | -10.00 |
| Vowel Throw II | Throat | OH → AH → IH, Y glide, squash 30%, velocity | -1.51 | -10.02 |
| Basement Answer II | Throat | OO → ER → EH, diode, squash 15% | -2.70 | -10.00 |
| Pocket Gremlin II | Squeak | OH → IH → EE, Y glide, T consonant, rise 3 st | -2.28 | -10.00 |
| Squeaky Clean II | Squeak | OO → AH → IH, Y glide, squash 10% | -3.35 | -10.00 |
| Neon Chirp II | Squeak | AH → EH → IH, fold, squash 20%, velocity, legato-only glide | -3.16 | -10.00 |
| Yoi Boi Prime | Rubber | UH → OH → EE, Y glide, K consonant, squash 35%, width 20%, velocity | -1.63 | -10.00 |
| Oi Oi Oi | Rubber | synced talk 1/8, OH → OH → EE, Y glide, squash 30% | -1.94 | -10.00 |
| Y-Glide Gremlin | Squeak | OO → AE → IH, Y glide, rise 5 st, legato-only glide | -1.85 | -10.00 |
| Yoink | Metal | OO → AH → EE, K consonant, drop 12 st, fold, squash 30% | -1.41 | -10.00 |

### Riddim (7)

Tight, chopped and squashed, for half-time riddim patterns.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Riddim Ritual | Metal | synced talk 1/16 triplet, OO → AE → EE, fold, squash 55%, velocity | -2.44 | -10.00 |
| Lunch Tray | Metal | OH → EH → IH, T consonant, crush, squash 45% | -2.05 | -10.00 |
| Knock Knock Riddim | Rubber | OO → UH → AH, K consonant, drop 5 st, diode, squash 50%, velocity | -1.58 | -10.00 |
| Dungeon Step | Throat | synced talk 1/8 triplet, OO → ER → UH, diode, squash 55% | -2.65 | -10.00 |
| Tin Can Shuffle | Metal | synced talk 1/16 triplet, OH → AE → IH, crush, squash 50% | -2.29 | -10.00 |
| Robo Grime | Metal | synced talk 1/16, EH → ER → AE, fold, squash 60%, width 15% | -2.68 | -10.00 |
| Clang Dance | Metal | OO → EH → EE, CH consonant, fold, squash 50%, velocity | -2.06 | -10.00 |

### Talk (7)

The mouth keeps moving while you hold a note: synced talk, mod wheel or aftertouch.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Chatterbox | Rubber | synced talk 1/16, OO → AH → EE, W glide, squash 30% | -1.81 | -10.00 |
| Mouth Off | Rubber | synced talk 1/8 triplet, UH → AE → IH, squash 35%, velocity | -1.51 | -10.00 |
| Say What | Throat | synced talk 1/4, UH → AH → EH, W glide, rise 4 st, diode, squash 25% | -1.82 | -10.00 |
| Back Talk | Rubber | mod-wheel talk, OO → AH → IH, squash 30% | -1.63 | -10.00 |
| Gossip Line | Squeak | synced talk 1/16 triplet, EH → OH → IH, Y glide, T consonant | -1.96 | -10.00 |
| Wah Wah Who | Throat | synced talk 1/8, OO → AH → OO, W glide, squash 30% | -1.59 | -10.00 |
| Yap Trap | Squeak | aftertouch talk, OO → IH → EE, Y glide | -3.08 | -10.00 |

### Growl (7)

Darker vowels (ER, UH) with Diode or Fold drive and heavy Squash.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Grizzly | Throat | OO → ER → UH, diode, squash 50% | -2.28 | -10.00 |
| Gut Rumble | Throat | synced talk 1/4 triplet, UH → ER → OH, diode, squash 45% | -1.61 | -10.00 |
| Snarl Engine | Metal | mod-wheel talk, OO → ER → AE, fold, squash 55% | -2.35 | -10.01 |
| Beast Mode | Throat | synced talk 1/8, OH → ER → AH, fold, squash 60%, velocity | -1.77 | -10.00 |
| Fang Fold | Metal | OO → AE → ER, K consonant, fold, squash 50% | -2.56 | -10.00 |
| Gravel Throat | Throat | UH → OH → ER, crush, squash 40% | -1.81 | -9.99 |
| Howl Pack | Throat | aftertouch talk, OO → AH → ER, diode, squash 45%, width 20% | -1.59 | -10.00 |

### Squelch (6)

Wet, rubbery vowel snaps and acid-style motion.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Squelch Pit | Squeak | OO → AE → IH, squash 35% | -1.79 | -10.00 |
| Wet Sock | Rubber | UH → OH → EH, W glide, diode, squash 30% | -1.94 | -10.00 |
| Acid Gargle | Metal | synced talk 1/16, OO → AH → IH, diode, squash 40%, legato-only glide | -2.11 | -10.00 |
| Bog Blip | Throat | OO → ER → AH, K consonant, drop 6 st | -1.49 | -10.00 |
| Slime Time | Squeak | synced talk 1/8 triplet, EH → OO → IH, squash 40% | -1.44 | -10.01 |
| Plunger | Rubber | OO → UH → OO, rise 7 st, diode, squash 25% | -2.46 | -10.00 |

### Wobble (7)

Synced OO-AH-OO wobbles from 1/16 to a full bar.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Wub Lord | Throat | synced talk 1/8, OO → AH → OO, squash 40% | -1.49 | -10.01 |
| Classic Wub | Rubber | synced talk 1/4, OO → AH → OO, squash 35% | -1.44 | -10.00 |
| Triplet Womp | Throat | synced talk 1/8 triplet, UH → AH → OO, diode, squash 40% | -1.77 | -10.00 |
| Half-Time Wobble | Throat | synced talk 1/2, OO → ER → OO, squash 35% | -1.90 | -10.01 |
| Dotted Drift | Rubber | synced talk 1/8 dotted, OO → AE → OO, squash 35%, width 20% | -1.68 | -9.98 |
| Slow Oo-Ah | Rubber | synced talk 1 bar, OO → AH → OO | -1.73 | -10.00 |
| Wobble Bell | Metal | synced talk 1/16, OO → IH → OO, fold, squash 40% | -2.06 | -10.00 |

### Reese (6)

Wide, slow-moving bodies over a mono sub.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Reese Piece | Throat | synced talk 1 bar, UH → OH → UH, squash 30%, width 60% | -1.31 | -10.00 |
| Dark Reese Talk | Throat | synced talk 1/2, OO → ER → UH, diode, squash 35%, width 50% | -1.42 | -10.00 |
| Widescreen Reese | Rubber | OH → UH → OH, squash 30%, width 85% | -1.30 | -10.01 |
| Neuro Mumble | Metal | synced talk 1/4 triplet, UH → AE → ER, fold, squash 45%, width 45% | -1.50 | -10.00 |
| Rolling Reese | Throat | synced talk 1/4 dotted, OO → UH → OH, squash 35%, width 55% | -1.31 | -9.99 |
| Gutter Reese | Throat | ER → UH → ER, crush, squash 35%, width 40% | -1.15 | -10.00 |

### Sub (6)

Clean, sub-heavy sounds with Sub Reset for an identical punch on every note.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Clean Sub Talk | Throat | OO → UH → OH, sub reset | -3.00 | -10.00 |
| 808 Yoi | Rubber | OO → OH → EE, drop 12 st, sub reset | -1.63 | -10.00 |
| Pressure Drop | Throat | UH → OO → OO, drop 24 st, sub reset | -1.29 | -10.02 |
| Deep Mumble | Throat | synced talk 1/4, OO → ER → OO, sub reset | -2.65 | -10.00 |
| Subterranean | Throat | OO → UH → OO, sub reset, driven sub | -1.76 | -10.00 |
| Sine Speech | Rubber | OO → AH → EE, W glide, velocity, sub reset | -3.18 | -10.00 |

### Space (6)

Wide and airy, with rises and long synced motion.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Space Bass Cadet | Squeak | synced talk 1/4 dotted, OO → AE → IH, fold, squash 40%, width 60% | -2.03 | -10.00 |
| Nebula Yoi | Rubber | OO → OH → IH, Y glide, rise 5 st, width 70% | -1.51 | -10.00 |
| Orbit Talk | Throat | synced talk 1/2 triplet, UH → AE → OO, squash 40%, width 55% | -1.53 | -10.00 |
| Wormhole Wah | Throat | synced talk 1 bar dotted, OO → AH → ER, diode, squash 35%, width 65% | -1.70 | -10.00 |
| Starlight Squeal | Squeak | AH → IH → EE, rise 12 st, fold, squash 30%, width 60%, legato-only glide | -1.66 | -10.00 |
| Zero Gravity | Rubber | OO → UH → AE, width 75% | -1.50 | -10.00 |

### Leads/FX (7)

Higher-register chirps, dives, risers and zaps.

| Sound | Family | What it uses | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Laser Gremlin | Squeak | OH → IH → EE, T consonant, drop 12 st, fold, squash 30% | -1.76 | -10.00 |
| Siren Yoi | Squeak | synced talk 1/2, OO → AH → IH, rise 12 st, width 30%, legato-only glide | -1.31 | -10.00 |
| Dive Bomb | Metal | EE → AH → OO, drop 24 st, fold, squash 45%, bend ±24 | -3.07 | -10.00 |
| Riser Chirp | Squeak | OO → EH → IH, rise 24 st, bend ±12 | -1.30 | -10.02 |
| Glitch Mouth | Metal | synced talk 1/16 triplet, AE → ER → IH, CH consonant, crush, squash 50% | -2.42 | -10.00 |
| Robo Pluck | Metal | OH → EH → IH, T consonant, velocity | -1.77 | -10.00 |
| Zap Reply | Squeak | OO → IH → EE, CH consonant, drop 18 st, fold, squash 25% | -1.56 | -10.00 |

### Classic (12)

The twelve original 0.1.3 sounds on their original engine, unchanged.

| Sound | Family | Vowel motion, length | True peak dBTP | LUFS I |
| --- | --- | --- | ---: | ---: |
| Tape Bounce | Rubber | OO → AH → EE, 1/8 | -1.69 | -10.00 |
| Bubble Trouble | Rubber | OO → OH → EE, 1/8 triplet | -1.29 | -10.00 |
| Elastic Yoi | Rubber | OH → AH → EE, 1/8 dotted | -1.38 | -10.00 |
| Chrome Chew | Metal | OO → EH → EE, 1/8 | -1.74 | -10.00 |
| Laser Reply | Metal | OH → EH → EE, 1/16 | -1.63 | -10.00 |
| Steel Syllable | Metal | AH → OH → EE, 1/8 triplet | -1.33 | -10.02 |
| Low Talker | Throat | OO → AH → EE, 1/4 | -1.44 | -10.00 |
| Vowel Throw | Throat | OH → AH → EE, 1/8 dotted | -1.32 | -10.01 |
| Basement Answer | Throat | OO → OH → EH, 1/4 triplet | -1.25 | -10.01 |
| Pocket Gremlin | Squeak | OH → EH → EE, 1/16 | -1.29 | -10.00 |
| Squeaky Clean | Squeak | OO → AH → EE, 1/16 triplet | -1.14 | -10.01 |
| Neon Chirp | Squeak | AH → EH → EE, 1/8 | -2.61 | -10.00 |

## Tips

- For a separate sub track, enable YOI-BOI's **Sub** output in your host. On engine-4 sounds the dedicated sub (Direct or Driven) then plays only there. You can also choose **Sub Off** and keep **Body low-cut** on.
- **ZOOM → Use latest sound (changes tone)** moves a Classic sound to engine 4 without changing its settings. Undo goes back.
- **Mutate** keeps a sound's engine. **New Yoi** always makes an engine-4 sound.
