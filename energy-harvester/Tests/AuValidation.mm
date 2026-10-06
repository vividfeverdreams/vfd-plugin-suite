// In-process Audio Unit validation for the AU in the build folder. The component's factory is registered with
// AudioComponentRegister for this process only, so nothing is installed or written to plug-in folders. The checks
// follow auval's main areas: open/close, channel configurations, parameter info, stream formats, initialisation,
// rendering at several slice sizes, state (ClassInfo) round trip, reset, bypass, latency, tail and the Cocoa view.
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& text) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", text.c_str()); if (!ok) ++failures; }
OSType fourCC(NSString* s) { const char* c = s.UTF8String; return OSType((uint8_t(c[0]) << 24) | (uint8_t(c[1]) << 16) | (uint8_t(c[2]) << 8) | uint8_t(c[3])); }

struct Source { double phase = 0, rate = 48000; };
OSStatus provide(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* data) {
    auto* s = static_cast<Source*>(ref);
    for (UInt32 i = 0; i < frames; ++i) {
        const float v = float(.4 * std::sin(s->phase) + .2 * std::sin(s->phase * 5.03));
        s->phase += 2 * M_PI * 110 / s->rate;
        for (UInt32 b = 0; b < data->mNumberBuffers; ++b) static_cast<float*>(data->mBuffers[b].mData)[i] = v;
    }
    return noErr;
}
AudioStreamBasicDescription format(double rate, UInt32 channels) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = rate; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = AudioFormatFlags(kAudioFormatFlagsNativeFloatPacked) | AudioFormatFlags(kAudioFormatFlagIsNonInterleaved);
    f.mBytesPerPacket = f.mBytesPerFrame = 4; f.mFramesPerPacket = 1; f.mChannelsPerFrame = channels; f.mBitsPerChannel = 32;
    return f;
}
struct Rendered { bool ok = true, finite = true; double peak = 0, differs = 0; };
Rendered render(AudioUnit unit, Source& source, UInt32 channels, UInt32 frames, int blocks, double& sampleTime) {
    Rendered r;
    std::vector<std::vector<float>> storage(channels, std::vector<float>(frames));
    std::vector<uint8_t> listMemory(sizeof(AudioBufferList) + sizeof(AudioBuffer) * channels);
    auto* list = reinterpret_cast<AudioBufferList*>(listMemory.data());
    for (int block = 0; block < blocks; ++block) {
        list->mNumberBuffers = channels;
        for (UInt32 c = 0; c < channels; ++c) { list->mBuffers[c].mNumberChannels = 1; list->mBuffers[c].mDataByteSize = frames * 4; list->mBuffers[c].mData = storage[c].data(); }
        AudioTimeStamp stamp{}; stamp.mSampleTime = sampleTime; stamp.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        const double phaseBefore = source.phase;
        if (AudioUnitRender(unit, &flags, &stamp, 0, frames, list) != noErr) { r.ok = false; return r; }
        // Recreate the dry input for this block to measure how far the output moved from it.
        double phase = phaseBefore;
        for (UInt32 i = 0; i < frames; ++i) {
            const float dry = float(.4 * std::sin(phase) + .2 * std::sin(phase * 5.03)); phase += 2 * M_PI * 110 / source.rate;
            for (UInt32 c = 0; c < channels; ++c) {
                const float y = static_cast<float*>(list->mBuffers[c].mData)[i];
                r.finite = r.finite && std::isfinite(y); r.peak = std::max(r.peak, double(std::abs(y))); r.differs = std::max(r.differs, double(std::abs(y - dry)));
            }
        }
        sampleTime += frames;
    }
    return r;
}
bool configure(AudioUnit unit, double rate, UInt32 channels, UInt32 maxFrames, Source& source) {
    auto f = format(rate, channels);
    bool ok = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) == noErr;
    ok = ok && AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f) == noErr;
    ok = ok && AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames) == noErr;
    source.rate = rate;
    AURenderCallbackStruct callback{provide, &source};
    ok = ok && AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback) == noErr;
    return ok;
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) { std::printf("usage: EnergyAuValidation <path to ENERGY HARVESTER.component>\n"); return 2; }
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        CFBundleRef bundle = CFBundleCreate(nullptr, (__bridge CFURLRef)url);
        check(bundle != nullptr && CFBundleLoadExecutable(bundle), "AU bundle loads from the build folder");
        if (bundle == nullptr) return 1;
        NSDictionary* info = (__bridge NSDictionary*)CFBundleGetInfoDictionary(bundle);
        NSDictionary* entry = [info[@"AudioComponents"] firstObject];
        check(entry != nil, "Info.plist declares an AudioComponents entry");
        if (entry == nil) return 1;
        AudioComponentDescription desc{fourCC(entry[@"type"]), fourCC(entry[@"subtype"]), fourCC(entry[@"manufacturer"]), 0, 0};
        check(desc.componentType == kAudioUnitType_Effect && desc.componentSubType == 'Enh1' && desc.componentManufacturer == 'Yoib', "component is aufx Enh1 Yoib");
        auto factory = (AudioComponentFactoryFunction)CFBundleGetFunctionPointerForName(bundle, (__bridge CFStringRef)entry[@"factoryFunction"]);
        check(factory != nullptr, std::string("factory function ") + [entry[@"factoryFunction"] UTF8String] + " is exported");
        AudioComponent component = AudioComponentRegister(&desc, (__bridge CFStringRef)entry[@"name"], UInt32([entry[@"version"] unsignedIntValue]), factory);
        check(component != nullptr, "component registered for this process only (nothing installed)");
        if (component == nullptr) return 1;
        std::printf("Component: %s, version %u\n", [entry[@"name"] UTF8String], unsigned([entry[@"version"] unsignedIntValue]));

        // Open and close repeatedly.
        bool cycles = true;
        for (int i = 0; i < 5; ++i) { AudioUnit u = nullptr; cycles = cycles && AudioComponentInstanceNew(component, &u) == noErr && u != nullptr && AudioComponentInstanceDispose(u) == noErr; }
        check(cycles, "five open/close cycles");

        AudioUnit unit = nullptr;
        check(AudioComponentInstanceNew(component, &unit) == noErr && unit != nullptr, "instance opens");
        UInt32 size = 0; Boolean writable = false;
        if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &size, &writable) == noErr && size > 0) {
            std::vector<AUChannelInfo> channels(size / sizeof(AUChannelInfo));
            AudioUnitGetProperty(unit, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, channels.data(), &size);
            bool mono = false, stereo = false;
            for (auto c : channels) { mono = mono || (c.inChannels == 1 && c.outChannels == 1) || (c.inChannels == -1 && c.outChannels == -1); stereo = stereo || (c.inChannels == 2 && c.outChannels == 2) || (c.inChannels == -1 && c.outChannels == -1); }
            check(mono && stereo, "supports mono-to-mono and stereo-to-stereo (" + std::to_string(channels.size()) + " channel configurations)");
        } else check(false, "SupportedNumChannels readable");

        // Parameters: every entry readable, named, with its default inside its range.
        size = 0; AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable);
        std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
        if (size > 0) AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size);
        bool sane = !ids.empty(); AudioUnitParameterID harvest = 0, mode = 0; bool foundHarvest = false, foundMode = false;
        for (auto id : ids) {
            AudioUnitParameterInfo p{}; UInt32 s = sizeof p;
            if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &p, &s) != noErr) { sane = false; continue; }
            NSString* name = (p.flags & kAudioUnitParameterFlag_HasCFNameString) ? (__bridge NSString*)p.cfNameString : [NSString stringWithUTF8String:p.name];
            sane = sane && name.length > 0 && p.minValue <= p.defaultValue && p.defaultValue <= p.maxValue && (p.flags & kAudioUnitParameterFlag_IsReadable);
            if ([name isEqualToString:@"Harvest"]) { harvest = id; foundHarvest = true; }
            if ([name isEqualToString:@"Mode"]) { mode = id; foundMode = true; }
            if (p.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(p.cfNameString);
        }
        check(sane && ids.size() >= 23 && foundHarvest && foundMode, std::to_string(ids.size()) + " parameters with valid names, ranges and defaults");

        Source source;
        check(configure(unit, 48000, 2, 4096, source), "48 kHz stereo float format and 4096-frame slices accepted");
        check(AudioUnitInitialize(unit) == noErr, "initialises");
        Float64 latency = -1, tail = -1; size = sizeof latency;
        AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size); size = sizeof tail;
        AudioUnitGetProperty(unit, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &size);
        check(latency == 0 && tail >= 8.0 && tail <= 60.0, "latency " + std::to_string(latency) + " s, tail " + std::to_string(tail) + " s");

        double time = 0;
        auto idle = render(unit, source, 2, 512, 20, time);
        check(idle.ok && idle.finite && idle.differs == 0, "idle render passes audio through exactly");
        bool sizes = true;
        for (UInt32 frames : {1u, 64u, 333u, 1024u, 4096u}) { auto r = render(unit, source, 2, frames, 4, time); sizes = sizes && r.ok && r.finite; }
        check(sizes, "renders 1, 64, 333, 1024 and 4096-frame slices");

        // HARVEST on: the spin changes the audio, then lands exactly back on the input.
        AudioUnitSetParameter(unit, harvest, kAudioUnitScope_Global, 0, 1, 0);
        auto spin = render(unit, source, 2, 512, 40, time);
        render(unit, source, 2, 512, 60, time);   // past the 800 ms default length
        auto landed = render(unit, source, 2, 512, 20, time);
        check(spin.ok && spin.finite && spin.differs > .05 && landed.differs == 0 && spin.peak < 1.5, "HARVEST spin changes the sound (max deviation " + std::to_string(spin.differs) + ") and lands exactly dry");
        AudioUnitSetParameter(unit, harvest, kAudioUnitScope_Global, 0, 0, 0);
        render(unit, source, 2, 512, 4, time);

        // Rewind mode through the parameter, then the bypass property.
        AudioUnitSetParameter(unit, mode, kAudioUnitScope_Global, 0, 4, 0);
        AudioUnitSetParameter(unit, harvest, kAudioUnitScope_Global, 0, 1, 0);
        auto rewind = render(unit, source, 2, 512, 20, time);
        check(rewind.ok && rewind.finite && rewind.differs > .05, "Rewind mode renders through the parameter interface");
        AudioUnitSetParameter(unit, harvest, kAudioUnitScope_Global, 0, 0, 0);
        UInt32 bypass = 1; AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass, sizeof bypass);
        render(unit, source, 2, 512, 4, time);
        auto bypassed = render(unit, source, 2, 512, 10, time);
        check(bypassed.ok && bypassed.differs == 0, "bypass property passes audio through exactly");
        bypass = 0; AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass, sizeof bypass);
        check(AudioUnitReset(unit, kAudioUnitScope_Global, 0) == noErr, "reset");

        // State round trip through ClassInfo into a fresh instance.
        CFPropertyListRef state = nullptr; size = sizeof state;
        const bool saved = AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size) == noErr && state != nullptr;
        AudioUnit other = nullptr; AudioComponentInstanceNew(component, &other);
        const bool restored = saved && AudioUnitSetProperty(other, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state) == noErr;
        AudioUnitParameterValue restoredMode = -1; AudioUnitGetParameter(other, mode, kAudioUnitScope_Global, 0, &restoredMode);
        check(restored && restoredMode == 4, "ClassInfo state round trip restores Rewind mode in a new instance");
        if (state) CFRelease(state);
        AudioComponentInstanceDispose(other);

        // Cocoa view is declared.
        size = 0; const bool hasView = AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) == noErr && size > 0;
        check(hasView, "Cocoa editor view declared");
        check(AudioUnitUninitialize(unit) == noErr, "uninitialises");

        // Mono at 44.1 kHz.
        check(configure(unit, 44100, 1, 1024, source) && AudioUnitInitialize(unit) == noErr, "re-initialises mono at 44.1 kHz");
        time = 0; auto mono = render(unit, source, 1, 441, 20, time);
        check(mono.ok && mono.finite, "mono render");
        AudioUnitUninitialize(unit);
        check(AudioComponentInstanceDispose(unit) == noErr, "instance closes");
        std::printf(failures == 0 ? "ENERGY HARVESTER AU validation passed\n" : "ENERGY HARVESTER AU validation FAILED (%d)\n", failures);
        return failures == 0 ? 0 : 1;
    }
}
