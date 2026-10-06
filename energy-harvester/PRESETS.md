# ENERGY HARVESTER factory presets

56 transition presets in 10 folders, all using the 0.4.0 sound (Latest Sound on). Loading a preset changes every control except HARVEST, as one undoable change for the host. User presets live in `~/Library/Application Support/ENERGY HARVESTER/Presets/<Folder>/` as `.ehpreset` files; saving never overwrites an existing file.

Every preset was rendered through the plug-in on the same deterministic 140 BPM half-time bass loop (sub, wobble saw, kick, snare, hats; peaks about −2.5 dBFS dry), with a playing 4/4 transport, HARVEST switched on 1.0 s in (beat 2.33). Stop and Hold presets were switched off 0.5 s after their deadline. **Length** is the actual latched length (SYNC and grid presets follow the 140 BPM transport). **Peak** is the whole render's sample peak. **Effect** is the RMS of the effected voice over the gesture (50 ms in from each end, clear of the crossfades), relative to the dry loop over the same span. Most of this loop's energy is its 49 Hz sub, so band-pass presets, which take the sub out on purpose, read low here (they sit closer to the dry level on material with more midrange), while Sub Keep presets read close to the dry level because the sub stays dry. **Landing** is what the render measured after the gesture: exact means the output was the dry input sample for sample.


## Tape Starts

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Classic Spin-Up | The original magnetic start, refined: 800 ms, S-curve, light phaser. | Tape Start; 800 ms; phaser 45 % | 800 ms | -2.5 dBFS | -3.2 dB | lands exactly dry at the deadline |
| Reel-to-Reel | True varispeed reel start with a soft resonant opening. | Tape Start (True Tape); 1200 ms; curve +20; LP 12 res 25 % from 300 Hz | 1200 ms | -2.5 dBFS | -6.5 dB | lands exactly dry at the deadline |
| Cassette Wake | Short, wobbly deck start from half speed. | Tape Start (True Tape); 600 ms; curve -30; −12 st; chorus 35 %; sweep from 500 Hz | 600 ms | -2.5 dBFS | -2.7 dB | lands exactly dry at the deadline |
| Slow Motor | Two-bar True Tape crawl from three octaves down into a ringing washout. | Tape Start (True Tape); 2 bars; curve +50; −36 st; washout 40 %; Tail landing | 3429 ms | -2.5 dBFS | -7.5 dB | lands, hall rings 2.93 s, then exact dry |
| Kick-Start | Eighth-note snap from an octave down; the sub stays put. | Tape Start; 1/8; curve -50; −12 st; Sub Keep 60 Hz | 214 ms | -2.5 dBFS | -2.9 dB | lands exactly dry at the deadline |
| Deep Iron | One bar from four octaves down through a growling 24 dB filter. | Tape Start; 1 bar; curve +30; −48 st; flanger 30 %; LP 24 res 30 % from 120 Hz | 1714 ms | -2.5 dBFS | -6.1 dB | lands exactly dry at the deadline |

## Tape Stops

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Power Cut | Plain motor power-down to silence. | Tape Stop; 800 ms | 800 ms | -2.5 dBFS | -4.9 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Dying Battery | A bar of fading motor: quick sag, long drag, resonant dusk. | Tape Stop; 1 bar; curve -40; chorus 30 %; LP 12 res 35 % from 250 Hz | 1714 ms | -2.5 dBFS | -8.9 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Reel Run-Out | Holds speed, then the reel runs out into reverb. | Tape Stop; 2000 ms; curve +40; washout 50 % | 2000 ms | -2.5 dBFS | -3.0 dB | reaches standstill, -47.7 dB re dry while held, exact dry 10.0 ms after OFF |
| Half-Bar Stop | Half-note power-down for quick fills. | Tape Stop; 1/2 | 857 ms | -2.5 dBFS | -4.8 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Sub-Safe Stop | The top end powers down while the sub keeps going. | Tape Stop; 1 bar; phaser 30 %; Sub Keep 90 Hz | 1714 ms | -2.5 dBFS | -1.2 dB | reaches standstill, -1.3 dB re dry while held, exact dry 10.0 ms after OFF |
| Washed Power-Down | Two bars down into a dark wash. | Tape Stop; 2 bars; curve +10; washout 70 %; LP 24 res 20 % from 200 Hz | 3429 ms | -2.5 dBFS | -5.2 dB | reaches standstill, -51.2 dB re dry while held, exact dry 10.0 ms after OFF |

