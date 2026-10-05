# SHIFTY 0.4: shifting the source formants

## Product focus

SHIFTY moves the spectral envelope of the incoming bass while retaining its played note frequencies. Source identity and useful formant movement are the primary behavior. A source with no distinct resonances can legitimately change less than a textured source. The design must not manufacture fixed vowels merely to pass a large-change threshold.

The mandatory 450/1100/2500 Hz contour introduced in 0.3 has been separated into an optional Vowel Color switch. It starts off and is locked out of Random. All 24 factory presets explicitly recall it off. Old states lacking the new parameter also load it off and add its lock while preserving the original parameter values and lock bits.

## Envelope estimation and relocation

The engine measures log magnitude, finds prominent partial spacing, and uses that spacing to limit FOCUS detail so it does not follow the individual pitch harmonics. A narrow symmetric maximum filter initializes the estimate from local peaks. Three bounded cepstral passes then fill harmonic gaps while preserving the broader source contour. The fixed pass count makes audio-thread cost predictable; vectors are preallocated and processing adds no latency.

SHIFT rescales the estimated envelope's frequency coordinate by `2^(semitones/12)`. BITE exaggerates the source-derived log-gain difference. The optional vowel contour contributes only when enabled. The final spectral correction is bounded to ±24 dB; this is a spectral bound, not an output peak limiter. Linked compensation controls the affected band's energy across both channels, then Sub Safe restores original low-frequency complex bins.

Without the peak initialization, a 165 Hz test source developed a false high resonance after shifting. The revised estimator corrected that issue without changing the test thresholds.

The estimator is a bounded adaptation of the true-envelope approach described by [Röbel and Rodet, DAFx 2005](https://www.dafx.de/paper-archive/2005/P_030.pdf). It is not a claim of full iterative convergence or exact vocal-tract modeling.

## Verification

The source-tracking suite synthesizes known, arbitrary resonance pairs at 730/2070, 970/2890 and 1270/3670 Hz on 80/110/165 Hz carriers. It compares output spectra with independently calculated shifted target envelopes at ±6 semitones, with Vowel off and Bite/Drive zero. It measures peak direction, peak location, best-fitting envelope displacement, target correlation and off-harmonic energy. A flat carrier is separately checked for absence of an imposed vowel pattern.

The required behavior is source-envelope relocation. Plain saw, square, Reese and FM examples remain available for listening, without a requirement that every source must change dramatically. The optional Vowel distinction and zero-shift neutral behavior are separate tests. See VALIDATION.md for measured results and scope.
