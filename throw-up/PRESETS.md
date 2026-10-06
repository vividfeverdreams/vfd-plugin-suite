# THROW-UP factory presets

**140 programs:** 90 rooms in 11 folders that play with the latest sound, then the original 50 rooms of 0.1.5 with their original sound.

- **Folders:**
  - Small, Medium, Large, Huge and Creative (10 each): the 0.1.5 rooms reworked for the latest sound.
  - **Synced Throws** (8): pre-delay locked to the host tempo.
  - **Freeze Pads** (6): long, dense rooms made for the FREEZE key.
  - **Shimmer** (8): pitch inside the tail.
  - **Swells** (6): reverse-style rises that cut on the grid.
  - **Sidechain Pumps** (6): strong, timed ducking.
  - **Send FX** (6): rooms for a return track with SEND on.
- **0.1.5 rooms (original sound):** the original 50, unchanged and in their original order. A project saved with 0.1.5 recalls its room from here.
- **What a room changes:** size, decay, pre-delay (milliseconds or a synced note value), low and high cut, diffusion, motion, width, grit, duck (with its release and threshold), shimmer, swell and the sound version.
- **What a room never changes:** THROW, Output, Bypass, the THROW! key and trigger settings, Freeze, Safe, Send, Return and the duck source. Your THROW automation keeps playing while you audition rooms, and choosing the current room again reverts your edits.
- **MIDI:** with NOTE ROOMS on, MIDI note 36 (C1) picks the first room of this list and each higher note the next, through all 90 current rooms.

## How the 0.1.5 rooms were reworked

The rework rules, applied to every 0.1.5 room and then checked with the wet-level audit:

- **Low cut** is set to twice the old label. 0.1.5's low cut was actually about 2.5 times above its label; the accurate cut at 2x keeps the sub just as clean and restores some low-mid body.
- **Grit:** heavy settings come down and light ones go up a little (new = 0.75 x old + 10). Grit now really saturates instead of adding level.
- **Duck** is converted so it gives the same reduction as 0.1.5 at a typical mid-bass level, but in decibels above a -30 dB threshold, with 0.1.5's 160 ms release.
- **Hand-tuned exceptions** keep a bloom after the note: Pocket Lint (low cut 450 Hz) and Mono Nook (400 Hz) are a little fuller; Soft Collision, Iron Lung, Night Reservoir, Crushed Velvet and Orange Oblivion duck less.

## Level check

Every room was rendered at full THROW on three mid-bass sources at -14.9 dBFS RMS (harmonic, reese and stabs), at 48 kHz and 140 BPM for synced rooms. Wet levels are relative to the dry bass. Every room passes its folder's audibility floors: 270 of 270 for the latest rooms, and 150 of 150 for the 0.1.5 rooms, unchanged.

- **Swells** are silent at the start of each cycle by design, so their first-arrival check is not applied.
- **Sidechain Pumps** are ducked under the note by design and bloom afterwards.
- **"Hot riddim"** is a clipped FM growl and a reese at -0.5 dBFS peak (-1.2 and -3.7 dBFS RMS), at full THROW with Output at 0 dB. Turn Return down or press SAFE for loud sources.

| Folder | Rooms | Active wet vs dry, median (lowest) | Strongest 100 ms, median (lowest) | Audit peak, max | Hot riddim peak, median / max |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small | 10 | +1.4 (-4.9) dB | +3.2 (-3.1) dB | +4.2 dBFS | +10.7 / +17.1 dBFS |
| Medium | 10 | -1.3 (-11.9) dB | +1.9 (-4.6) dB | +1.9 dBFS | +10.2 / +11.6 dBFS |
| Large | 10 | -4.3 (-11.7) dB | -1.5 (-6.0) dB | +1.8 dBFS | +10.0 / +14.2 dBFS |
| Huge | 10 | -8.4 (-15.3) dB | +0.9 (-5.1) dB | +0.7 dBFS | +8.8 / +16.2 dBFS |
| Creative | 10 | -8.0 (-15.4) dB | -2.0 (-9.4) dB | +3.7 dBFS | +6.5 / +12.2 dBFS |
| Synced Throws | 8 | -0.2 (-11.4) dB | +7.7 (+1.7) dB | +8.1 dBFS | +11.3 / +12.3 dBFS |
| Freeze Pads | 6 | +0.5 (-4.8) dB | +5.8 (+1.2) dB | +2.8 dBFS | +11.5 / +12.4 dBFS |
| Shimmer | 8 | -0.4 (-7.3) dB | +4.8 (+0.9) dB | +8.2 dBFS | +12.0 / +16.5 dBFS |
| Swells | 6 | -2.3 (-23.3) dB | +6.6 (-2.5) dB | +5.3 dBFS | +12.5 / +13.5 dBFS |
| Sidechain Pumps | 6 | -11.3 (-18.5) dB | +5.6 (-1.5) dB | +4.4 dBFS | +7.4 / +10.4 dBFS |
| Send FX | 6 | -0.3 (-12.1) dB | +3.3 (+0.1) dB | +2.4 dBFS | +11.5 / +12.5 dBFS |
| 0.1.5 Small | 10 | +1.5 (-4.5) dB | +2.8 (-3.5) dB | +6.4 dBFS | +10.5 / +17.1 dBFS |
| 0.1.5 Medium | 10 | -0.5 (-11.6) dB | +3.4 (-4.8) dB | +4.9 dBFS | +8.4 / +11.5 dBFS |
| 0.1.5 Large | 10 | -3.2 (-15.6) dB | +2.4 (-6.9) dB | +5.7 dBFS | +8.1 / +10.8 dBFS |
| 0.1.5 Huge | 10 | -8.4 (-19.5) dB | +2.6 (-7.6) dB | +5.2 dBFS | +11.0 / +13.2 dBFS |
| 0.1.5 Creative | 10 | -5.2 (-20.5) dB | +1.7 (-12.6) dB | +6.2 dBFS | +10.4 / +12.8 dBFS |

