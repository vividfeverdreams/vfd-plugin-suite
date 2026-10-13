# BACK-SCRATCH 0.11.0 presets

Version 0.11 has **230 built-in presets in 17 folders**. Open them from the **⌄** menu beside the preset name, or step through them with the **upper arrows**, which follow the menu order folder by folder. Every preset processes the track's incoming audio; nothing is captured.

| Folder | Presets | What is in it |
| --- | --- | --- |
| Bass | 25 | The 25 original bass recipes, with the Keep Sub split lowered to 120 Hz. |
| Drums | 25 | The 25 original drum recipes, with the Keep Sub split lowered to 120 Hz. |
| Vocals | 25 | The 25 original vocal recipes (200 Hz split, as before). |
| Fills | 11 | One-bar fills that play only on the last bar of every 2, 4 or 8 bars. Leave them on the track. |
| Rolls | 10 | Beat-repeat rolls from 1/8 to 1/64, including accelerating, triplet and reverse rolls. |
| Stutters | 8 | Short repeats, gates and glitch cuts inside the groove. |
| Scratches | 10 | New hand-style scratches, plus four original recipes pushed deeper or tighter with Depth. |
| Tape Stops | 6 | Wind-downs, spin-ups and power cuts. |
| Builds | 6 | Two-, four- and eight-bar risers that speed up into the drop. |
| Halftime | 6 | Half-speed and quarter-speed drags. |
| Vocal Chops | 7 | Word repeats, stammers, reversed words and look-back answers. |
| Riddim | 6 | Tight chops, repeats and stop-starts for riddim and dubstep bass. |
| Space Bass | 6 | Deep, blended drifts using Depth, Mix and Look Back. |
| Chains | 4 | Two, four or eight different bars that take turns, using pattern slots. |
| Classic/Bass, Classic/Drums, Classic/Vocals | 75 | The 75 original recipes exactly as 0.10 selected them, with every 0.11 setting neutral. |

**How a preset loads.** A new preset (Fills to Chains) loads its pattern as an editable drawing, with its own phrase length, timing and 0.11 settings (Engage Snap, Retrigger, Play Every, Chain, Mix, Depth, Keep Sub Split, Look Back), and resets the grid shift to zero. They set their phrase length even when the length lock is on, because their rhythm is written for that length; turn the knob afterwards to retime them. Chain presets also fill slots B–H; other new presets empty them. Engage, Output, Keep Sub on/off, the length lock and the History setting are left as you have them. Like loading a file, a new preset starts a fresh Undo history. The original recipes (Bass, Drums, Vocals, Classic) select the recipe the same way 0.10 did: Phrase Length Lock, timing and Keep Sub behave as before. **Classic** entries also return the 0.11 settings to neutral (snap 1/16, Retrigger off, every phrase, no chain, Mix 100%, Depth 100%, 200 Hz split, no Look Back), so they sound exactly like 0.10. The tuned Bass/Drums/Vocals entries apply their own split instead.

**With slot B–H selected**, choosing a preset only puts its pattern into that slot (one undoable step); the rest of your settings stay as they are. Select slot A to load complete presets.

## New presets in 0.11

"Plays" is the Play Every setting: "every 4th" means the pattern plays on bars 4, 8, 12 and so on (counted from the start of the song, or from where you engaged with Retrigger on), and the other bars pass through clean. "Engage snaps to" is the Engage Snap the preset sets. A Keep Sub split of 120 Hz keeps only the sub and kick fundamental clean, so more of the bass growl or snare body is scratched.

### Fills

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Bar 4 Rewind | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, beat four rewinds one beat into the downbeat. |
| Bar 8 Tape Stop | 1 bar | every 8th | Keep Sub split 120 Hz, Engage snaps to the bar | Every eighth bar, the second half of the bar winds down to a stop. |
| Last Beat Roll | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, beat four rolls in sixteenths, then thirty-seconds. |
| Turnaround Chirps | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, three triplet chirps close the phrase. |
| Pre-Drop Rewind | 1 bar | every 8th | Keep Sub split 120 Hz, Engage snaps to the bar | Every eighth bar, a deep two-beat rewind pulls back before the drop. |
| Snare Flip | 1 bar | every 2nd | Keep Sub split 120 Hz, Engage snaps to the bar | Every second bar, the last beat flips its snare backwards in sixteenths. |
| Half Bar Spin | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, the back half drops to half speed. |
| Gate Rush | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, beat four is chopped in thirty-seconds. |
| Double Back | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, a short push on beat three and a rewind on beat four. |
| Stop & Drop | 1 bar | every 4th | Keep Sub split 120 Hz, Engage snaps to the bar | Every fourth bar, beat four stops dead and spins back up. |
| Flashback | 1 bar | every 2nd | Keep Sub split 200 Hz, Look Back 2 bars, Engage snaps to the bar | Every second bar, the second half replays the line from two bars earlier. |