## Brakes

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Turntable Brake | Platter brake: constant deceleration to a stop. | Brake; 600 ms | 600 ms | -2.5 dBFS | -4.9 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Hard Brake | Quarter-second stop for hard cuts. | Brake; 250 ms; curve -50 | 250 ms | -2.5 dBFS | -10.6 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Slow Platter | A lazy hand on the platter, with phase. | Brake; 1500 ms; curve +30; phaser 25 % | 1500 ms | -2.5 dBFS | -3.1 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Vinyl Drag | Quarter-note drag with chorus wobble and a resonant edge. | Brake; 1/4; chorus 40 %; LP 12 res 20 % from 450 Hz | 429 ms | -2.5 dBFS | -5.5 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Brake & Keep | Brakes the top end while the sub holds steady. | Brake; 700 ms; Sub Keep 60 Hz | 700 ms | -2.5 dBFS | -3.2 dB | reaches standstill, -3.9 dB re dry while held, exact dry 10.0 ms after OFF |

## Rewinds

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Selecta Rewind | Classic pull-back: brake, spin back, snap to the drop. | Rewind; 1200 ms | 1200 ms | -2.5 dBFS | -1.9 dB | lands exactly dry at the deadline |
| Pull-Up | One-bar rewind with a hall tail after the snap. | Rewind; 1 bar; curve -30; washout 30 %; Tail landing | 1714 ms | -2.5 dBFS | -3.9 dB | lands, hall rings 2.96 s, then exact dry |
| Quick Spin-Back | Half-second spin-back for tight edits. | Rewind; 500 ms; curve -60 | 500 ms | -2.5 dBFS | -0.3 dB | lands exactly dry at the deadline |
| Backspin Wash | Rewind into a washed-out tail. | Rewind; 1500 ms; washout 60 %; Tail landing | 1500 ms | -2.5 dBFS | -3.5 dB | lands, hall rings 5.61 s, then exact dry |
| Siren Rewind | Rewind through a resonant band-pass. | Rewind; 1000 ms; flanger 20 %; BP res 50 % from 600 Hz; output +6 dB | 1000 ms | -2.5 dBFS | -23.4 dB | lands exactly dry at the deadline |

## Power-Ups

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Stop-Start Stutter | Half-note stop, then straight back up to speed. | Stop > Start; 1/2 | 857 ms | -2.5 dBFS | -5.2 dB | lands exactly dry at the deadline |
| Brownout | A bar-long sag and recovery with a resonant sweep. | Stop > Start; 1 bar; curve +20; phaser 25 %; LP 12 res 30 % from 300 Hz | 1714 ms | -2.5 dBFS | -5.9 dB | lands exactly dry at the deadline |
| Generator Restart | Two-second stall and restart with a light wash. | Stop > Start; 2000 ms; washout 30 % | 2000 ms | -2.5 dBFS | -4.7 dB | lands exactly dry at the deadline |
| Hiccup | Eighth-note dropout and recovery. | Stop > Start; 1/8 | 214 ms | -2.5 dBFS | -13.8 dB | lands exactly dry at the deadline |
| Reactor Power-Up | Two bars from four octaves down with phase, flange and a resonant 24 dB rise. | Tape Start (True Tape); 2 bars; curve +20; −48 st; phaser, flanger 50 %; LP 24 res 40 % from 150 Hz | 3429 ms | -2.5 dBFS | -8.1 dB | lands exactly dry at the deadline |

