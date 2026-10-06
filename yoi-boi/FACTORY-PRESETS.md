# YOI-BOI 0.2.0 factory bank

YOI-BOI 0.2.0 ships **87 factory sounds** in eleven Library folders:

- **75 sounds on the new engine 4**, in ten folders. Twelve of them are reworked versions of the originals, marked "II", in the **Yoi** folder.
- **The twelve original sounds** in **Classic**, on their original engine and unchanged.

All of them are hand-authored recipes, not saved randomizer results or copies of commercial presets. **Tape Bounce** and **Tape Bounce II** aim for a Tape B-inspired bounce. They are not official artist presets, endorsements or reconstructions of a track.

## Finding and playing them

Open **LIBRARY**. The folder list shows the new folders first, then **Classic**, then your own folders. Search matches names, folders, families and tags (for example "triplet", "wide" or "sub").

Start around MIDI notes **36–48**. Every engine-4 sound uses the 110 Hz **Body low-cut**, and all but **Subterranean** (Driven) use the clean **Direct** sub. A few sounds are played by hand:

- **Back Talk** and **Snarl Engine** talk with the **mod wheel**.
- **Yap Trap** and **Howl Pack** talk with **aftertouch** (channel or key pressure).

Without the wheel or pressure, these sounds hold their first vowel. Sounds marked **legato-only glide** slide only between connected notes. **Dive Bomb** uses a ±24 semitone bend range, and **Riser Chirp** ±12.

Favorites and ids are unchanged: the originals are still `factory:0`–`factory:11`, and the new sounds are `factory:12`–`factory:86`.

## How the numbers were measured

Every sound was rendered through the real processor at 48 kHz and 140 BPM. Each render plays the same four-note phrase (notes 43 and 46, velocity 112) over 3 seconds and sweeps the mod wheel and pressure for the hand-played sounds. The checks:

- **Peak:** the highest sample, in dBFS.
- **LUFS-ish:** BS.1770 K-weighted loudness, ungated, over the whole 3 seconds. That includes about a second of silence, so it reads lower than a gated loudness meter. Use it to compare sounds, not as a mastering figure.
- **DC offset** and **clipped samples:** samples above 0.985.

A sound passes when its peak is between 0.01 and 0.99, its DC offset is below 0.005, and nothing is clipped, silent or non-finite. **All 87 pass.** The engine-4 output trims were set from these renders so the new bank sits within half a dB of itself.

| Group | Sounds | LUFS-ish | Peak | Largest DC | Clipped samples |
| --- | ---: | --- | --- | ---: | ---: |
| Engine 4 (ten folders) | 75 | −25.2 to −24.8 | −16.8 to −9.5 dBFS | 0.00001 | 0 |
| Classic (original engine) | 12 | −32.3 to −20.7 | −21.6 to −10.4 dBFS | < 0.000005 | 0 |

The Classic sounds were not retrimmed, because changing them would change existing Favorites and projects.

To repeat the measurements:

- `YoiBoiTests --preset-stats <folder>` prints this table for every sound.
- `--render-demos <folder>` also writes each demo and a combined tour as WAV files.

In the tables, **drop** and **rise** show the pitch envelope (the note starts that many semitones above or below and settles). **Synced talk** gives the talk rate. **Squash** and **width** show their amounts. **Velocity** means quieter notes soften Mouth, Bite or Dirt.

## The sounds

### Yoi (16)