### Rolls

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Roll 1/8 | 1 bar | every phrase | Keep Sub split 120 Hz | The second half of the bar repeats in eighth notes. |
| Roll 1/16 | 1 bar | every phrase | Keep Sub split 120 Hz | The second half of the bar repeats in sixteenths. |
| Roll 1/32 | 1 bar | every phrase | Keep Sub split 120 Hz | Beat four repeats in thirty-seconds. |
| Accelerating Roll | 1 bar | every phrase | Keep Sub split 120 Hz | Eighths, sixteenths, then thirty-seconds over the second half. |
| Triplet Roll | 1 bar | every phrase | Keep Sub split 120 Hz | The second half repeats in sixteenth-note triplets. |
| Reverse Roll | 1 bar | every phrase | Keep Sub split 120 Hz | The second half repeats a sixteenth backwards. |
| Chopped Roll | 1 bar | every phrase | Keep Sub split 120 Hz | Sixteenth repeats with a short crossfader gap between hits. |
| Off-Beat Rolls | 1 bar | every phrase | Keep Sub split 120 Hz | Each off-beat eighth stutters in thirty-seconds. |
| Machine Gun | 1 bar | every phrase | Keep Sub split 120 Hz | The last eighth fires a sixty-fourth-note burst. |
| Riser Roll | 2 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Two bars: quarters to thirty-seconds on one hit across the second bar. |

### Stutters

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Beat Repeat 1/4 | 1 bar | every phrase | Keep Sub split 120 Hz | Beats one and three each play twice. |
| Sixteenth Stutter | 1 bar | every phrase | Keep Sub split 120 Hz | Beats two and four stutter in gapped sixteenths. |
| Stutter Gate 1/32 | 1 bar | every phrase | Keep Sub split 120 Hz | The second half is chopped in thirty-seconds. |
| Double Tap | 1 bar | every phrase | Keep Sub split 120 Hz | The first eighth of every beat plays twice. |
| Stutter Edit | 1 bar | every phrase | Keep Sub split 120 Hz | Mixed sixteenth, thirty-second and sixty-fourth repeats. |
| Syncopated Repeat | 1 bar | every phrase | Keep Sub split 120 Hz | Off-beat eighths repeat against the groove. |
| Triplet Stutter | 1 bar | every phrase | Keep Sub split 120 Hz | Beats two and four repeat in sixteenth triplets. |
| Glitch Cut | 1 bar | every phrase | Keep Sub split 120 Hz | A thirty-second burst, a sixty-fourth gate, then a gapped roll. |

### Scratches

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Baby Pocket | 1 bar | every phrase | Keep Sub split 120 Hz | Two rounded baby scratches between the kick and snare. |
| Chirp Flare | 1 bar | every phrase | Keep Sub split 120 Hz | Quick chirps with the crossfader closing on each return. |
| Transformer | 1 bar | every phrase | Keep Sub split 120 Hz | A slow push and pull chopped in thirty-seconds. |
| Tear Scratch | 1 bar | every phrase | Keep Sub split 120 Hz | A backward tear on beat two answered by a push. |
| Rubber Pull | 1 bar | every phrase | Keep Sub split 120 Hz | Two long elastic strokes across the bar. |
| Crab Fill | 1 bar | every phrase | Keep Sub split 120 Hz | Beat four transformer chops in sixty-fourths. |
| Sub Tug Lift | recipe length (unless locked) | every phrase | original recipe #1, Keep Sub split 120 Hz, Depth 125% | Sub Tug with deeper strokes over a 120 Hz Keep Sub split. |
| Rubber Teeth Wide | recipe length (unless locked) | every phrase | original recipe #2, Keep Sub split 120 Hz, Depth 145% | Rubber Teeth pulled 45% further. |
| Switchblade Tight | recipe length (unless locked) | every phrase | original recipe #6, Keep Sub split 120 Hz, Depth 70% | Switchblade with short, tight strokes. |
| Tape Throat Deep | recipe length (unless locked) | every phrase | original recipe #5, Keep Sub split 120 Hz, Depth 160% | Tape Throat bent 60% deeper. |