## The current rooms

### Small

| Preset | Character |
| --- | --- |
| Pocket Slap | A tiny, dry-edged room for the last note of a bass phrase. |
| Orange Crate | Warm plywood reflections with a soft, low-mid knock. |
| Tile Bite | Bright, tightly packed reflections for talking basses. |
| Pocket Lint | A dark, fuzzy little space that hugs gritty bass stabs. |
| Airlock | A narrow pressure chamber with a clean, delayed entrance. |
| Tin Teeth | Low diffusion exposes a wiry ring around short bass hits. |
| Duck Booth | A compact room that gets out of the way while the bass plays. |
| Mono Nook | Centered, dark ambience for a tightly focused bass break. |
| Static Closet | A fluttery, overdriven chamber for electrical mid-bass. |
| Glass Pebble | A polished, airy tail that clears before the next phrase. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Pocket Slap | 8 | 0.28 | 7 ms | 360 | 5800 | 30 | 4 | 90 | 19 | 8 | — |
| Orange Crate | 14 | 0.42 | 12 ms | 360 | 3700 | 62 | 8 | 100 | 24 | 16 | — |
| Tile Bite | 11 | 0.36 | 3 ms | 720 | 12800 | 42 | 2 | 116 | 16 | 7 | — |
| Pocket Lint | 18 | 0.55 | 9 ms | 450 | 2100 | 78 | 17 | 84 | 49 | 23 | — |
| Airlock | 20 | 0.62 | 34 ms | 440 | 5400 | 70 | 6 | 55 | 13 | 26 | — |
| Tin Teeth | 6 | 0.72 | 0 ms | 1000 | 9800 | 8 | 0 | 125 | 40 | 0 | — |
| Duck Booth | 22 | 0.68 | 16 ms | 400 | 6200 | 84 | 12 | 110 | 18 | 43 | — |
| Mono Nook | 12 | 0.48 | 21 ms | 400 | 2900 | 66 | 0 | 0 | 28 | 15 | — |
| Static Closet | 24 | 0.82 | 4 ms | 840 | 7200 | 22 | 58 | 132 | 64 | 12 | — |
| Glass Pebble | 16 | 0.58 | 27 ms | 1000 | 16200 | 92 | 10 | 145 | 0 | 20 | — |

### Medium

| Preset | Character |
| --- | --- |
| Back Room | A balanced first stop for weighty, syncopated bass throws. |
| Concrete Mouth | Grainy warehouse edges with a low-mid emphasis. |
| Velvet Vent | Dark and dense, with a gentle movement around the source. |
| Chrome Alley | A bright, lean passageway for raspy formant basses. |
| Elastic Room | A distinctly moving room for bent and rubbery bass notes. |
| Side Pocket | Wide upper mids, tucked behind a clean bass attack. |
| Underpass | A gritty concrete pocket with exposed early reflections. |
| Soft Collision | Dense, softened reflections for aggressive reese endings. |
| Bent Stairwell | A spiraling, slightly metallic space with lively motion. |
| After You | A delayed room that blossoms in the gap after the bass. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Back Room | 36 | 1.15 | 24 ms | 420 | 6400 | 76 | 18 | 110 | 21 | 24 | — |
| Concrete Mouth | 42 | 1.45 | 18 ms | 340 | 4200 | 48 | 9 | 120 | 43 | 19 | — |
| Velvet Vent | 38 | 1.75 | 38 ms | 560 | 2300 | 95 | 32 | 128 | 22 | 32 | — |
| Chrome Alley | 46 | 1.28 | 46 ms | 960 | 11600 | 52 | 15 | 138 | 30 | 18 | — |
| Elastic Room | 40 | 1.9 | 11 ms | 600 | 6800 | 82 | 72 | 125 | 27 | 20 | — |
| Side Pocket | 32 | 1.35 | 62 ms | 1000 | 8900 | 88 | 24 | 150 | 15 | 35 | — |
| Underpass | 49 | 1.62 | 28 ms | 380 | 3600 | 28 | 7 | 105 | 54 | 23 | — |
| Soft Collision | 35 | 1.05 | 6 ms | 660 | 5100 | 100 | 26 | 118 | 34 | 30 | — |
| Bent Stairwell | 44 | 2.1 | 33 ms | 780 | 10100 | 35 | 54 | 142 | 39 | 13 | — |
| After You | 47 | 1.85 | 105 ms | 500 | 7600 | 85 | 20 | 124 | 19 | 40 | — |