The core talking bass. The twelve originals reworked for engine 4 ("II"), plus four new ones.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Tape Bounce II | Rubber | OO → AH → EE, Y glide, K consonant, squash 25%, velocity | −14.6 | −25.2 |
| Bubble Trouble II | Rubber | OO → UH → EE, W glide, squash 20%, velocity | −13.7 | −25.2 |
| Elastic Yoi II | Rubber | OH → AH → IH, Y glide, diode, squash 25%, legato-only glide | −12.8 | −25.0 |
| Chrome Chew II | Metal | OO → AE → EE, T consonant, fold, squash 30%, velocity | −13.1 | −25.0 |
| Laser Reply II | Metal | OH → EH → IH, T consonant, drop 7 st, squash 30% | −15.8 | −24.9 |
| Steel Syllable II | Metal | AH → ER → EE, fold, squash 35%, velocity | −15.4 | −25.2 |
| Low Talker II | Throat | OO → UH → EE, W glide, diode, squash 20% | −12.8 | −24.9 |
| Vowel Throw II | Throat | OH → AH → IH, Y glide, squash 30%, velocity | −12.4 | −25.0 |
| Basement Answer II | Throat | OO → ER → EH, diode, squash 15% | −12.6 | −24.8 |
| Pocket Gremlin II | Squeak | OH → IH → EE, Y glide, T consonant, rise 3 st | −13.6 | −24.8 |
| Squeaky Clean II | Squeak | OO → AH → IH, Y glide, squash 10% | −14.3 | −24.8 |
| Neon Chirp II | Squeak | AH → EH → IH, fold, squash 20%, velocity, legato-only glide | −15.6 | −24.8 |
| Yoi Boi Prime | Rubber | UH → OH → EE, Y glide, K consonant, squash 35%, width 20%, velocity | −15.2 | −24.9 |
| Oi Oi Oi | Rubber | synced talk 1/8, OH → OH → EE, Y glide, squash 30% | −14.2 | −25.1 |
| Y-Glide Gremlin | Squeak | OO → AE → IH, Y glide, rise 5 st, legato-only glide | −13.3 | −24.8 |
| Yoink | Metal | OO → AH → EE, K consonant, drop 12 st, fold, squash 30% | −14.5 | −25.1 |

### Riddim (7)

Tight, chopped and squashed, for half-time riddim patterns.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Riddim Ritual | Metal | synced talk 1/16 triplet, OO → AE → EE, fold, squash 55%, velocity | −15.9 | −25.1 |
| Lunch Tray | Metal | OH → EH → IH, T consonant, crush, squash 45% | −16.3 | −24.8 |
| Knock Knock Riddim | Rubber | OO → UH → AH, K consonant, drop 5 st, diode, squash 50%, velocity | −12.0 | −24.8 |
| Dungeon Step | Throat | synced talk 1/8 triplet, OO → ER → UH, diode, squash 55% | −13.7 | −24.9 |
| Tin Can Shuffle | Metal | synced talk 1/16 triplet, OH → AE → IH, crush, squash 50% | −15.7 | −24.9 |
| Robo Grime | Metal | synced talk 1/16, EH → ER → AE, fold, squash 60%, width 15% | −16.6 | −24.8 |
| Clang Dance | Metal | OO → EH → EE, CH consonant, fold, squash 50%, velocity | −15.4 | −24.8 |

### Talk (7)

The mouth keeps moving while you hold a note: synced talk, mod wheel or aftertouch.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Chatterbox | Rubber | synced talk 1/16, OO → AH → EE, W glide, squash 30% | −13.4 | −25.0 |
| Mouth Off | Rubber | synced talk 1/8 triplet, UH → AE → IH, squash 35%, velocity | −13.5 | −25.0 |
| Say What | Throat | synced talk 1/4, UH → AH → EH, W glide, rise 4 st, diode, squash 25% | −14.0 | −25.2 |
| Back Talk | Rubber | mod-wheel talk, OO → AH → IH, squash 30% | −13.7 | −24.9 |
| Gossip Line | Squeak | synced talk 1/16 triplet, EH → OH → IH, Y glide, T consonant | −13.3 | −25.2 |
| Wah Wah Who | Throat | synced talk 1/8, OO → AH → OO, W glide, squash 30% | −12.2 | −25.1 |
| Yap Trap | Squeak | aftertouch talk, OO → IH → EE, Y glide | −15.1 | −25.1 |

### Growl (7)