### Tape Stops

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Stop 1 Beat | 1 bar | every phrase | Keep Sub split 120 Hz | Beat four winds down to a stop. |
| Stop Half Bar | 1 bar | every phrase | Keep Sub split 120 Hz | The second half winds down to a stop. |
| Stop Full Bar | 2 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Two bars: the second bar winds down to a stop. |
| Spin Down Spin Up | 1 bar | every phrase | Keep Sub split 120 Hz | Beat three stops, beat four spins back up. |
| Power Cut | 1 bar | every phrase | Keep Sub split 120 Hz | A sixteenth-long stop, then silence to the bar line. |
| Brake Tail | 1 bar | every phrase | Keep Sub split 120 Hz | A two-beat brake that leaves a short silence. |

### Builds

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| 8 Bar Roll Build | 8 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Eight bars: the last two roll in quarters down to thirty-seconds. |
| 4 Bar Riser Stutter | 4 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Four bars: eighths, sixteenths, then thirty-seconds in the last bar. |
| Snare Build | 2 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Two bars: the second bar hammers one hit faster and faster. |
| Gate Build | 4 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Four bars: the last bar chops faster every two beats. |
| Drop Rewind | 4 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Four bars: the last two beats rewind hard into the drop. |
| Tension Half-Time | 2 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Two bars: the second bar drags at half speed. |

### Halftime

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Half-Time Drag | 1 bar | every phrase | Keep Sub split 120 Hz | The second half plays at half speed, an octave down. |
| Half-Time Groove | 1 bar | every phrase | Keep Sub split 120 Hz | Each half bar starts live and drags to half speed. |
| Slow Motion Tail | 1 bar | every phrase | Keep Sub split 120 Hz | Beat four drags at half speed. |
| Half-Time Chop | 1 bar | every phrase | Keep Sub split 120 Hz | Beats three and four each restart at half speed. |
| Quarter Speed | 1 bar | every phrase | Keep Sub split 120 Hz | The second half crawls at quarter speed, two octaves down. |
| Halftime Switch | 2 bars | every phrase | Keep Sub split 120 Hz, Engage snaps to the bar | Two bars: the second bar switches to half speed. |

### Vocal Chops

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Word Repeat | 1 bar | every phrase | Keep Sub split 200 Hz | Beat three repeats in eighths, beat four in sixteenths. |
| Syllable Stutter | 1 bar | every phrase | Keep Sub split 200 Hz | Short syllable stutters on beats two and four. |
| Reverse Word | 1 bar | every phrase | Keep Sub split 200 Hz | Beat three plays backwards, then the line continues. |
| Call & Response | 1 bar | every phrase | Keep Sub split 200 Hz, Look Back 1 bar | The second half answers with the previous bar's second half. |
| Vocal Gate | 1 bar | every phrase | Keep Sub split 200 Hz | A sixteenth-note crossfader gate. |
| Stammer | 1 bar | every phrase | Keep Sub split 200 Hz, Look Back 1 beat | Beats two and four repeat the beat before them. |
| Hook Echo | 1 bar | every 2nd | Keep Sub split 200 Hz | Every second bar, beat four repeats beat three. |

### Riddim

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Riddim Chop | 1 bar | every phrase | Keep Sub split 120 Hz | An eighth-note chop over the whole bar. |
| Riddim Repeat | 1 bar | every phrase | Keep Sub split 120 Hz | Beats two and four repeat in eighths. |
| Triplet Riddim | 1 bar | every phrase | Keep Sub split 120 Hz | An eighth-triplet chop over the whole bar. |
| Riddim Stop & Go | 1 bar | every phrase | Keep Sub split 120 Hz | Beats two and four stop and spin back up. |
| Sub Lock Gate | 1 bar | every phrase | Keep Sub split 160 Hz | A sixteenth gate above a 160 Hz Keep Sub split. |
| Riddim Rewind | 1 bar | every phrase | Keep Sub split 120 Hz | A push on beat three and a rewind on beat four. |

### Space Bass

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Space Drift | 1 bar | every phrase | Keep Sub split 120 Hz, Depth 160%, Mix 70%, Engage snaps to the beat | A blended half-speed drift, pushed deeper. |
| Wormhole | 2 bars | every phrase | Keep Sub split 120 Hz, Depth 180%, Mix 80%, Engage snaps to the bar | Two bars: the second bar falls backwards through a deep rewind. |
| Zero G | 1 bar | every phrase | Keep Sub split 120 Hz, Depth 130%, Engage snaps to the beat | The second half stops and floats back up to speed. |
| Nebula Smear | 1 bar | every phrase | Keep Sub split 120 Hz, Depth 150%, Mix 60%, Engage snaps to the beat | A slow drag blended under the dry bass. |
| Warp Echo | 1 bar | every phrase | Keep Sub split 120 Hz, Mix 70%, Look Back 1 bar, Engage snaps to the beat | The second half replays the previous bar, blended. |
| Orbit | 1 bar | every phrase | Keep Sub split 120 Hz, Depth 160%, Mix 85%, chain A-B, Engage snaps to the beat | Alternates a deep swirl and a reverse roll each bar. |