### Large

| Preset | Character |
| --- | --- |
| Bass Hangar | A broad, controlled warehouse tail for emphatic phrase endings. |
| Rust Cathedral | A rough, dark hall for distorted mid-bass punctuation. |
| Neon Gallery | Bright, smooth upper-mid space with a wide stereo finish. |
| Concrete Bloom | A dense tail that rises behind sustained bass and releases slowly. |
| Hollow Giant | Sparse, cavernous reflections that preserve a grainy character. |
| Liquid Atrium | A flowing, animated hall for watery and stretched bass tones. |
| Highline | An elevated, airy throw with most low mids removed. |
| Smoke Chamber | Muted high frequencies and a thick, softly driven decay. |
| Wide Exit | A clean, open tail with room for the original transient. |
| Iron Lung | Compressed-feeling grit and strong ducking for dense drops. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Bass Hangar | 62 | 2.8 | 36 ms | 440 | 6500 | 82 | 22 | 128 | 24 | 29 | — |
| Rust Cathedral | 72 | 3.65 | 52 ms | 560 | 3300 | 58 | 28 | 136 | 60 | 26 | — |
| Neon Gallery | 60 | 2.95 | 68 ms | 1000 | 13200 | 94 | 32 | 150 | 16 | 32 | — |
| Concrete Bloom | 68 | 3.2 | 22 ms | 520 | 5600 | 100 | 18 | 125 | 30 | 44 | — |
| Hollow Giant | 77 | 4.1 | 14 ms | 380 | 4700 | 18 | 4 | 116 | 36 | 18 | — |
| Liquid Atrium | 65 | 3.55 | 44 ms | 700 | 8400 | 88 | 76 | 140 | 25 | 28 | — |
| Highline | 58 | 2.45 | 92 ms | 1000 | 17000 | 91 | 40 | 145 | 13 | 36 | — |
| Smoke Chamber | 70 | 4.4 | 30 ms | 480 | 1850 | 96 | 45 | 130 | 46 | 34 | — |
| Wide Exit | 74 | 3.15 | 125 ms | 640 | 9300 | 80 | 12 | 150 | 0 | 38 | — |
| Iron Lung | 67 | 3.8 | 20 ms | 820 | 5900 | 68 | 35 | 122 | 60 | 34 | — |

### Huge

| Preset | Character |
| --- | --- |
| Event Horizon | A vast, dark decay for the empty space after a drop. |
| Orbit Debris | Long, scattered reflections orbit a rough-edged bass hit. |
| Amber Ocean | A warm, densely moving wash with a slow and generous decay. |
| Empty Terminal | A cold, towering room with a clearly separated entrance. |
| Subtracted Sky | An immense, airy tail that stays above the body of the bass. |
| Deep End | A slow, centered cavern with tightly shaded upper frequencies. |
| Night Reservoir | A huge, breathing dark space for sparse halftime arrangements. |
| Satellite Ghost | A distant, shifting tail with a very late arrival. |
| Endless Stair | An intentionally exposed, towering network of reflections. |
| Last Light | A smooth maximum-length send-off for a final bass note. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Event Horizon | 94 | 9.5 | 78 ms | 520 | 3100 | 96 | 30 | 145 | 24 | 35 | — |
| Orbit Debris | 88 | 7.2 | 42 ms | 760 | 9200 | 26 | 48 | 150 | 58 | 21 | — |
| Amber Ocean | 100 | 11 | 115 ms | 600 | 4900 | 100 | 68 | 144 | 31 | 37 | — |
| Empty Terminal | 90 | 6.4 | 165 ms | 840 | 11800 | 72 | 14 | 137 | 15 | 28 | — |
| Subtracted Sky | 96 | 12.5 | 92 ms | 1000 | 17500 | 95 | 45 | 150 | 0 | 39 | — |
| Deep End | 85 | 8.8 | 35 ms | 320 | 1600 | 88 | 20 | 80 | 36 | 32 | — |
| Night Reservoir | 98 | 14 | 140 ms | 680 | 2600 | 100 | 58 | 132 | 27 | 36 | — |
| Satellite Ghost | 92 | 10.5 | 225 ms | 1000 | 10800 | 84 | 84 | 148 | 18 | 40 | — |
| Endless Stair | 100 | 16 | 8 ms | 900 | 6800 | 12 | 8 | 125 | 45 | 18 | — |
| Last Light | 97 | 18 | 185 ms | 1000 | 7600 | 98 | 38 | 150 | 19 | 43 | — |

