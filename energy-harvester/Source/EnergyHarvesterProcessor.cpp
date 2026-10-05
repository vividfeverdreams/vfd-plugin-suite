#include "EnergyHarvesterProcessor.h"
#include "EnergyHarvesterEditor.h"
#include "AutomationBridge.h"
#include <cstdlib>
#include "Timing.h"

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
    return p;
}
EnergyHarvesterProcessor::EnergyHarvesterProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
    .withOutput("Output",juce::AudioChannelSet::stereo(),true)),apvts(*this,nullptr,"ENERGY_HARVESTER_STATE",layout()) { setLatencySamples(0); for(size_t i=0;i<ids.size();++i){parameters_[i]=apvts.getParameter(ids[i]);raw_[i]=apvts.getRawParameterValue(ids[i]);} }
bool EnergyHarvesterProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out=l.getMainOutputChannelSet(); return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==l.getMainInputChannelSet();
}
void EnergyHarvesterProcessor::configure() noexcept {
    const float ms=values_[8]>=.5f?float(energy::syncMilliseconds(int(std::round(values_[9])),bpm_,numerator_,denominator_)):values_[1];
    engine.setTime(ms); engine.setFx(values_[2]);engine.setOutputDb(values_[10]);
    unsigned mask=0;for(int i=3;i<=7;++i)if(values_[size_t(i)]>=.5f)mask|=1u<<(i-3);
    engine.setEffects(mask);desiredTimeMs.store(ms,std::memory_order_relaxed);
}
void EnergyHarvesterProcessor::prepareToPlay(double rate,int) {
    for(size_t i=0;i<raw_.size();++i)values_[i]=raw_[i]->load();gate_=values_[0]>=.5f;
    configure();engine.prepare(rate,getTotalNumInputChannels());engine.reset(gate_);
    resetPending=true; hadTimeline_=false; wasPlaying_=false; publish(false);
}
void EnergyHarvesterProcessor::publish(bool bypass) noexcept {
    visualGate.store(gate_,std::memory_order_relaxed); visualBypass.store(bypass,std::memory_order_relaxed);
    visualState.store(int(engine.state()),std::memory_order_relaxed); visualProgress.store(engine.progress(),std::memory_order_relaxed);
    latchedTime.store(engine.latchedMs(),std::memory_order_relaxed); launchCount.store(engine.launches(),std::memory_order_relaxed);
}
void EnergyHarvesterProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) {process(b,false);}
void EnergyHarvesterProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) {process(b,true);}
void EnergyHarvesterProcessor::process(juce::AudioBuffer<float>& b,bool forceBypass) {
    juce::ScopedNoDenormals guard;
    for(int ch=getTotalNumInputChannels();ch<b.getNumChannels();++ch) b.clear(ch,0,b.getNumSamples());
    const auto source=energyBridge::current;
    struct Cursor { int q=-1,index=0,count=0,offset=0; float value=0; bool ready=false; };
    std::array<Cursor,12> cursors{};
    const auto advance=[&](Cursor& c) {
        c.ready=false;
        while(c.index<c.count) {
            int offset=0; float value=0;
            if(source.point(source.changes,c.q,c.index++,offset,value) && std::isfinite(value) && offset>=0 && offset<std::max(1,b.getNumSamples())) {
                c.offset=offset; c.value=juce::jlimit(0.f,1.f,value); c.ready=true; break;
            }
        }
    };
    if(source.active() && source.changes) {
        for(int q=0;q<source.queueCount(source.changes);++q) {
            const void* param=source.parameter(source.owner,source.changes,q); int slot=-1;
            for(int p=0;p<11;++p) if(param==parameters_[size_t(p)]) slot=p;
            if(param==source.bypassParameter) slot=11;
            if(slot>=0) { auto& c=cursors[size_t(slot)]; c.q=q;c.count=source.pointCount(source.changes,q); advance(c); }
        }
    }
    bool lifecycle=resetPending.exchange(false);
    bpm_=120;numerator_=4;denominator_=4;hasHostTempo.store(false,std::memory_order_relaxed);
    if(auto* playhead=getPlayHead()) if(auto info=playhead->getPosition()) {
        if(auto bpm=info->getBpm();bpm && std::isfinite(*bpm) && *bpm>=10 && *bpm<=999){bpm_=*bpm;hasHostTempo.store(true,std::memory_order_relaxed);}
        if(auto sig=info->getTimeSignature()){numerator_=sig->numerator;denominator_=sig->denominator;}
        const bool playing=info->getIsPlaying(); if(playing&&!wasPlaying_) lifecycle=true;
        if(auto position=info->getTimeInSamples()) {
            if(playing&&hadTimeline_&&wasPlaying_&&*position!=nextPosition_) lifecycle=true;
            nextPosition_=*position+b.getNumSamples(); hadTimeline_=true;
        } else hadTimeline_=false;
        wasPlaying_=playing;
    }
    // Queue-backed values use the previously accepted state, never the wrapper's final scalar.
    hostBpm.store(float(bpm_),std::memory_order_relaxed);
    for(size_t i=1;i<raw_.size();++i)if(cursors[i].q<0)values_[i]=raw_[i]->load();
    if(cursors[0].q<0 && lifecycle) gate_=raw_[0]->load()>=.5f;
    bool bypass=source.active()?source.initialBypass:forceBypass;
    if(lifecycle) {
        if(cursors[0].ready && cursors[0].offset==0) { gate_=cursors[0].value>=.5f; advance(cursors[0]); }
        engine.reset(gate_);
    }
    engine.setBypass(bypass);configure();
    if(cursors[0].q<0 && !lifecycle) {gate_=raw_[0]->load()>=.5f; engine.setGate(gate_);}
    const int count=b.getNumSamples(), channels=std::min(2,b.getNumChannels());
    // A zero-sample parameter flush updates state without manufacturing audible time.
    for(int sample=0;sample<std::max(1,count);++sample) {
        for(int slot: {1,8,9,3,4,5,6,7,2,10,11,0}) {
            auto& c=cursors[size_t(slot)];
            while(c.ready && c.offset<=sample) {
                queuePoints.fetch_add(1,std::memory_order_relaxed);
                if(slot>0 && slot<11) {values_[size_t(slot)]=parameters_[size_t(slot)]->convertFrom0to1(c.value);configure();}
                if(slot==11) {bypass=c.value>=.5f; engine.setBypass(bypass);}
                if(slot==0) {gate_=c.value>=.5f; engine.setGate(gate_);}
                advance(c);
            }
        }
        if(sample<count && channels>0) {
            energy::Frame input{b.getSample(0,sample),channels==2?b.getSample(1,sample):b.getSample(0,sample)};
            auto output=engine.process(input,bypass); b.setSample(0,sample,output.left);
            if(channels==2) b.setSample(1,sample,output.right);
        }
    }
    publish(bypass);
}
void EnergyHarvesterProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state=apvts.copyState(); state.setProperty("schemaVersion",3,nullptr);state.setProperty("dspVersion",3,nullptr);
    state.setProperty("reducedMotion",reducedMotion.load(),nullptr);state.setProperty("uiScale",uiScale.load(),nullptr);
    if(auto xml=state.createXml()) copyXmlToBinary(*xml,block);
}
void EnergyHarvesterProcessor::setStateInformation(const void* data,int size) {
    auto xml=getXmlFromBinary(data,size); if(!xml || !xml->hasTagName("ENERGY_HARVESTER_STATE")) return;
    auto state=juce::ValueTree::fromXml(*xml);const int schema=int(state.getProperty("schemaVersion",1));if(schema<1||schema>3)return;
    std::array<bool,11> found{};
    for(auto child:state) {
        if(!child.hasType("PARAM"))return;
        auto id=child.getProperty("id").toString();int slot=-1;
        for(size_t i=0;i<ids.size();++i)if(id==ids[i])slot=int(i);
        if(slot<0 || (slot>=(schema==1?3:schema==2?10:11)) || found[size_t(slot)]) return;found[size_t(slot)]=true;
        auto raw=child.getProperty("value"); if(!raw.isDouble()&&!raw.isInt()&&!raw.isString()) return;
        const auto text=raw.toString().toStdString();char* end=nullptr;
        const double value=std::strtod(text.c_str(),&end);
        if(end==text.c_str()||*end!='\0'||!std::isfinite(value))return;
        if((slot==1&&(value<80||value>8000))||(slot==2&&(value<0||value>100))||(slot==9&&(value<0||value>5||value!=std::floor(value))))return;
        if((slot==0||(slot>=3&&slot<=8))&&value!=0&&value!=1)return;
        if(slot==10&&(value< -18 || value>18))return;
    }
    const size_t required=schema==1?3u:schema==2?10u:11u;
    for(size_t i=0;i<required;++i)if(!found[i])return;
    for(size_t i=required;i<ids.size();++i){juce::ValueTree child("PARAM");child.setProperty("id",ids[i],nullptr);child.setProperty("value",defaults[i],nullptr);state.addChild(child,-1,nullptr);}
    state.setProperty("schemaVersion",3,nullptr);state.setProperty("dspVersion",3,nullptr);
    apvts.replaceState(state); reducedMotion=bool(state.getProperty("reducedMotion",false));
    const int scale=int(state.getProperty("uiScale",100));uiScale=(scale==90||scale==125)?scale:100;
    resetPending=true;
}
juce::AudioProcessorEditor* EnergyHarvesterProcessor::createEditor(){return new EnergyHarvesterEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EnergyHarvesterProcessor();}