### Chains

These use the pattern slots: with Chain set to A-B, A-D or A-H, slot A plays on the first bar, slot B on the second, and so on, then the cycle repeats. Click a slot key on the PERFORM page to see or edit that bar.

| Preset | Length | Plays | Settings | What it does |
| --- | --- | --- | --- | --- |
| Scratch / Roll | 1 bar | every phrase | Keep Sub split 120 Hz, chain A-B, Engage snaps to the bar | Bar 1 scratches, bar 2 rolls. |
| Call & Answer | 1 bar | every phrase | Keep Sub split 120 Hz, chain A-B, Engage snaps to the bar | Bar 1 pushes, bar 2 rewinds. |
| 4 Bar Story | 1 bar | every phrase | Keep Sub split 120 Hz, chain A-D, Engage snaps to the bar | Clean, chirps, roll, then a tape stop over four bars. Slot A is intentionally clean, so its graph is flat. |
| Eight Fills | 1 bar | every phrase | Keep Sub split 120 Hz, chain A-H, Engage snaps to the bar | A different fill on every bar of an eight-bar phrase. |

## The 75 original recipes (Bass, Drums, Vocals and Classic)

These are the **75 distinct factory patterns** from 0.10: **25 bass, 25 drums and 25 vocals**. The Bass, Drums and Vocals folders and the three Classic folders contain the same recipes; only the settings described above differ. Categories are starting points; every pattern works with any source. Their IDs, names and musical arrangements are unchanged, and the host's **Scratch Pattern** control still selects them by number. Factory selections and generated patterns use **motion recipe version 2**, with eased scratch motion instead of sustained fixed-pitch ramps. Saved patterns from earlier releases retain their original motion recipe when recalled.

The **Original duration** column gives each recipe’s native length in quarter-note beats, with its 4/4 equivalent. The base length can be **½ beat, 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars or 16 bars**. Bar choices follow the host’s meter. **Dotted** multiplies that duration by **1.5**; **Triplet** multiplies it by **⅔**. Changing length or timing retimes the existing pattern once without duplicating or regenerating gestures. The descriptions give the original gesture order; a circular grid shift moves its position within the phrase.