### Creative

| Preset | Character |
| --- | --- |
| Sewer Halo | A filthy small space with an unusually long, wobbling tail. |
| Rubber Universe | Maximum motion stretches a giant, diffuse bass afterimage. |
| Needle Rain | Tiny, bright, low-diffusion reflections persist like metallic rain. |
| Late Regret | The maximum predelay leaves a dramatic gap before a dirty room. |
| Vacuum Kiss | Maximum ducking makes a long tail surge into the rests. |
| Phone In A Cave | A tightly band-limited mono tail for a strange, distant voice. |
| Crushed Velvet | Full wet drive inside a very dark, densely packed room. |
| Wrong Elevator | A tall space with a short decay and unstable-feeling motion. |
| Glitter Gutter | Bright grit, wide reflections, and strong motion leave a jagged trail. |
| Orange Oblivion | A maximal, dirty, breathing cloud for the final impossible throw. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Sewer Halo | 7 | 6.8 | 18 ms | 840 | 3200 | 16 | 100 | 145 | 85 | 26 | — |
| Rubber Universe | 100 | 7.7 | 65 ms | 720 | 7900 | 98 | 100 | 150 | 42 | 37 | — |
| Needle Rain | 2 | 4.5 | 0 ms | 1000 | 18000 | 0 | 3 | 146 | 21 | 0 | — |
| Late Regret | 52 | 3.8 | 250 ms | 600 | 6300 | 60 | 32 | 135 | 66 | 31 | — |
| Vacuum Kiss | 80 | 8.4 | 10 ms | 1000 | 4400 | 100 | 60 | 150 | 36 | 48 | — |
| Phone In A Cave | 86 | 5.2 | 110 ms | 1000 | 1500 | 78 | 16 | 0 | 57 | 12 | — |
| Crushed Velvet | 43 | 2.6 | 6 ms | 600 | 1000 | 100 | 40 | 118 | 85 | 28 | — |
| Wrong Elevator | 100 | 0.7 | 75 ms | 520 | 9600 | 10 | 95 | 70 | 51 | 16 | — |
| Glitter Gutter | 28 | 5.5 | 48 ms | 1000 | 15500 | 38 | 88 | 150 | 78 | 29 | — |
| Orange Oblivion | 100 | 18 | 200 ms | 1000 | 8800 | 100 | 100 | 150 | 76 | 38 | — |

### Synced Throws

| Preset | Character |
| --- | --- |
| Sixteenth Slap | A tight room that answers each note a sixteenth later. |
| Eighth Echo Room | An eighth-note pre-delay turns the room into a rhythmic answer. |
| Dotted Drop | A dotted-eighth bloom that swings against straight riddim patterns. |
| Triplet Bounce | Triplet-eighth arrival for bouncy, rolling mid-bass. |
| Quarter Hall | A hall that enters on the next beat, clear of the original hit. |
| Half-Time Gap | A half-note gap, then a big dark space for halftime drops. |
| Dotted Quarter Bloom | A late, wide bloom a dotted quarter behind the bass. |
| Thirty-Second Rattle | A tiny bright room on a thirty-second grid for rattling stabs. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Sixteenth Slap | 18 | 0.6 | 1/16 | 300 | 6000 | 60 | 6 | 115 | 18 | 20 | — |
| Eighth Echo Room | 34 | 1.3 | 1/8 | 320 | 5500 | 55 | 10 | 125 | 22 | 28 | — |
| Dotted Drop | 48 | 2.2 | 1/8 dotted | 280 | 6200 | 70 | 14 | 135 | 20 | 36 | — |
| Triplet Bounce | 30 | 1.1 | 1/8 triplet | 350 | 7500 | 50 | 12 | 130 | 24 | 24 | — |
| Quarter Hall | 70 | 3.6 | 1/4 | 260 | 5200 | 88 | 18 | 140 | 14 | 40 | — |
| Half-Time Gap | 82 | 5.5 | 1/2 | 300 | 4400 | 92 | 22 | 145 | 12 | 48 | — |
| Dotted Quarter Bloom | 64 | 4.2 | 1/4 dotted | 340 | 6800 | 90 | 25 | 142 | 18 | 44 | — |
| Thirty-Second Rattle | 10 | 0.45 | 1/32 | 450 | 9500 | 30 | 4 | 120 | 32 | 10 | — |

### Freeze Pads