Darker vowels (ER, UH) with Diode or Fold drive and heavy Squash.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Grizzly | Throat | OO → ER → UH, diode, squash 50% | −14.0 | −24.9 |
| Gut Rumble | Throat | synced talk 1/4 triplet, UH → ER → OH, diode, squash 45% | −13.1 | −25.2 |
| Snarl Engine | Metal | mod-wheel talk, OO → ER → AE, fold, squash 55% | −16.1 | −25.1 |
| Beast Mode | Throat | synced talk 1/8, OH → ER → AH, fold, squash 60%, velocity | −15.6 | −24.9 |
| Fang Fold | Metal | OO → AE → ER, K consonant, fold, squash 50% | −16.8 | −24.8 |
| Gravel Throat | Throat | UH → OH → ER, crush, squash 40% | −15.1 | −25.0 |
| Howl Pack | Throat | aftertouch talk, OO → AH → ER, diode, squash 45%, width 20% | −13.0 | −24.8 |

### Squelch (6)

Wet, rubbery vowel snaps and acid-style motion.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Squelch Pit | Squeak | OO → AE → IH, squash 35% | −13.4 | −25.1 |
| Wet Sock | Rubber | UH → OH → EH, W glide, diode, squash 30% | −11.4 | −25.2 |
| Acid Gargle | Metal | synced talk 1/16, OO → AH → IH, diode, squash 40%, legato-only glide | −15.2 | −25.2 |
| Bog Blip | Throat | OO → ER → AH, K consonant, drop 6 st | −11.5 | −24.9 |
| Slime Time | Squeak | synced talk 1/8 triplet, EH → OO → IH, squash 40% | −13.9 | −24.8 |
| Plunger | Rubber | OO → UH → OO, rise 7 st, diode, squash 25% | −14.4 | −24.9 |

### Wobble (7)

Synced OO-AH-OO wobbles from 1/16 to a full bar.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Wub Lord | Throat | synced talk 1/8, OO → AH → OO, squash 40% | −12.7 | −24.9 |
| Classic Wub | Rubber | synced talk 1/4, OO → AH → OO, squash 35% | −12.5 | −25.0 |
| Triplet Womp | Throat | synced talk 1/8 triplet, UH → AH → OO, diode, squash 40% | −14.3 | −24.8 |
| Half-Time Wobble | Throat | synced talk 1/2, OO → ER → OO, squash 35% | −13.8 | −25.1 |
| Dotted Drift | Rubber | synced talk 1/8 dotted, OO → AE → OO, squash 35%, width 20% | −12.7 | −25.1 |
| Slow Oo-Ah | Rubber | synced talk 1 bar, OO → AH → OO | −13.0 | −24.8 |
| Wobble Bell | Metal | synced talk 1/16, OO → IH → OO, fold, squash 40% | −14.0 | −24.9 |

### Reese (6)

Wide, slow-moving bodies over a mono sub.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Reese Piece | Throat | synced talk 1 bar, UH → OH → UH, squash 30%, width 60% | −13.2 | −25.2 |
| Dark Reese Talk | Throat | synced talk 1/2, OO → ER → UH, diode, squash 35%, width 50% | −14.1 | −25.2 |
| Widescreen Reese | Rubber | OH → UH → OH, squash 30%, width 85% | −13.0 | −24.8 |
| Neuro Mumble | Metal | synced talk 1/4 triplet, UH → AE → ER, fold, squash 45%, width 45% | −13.8 | −24.8 |
| Rolling Reese | Throat | synced talk 1/4 dotted, OO → UH → OH, squash 35%, width 55% | −13.7 | −25.0 |
| Gutter Reese | Throat | ER → UH → ER, crush, squash 35%, width 40% | −12.1 | −24.9 |

### Sub (6)

Clean, sub-heavy sounds with Sub Reset for an identical punch on every note.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Clean Sub Talk | Throat | OO → UH → OH, sub reset | −14.2 | −25.0 |
| 808 Yoi | Rubber | OO → OH → EE, drop 12 st, sub reset | −13.7 | −25.2 |
| Pressure Drop | Throat | UH → OO → OO, drop 24 st, sub reset | −12.8 | −25.2 |
| Deep Mumble | Throat | synced talk 1/4, OO → ER → OO, sub reset | −14.4 | −25.2 |
| Subterranean | Throat | OO → UH → OO, sub reset, driven sub | −13.7 | −24.8 |
| Sine Speech | Rubber | OO → AH → EE, W glide, velocity, sub reset | −15.4 | −24.9 |