| # | Preset | Category | Original duration | Character |
| --- | --- | --- | --- | --- |
| 01 | Sub Tug | Bass | 2 beats · ½ bar | Two roomy push-pull scratches, leaving the downbeat intact. |
| 02 | Rubber Teeth | Bass | 2 beats · ½ bar | Three accelerating rubbery pulls with unequal note lengths. |
| 03 | Half-Time Drag | Bass | 4 beats · 1 bar | An open first half, a long reverse growl, and a short release. |
| 04 | Triplet Growl | Bass | 4 beats · 1 bar | Three swung growls answer a clear opening beat. |
| 05 | Tape Throat | Bass | 4 beats · 1 bar | A descending tape bend ending in a clipped backward bark. |
| 06 | Switchblade | Bass | 2 beats · ½ bar | Sharp crossfader cuts across two asymmetric push-pulls. |
| 07 | Snarl & Release | Bass | 4 beats · 1 bar | A reverse snarl followed by two progressively longer throws. |
| 08 | Offbeat Chew | Bass | 4 beats · 1 bar | Small offbeat bites that keep the main bass rhythm readable. |
| 09 | Two-Bar Beast | Bass | 8 beats · 2 bars | A sparse two-bar phrase with a dense triplet turnaround. |
| 10 | Kickback | Drums | 2 beats · ½ bar | A tiny pickup and a longer backspin, with room for the kick. |
| 11 | Snare Snatch | Drums | 4 beats · 1 bar | Backward snare accents and a late pickup into the downbeat. |
| 12 | Pocket Chirp | Drums | 2 beats · ½ bar | Short gated chirps tucked between the main drum hits. |
| 13 | Breakspin | Drums | 4 beats · 1 bar | An open break that develops into a rolling scratch fill. |
| 14 | Triplet Chopper | Drums | 2 beats · ½ bar | Three tightly cut scratches in the final beat. |
| 15 | Last-Beat Flare | Drums | 4 beats · 1 bar | Three clean beats, then a doubled crossfader flourish. |
| 16 | Rewind Roll | Drums | 4 beats · 1 bar | Repeating sixteenth-note material falls into a reverse throw. |
| 17 | Two-Bar Turn | Drums | 8 beats · 2 bars | A two-bar turnaround combining chirps, a roll, and a pullback. |
| 18 | Word Flip | Vocals | 2 beats · ½ bar | A backward word answered by a longer forward release. |
| 19 | Stutter Talk | Vocals | 2 beats · ½ bar | A fast repeated syllable opens into a gated baby scratch. |
| 20 | Baby Talk | Vocals | 2 beats · ½ bar | Two rounded, unequal baby scratches for spoken phrases. |
| 21 | Triplet Syllables | Vocals | 4 beats · 1 bar | A syllable triplet and one broad rounded answer. |
| 22 | Echo Scratch | Vocals | 4 beats · 1 bar | A delayed word repeats before a broad push-pull answer. |
| 23 | Breath Catch | Vocals | 4 beats · 1 bar | A breathy tape catch, a rounded pull, and a little final chirp. |
| 24 | Call / Reply | Vocals | 8 beats · 2 bars | A short call and a longer chopped reply over two bars. |
| 25 | Vocal Guillotine | Vocals | 4 beats · 1 bar | Syncopated word cuts build into a rapid final scratch. |
| 26 | Concrete Swing | Bass | 4 beats · 1 bar | A lopsided bass tug answers a clipped three-hit pickup. |
| 27 | Quicksand | Bass | 8 beats · 2 bars | A spacious two-bar drag sinks into a slow tape catch. |
| 28 | Rattlesnake | Bass | 2 beats · ½ bar | A tight rattling fragment snaps into a gated reverse bite. |
| 29 | Bent Rebar | Bass | 4 beats · 1 bar | A broad rounded bend meets two hard-edged late scratches. |
| 30 | Sewer Step | Bass | 4 beats · 1 bar | Low offbeat pulls climb into a short reversed turnaround. |
| 31 | Elastic Jaw | Bass | 2 beats · ½ bar | An uneven pair of elastic scratches stretches the second answer. |
| 32 | Ghost Note Growl | Bass | 4 beats · 1 bar | Quiet spaces surround a triplet pickup and a long backward growl. |
| 33 | Quarterback | Bass | 8 beats · 2 bars | Two short calls lead to a wide, open two-bar pullback. |
| 34 | Broken Spring | Bass | 4 beats · 1 bar | A tape catch breaks into short, unequal sprung rebounds. |
| 35 | Triple Threat | Bass | 4 beats · 1 bar | Three broad triplet strokes close with a small clipped tail. |
| 36 | Gravity Drop | Bass | 4 beats · 1 bar | A repeated bass hit drops through a brake into a final reverse. |
| 37 | Ripcord | Bass | 2 beats · ½ bar | A sudden reverse tug releases into two compact forward throws. |
| 38 | Sludge Shuffle | Bass | 4 beats · 1 bar | Swung rounded pulls leave deliberate air around a heavier last note. |
| 39 | Aftershock | Bass | 8 beats · 2 bars | One broad call and a separated double reply frame a two-bar fill. |
| 40 | Split Tongue | Bass | 4 beats · 1 bar | Alternating smooth and gated strokes form a syncopated bass answer. |
| 41 | Low Rider | Bass | 4 beats · 1 bar | A laid-back long reverse cruises into a short rubbery rebound. |
| 42 | Crate Skid | Drums | 4 beats · 1 bar | A clipped offbeat skid opens into a wider snare-length backspin. |
| 43 | Ghost Snare | Drums | 4 beats · 1 bar | Tiny snare fragments lead to a late, rounded scratch accent. |
| 44 | Boomerang Break | Drums | 4 beats · 1 bar | A broad backward break returns through two unequal forward strokes. |
| 45 | Hi-Hat Hitch | Drums | 2 beats · ½ bar | Short high-hat grains stumble into a small gated pullback. |
| 46 | Amen Corner | Drums | 8 beats · 2 bars | A two-bar break stays open before a descending chopped turnaround. |
| 47 | Rimshot Relay | Drums | 4 beats · 1 bar | Separated rimshot-sized cuts pass into a triplet release. |
| 48 | Crab Walk | Drums | 2 beats · ½ bar | Short crossfader-like cuts alternate with a single longer backspin. |
| 49 | Snare Staircase | Drums | 4 beats · 1 bar | An increasing train of repeated fragments ends with a reverse snare. |
| 50 | Loose Change | Drums | 4 beats · 1 bar | Unequal offbeat chirps scatter around a roomy center beat. |
| 51 | Brake Check | Drums | 4 beats · 1 bar | A compact tape slowdown is answered by a fast, double-cut scratch. |
| 52 | Skip Proof | Drums | 4 beats · 1 bar | Two stuttering fragments and a rounded pull form a half-time fill. |
| 53 | Tripwire | Drums | 4 beats · 1 bar | Two displaced triplet groups cross into a clipped ending. |
| 54 | Sidechain Shuffle | Drums | 4 beats · 1 bar | The downbeats breathe while broad offbeat scratches pull against them. |
| 55 | Breakbeat Telegraph | Drums | 8 beats · 2 bars | Short and long cuts exchange a rhythmic call across two bars. |
| 56 | Pocket Lurch | Drums | 4 beats · 1 bar | A late reverse lean resolves through a small clean push-pull. |
| 57 | Double Clutch | Drums | 4 beats · 1 bar | Two different tape catches interrupt a running break. |
| 58 | Final Countdown | Drums | 4 beats · 1 bar | A final-beat fill accelerates from one broad cut to two tight chirps. |
| 59 | Syllable Slingshot | Vocals | 2 beats · ½ bar | A clipped syllable repetition launches a long rounded word throw. |
| 60 | Tongue Twister | Vocals | 4 beats · 1 bar | Unequal word reversals trade places with sharp little crossfader cuts. |
| 61 | Broken Sentence | Vocals | 4 beats · 1 bar | A held-back word, an open pause, and a chopped closing phrase. |
| 62 | Mouthful | Vocals | 4 beats · 1 bar | A dense early syllable cluster clears space for one broad reply. |
| 63 | Backchat | Vocals | 2 beats · ½ bar | Two short backward words lead into a longer gated answer. |
| 64 | Question Mark | Vocals | 4 beats · 1 bar | A slow questioning bend is answered by a crisp late word flip. |
| 65 | Name Drop | Vocals | 4 beats · 1 bar | A repeated name-sized fragment falls into a descending tape catch. |
| 66 | Cut Up Poetry | Vocals | 8 beats · 2 bars | Separated short word cuts develop into a two-bar rolling reply. |
| 67 | Rhyme Rewind | Vocals | 4 beats · 1 bar | A long reverse rhyme snaps forward into two unequal baby scratches. |
| 68 | Breathless | Vocals | 4 beats · 1 bar | Compact triplet syllables spill into a final gated word. |
| 69 | Word Elastic | Vocals | 4 beats · 1 bar | A small rounded syllable and a long elastic answer share the phrase. |
| 70 | Hiccup Hook | Vocals | 2 beats · ½ bar | Two isolated hiccups leave a hook intact before a quick backward catch. |
| 71 | Chopped Confession | Vocals | 8 beats · 2 bars | A two-bar vocal call returns as repeated fragments and a soft reverse. |
| 72 | Telltale Stutter | Vocals | 4 beats · 1 bar | Two unequal stutters introduce a rounded, stretched last syllable. |
| 73 | Reverse Whisper | Vocals | 4 beats · 1 bar | A gentle tape bend leads to a long uncut backward word. |
| 74 | Cross Talk | Vocals | 4 beats · 1 bar | A clipped call and smooth reply alternate over a swinging rhythm. |
| 75 | Mic Drop | Vocals | 4 beats · 1 bar | An open phrase closes with a word repeat, tape stop, and final chirp. |

