#include "EnergyHarvesterProcessor.h"
#include "EnergyHarvesterEditor.h"
#include "AutomationBridge.h"
#include <cstdlib>
#include "Timing.h"

static juce::String curveName(float value) {
    const int v=juce::roundToInt(value);
    if(v==0) return "S-CURVE";
    if(v<=-70) return "LOG";
    if(v<0) return "EARLY "+juce::String(-v);
    if(v>=70) return "SLAM LATE";
    return "EXPO "+juce::String(v);
}

juce::AudioProcessorValueTreeState::ParameterLayout EnergyHarvesterProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("harvest",1),"Harvest",false));
    juce::NormalisableRange<float> time(80,8000,
        [](float,float,float x){return 80.f*std::pow(100.f,x);},
        [](float,float,float x){return std::log(x/80.f)/std::log(100.f);});
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("spinUpMs",1),"Spin-Up Time",time,800,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("fxAmount",1),"FX Amount",
        juce::NormalisableRange<float>(0,100,.01f),45,juce::AudioParameterFloatAttributes().withLabel("%")));
    const char* names[]{"Panning","Chorus","Phaser","Flanger","Washout"};
    for(int i=3;i<=7;++i) p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(ids[size_t(i)],1),names[i-3],i==5));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("syncMode",1),"Tempo Sync",false));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("syncDivision",1),"Sync Division",juce::StringArray{"1/8","1/4","1/2","1 BAR","2 BARS","4 BARS"},1));
    juce::NormalisableRange<float> outputRange(-18,18,
        [](float lo,float hi,float value){return lo+(hi-lo)*value;},
        [](float lo,float hi,float value){return (value-lo)/(hi-lo);},
        [](float lo,float hi,float value){return juce::jlimit(lo,hi,std::round(value*10.f)/10.f);});
    outputRange.interval=.1f;
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("outputDb",1),"Harvest Output Level",
        outputRange,0,juce::AudioParameterFloatAttributes().withLabel("dB")));
    // 0.4.0 additions, appended so every original parameter keeps its index and ID.
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("mode",1),"Mode",juce::StringArray{"Tape Start","Tape Stop","Brake","Stop > Start","Rewind"},0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("engine",1),"Tape Engine",juce::StringArray{"Classic","True Tape"},0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("curve",1),"Speed Curve",juce::NormalisableRange<float>(-100,100,1),0,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v,int){return curveName(v);})));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("depth",1),"Start Depth",juce::NormalisableRange<float>(1,48,1),24,
        juce::AudioParameterFloatAttributes().withLabel("st").withStringFromValueFunction([](float v,int){return "-"+juce::String(juce::roundToInt(v))+" st";})));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("filterMode",1),"Filter Type",juce::StringArray{"LP 12","LP 24","BP"},0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("resonance",1),"Filter Resonance",juce::NormalisableRange<float>(0,100,.1f),0,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    juce::NormalisableRange<float> start(40,4000,
        [](float,float,float x){return 40.f*std::pow(100.f,x);},
        [](float,float,float x){return std::log(x/40.f)/std::log(100.f);});
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("filterStart",1),"Filter Start",start,350,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v,int){return v<1000?juce::String(juce::roundToInt(v))+" Hz":juce::String(v*.001f,2)+" kHz";})));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("subKeep",1),"Sub Keep",juce::StringArray{"Off","60 Hz","90 Hz","120 Hz","150 Hz"},0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("gridLand",1),"Land On Grid",juce::StringArray{"Off","1/4","1 Bar","2 Bars"},0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("landing",1),"Landing",juce::StringArray{"Clean","Tail","Hold"},0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("mix",1),"Mix",juce::NormalisableRange<float>(0,100,.1f),100,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    // Not automatable: chooses between the exact 0.3.2 rendering and the 0.4.0 refinements. Undoable as a gesture.
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("latestSound",1),"Latest Sound",true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)));
    return p;
}
EnergyHarvesterProcessor::EnergyHarvesterProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
    .withOutput("Output",juce::AudioChannelSet::stereo(),true)),apvts(*this,nullptr,"ENERGY_HARVESTER_STATE",layout()) { setLatencySamples(0); for(size_t i=0;i<ids.size();++i){parameters_[i]=apvts.getParameter(ids[i]);raw_[i]=apvts.getRawParameterValue(ids[i]);jassert(parameters_[i]!=nullptr);} }
