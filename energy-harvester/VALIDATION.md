# ENERGY HARVESTER 0.3.1 validation

3 October 2026, Apple M1 Pro / macOS 14.5, Release arm64. This revision changes only OUTPUT drawing/layout and the protected animation area. The spin-up-only gain, parameter IDs, state schema and audio algorithm are unchanged from 0.3.0.

- Release build completed and all three CTest targets passed: DSP, state and actual VST3 automation. Evidence: Checks/build.txt and ctest.txt.
- Native editor render and integration checks passed: editable output text accepts +6.2 dB and updates the parameter; native TIME/SYNC and FX controls, INFO, transparency, resizing and exact six-second loop closure pass. Ready, boosted and compact screenshots were visually inspected. The rectangular dark mount is removed; the circular pink socket is drawn directly on the footer PCB. Evidence: Checks/native-editor.txt and native PNGs.
- Native full snapshot p95 4.64546 ms and animation paint p95 2.210 ms at 560 × 720, excluding probe allocation. These are renderer harness measurements, not DAW performance measurements.
- Pluginval strictness 5 passed on the final packaged standalone VST3, including editor/audio/state/automation checks. Evidence: Checks/pluginval-console.txt.
- Individual installer payload and transaction checks are recorded in installer-tests.txt. Suite payload and transaction checks accompany suite 1.2.1. Packaging checks verify byte-identical ENERGY payloads across standalone/individual/suite, unchanged four sibling payloads, and strict signatures on both original and extracted installers. Evidence: Checks/release-verification.txt and archive-verification.txt.

All installer self-tests use temporary fixtures. No live plugin folder was changed. This is an ad hoc signed Apple Silicon VST3 build targeting macOS 12+, tested on macOS 14.5. Real DAW sessions and subjective listening remain audition work.