## Duration, timing and lock

Drag the detented Phrase Length knob vertically, use the mouse wheel, or hold Shift while dragging for finer control. Double-click restores one bar. A longer setting spreads the same pattern across more time; a shorter setting compresses it. Both time and source offsets scale together, preserving the pattern’s source speeds, gate shapes and ordered gestures.

The small **Dotted** and **Triplet** buttons light when active. They are mutually exclusive; clicking the active button again returns to straight timing. They scale the same pattern, retaining its source speeds and cuts. For example, a one-beat base becomes 1½ beats with Dotted or ⅔ beat with Triplet.

**Phrase Length Lock** keeps the selected base knob length during Randomize and when you choose one of the 75 original recipes. Unlocking lets Randomize choose any of the eight base lengths; choosing an original recipe then picks the closest available base length to its native length. Timing mode is retained either way. The new 0.11 presets always set their own length and timing (see above). Factory selection restores zero grid shift; Randomize retains the current shift. You can adjust length, timing and shift with the lock on.

## Shift within the grid

The **upper arrows** select the previous or next preset in the menu's order: through each folder in turn (Bass, Drums, Vocals, Fills … Chains, then Classic), wrapping at the ends. In a project from an earlier version, the arrows start from the Classic entry of the recipe it is playing. The **bottom arrows**, centered on the display’s phrase-length row, move the current pattern one of its **16 equal visible grid cells** left or right. The entire curve and its cuts move together and wrap at the edges. Sixteen clicks return to the original position. Changing duration preserves this relative position in the phrase.

## Generate something new