### Space (6)

Wide and airy, with rises and long synced motion.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Space Bass Cadet | Squeak | synced talk 1/4 dotted, OO → AE → IH, fold, squash 40%, width 60% | −14.5 | −25.2 |
| Nebula Yoi | Rubber | OO → OH → IH, Y glide, rise 5 st, width 70% | −14.0 | −25.0 |
| Orbit Talk | Throat | synced talk 1/2 triplet, UH → AE → OO, squash 40%, width 55% | −12.9 | −24.8 |
| Wormhole Wah | Throat | synced talk 1 bar dotted, OO → AH → ER, diode, squash 35%, width 65% | −13.1 | −24.9 |
| Starlight Squeal | Squeak | AH → IH → EE, rise 12 st, fold, squash 30%, width 60%, legato-only glide | −13.1 | −25.1 |
| Zero Gravity | Rubber | OO → UH → AE, width 75% | −12.2 | −25.2 |

### Leads/FX (7)

Higher-register chirps, dives, risers and zaps.

| Sound | Family | What it uses | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Laser Gremlin | Squeak | OH → IH → EE, T consonant, drop 12 st, fold, squash 30% | −13.3 | −25.0 |
| Siren Yoi | Squeak | synced talk 1/2, OO → AH → IH, rise 12 st, width 30%, legato-only glide | −10.7 | −25.1 |
| Dive Bomb | Metal | EE → AH → OO, drop 24 st, fold, squash 45%, bend ±24 | −15.0 | −24.8 |
| Riser Chirp | Squeak | OO → EH → IH, rise 24 st, bend ±12 | −9.5 | −25.0 |
| Glitch Mouth | Metal | synced talk 1/16 triplet, AE → ER → IH, CH consonant, crush, squash 50% | −14.7 | −24.8 |
| Robo Pluck | Metal | OH → EH → IH, T consonant, velocity | −15.7 | −25.0 |
| Zap Reply | Squeak | OO → IH → EE, CH consonant, drop 18 st, fold, squash 25% | −13.0 | −24.8 |

### Classic (12)

The twelve original 0.1.3 sounds on their original engine, unchanged.

| Sound | Family | Vowel motion, length | Peak dBFS | LUFS-ish |
| --- | --- | --- | ---: | ---: |
| Tape Bounce | Rubber | OO → AH → EE, 1/8 | −15.4 | −26.0 |
| Bubble Trouble | Rubber | OO → OH → EE, 1/8 triplet | −18.1 | −29.1 |
| Elastic Yoi | Rubber | OH → AH → EE, 1/8 dotted | −10.4 | −20.7 |
| Chrome Chew | Metal | OO → EH → EE, 1/8 | −11.1 | −22.5 |
| Laser Reply | Metal | OH → EH → EE, 1/16 | −12.6 | −23.9 |
| Steel Syllable | Metal | AH → OH → EE, 1/8 triplet | −11.2 | −21.4 |
| Low Talker | Throat | OO → AH → EE, 1/4 | −16.8 | −28.5 |
| Vowel Throw | Throat | OH → AH → EE, 1/8 dotted | −10.5 | −22.3 |
| Basement Answer | Throat | OO → OH → EH, 1/4 triplet | −17.0 | −28.9 |
| Pocket Gremlin | Squeak | OH → EH → EE, 1/16 | −12.4 | −23.9 |
| Squeaky Clean | Squeak | OO → AH → EE, 1/16 triplet | −21.6 | −32.3 |
| Neon Chirp | Squeak | AH → EH → EE, 1/8 | −12.9 | −22.9 |

## Tips

- For a separate sub track, enable YOI-BOI's **Sub** output in your host. On engine-4 sounds the dedicated sub (Direct or Driven) then plays only there. You can also choose **Sub Off** and keep **Body low-cut** on.
- **ZOOM → Use latest sound (changes tone)** moves a Classic sound to engine 4 without changing its settings. Undo goes back.
- **Mutate** keeps a sound's engine. **New Yoi** always makes an engine-4 sound.
