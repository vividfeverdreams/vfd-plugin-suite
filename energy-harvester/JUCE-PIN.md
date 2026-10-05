# Bundled JUCE

JUCE 9.0.2, copied into this project from the existing local framework checkout. No sibling source was edited. The included tree already contains the ENERGY-only VST3 queue patch; do not apply it twice.

The patch reconstructs the unmodified 9.0.2 wrapper baseline from the existing documented BACK-SCRATCH patch, then adds a non-owning thread-local queue view and zero-length control flush delivery. It carries no audio/event buffer and imposes no queue capacity limit. The processor streams the host-owned queues during the callback and the wrapper clears the view through RAII on all returns. Same-offset duration/intensity/bypass changes precede Harvest.

JUCE license: external/JUCE/LICENSE.md.

To apply to another pristine 9.0.2 checkout: `cd external/JUCE && patch -p1 < ../../JUCE-VST3-automation.patch`.

Patched wrapper SHA-256: `c4840d73a49be19413ccab0aaff3b6ac01f15bd81e4f3280a295f4d6ecb3aa2c`
Patch SHA-256: `590047f25219fc0dd1316f1574791dffccebc96df7e6a2a1756a7b1829863e6f`