The compact dice **Randomize** button uses an independent seeded composer. Each click builds a new arrangement of scratch gestures, placements, cuts and rests; it does not select one of the built-in presets or merely vary a factory recipe. The preview and name identify generated patterns. With slot B–H selected on the PERFORM page, Randomize composes a new pattern for that slot only.

A generated pattern repeats consistently over fresh incoming audio. Its seed, original composition duration and original meter define its identity. The original duration is the base length before Dotted or Triplet is applied. Turning the knob or changing timing only retimes that composition; shifting changes its position. A new pattern is composed when you click Randomize, retaining your timing mode and shift. Randomize and factory navigation replace any custom drawing and clear its Undo/Redo history.

## Make a pattern your own

Click the display's **Pencil** button to arm drawing; its small arrow opens the tool selector. **Free Draw** follows your pointer between grid lines, gently smoothing hand jitter. A click places a small mark at the clicked height. **Line** previews a straight stroke from the point where you pressed to your current pointer. **Curve** does the same with a smooth bend that eases its playback speed at both ends. Moving the pointer back shortens a Line or Curve preview without leaving previous previews behind.

The same menu has **rolls**: Roll 1/8, 1/16, 1/32 and 1/16 Triplet, Accelerating Roll, Reverse Roll and Chopped Roll. Choose one and drag across the graph: the slice where you press repeats at that rate until you let go. Hovering shows where the roll will land. **Snap to grid**, at the bottom of the same menu, makes Line, Curve and Roll start and end on the 16-cell grid.

Hear changes during playback. The highlighted endpoint shows the actual shape, including the limit that keeps forward and reverse playback speeds within **2×** (Depth above 100% can push strokes further, up to 4×). Drawn strokes preserve the existing sound outside their span. The **16-cell** grid remains a visual guide and controls the eraser and shift increment; drawing is no longer restricted to whole cells.

Choose **Eraser** and click or drag over cells to restore clean input in those cells. This creates a dry passage rather than muting the track. The graph keeps its vertical range between gestures, even after a complete erase. Click the selected Pencil or Eraser again to disarm it.

The **Undo** and **Redo** icons work on complete gestures, with a **64-gesture** history. While a tool is armed, **Cmd+Z** and **Cmd+Shift+Z** do the same. Press **Escape** or close the editor to cancel an unfinished gesture; Escape also disarms the tool. Completed edits survive length changes, Dotted/Triplet changes and circular shifts. Presets and host projects save the drawing; Undo/Redo history is not stored in the preset.

## Quick scratch shapes