## Risers

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Harvest Riser | Four bars, slam-late curve, phaser and a resonant climb. | Tape Start; 4 bars; curve +60; −36 st; phaser 60 %; LP 12 res 50 % from 200 Hz | 6857 ms | -2.5 dBFS | -8.0 dB | lands exactly dry at the deadline |
| Resonant Climb | Two-bar True Tape rise through a singing 24 dB filter. | Tape Start (True Tape); 2 bars; LP 24 res 70 % from 150 Hz; output -1 dB | 3429 ms | -2.5 dBFS | -8.2 dB | lands exactly dry at the deadline |
| Band Riser | Two bars of band-pass sweep with chorus. | Tape Start; 2 bars; curve +10; chorus 40 %; BP res 60 % from 300 Hz; output +6 dB | 3429 ms | -2.5 dBFS | -30.7 dB | lands exactly dry at the deadline |
| Phase Ascension | A bar of deep phaser as the tape climbs. | Tape Start; 1 bar; curve +20; phaser 85 % | 1714 ms | -2.5 dBFS | -4.5 dB | lands exactly dry at the deadline |
| Space Bass Lift | Two bars of flange and wash above a steady 120 Hz sub, ringing on after the drop. | Tape Start (True Tape); 2 bars; flanger, washout 50 %; Sub Keep 120 Hz; Tail landing | 3429 ms | -2.5 dBFS | -0.7 dB | lands, hall rings 3.71 s, then exact dry |
| Slow Dawn | Eight seconds of light, washed climb, mixed in parallel. | Tape Start; 8000 ms; curve +30; washout 50 %; mix 80 % | 8000 ms | -2.5 dBFS | -5.9 dB | lands exactly dry at the deadline |

## Drops

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Drop Slam | Quarter-note slam: stays low, then hits speed on the drop. | Tape Start; 1/4; curve +100 | 429 ms | -2.5 dBFS | -8.1 dB | lands exactly dry at the deadline |
| Riddim Snap | Eighth-note True Tape snap from an octave down, sub held. | Tape Start (True Tape); 1/8; curve +80; −12 st; Sub Keep 60 Hz | 214 ms | -2.5 dBFS | -4.5 dB | lands exactly dry at the deadline |
| Half-Time Drop | Half-note start from an octave down. | Tape Start; 1/2; −12 st; phaser 20 % | 857 ms | -2.5 dBFS | -3.4 dB | lands exactly dry at the deadline |
| Wobble Wake | Quarter-note start through a resonant, phased low-pass. | Tape Start; 1/4; curve +50; phaser 70 %; LP 12 res 45 % from 250 Hz | 429 ms | -2.5 dBFS | -7.4 dB | lands exactly dry at the deadline |
| Sub Punch | 300 ms True Tape punch above a steady 90 Hz sub. | Tape Start (True Tape); 300 ms; curve +60; Sub Keep 90 Hz | 300 ms | -2.5 dBFS | -1.5 dB | lands exactly dry at the deadline |
| Drop & Hold | Lands at speed and keeps the phaser on until HARVEST is switched off. | Tape Start; 1/4; curve +30; phaser 60 %; Hold landing | 429 ms | -1.4 dBFS | -4.9 dB | holds at speed until OFF, exact dry 10.0 ms after OFF |