bool EnergyHarvesterProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out=l.getMainOutputChannelSet(); return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==l.getMainInputChannelSet();
}
void EnergyHarvesterProcessor::configure() noexcept {
    const float ms=values_[8]>=.5f?float(energy::syncMilliseconds(int(std::round(values_[9])),bpm_,numerator_,denominator_)):values_[1];
    engine.setTime(ms); engine.setFx(values_[2]);engine.setOutputDb(values_[10]);
    unsigned mask=0;for(int i=3;i<=7;++i)if(values_[size_t(i)]>=.5f)mask|=1u<<(i-3);
    engine.setEffects(mask);desiredTimeMs.store(ms,std::memory_order_relaxed);
    engine.setMode(int(std::round(values_[11]))); engine.setTrueTape(values_[12]>=.5f);
    engine.setCurve(values_[13]/100.f); engine.setDepth(values_[14]);
    // The log range's round trip can land a hair off the 350 Hz default; snap so the default is exact.
    const float start=std::abs(values_[17]-350.f)<.05f?350.f:values_[17];
    engine.setFilter(int(std::round(values_[15])),values_[16]/100.f,start);
    constexpr float keeps[]{0,60,90,120,150};
    engine.setSubKeep(keeps[size_t(juce::jlimit(0,4,int(std::round(values_[18]))))]);
    engine.setLanding(int(std::round(values_[20]))); engine.setMix(values_[21]/100.f); engine.setLatestSound(values_[22]>=.5f);
}
float EnergyHarvesterProcessor::gridMilliseconds(int sample) const noexcept {
    // Land on the grid: the spin ends on the next quarter, bar or pair of bars after the gate edge.
    const int grid=int(std::round(values_[19]));
    if(grid<=0 || !playing_ || !ppqValid_ || bpm_<10 || rate_<=0) return -1;
    const double ppq=ppqStart_+double(sample)*bpm_/(60.0*rate_), bar=numerator_*4.0/denominator_;
    double unit=1,origin=0;
    if(grid>=2) {
        unit=bar*(grid==3?2:1); origin=barStartValid_?barStart_:0;
        if(grid==3 && barStartValid_ && std::fmod(std::abs(std::round(barStart_/bar)),2.0)>.5) origin-=bar;
    }
    double next=origin+std::ceil((ppq-origin)/unit-1e-9)*unit;
    if((next-ppq)*60000/bpm_<30) next+=unit;   // never squeeze a launch below 30 ms
    return float((next-ppq)*60000/bpm_);
}
void EnergyHarvesterProcessor::edge(bool on,int sample) noexcept {
    if(on && !gate_) if(const float ms=gridMilliseconds(sample); ms>0) engine.setTime(ms);
    gate_=on; engine.setGate(gate_);
    configure();
}
void EnergyHarvesterProcessor::prepareToPlay(double rate,int) {
    for(size_t i=0;i<raw_.size();++i)values_[i]=raw_[i]->load();gate_=values_[0]>=.5f;
    rate_=rate;configure();engine.prepare(rate,getTotalNumInputChannels());engine.reset(gate_);
    resetPending=true; hadTimeline_=false; wasPlaying_=false; publish(false);
}
void EnergyHarvesterProcessor::publish(bool bypass,float peak) noexcept {
    visualGate.store(gate_,std::memory_order_relaxed); visualBypass.store(bypass,std::memory_order_relaxed);
    visualState.store(int(engine.state()),std::memory_order_relaxed); visualProgress.store(engine.progress(),std::memory_order_relaxed);
    latchedTime.store(engine.latchedMs(),std::memory_order_relaxed); launchCount.store(engine.launches(),std::memory_order_relaxed);
    publishedSamples.store(engine.processedSamples(),std::memory_order_relaxed);
    visualRatio.store(engine.speed(),std::memory_order_relaxed); visualMode.store(int(engine.mode()),std::memory_order_relaxed);
    // Hold the largest output peak until the UI consumes it with exchange(0): lock-free CAS max.
    if(std::isfinite(peak) && peak>0) {
        float held=visualLevel.load(std::memory_order_relaxed);
        while(peak>held && !visualLevel.compare_exchange_weak(held,peak,std::memory_order_relaxed)) {}
    }
}
void EnergyHarvesterProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) {process(b,false);}
void EnergyHarvesterProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) {process(b,true);}
void EnergyHarvesterProcessor::process(juce::AudioBuffer<float>& b,bool forceBypass) {
    juce::ScopedNoDenormals guard;
    const int count=b.getNumSamples(), channels=std::min(2,b.getNumChannels());
    for(int ch=getTotalNumInputChannels();ch<b.getNumChannels();++ch) b.clear(ch,0,count);
    const auto source=energyBridge::current;
    struct Cursor { int q=-1,index=0,count=0,offset=0; float value=0; bool ready=false; };
    constexpr int bypassSlot=parameterCount;
    std::array<Cursor,parameterCount+1> cursors{};
    // Out-of-range offsets are clamped into this block rather than dropped (clamping keeps queue order).
    const int lastOffset=std::max(0,count-1);
    const auto advance=[&](Cursor& c) {
        c.ready=false;
        while(c.index<c.count) {
            int offset=0; float value=0;
            if(source.point(source.changes,c.q,c.index++,offset,value) && std::isfinite(value)) {
                c.offset=std::clamp(offset,0,lastOffset); c.value=juce::jlimit(0.f,1.f,value); c.ready=true; break;
            }
        }
    };
    if(source.active() && source.changes) {
        for(int q=0;q<source.queueCount(source.changes);++q) {
            const void* param=source.parameter(source.owner,source.changes,q); int slot=-1;
            for(int p=0;p<parameterCount;++p) if(param==parameters_[size_t(p)]) slot=p;
            if(param==source.bypassParameter) slot=bypassSlot;
            if(slot>=0) { auto& c=cursors[size_t(slot)]; c.q=q;c.count=source.pointCount(source.changes,q); advance(c); }
        }
    }
    const bool hardReset=resetPending.exchange(false);
    bool lifecycle=false;
    // A zero-length flush advances no audio time and may arrive without a ProcessContext (JUCE then reports
    // stopped at sample 0). It must not look like a transport start or erase the host tempo: keep the last view.
    if(count>0) {
        bpm_=120;numerator_=4;denominator_=4;bool tempo=false;ppqValid_=barStartValid_=playing_=false;
        if(auto* playhead=getPlayHead()) if(auto info=playhead->getPosition()) {
            if(auto bpm=info->getBpm();bpm && std::isfinite(*bpm) && *bpm>=10 && *bpm<=999){bpm_=*bpm;tempo=true;}
            if(auto sig=info->getTimeSignature()){numerator_=sig->numerator;denominator_=sig->denominator;}
            if(auto ppq=info->getPpqPosition();ppq && std::isfinite(*ppq)){ppqStart_=*ppq;ppqValid_=tempo;}
            if(auto bar=info->getPpqPositionOfLastBarStart();bar && std::isfinite(*bar)){barStart_=*bar;barStartValid_=true;}
            const bool playing=info->getIsPlaying(); playing_=playing; if(playing&&!wasPlaying_) lifecycle=true;
            if(auto position=info->getTimeInSamples()) {
                if(playing&&hadTimeline_&&wasPlaying_&&*position!=nextPosition_) lifecycle=true;
                nextPosition_=*position+count; hadTimeline_=true;
            } else hadTimeline_=false;
            wasPlaying_=playing;
        }
        hasHostTempo.store(tempo,std::memory_order_relaxed); hostBpm.store(float(bpm_),std::memory_order_relaxed);
    }
    // Queue-backed values use the previously accepted state, never the wrapper's final scalar.
    // Seqlock against setStateInformation: if a recall overlapped these raw reads (odd or changed epoch), the
    // restored values become a new baseline without a gate edge instead of being treated as automation.
    const uint32_t epochBefore=stateEpoch.load();
    for(size_t i=1;i<raw_.size();++i)if(cursors[i].q<0)values_[i]=raw_[i]->load();
    const bool rawGate=raw_[0]->load()>=.5f;
    const uint32_t epochAfter=stateEpoch.load();
    // A consistent read saw the same even epoch on both sides. After an inconsistent one the raw values may
    // predate the recall's last writes, so keep resyncing (never edge-detecting) until a consistent read.
    const bool consistent=(epochBefore&1u)==0 && epochBefore==epochAfter;
    if(!consistent || epochBefore!=seenEpoch_ || recallUnsettled_) lifecycle=true;
    recallUnsettled_=!consistent; seenEpoch_=epochAfter;
    const bool resync=hardReset||lifecycle;
    if(cursors[0].q<0 && resync) gate_=rawGate;
    bool bypass=source.active()?source.initialBypass:forceBypass;
    if(resync) {
        if(cursors[0].ready && cursors[0].offset==0) { gate_=cursors[0].value>=.5f; advance(cursors[0]); }
        // After a processing gap the history is stale; while audio keeps running, fade the voice and keep it.
        if(hardReset) engine.reset(gate_); else engine.restart(gate_);
    }
    engine.setBypass(bypass);configure();
    if(cursors[0].q<0 && !resync && rawGate!=gate_) edge(rawGate,0);
    float peak=0;
    // A zero-sample parameter flush updates state without manufacturing audible time.
    for(int sample=0;sample<std::max(1,count);++sample) {
        // Same-sample order: timing and launch-latched controls, effects and tone, bypass, then HARVEST.
        for(int slot: {1,8,9,11,12,13,14,19,20,3,4,5,6,7,2,10,15,16,17,18,21,22,bypassSlot,0}) {
            auto& c=cursors[size_t(slot)];
            while(c.ready && c.offset<=sample) {
                queuePoints.fetch_add(1,std::memory_order_relaxed);
                if(slot>0 && slot<parameterCount) {values_[size_t(slot)]=parameters_[size_t(slot)]->convertFrom0to1(c.value);configure();}
                if(slot==bypassSlot) {bypass=c.value>=.5f; engine.setBypass(bypass);}
                if(slot==0) { const bool on=c.value>=.5f; if(on!=gate_) edge(on,sample); }
                advance(c);
            }
        }
        if(sample<count && channels>0) {
            energy::Frame input{b.getSample(0,sample),channels==2?b.getSample(1,sample):b.getSample(0,sample)};
            auto output=engine.process(input,bypass); b.setSample(0,sample,output.left);
            if(channels==2) b.setSample(1,sample,output.right);
            peak=std::max(peak,std::max(std::abs(output.left),channels==2?std::abs(output.right):0.f));
        }
    }
    publish(bypass,peak);
}
void EnergyHarvesterProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state=apvts.copyState(); state.setProperty("schemaVersion",4,nullptr);state.setProperty("dspVersion",4,nullptr);
    state.setProperty("reducedMotion",reducedMotion.load(),nullptr);state.setProperty("uiScale",uiScale.load(),nullptr);
    const auto [folder,name]=presetLabel();state.setProperty("presetName",name,nullptr);state.setProperty("presetFolder",folder,nullptr);
    if(auto xml=state.createXml()) copyXmlToBinary(*xml,block);
}
namespace {
// Accepted range for a stored parameter value; integers for switches and choices.
bool storedValueValid(int slot,double value) {
    const auto whole=[&](double lo,double hi){return value>=lo&&value<=hi&&value==std::floor(value);};
    switch(slot) {
        case 0: case 3: case 4: case 5: case 6: case 7: case 8: case 12: case 22: return whole(0,1);
        case 1: return value>=80&&value<=8000;
        case 2: case 16: case 21: return value>=0&&value<=100;
        case 9: return whole(0,5);
        case 10: return value>=-18&&value<=18;
        case 11: return whole(0,4);
        case 13: return value>=-100&&value<=100;
        case 14: return value>=1&&value<=48;
        case 15: return whole(0,2);
        case 17: return value>=40&&value<=4000;
        case 18: return whole(0,4);
        case 19: return whole(0,3);
        case 20: return whole(0,2);
        default: return false;
    }
}
}
void EnergyHarvesterProcessor::setStateInformation(const void* data,int size) {
    auto xml=getXmlFromBinary(data,size); if(!xml || !xml->hasTagName("ENERGY_HARVESTER_STATE")) return;
    auto state=juce::ValueTree::fromXml(*xml);const int schema=int(state.getProperty("schemaVersion",1));if(schema<1||schema>4)return;
    // Schema 1-3 hold the 0.1-0.3 parameters only; schema 4 may hold all of them.
    const size_t stored=schema==1?3u:schema==2?10u:schema==3?11u:size_t(parameterCount);
    std::array<bool,parameterCount> found{};
    for(auto child:state) {
        if(!child.hasType("PARAM"))return;
        auto id=child.getProperty("id").toString();int slot=-1;
        for(size_t i=0;i<ids.size();++i)if(id==ids[i])slot=int(i);
        if(slot<0 || size_t(slot)>=stored || found[size_t(slot)]) return;found[size_t(slot)]=true;
        auto raw=child.getProperty("value"); if(!raw.isDouble()&&!raw.isInt()&&!raw.isString()) return;
        const auto text=raw.toString().toStdString();char* end=nullptr;
        const double value=std::strtod(text.c_str(),&end);
        if(end==text.c_str()||*end!='\0'||!std::isfinite(value)||!storedValueValid(slot,value))return;
    }
    const size_t required=schema==1?3u:schema==2?10u:11u;
    for(size_t i=0;i<required;++i)if(!found[i])return;
    // Missing controls take their defaults, except that projects from 0.3.x and earlier keep the 0.3.2 sound
    // (Latest Sound off) until the user upgrades them deliberately.
    for(size_t i=0;i<ids.size();++i)if(!found[i]){
        juce::ValueTree child("PARAM");child.setProperty("id",ids[i],nullptr);
        child.setProperty("value",i==22&&schema<4?0.f:defaults[i],nullptr);state.addChild(child,-1,nullptr);}
    state.setProperty("schemaVersion",4,nullptr);state.setProperty("dspVersion",4,nullptr);
    // Bracket the recall (odd epoch while parameters change) so a restored HARVEST ON can never read as a
    // gate edge; the next callbacks adopt it as clean AT SPEED and fade any running voice.
    stateEpoch.fetch_add(1);
    apvts.replaceState(state); reducedMotion=bool(state.getProperty("reducedMotion",false));
    const int scale=int(state.getProperty("uiScale",100));
    uiScale=std::find(uiScales.begin(),uiScales.end(),scale)!=uiScales.end()?scale:100;
    setPresetLabel(state.getProperty("presetFolder","").toString(),state.getProperty("presetName",schema<4?"Project":"Default").toString());
    stateEpoch.fetch_add(1);
}
std::map<juce::String,float> EnergyHarvesterProcessor::currentValues() const {
    std::map<juce::String,float> values;
    for(size_t i=1;i<ids.size();++i) values[ids[i]]=raw_[i]->load();
    return values;
}
void EnergyHarvesterProcessor::applyPreset(const std::map<juce::String,float>& values,const juce::String& folder,const juce::String& name) {
    // Every control except HARVEST moves to the preset (missing ones to their defaults) as host-visible gestures,
    // so the host can undo the whole change. HARVEST is a performance switch and is never touched by presets.
    for(size_t i=1;i<ids.size();++i) {
        auto* parameter=parameters_[i];
        const auto found=values.find(ids[i]);
        const float value=found!=values.end()&&std::isfinite(found->second)?found->second:defaults[i];
        const float normalised=parameter->convertTo0to1(parameter->getNormalisableRange().snapToLegalValue(value));
        if(std::abs(normalised-parameter->getValue())<1e-7f) continue;
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(normalised); parameter->endChangeGesture();
    }
    setPresetLabel(folder,name);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}
juce::AudioProcessorEditor* EnergyHarvesterProcessor::createEditor(){return new EnergyHarvesterEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EnergyHarvesterProcessor();}
