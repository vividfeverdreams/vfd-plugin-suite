# Preserved synthesis engines

These sources preserve the sound of sessions and presets made before the 0.1.3
engine redesign. They are compiled into the current plugin; the old plugin
bundles are not needed at runtime.

- `YoiVoice010` is the exact engine shipped in 0.1.0 and 0.1.1. The two releases'
  engine sources are byte-identical.
- `YoiVoice012` is the exact engine shipped in 0.1.2.

Only the namespace and the included header's basename differ from the released
source archives. Keep the algorithms frozen: changes to old sound belong in a
new engine version. Each engine has its own matching `VoiceControls` type and
the same prepare, reset, control, note, bend, sample and gesture methods as the
modern engine. Both preserved engines have zero intrinsic latency.

The historical 0.1.0/0.1.1 implementation differs audibly from 0.1.2: it clamps
gesture duration to 1.5 seconds and output trim to unity, uses a float gesture
clock, resets the formant update clock at each host block, and does not smooth
FM ratio or resonance changes. Preserving those details avoids silently
changing a saved track during an update.

Unversioned historical state can be identified from data actually written by
those releases. Version 0.1.2 always writes a `Recent` child in the session state
and performance properties including `tempoSync` and `freeLengthMilliseconds`
in each patch; 0.1.0/0.1.1 do not. Version 0.1.3 adds `sub_mode` to parameter
state and `subMode` to serialized patches. New saves should explicitly record
the chosen engine so future releases do not need additional heuristics.

Run `tests/verify-archives.sh` with the original 0.1.0, 0.1.1 and 0.1.2 source
ZIP paths. It verifies source identity and compiles the unmodified archived
engine and preserved engine independently. It compares exact audio and gesture
hashes across 126 scenarios per engine: three rates, seven host block sizes,
six patches, long gestures, live parameter movement, bend, legato, reset and
release. Verification uses temporary files only.