| Preset | Character |
| --- | --- |
| Ice Sheet | Press FREEZE on a held note for a smooth, wide pad. |
| Frozen Sub Choir | A dark, chorused hold that sits under the next phrase. |
| Glass Drone | A bright octave-lit drone; freeze it for a shimmering bed. |
| Static Hold | A gritty, buzzing hold for electrical, glitchy breaks. |
| Cold Storage | Huge and soft; freeze a stab into a slow, dark cloud. |
| Permafrost | The longest, widest room in the bank, made to be frozen. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Ice Sheet | 90 | 12 | 20 ms | 300 | 5000 | 100 | 30 | 140 | 12 | 22 | — |
| Frozen Sub Choir | 80 | 9 | 12 ms | 220 | 2800 | 96 | 40 | 130 | 18 | 30 | — |
| Glass Drone | 70 | 10 | 10 ms | 600 | 12000 | 92 | 20 | 145 | 10 | 22 | Shimmer 20% +12 st |
| Static Hold | 50 | 8 | 6 ms | 400 | 7000 | 70 | 55 | 135 | 50 | 20 | — |
| Cold Storage | 95 | 15 | 30 ms | 260 | 3600 | 100 | 15 | 120 | 14 | 28 | — |
| Permafrost | 100 | 18 | 40 ms | 350 | 6200 | 100 | 35 | 150 | 22 | 25 | — |

### Shimmer

| Preset | Character |
| --- | --- |
| Octave Halo | An octave-up halo rises out of the tail behind melodic basses. |
| Fifth Heaven | A fifth above the bass blooms into a bright, open chord. |
| Sub Shadow | An octave-down shadow: dark space-bass weight in the tail. |
| Falling Fourth | A fourth below the note sinks slowly into the reverb. |
| Riser Glow | Strong octave climb with a long tail, for build-ups. |
| Dark Mirror | A heavy octave-down mirror in a dark, dense room. |
| Crystal Throw | A short, synced shimmer throw that sparkles on the eighth. |
| Nebula Drift | A huge, moving fifth-shimmer cloud for space-bass outros. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Octave Halo | 70 | 6 | 24 ms | 400 | 10000 | 95 | 20 | 145 | 0 | 30 | Shimmer 45% +12 st |
| Fifth Heaven | 64 | 5 | 30 ms | 380 | 9000 | 92 | 18 | 140 | 8 | 30 | Shimmer 40% +7 st |
| Sub Shadow | 75 | 5 | 18 ms | 180 | 3000 | 94 | 16 | 130 | 18 | 34 | Shimmer 40% -12 st |
| Falling Fourth | 60 | 4.2 | 20 ms | 300 | 6000 | 90 | 22 | 135 | 10 | 30 | Shimmer 35% -5 st |
| Riser Glow | 88 | 9 | 40 ms | 450 | 12500 | 98 | 26 | 150 | 0 | 40 | Shimmer 60% +12 st |
| Dark Mirror | 82 | 8 | 28 ms | 250 | 2400 | 98 | 30 | 135 | 24 | 40 | Shimmer 60% -12 st |
| Crystal Throw | 40 | 2.4 | 1/8 | 420 | 11000 | 85 | 12 | 140 | 6 | 24 | Shimmer 30% +12 st |
| Nebula Drift | 100 | 12 | 60 ms | 360 | 8000 | 100 | 60 | 150 | 10 | 50 | Shimmer 55% +7 st |

### Swells

| Preset | Character |
| --- | --- |
| Bar Swell | The tail rises through each bar and cuts on the downbeat. |
| Two-Bar Rise | A long, two-bar suck-in for the gap before a drop. |
| Half-Bar Suck | A half-bar reverse pull for fills and transitions. |
| Quarter Pump | Beat-length swells that pump the space on every quarter. |
| Reverse Drop | A dark bar-long swell with an octave-down shadow. |
| Pre-Drop Wash | A bright two-bar wash with an octave-up glow into the drop. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Bar Swell | 80 | 6 | 20 ms | 300 | 6000 | 95 | 22 | 140 | 10 | 0 | Swell 1 bar |
| Two-Bar Rise | 95 | 10 | 30 ms | 320 | 7000 | 98 | 28 | 148 | 12 | 0 | Swell 2 bars |
| Half-Bar Suck | 60 | 3 | 10 ms | 350 | 6500 | 90 | 16 | 135 | 14 | 0 | Swell 1/2 |
| Quarter Pump | 40 | 1.8 | 0 ms | 380 | 7500 | 85 | 12 | 130 | 16 | 0 | Swell 1/4 |
| Reverse Drop | 85 | 7 | 20 ms | 260 | 4000 | 96 | 24 | 140 | 18 | 0 | Shimmer 25% -12 st; Swell 1 bar |
| Pre-Drop Wash | 96 | 14 | 40 ms | 420 | 11000 | 100 | 30 | 150 | 0 | 0 | Shimmer 30% +12 st; Swell 2 bars |

### Sidechain Pumps

