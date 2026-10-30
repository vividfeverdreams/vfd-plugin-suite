#pragma once
// SHA-256 of interleaved float32 renders from unmodified 0.4.1 (2026-10-21, Apple clang).
// Input/settings are in testSubSafeCompatibility(). Check the whole render, not just its tail.
// libm may round differently on other platforms; these exact fingerprints target macOS arm64.
struct SubSafeReference { int sampleRate, hz; const char* sha256; };
constexpr SubSafeReference subSafeReferences[] {
    { 44100, 0, "21dda7037a7847d73387220e516fa277728a5bc6a289cd385f6628414e3560f5" },
    { 44100, 80, "b24a7bdfd217d6355c38adc4af3cd328b38e7144a9b5c1691fef5322c1fc5730" },
    { 44100, 120, "d2a14f8f5684a38c065094080838b12d00565833b41cd2e24d659c9279f16795" },
    { 44100, 200, "34895da1b4568f9de71544a8bfe91b68eac3c36e01b5affed00bdedd54ea2654" },
    { 48000, 0, "ec801ca961c66d8f021ec9f2df286ca9df6bb226d9495933a65fbfe56f3203c4" },
    { 48000, 80, "1a0d7fdd73a34f384d6a3e2b395181ebdc83527b3f27a9d6b7c3316b131357f6" },
    { 48000, 120, "519184652e4ace5464e0e8ff64307ab5abaa13838d3439818b5bcb457cba77f0" },
    { 48000, 200, "7f8896b6e47a498b0d4b3a4830ca06439c25105c561136411c82aa231e6fa107" },
    { 96000, 0, "462484c265848720cde01c9c295f420df3956e62a604f12f09c33b54457622cd" },
    { 96000, 80, "05a1fb1897a81a5e3f83162393d0b46e37dcfebc7e3f7b40f51894708c1d25cd" },
    { 96000, 120, "165ef2a9d240c8470d4e0b089e7d4ef4cc515b2fe83596702941b463fb2f01d6" },
    { 96000, 200, "edb5067b5e9d039668ecacf074929087a7b7b1e6da79454e806b09c7ec853176" },
};