The **Shapes** dropdown beside the drawing tool offers **15 starting contours**: the 10 scratch shapes from 0.10 and 5 roll and tape starters added in 0.11. They are separate from the built-in presets. Each miniature preview shows the curve that will be applied. Selecting a shape replaces the whole phrase (or the selected slot's phrase), retains the current phrase length, timing and shift, and arms Free Draw for further editing. Shape selection is one undoable action. It does not change Keep Sub or the length lock.

The 10 scratch shapes move smoothly into reverse and back to live audio, with clean space around their strokes. The 5 starters keep the first half of the phrase clean and put the roll or stop in the second half. Choose a shape, adjust its length, then draw or erase to make it your own.

| Shape | Character |
| --- | --- |
| Baby Scratch | One rounded pull and a longer release, with space before and after. |
| Double Push | Two matching compact scratches separated by a clear pause. |
| Slow Drag | A broad, gradual reverse bend followed by a long return. |
| Rubber Band | A quick deeper pull that stretches into a slower elastic release. |
| Chirp | Two short, light reverse accents with quick recoveries. |
| Reverse Swell | One deep, spacious swell with an extended return to the original phrase. |
| Stop & Go | A small scratch, a pause, then a larger second answer. |
| Triplet Bounce | Three compact scratches evenly spaced across thirds of the phrase. |
| Late Scratch | One broad scratch saved for the second half of the phrase. |
| Wobble Roll | Four small, fast pulls with open gaps between them. |
| Accelerating Roll | The second half repeats faster and faster (in a one-bar phrase: eighths, sixteenths, then thirty-seconds). |
| Triplet Roll | The second half repeats in sixteenth-note triplets (one-bar phrase). |
| Reverse Roll | The second half repeats a sixteenth backwards (one-bar phrase). |
| Tape Stop | The second half winds down to a stop. |
| Half-Time Drag | The second half plays at half speed, an octave down. |

## Your presets and folders

**Save** opens a short menu: pick one of your preset folders, or **New folder or another place...** to make a folder or save anywhere else. Your folders live in **~/Library/Application Support/BACK-SCRATCH/Presets/**, one folder per group (the first save offers **My Presets**). **Load** lists the same folders, plus **Browse files...** for a preset stored anywhere, such as files saved by earlier versions. The **⌄** preset menu also has a **User Presets** submenu with every folder; files saved directly in the Presets folder appear under **Unfiled**.

BACK-SCRATCH never moves, renames or rewrites your existing preset files. Nothing is migrated: presets you saved with 0.10 stay where you saved them and still load through Browse files..., or you can copy them into a folder yourself.

## Save and recall

Save retains custom geometry and graph scale, generated identity, seed, original composition duration and meter, or the selected factory recipe and variation, along with base duration, timing mode, grid shift, length lock and Keep Sub. Version 0.11 presets also save Engage Snap, Retrigger, Play Every, the selected slot, Slot Chain, Mix, Keep Sub Split, Depth, Look Back and the patterns in slots B–H. Engage, Output and History are performance settings: loading a preset leaves them as they are. Load and host project recall restore those settings. Loading begins a fresh drawing history. The resulting sound still depends on incoming audio and host tempo/meter.

With slot B–H selected, loading a preset file only puts its pattern into that slot, as one undoable step.

The host's **Edited Pattern** control selects custom geometry saved in the plug-in state. The drawing points themselves are not automatable parameters: mouse gestures update the current pattern rather than recording a time series into an automation lane. Phrase length, timing, shift and the other host controls remain available for automation.

Version 0.11 saves **schema 10** files. Presets saved by 0.10 (**schema 9**) and earlier load with every 0.11 setting neutral (snap 1/16, Retrigger off, every phrase, slot A, no chain, Mix 100%, 200 Hz split, Depth 100%, no Look Back) and slots B–H empty, so they sound as they did. Version 0.10 saved **schema 9** files with the motion recipe version, custom geometry and graph scale. New factory and generated patterns use **motion recipe version 2**. **Schemas 4–8** remain compatible and retain their original **version 1** factory/generated motion rather than being silently converted to the new curves. Custom drawings from **schema 8** retain their saved points, curve domains and graph scale.

Schemas 7 and 8 retain `phraseTiming` and `patternShift`, while schemas 4, 5 and 6 restore straight timing with zero shift, including presets and projects from 0.6 and 0.7. Schemas 4–7 start without a custom drawing. Invalid timing or custom geometry leaves existing settings unchanged. Factory motifs stretch once instead of repeating across longer phrases, so early factory presets from before that timing change may sound different at their saved duration. Version 0.5 generated files migrate their composition identity for later retiming. Version 0.4’s former Remove Sub setting now means full-range scratching. Captured wheel states from versions 0.1–0.3 open bypassed.

## Keep Sub and long phrases

**On** preserves the original complementary low layer under the scratched upper layer. **Sub Split** on the PERFORM page sets where the layers divide: **120, 160, 200 or 300 Hz** (200 Hz is the default and the 0.10 sound). All four use the same compensated timing, so changing it never moves the track. **Off** scratches the full signal, including bass. Randomize and choosing an original recipe preserve your on/off choice; loading a saved preset restores its saved switch state.

The rolling history holds the incoming audio that scratches reach back into. **History** on the PERFORM page sets its size: **Auto** (the default for new instances) starts at 16 seconds and steps up to 32, 64 or 128 seconds when the pattern, tempo, Depth or Look Back needs more; or choose a fixed 16, 32, 64 or 128 seconds. Projects from earlier versions keep the fixed 128 seconds they always had. After startup, a manual seek, or a history size change, unavailable past audio fades to dry until it has arrived. Normal host loop wraps preserve the history instead of clearing it on each pass. Some long settings, including dotted 16-bar patterns in 4/4 at 20 BPM, can exceed even 128 seconds. Those portions also fade smoothly to dry and show **LIVE INPUT** in the display; the plug-in never reads audio it does not have.

The displayed pattern and playhead use the same delayed playback context, and closed gates appear dimmer. **LIVE INPUT** identifies a gesture that is temporarily using the original audio because its required lookback is unavailable. Automated host checks cover these behaviors; actual Ableton session recording and subjective listening remain unverified.

## Portable factory files

All 230 built-in presets are inside the plug-in; no files are needed. For a release, `BackScratchHostSmoke --export-presets <folder>` writes the whole bank as optional **schema 10** `.backscratch` files, one folder per bank folder (Classic as Classic/Bass, Classic/Drums, Classic/Vocals). Each file is reopened and compared with the built-in preset before the export succeeds. Load them through the plug-in, or copy them into your user preset folders. A saved file records the length lock and Keep Sub as they were when exported (lock on, Keep Sub on). The 15 quick Shapes remain available directly in the display; they are not preset files.