| Preset | Character |
| --- | --- |
| Kick Pocket | A medium tail that drops out under every hit and pumps back fast. |
| Snare Gate Hall | A big hall that clears for the snare and breathes back. |
| Halftime Breather | Slow, deep breathing for halftime riddim and wobble. |
| Sixteenth Pump | A fast pump on a sixteenth-synced room for busy patterns. |
| Swing Duck | Dotted-eighth arrival with a swung, ducked release. |
| Wobble Breath | A moving room that ducks hard and wobbles back into gaps. |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Kick Pocket | 60 | 3 | 18 ms | 300 | 6000 | 88 | 16 | 135 | 14 | 80 | Duck release 120 ms, threshold -30 dB |
| Snare Gate Hall | 75 | 4.5 | 24 ms | 320 | 6800 | 92 | 18 | 140 | 12 | 90 | Duck release 250 ms, threshold -24 dB |
| Halftime Breather | 85 | 6 | 30 ms | 280 | 5200 | 95 | 20 | 140 | 16 | 75 | Duck release 400 ms, threshold -30 dB |
| Sixteenth Pump | 40 | 2 | 1/16 | 350 | 7000 | 80 | 10 | 130 | 18 | 70 | Duck release 60 ms, threshold -28 dB |
| Swing Duck | 55 | 2.8 | 1/8 dotted | 330 | 6400 | 86 | 14 | 135 | 16 | 78 | Duck release 180 ms, threshold -26 dB |
| Wobble Breath | 58 | 3.4 | 12 ms | 300 | 6000 | 84 | 60 | 138 | 22 | 65 | Duck release 90 ms, threshold -24 dB |

### Send FX

| Preset | Character |
| --- | --- |
| Return Hall | A general-purpose, gently ducked hall for a send/return track (DRY off). |
| Return Plate | A dense, bright, plate-like return for snares and stabs (DRY off). |
| Return Space | A huge, wide return for big throws and atmospheres (DRY off). |
| Return Dirt | A driven return to put OTT-style grit after the space (DRY off). |
| Return Shimmer | An octave-up shimmer return for melodic layers (DRY off). |
| Return Slapback | A synced sixteenth slap return for tight rhythmic doubling (DRY off). |

| Preset | Size % | Decay s | Pre-delay | Low cut Hz | High cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % | Extras |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Return Hall | 72 | 3.4 | 30 ms | 420 | 7000 | 92 | 18 | 130 | 20 | 26 | — |
| Return Plate | 30 | 1.8 | 8 ms | 520 | 10000 | 100 | 12 | 140 | 22 | 24 | — |
| Return Space | 96 | 9 | 40 ms | 480 | 7000 | 98 | 30 | 150 | 18 | 30 | — |
| Return Dirt | 50 | 2.5 | 12 ms | 450 | 6000 | 80 | 20 | 130 | 60 | 22 | — |
| Return Shimmer | 80 | 7 | 30 ms | 560 | 11000 | 96 | 22 | 145 | 16 | 28 | Shimmer 40% +12 st |
| Return Slapback | 12 | 0.4 | 1/16 | 480 | 8000 | 25 | 4 | 120 | 18 | 18 | — |

## 0.1.5 rooms (original sound)

These are the 0.1.5 values. Low cut here is the 0.1.5 control value; with the 0.1.5 sound its tail is actually about 2.5 times higher.

### 0.1.5 Small

| Preset | Character |
| --- | --- |
| Pocket Slap | A tiny, dry-edged room for the last note of a bass phrase. |
| Orange Crate | Warm plywood reflections with a soft, low-mid knock. |
| Tile Bite | Bright, tightly packed reflections for talking basses. |
| Pocket Lint | A dark, fuzzy little space that hugs gritty bass stabs. |
| Airlock | A narrow pressure chamber with a clean, delayed entrance. |
| Tin Teeth | Low diffusion exposes a wiry ring around short bass hits. |
| Duck Booth | A compact room that gets out of the way while the bass plays. |
| Mono Nook | Centered, dark ambience for a tightly focused bass break. |
| Static Closet | A fluttery, overdriven chamber for electrical mid-bass. |
| Glass Pebble | A polished, airy tail that clears before the next phrase. |

| Preset | Size % | Decay s | Pre ms | Low-cut Hz | High-cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Pocket Slap | 8 | 0.28 | 7 | 180 | 5800 | 30 | 4 | 90 | 12 | 8 |
| Orange Crate | 14 | 0.42 | 12 | 180 | 3700 | 62 | 8 | 100 | 18 | 18 |
| Tile Bite | 11 | 0.36 | 3 | 360 | 12800 | 42 | 2 | 116 | 8 | 6 |
| Pocket Lint | 18 | 0.55 | 9 | 300 | 2100 | 78 | 17 | 84 | 52 | 28 |
| Airlock | 20 | 0.62 | 34 | 220 | 5400 | 70 | 6 | 55 | 4 | 35 |
| Tin Teeth | 6 | 0.72 | 0 | 520 | 9800 | 8 | 0 | 125 | 40 | 0 |
| Duck Booth | 22 | 0.68 | 16 | 200 | 6200 | 84 | 12 | 110 | 10 | 82 |
| Mono Nook | 12 | 0.48 | 21 | 260 | 2900 | 66 | 0 | 0 | 24 | 16 |
| Static Closet | 24 | 0.82 | 4 | 420 | 7200 | 22 | 58 | 132 | 72 | 12 |
| Glass Pebble | 16 | 0.58 | 27 | 600 | 16200 | 92 | 10 | 145 | 0 | 24 |