## Washouts

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Cathedral Start | A bar-long start drowned in hall, ringing on after the drop. | Tape Start; 1 bar; washout 90 %; Tail landing | 1714 ms | -2.5 dBFS | -3.9 dB | lands, hall rings 6.38 s, then exact dry |
| Reverb Dive | Power-down into a deep wash. | Tape Stop; 1 bar; washout 85 % | 1714 ms | -2.5 dBFS | -4.6 dB | reaches standstill, -36.3 dB re dry while held, exact dry 10.0 ms after OFF |
| Wash & Rewind | Rewind with a hall tail after the snap. | Rewind; 1 bar; washout 70 %; Tail landing | 1714 ms | -2.5 dBFS | -3.7 dB | lands, hall rings 6.43 s, then exact dry |
| Fog Machine | Two bars of chorus and hall, mixed in parallel. | Tape Start; 2 bars; curve +20; chorus, washout 60 %; Tail landing; mix 85 % | 3429 ms | -2.5 dBFS | -5.5 dB | lands, hall rings 4.44 s, then exact dry |
| Ghost Tail | Short start with a long ghostly tail. | Tape Start; 600 ms; washout 75 %; Tail landing | 600 ms | -2.5 dBFS | -3.7 dB | lands, hall rings 5.53 s, then exact dry |

## Lo-Fi

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Broken Walkman | Wobbly True Tape start through a narrow band-pass. | Tape Start (True Tape); 700 ms; curve -20; −12 st; chorus 60 %; BP res 40 % from 400 Hz; output +4 dB | 700 ms | -2.5 dBFS | -33.3 dB | lands exactly dry at the deadline |
| Radio Tune-In | A bar of radio band-pass sweep with flange. | Tape Start; 1 bar; flanger 30 %; BP res 70 % from 500 Hz; output +6 dB | 1714 ms | -2.5 dBFS | -29.9 dB | lands exactly dry at the deadline |
| Worn Cassette | Chorus, flange and a soft resonant edge, in parallel. | Tape Start (True Tape); 1000 ms; chorus, flanger 50 %; LP 12 res 25 % from 600 Hz; mix 85 % | 1000 ms | -2.5 dBFS | -7.2 dB | lands exactly dry at the deadline |
| Bitter Tape Stop | Band-passed power-down with chorus. | Tape Stop; 700 ms; chorus 30 %; BP res 50 % from 600 Hz; output +6 dB | 700 ms | -2.5 dBFS | -31.1 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Dusty Rewind | Band-passed rewind with gentle panning. | Rewind; 800 ms; pan 40 %; BP res 30 % from 450 Hz; output +6 dB | 800 ms | -2.5 dBFS | -21.0 dB | lands exactly dry at the deadline |

## Grid-Locked

| Preset | What it does | Settings | Length | Peak | Effect | Landing |
|---|---|---|---|---|---|---|
| Land on the Bar | Classic start that always lands on the next bar line. | Tape Start; grid bar; phaser 45 % | 712 ms | -2.5 dBFS | -3.7 dB | lands exactly dry at the deadline |
| Quarter Lock | True Tape snap that lands on the next beat. | Tape Start (True Tape); grid 1/4; −12 st | 283 ms | -2.5 dBFS | -3.2 dB | lands exactly dry at the deadline |
| Two-Bar Build | Lands on the next two-bar line with a resonant, phased climb. | Tape Start; grid 2 bars; curve +40; −36 st; phaser 50 %; LP 12 res 40 % from 200 Hz | 2426 ms | -2.5 dBFS | -6.9 dB | lands exactly dry at the deadline |
| Bar-Line Stop | Power-down that reaches silence exactly on the bar line. | Tape Stop; grid bar | 712 ms | -2.5 dBFS | -4.6 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |
| Rewind to the One | Spin-back that snaps back on the next bar line. | Rewind; grid bar | 712 ms | -2.5 dBFS | -1.5 dB | lands exactly dry at the deadline |
| Stutter on the Beat | Stop-start that recovers on the next beat. | Stop > Start; grid 1/4 | 283 ms | -2.5 dBFS | -7.6 dB | lands exactly dry at the deadline |
| Brake on the Bar | Turntable brake that stops on the bar line. | Brake; grid bar | 712 ms | -2.5 dBFS | -4.7 dB | reaches standstill, -150.0 dB re dry while held, exact dry 10.0 ms after OFF |

Regenerate the WAVs with `EnergyHostTests --render-presets <folder>`; the same checks run in `ctest`.