### 0.1.5 Medium

| Preset | Character |
| --- | --- |
| Back Room | A balanced first stop for weighty, syncopated bass throws. |
| Concrete Mouth | Grainy warehouse edges with a low-mid emphasis. |
| Velvet Vent | Dark and dense, with a gentle movement around the source. |
| Chrome Alley | A bright, lean passageway for raspy formant basses. |
| Elastic Room | A distinctly moving room for bent and rubbery bass notes. |
| Side Pocket | Wide upper mids, tucked behind a clean bass attack. |
| Underpass | A gritty concrete pocket with exposed early reflections. |
| Soft Collision | Dense, softened reflections for aggressive reese endings. |
| Bent Stairwell | A spiraling, slightly metallic space with lively motion. |
| After You | A delayed room that blossoms in the gap after the bass. |

| Preset | Size % | Decay s | Pre ms | Low-cut Hz | High-cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Back Room | 36 | 1.15 | 24 | 210 | 6400 | 76 | 18 | 110 | 14 | 30 |
| Concrete Mouth | 42 | 1.45 | 18 | 170 | 4200 | 48 | 9 | 120 | 44 | 22 |
| Velvet Vent | 38 | 1.75 | 38 | 280 | 2300 | 95 | 32 | 128 | 16 | 48 |
| Chrome Alley | 46 | 1.28 | 46 | 480 | 11600 | 52 | 15 | 138 | 26 | 20 |
| Elastic Room | 40 | 1.9 | 11 | 300 | 6800 | 82 | 72 | 125 | 22 | 24 |
| Side Pocket | 32 | 1.35 | 62 | 560 | 8900 | 88 | 24 | 150 | 6 | 55 |
| Underpass | 49 | 1.62 | 28 | 190 | 3600 | 28 | 7 | 105 | 58 | 28 |
| Soft Collision | 35 | 1.05 | 6 | 330 | 5100 | 100 | 26 | 118 | 32 | 62 |
| Bent Stairwell | 44 | 2.1 | 33 | 390 | 10100 | 35 | 54 | 142 | 38 | 14 |
| After You | 47 | 1.85 | 105 | 250 | 7600 | 85 | 20 | 124 | 12 | 72 |

### 0.1.5 Large

| Preset | Character |
| --- | --- |
| Bass Hangar | A broad, controlled warehouse tail for emphatic phrase endings. |
| Rust Cathedral | A rough, dark hall for distorted mid-bass punctuation. |
| Neon Gallery | Bright, smooth upper-mid space with a wide stereo finish. |
| Concrete Bloom | A dense tail that rises behind sustained bass and releases slowly. |
| Hollow Giant | Sparse, cavernous reflections that preserve a grainy character. |
| Liquid Atrium | A flowing, animated hall for watery and stretched bass tones. |
| Highline | An elevated, airy throw with most low mids removed. |
| Smoke Chamber | Muted high frequencies and a thick, softly driven decay. |
| Wide Exit | A clean, open tail with room for the original transient. |
| Iron Lung | Compressed-feeling grit and strong ducking for dense drops. |

| Preset | Size % | Decay s | Pre ms | Low-cut Hz | High-cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Bass Hangar | 62 | 2.8 | 36 | 220 | 6500 | 82 | 22 | 128 | 18 | 42 |
| Rust Cathedral | 72 | 3.65 | 52 | 280 | 3300 | 58 | 28 | 136 | 66 | 34 |
| Neon Gallery | 60 | 2.95 | 68 | 500 | 13200 | 94 | 32 | 150 | 8 | 48 |
| Concrete Bloom | 68 | 3.2 | 22 | 260 | 5600 | 100 | 18 | 125 | 26 | 85 |
| Hollow Giant | 77 | 4.1 | 14 | 190 | 4700 | 18 | 4 | 116 | 35 | 20 |
| Liquid Atrium | 65 | 3.55 | 44 | 350 | 8400 | 88 | 76 | 140 | 20 | 38 |
| Highline | 58 | 2.45 | 92 | 780 | 17000 | 91 | 40 | 145 | 4 | 60 |
| Smoke Chamber | 70 | 4.4 | 30 | 240 | 1850 | 96 | 45 | 130 | 48 | 54 |
| Wide Exit | 74 | 3.15 | 125 | 320 | 9300 | 80 | 12 | 150 | 0 | 65 |
| Iron Lung | 67 | 3.8 | 20 | 410 | 5900 | 68 | 35 | 122 | 85 | 94 |

### 0.1.5 Huge

| Preset | Character |
| --- | --- |
| Event Horizon | A vast, dark decay for the empty space after a drop. |
| Orbit Debris | Long, scattered reflections orbit a rough-edged bass hit. |
| Amber Ocean | A warm, densely moving wash with a slow and generous decay. |
| Empty Terminal | A cold, towering room with a clearly separated entrance. |
| Subtracted Sky | An immense, airy tail that stays above the body of the bass. |
| Deep End | A slow, centered cavern with tightly shaded upper frequencies. |
| Night Reservoir | A huge, breathing dark space for sparse halftime arrangements. |
| Satellite Ghost | A distant, shifting tail with a very late arrival. |
| Endless Stair | An intentionally exposed, towering network of reflections. |
| Last Light | A smooth maximum-length send-off for a final bass note. |

| Preset | Size % | Decay s | Pre ms | Low-cut Hz | High-cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Event Horizon | 94 | 9.5 | 78 | 260 | 3100 | 96 | 30 | 145 | 18 | 55 |
| Orbit Debris | 88 | 7.2 | 42 | 380 | 9200 | 26 | 48 | 150 | 64 | 25 |
| Amber Ocean | 100 | 11.0 | 115 | 300 | 4900 | 100 | 68 | 144 | 28 | 62 |
| Empty Terminal | 90 | 6.4 | 165 | 420 | 11800 | 72 | 14 | 137 | 6 | 38 |
| Subtracted Sky | 96 | 12.5 | 92 | 900 | 17500 | 95 | 45 | 150 | 0 | 68 |
| Deep End | 85 | 8.8 | 35 | 160 | 1600 | 88 | 20 | 80 | 34 | 48 |
| Night Reservoir | 98 | 14.0 | 140 | 340 | 2600 | 100 | 58 | 132 | 22 | 88 |
| Satellite Ghost | 92 | 10.5 | 225 | 610 | 10800 | 84 | 84 | 148 | 10 | 72 |
| Endless Stair | 100 | 16.0 | 8 | 450 | 6800 | 12 | 8 | 125 | 46 | 20 |
| Last Light | 97 | 18.0 | 185 | 520 | 7600 | 98 | 38 | 150 | 12 | 80 |

### 0.1.5 Creative

| Preset | Character |
| --- | --- |
| Sewer Halo | A filthy small space with an unusually long, wobbling tail. |
| Rubber Universe | Maximum motion stretches a giant, diffuse bass afterimage. |
| Needle Rain | Tiny, bright, low-diffusion reflections persist like metallic rain. |
| Late Regret | The maximum predelay leaves a dramatic gap before a dirty room. |
| Vacuum Kiss | Maximum ducking makes a long tail surge into the rests. |
| Phone In A Cave | A tightly band-limited mono tail for a strange, distant voice. |
| Crushed Velvet | Full wet drive inside a very dark, densely packed room. |
| Wrong Elevator | A tall space with a short decay and unstable-feeling motion. |
| Glitter Gutter | Bright grit, wide reflections, and strong motion leave a jagged trail. |
| Orange Oblivion | A maximal, dirty, breathing cloud for the final impossible throw. |

| Preset | Size % | Decay s | Pre ms | Low-cut Hz | High-cut Hz | Diffusion % | Motion % | Width % | Grit % | Duck % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Sewer Halo | 7 | 6.8 | 18 | 420 | 3200 | 16 | 100 | 145 | 100 | 34 |
| Rubber Universe | 100 | 7.7 | 65 | 360 | 7900 | 98 | 100 | 150 | 42 | 62 |
| Needle Rain | 2 | 4.5 | 0 | 1000 | 18000 | 0 | 3 | 146 | 14 | 0 |
| Late Regret | 52 | 3.8 | 250 | 300 | 6300 | 60 | 32 | 135 | 75 | 45 |
| Vacuum Kiss | 80 | 8.4 | 10 | 540 | 4400 | 100 | 60 | 150 | 34 | 100 |
| Phone In A Cave | 86 | 5.2 | 110 | 1000 | 1500 | 78 | 16 | 0 | 62 | 12 |
| Crushed Velvet | 43 | 2.6 | 6 | 380 | 1000 | 100 | 40 | 118 | 100 | 74 |
| Wrong Elevator | 100 | 0.7 | 75 | 260 | 9600 | 10 | 95 | 70 | 55 | 18 |
| Glitter Gutter | 28 | 5.5 | 48 | 850 | 15500 | 38 | 88 | 150 | 90 | 42 |
| Orange Oblivion | 100 | 18.0 | 200 | 620 | 8800 | 100 | 100 | 150 | 88 | 92 |

Version 0.1.4 calibrates the wet return 18 dB louder than 0.1.3 across the entire bank. Room settings and preset names are unchanged. At high THROW values, trim Output if the combined dry and reverb level is too hot.
