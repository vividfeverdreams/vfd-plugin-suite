#include "EnergyHarvesterProcessor.h"
#include "EnergyHarvesterEditor.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <atomic>
#include <string>
#include <thread>
#include "Timing.h"
#include "Presets.h"
#include "BassLoop.h"
#include <cstdlib>
#include <iomanip>
#include <map>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<'\n';}
static void set(EnergyHarvesterProcessor& p,const char* id,float value){auto* v=p.apvts.getParameter(id);v->setValueNotifyingHost(v->convertTo0to1(value));}
static juce::Component* named(juce::Component& p,const char* name){if(p.getName()==name)return &p;for(auto* c:p.getChildren())if(auto* f=named(*c,name))return f;return nullptr;}
static juce::Image snapshot(juce::Component& e,const juce::File& file){auto image=e.createComponentSnapshot(e.getLocalBounds(),true);juce::FileOutputStream s(file);s.setPosition(0);s.truncate();check(s.openedOk()&&juce::PNGImageFormat().writeImageToStream(image,s),"native screenshot saved");return image;}
static void process(EnergyHarvesterProcessor& p,int samples){juce::AudioBuffer<float> b(2,samples);b.clear();juce::MidiBuffer m;p.processBlock(b,m);}
namespace {
// Minimal host transport for processor-level regressions.
struct FakeHead final : juce::AudioPlayHead {
    bool playing=false; juce::int64 position=0; double bpm=120; int numerator=4,denominator=4;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;info.setIsPlaying(playing);info.setTimeInSamples(position);info.setBpm(bpm);
        info.setTimeSignature(TimeSignature{numerator,denominator});
        // Musical position for Land On Grid: quarter notes from the start, bars from 0.
        const double ppq=double(position)/48000.*bpm/60.,bar=numerator*4.0/denominator;
        info.setPpqPosition(ppq);info.setPpqPositionOfLastBarStart(std::floor(ppq/bar+1e-9)*bar);return info;
    }
};
struct Verdicts { int failed=0; void check(bool ok,const std::string& text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failed;} };
// Streams a continuous 0.5-peak 60 Hz sine and reports the largest sample-to-sample output step.
struct SineRun {
    juce::int64 phase=0; float last=0; bool primed=false;
    double render(EnergyHarvesterProcessor& p,FakeHead* head,int samples){
        juce::AudioBuffer<float> b(2,samples);
        for(int i=0;i<samples;++i){const float x=float(.5*std::sin(2*juce::MathConstants<double>::pi*60*double(phase+i)/48000.+1.1));b.setSample(0,i,x);b.setSample(1,i,x);}
        juce::MidiBuffer m;p.processBlock(b,m);double step=0;
        for(int i=0;i<samples;++i){const float y=b.getSample(0,i);if(primed)step=std::max(step,double(std::abs(y-last)));last=y;primed=true;}
        phase+=samples;if(head!=nullptr&&head->playing)head->position+=samples;return step;
    }
};
}
static void demo(const juce::File& file,int fixture,unsigned mask=4,float outputDb=0){
    constexpr int rate=48000,seconds=9,length=rate*seconds;
    energy::HarvestEngine e;e.setTime(1600);e.setFx(45);e.setEffects(mask);e.prepare(rate,2);
    juce::AudioBuffer<float> b(2,length);
    for(int i=0;i<length;++i){double t=(i% (rate*3))/double(rate),beat=std::fmod(t,.5);int section=i/(rate*3);
        e.setFx(section==1?0.f:75.f);e.setOutputDb(section==2?outputDb:0);e.setGate(section>0&&t>=.35&&t<2.2);
        double f=fixture==0?110:fixture==1?220:330;
        double envelope=fixture==0?std::exp(-beat*7):fixture==1?.65:std::min(1.,beat/.008)*std::exp(-beat*3);
        double x=fixture==0?std::sin(2*juce::MathConstants<double>::pi*(f*t+1.5*(1-std::exp(-beat*20)))):
            std::sin(2*juce::MathConstants<double>::pi*f*t)+.3*std::sin(2*juce::MathConstants<double>::pi*f*1.5*t)+.15*std::sin(2*juce::MathConstants<double>::pi*f*2*t);
        const float input=float(x*.28*envelope);auto y=e.process({input,input});b.setSample(0,i,y.left);b.setSample(1,i,y.right);
    }
    juce::WavAudioFormat format;auto stream=file.createOutputStream();check(bool(stream),"demo file opened");stream->setPosition(0);stream->truncate();
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,2,24,{},0));
    check(writer&&writer->writeFromAudioSampleBuffer(b,0,length),"dry / tape start / selected FX comparison WAV saved");
}
// --spin-frames <folder>: render the real editor at 30 fps through one HARVEST cycle on a wobble bass
// (idle, launch, 1.6 s spin-up, at speed, release) for reviewing the light show.
static int spinFrames(const juce::File& folder,int mode=0){
    folder.createDirectory();EnergyHarvesterProcessor p;p.prepareToPlay(48000,800);set(p,"spinUpMs",1600);set(p,"harvest",0);set(p,"mode",float(mode));
    EnergyHarvesterEditor editor(p);const int block=800,fps=60;double phase=0;
    for(int n=0;n<fps*5;++n){const double t=double(n)/fps;
        if(n==fps/2)set(p,"harvest",1);if(n==fps*4)set(p,"harvest",0);
        juce::AudioBuffer<float> b(2,block);juce::MidiBuffer m;
        for(int i=0;i<block;++i){const double time=t+double(i)/48000,wob=.5+.5*std::sin(juce::MathConstants<double>::twoPi*4.667*time);
            phase+=55.0/48000;phase-=std::floor(phase);const float v=float((2*phase-1)*(.15+.45*wob*wob));b.setSample(0,i,v);b.setSample(1,i,v);}
        p.processBlock(b,m);editor.stepAnimationForTesting(1.0/fps);
        if(n%2==0){auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true);juce::FileOutputStream s(folder.getChildFile(juce::String::formatted("spin-%03d.png",n/2)));s.setPosition(0);s.truncate();juce::PNGImageFormat().writeImageToStream(image,s);}
    }
    std::cout<<"spin frames written to "<<folder.getFullPathName()<<'\n';return 0;
}

// Renders every factory preset through the processor on the 140 BPM test loop with a playing transport (so SYNC
// and Land On Grid apply), launching 1 s in. Reports peak, effect level and how the preset lands.
static void renderPresets(const juce::File& folder,Verdicts& v){
    const bool write=folder!=juce::File();if(write)folder.createDirectory();
    std::cout<<"Factory preset renders: 140 BPM test loop, 48 kHz stereo, HARVEST on at 1.0 s\n";
    int index=0;
    for(const auto& preset:energy::presets::factory()){
        ++index;EnergyHarvesterProcessor q;FakeHead head;head.bpm=140;head.playing=true;q.setPlayHead(&head);q.prepareToPlay(48000,512);
        q.applyPreset(preset.values,preset.folder,preset.name);
        const int mode=juce::roundToInt(preset.values.at("mode")),landing=juce::roundToInt(preset.values.at("landing"));
        const bool stops=mode==1||mode==2,tail=landing==1,hold=landing==2;
        constexpr int block=512,launchAt=48000;const int total=48000*(tail?24:12);
        std::vector<float> outL,outR,dryL,dryR;for(auto* x:{&outL,&outR,&dryL,&dryR})x->reserve(size_t(total+block));
        juce::int64 n=0;int launchSample=-1,deadline=-1,offAt=-1;juce::AudioBuffer<float> b(2,block);juce::MidiBuffer m;
        while(n<total){
            if(n>=launchAt&&launchSample<0){set(q,"harvest",1);launchSample=int(n);}
            if(offAt>=0&&n>=offAt&&q.apvts.getRawParameterValue("harvest")->load()>.5f){set(q,"harvest",0);offAt=int(n);}
            for(int i=0;i<block;++i){const auto x=energy::test::bassLoop(n+i,48000);b.setSample(0,i,x.first);b.setSample(1,i,x.second);dryL.push_back(x.first);dryR.push_back(x.second);}
            q.processBlock(b,m);for(int i=0;i<block;++i){outL.push_back(b.getSample(0,i));outR.push_back(b.getSample(1,i));}
            if(launchSample>=0&&deadline<0){deadline=launchSample+int(std::llround(q.latchedTime.load()*48.));if(stops||hold)offAt=deadline+24000;}
            n+=block;head.position=n;
        }
        double peak=0,effect=0,dry=0;for(size_t i=0;i<outL.size();++i)peak=std::max({peak,double(std::abs(outL[i])),double(std::abs(outR[i]))});
        // Voice level over the gesture's interior (50 ms in from each end, clear of the crossfades).
        const int from=launchSample+(deadline-launchSample>9600?2400:0),to=deadline-(deadline-launchSample>9600?2400:0);
        for(int i=from;i<to;++i){effect+=double(outL[size_t(i)])*outL[size_t(i)];dry+=double(dryL[size_t(i)])*dryL[size_t(i)];}
        const double effectDb=10*std::log10(effect/std::max(1e-12,dry)+1e-15);
        // First sample from which the output stays exactly the dry input until the end of the render.
        const auto exactFrom=[&](int from){for(int i=int(outL.size())-1;i>=from;--i)if(outL[size_t(i)]!=dryL[size_t(i)]||outR[size_t(i)]!=dryR[size_t(i)])return i+1;return from;};
        juce::String landingText;bool ok=std::isfinite(peak)&&peak<1.6&&deadline>launchSample;
        if(stops){double held=0,ref=0;for(int i=deadline+2400;i<offAt;++i){held+=double(outL[size_t(i)])*outL[size_t(i)];ref+=double(dryL[size_t(i)])*dryL[size_t(i)];}
            const int clean=exactFrom(offAt);ok=ok&&clean<=offAt+int(.011*48000);
            landingText="reaches standstill, "+juce::String(10*std::log10(held/ref+1e-15),1)+" dB re dry while held, exact dry "+juce::String((clean-offAt)/48.,1)+" ms after OFF";}
        else if(hold){const int clean=exactFrom(offAt);ok=ok&&clean<=offAt+int(.011*48000);landingText="holds at speed until OFF, exact dry "+juce::String((clean-offAt)/48.,1)+" ms after OFF";}
        else{const int clean=exactFrom(deadline);const double after=(clean-deadline)/48000.;ok=ok&&(tail?after<12:clean<=deadline+1);
            landingText=tail?"lands, hall rings "+juce::String(after,2)+" s, then exact dry":"lands exactly dry at the deadline";}
        v.check(ok,(juce::String(index).paddedLeft('0',2)+" "+preset.folder+" / "+preset.name+": "+juce::String(q.latchedTime.load(),0)+" ms, peak "
            +juce::String(20*std::log10(peak),1)+" dBFS, effect "+juce::String(effectDb,1)+" dB re dry, "+landingText).toStdString());
        if(write){juce::AudioBuffer<float> wav(2,int(outL.size()));for(size_t i=0;i<outL.size();++i){wav.setSample(0,int(i),outL[i]);wav.setSample(1,int(i),outR[i]);}
            auto file=folder.getChildFile(juce::String(index).paddedLeft('0',2)+" "+preset.folder+" - "+preset.name+".wav");file.deleteFile();
            if(auto stream=file.createOutputStream()){juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),48000,2,24,{},0));
                if(writer)writer->writeFromAudioSampleBuffer(wav,0,wav.getNumSamples());}}
    }
}
int main(int argc,char** argv){try{
    // Tests never touch the real preset folder.
    const auto scratchPresets=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("energy-harvester-test-presets-"+juce::String(juce::Time::getMillisecondCounter()));
    setenv("ENERGY_HARVESTER_PRESET_ROOT",scratchPresets.getFullPathName().toRawUTF8(),1);
    if(argc>2&&juce::String(argv[1])=="--spin-frames")return spinFrames(juce::File(juce::String::fromUTF8(argv[2])),argc>3?juce::String(argv[3]).getIntValue():0);
    if(argc>2&&juce::String(argv[1])=="--render-presets"){juce::ScopedJuceInitialiser_GUI gui;Verdicts v;renderPresets(juce::File(juce::String::fromUTF8(argv[2])),v);scratchPresets.deleteRecursively();return v.failed?1:0;}
    juce::ScopedJuceInitialiser_GUI gui;EnergyHarvesterProcessor p;
    {bool original=true;const char* ids[]{"harvest","spinUpMs","fxAmount","fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout","syncMode","syncDivision","outputDb"};
        for(int i=0;i<11;++i)original=original&&dynamic_cast<juce::AudioProcessorParameterWithID*>(p.getParameters()[i])->paramID==ids[i];
        check(p.getParameters().size()==23&&original,"23 controls: the original eleven keep their IDs and order, twelve appended");}
    check(p.getLatencySamples()==0&&p.getTailLengthSeconds()==8.01,"zero fixed latency and finite tail");
    set(p,"harvest",1);set(p,"spinUpMs",1234);set(p,"fxAmount",73.21f);set(p,"outputDb",6.2f);p.reducedMotion=true;p.uiScale=125;
    juce::MemoryBlock state;p.getStateInformation(state);EnergyHarvesterProcessor recalled;
    recalled.setStateInformation(state.getData(),int(state.getSize()));
    check(recalled.apvts.getRawParameterValue("harvest")->load()==1&&std::abs(recalled.apvts.getRawParameterValue("spinUpMs")->load()-1234)<.01&&recalled.reducedMotion&&recalled.uiScale==125,"project recalls sound controls and UI preferences");
    check(std::abs(recalled.apvts.getRawParameterValue("outputDb")->load()-6.2f)<.0001,"output dB survives state save and recall");
    recalled.prepareToPlay(48000,128);process(recalled,128);
    check(recalled.launchCount==0&&recalled.visualState==2,"saved ON restores clean AT SPEED without launch");
    recalled.setStateInformation("broken",6);check(recalled.apvts.getRawParameterValue("harvest")->load()==1,"malformed state ignored");
    auto legacy=juce::ValueTree("ENERGY_HARVESTER_STATE");legacy.setProperty("schemaVersion",1,nullptr);
    for(auto entry:{std::pair{"harvest",1.f},std::pair{"spinUpMs",900.f},std::pair{"fxAmount",64.f}}){juce::ValueTree child("PARAM");child.setProperty("id",entry.first,nullptr);child.setProperty("value",entry.second,nullptr);legacy.addChild(child,-1,nullptr);}
    juce::MemoryBlock old;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),old);EnergyHarvesterProcessor migrated;migrated.setStateInformation(old.getData(),int(old.getSize()));
    check(std::abs(migrated.apvts.getRawParameterValue("spinUpMs")->load()-900)<.01 && migrated.apvts.getRawParameterValue("fxPhaser")->load()==1 && migrated.apvts.getRawParameterValue("syncMode")->load()==0,"v0.1 projects migrate with original timing and phaser selected");
    auto version2=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize())));
    version2.setProperty("schemaVersion",2,nullptr);version2.setProperty("dspVersion",2,nullptr);
    // A real 0.2 project has only the first ten controls: drop outputDb and everything 0.4.0 appended.
    for(int i=version2.getNumChildren();--i>=0;)if(!juce::StringArray{"harvest","spinUpMs","fxAmount","fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout","syncMode","syncDivision"}.contains(version2.getChild(i).getProperty("id").toString()))version2.removeChild(i,nullptr);
    juce::AudioProcessor::copyXmlToBinary(*version2.createXml(),old);EnergyHarvesterProcessor migrated2;migrated2.setStateInformation(old.getData(),int(old.getSize()));
    check(migrated2.apvts.getRawParameterValue("outputDb")->load()==0 && migrated2.apvts.getRawParameterValue("fxAmount")->load()>73,"v0.2 states migrate at unity output without changing effects");
    auto invalid=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize())));
    for(auto child:invalid)if(child.getProperty("id").toString()=="outputDb")child.setProperty("value",19,nullptr);
    juce::AudioProcessor::copyXmlToBinary(*invalid.createXml(),old);recalled.setStateInformation(old.getData(),int(old.getSize()));check(std::abs(recalled.apvts.getRawParameterValue("outputDb")->load()-6.2f)<.0001,"out-of-range output state is ignored transactionally");
    set(migrated,"fxPan",1);set(migrated,"fxChorus",1);set(migrated,"fxFlanger",1);set(migrated,"fxWashout",1);set(migrated,"syncMode",1);set(migrated,"syncDivision",5);migrated.getStateInformation(old);recalled.setStateInformation(old.getData(),int(old.getSize()));
    check(recalled.apvts.getRawParameterValue("fxWashout")->load()==1 && recalled.apvts.getRawParameterValue("syncDivision")->load()==5,"effect selection and sync division survive save and restore");
    set(recalled,"harvest",0);recalled.prepareToPlay(48000,128);process(recalled,128);set(recalled,"harvest",1);process(recalled,128);
    check(recalled.latchedTime==8000 && !recalled.hasHostTempo,"missing host tempo uses 120 BPM / 4-4 fallback");
    auto mono=p.getBusesLayout();mono.inputBuses.set(0,juce::AudioChannelSet::mono());mono.outputBuses.set(0,juce::AudioChannelSet::mono());check(p.isBusesLayoutSupported(mono),"mono supported");
    mono.outputBuses.set(0,juce::AudioChannelSet::stereo());check(!p.isBusesLayoutSupported(mono),"mismatched buses rejected");
    {Verdicts v;
        // E2: recalling a project or preset while audio runs must never fabricate a launch.
        {EnergyHarvesterProcessor r;r.prepareToPlay(48000,64);juce::MemoryBlock onState,offState;
            set(r,"harvest",1);r.getStateInformation(onState);set(r,"harvest",0);r.getStateInformation(offState);process(r,64);
            std::atomic<bool> stop{false};std::atomic<int> blocks{0};
            std::thread audio([&]{juce::AudioBuffer<float> b(2,64);juce::MidiBuffer m;while(!stop.load()){b.clear();r.processBlock(b,m);++blocks;}});
            for(int i=0;i<4000;++i){const auto& s=(i%2==0)?onState:offState;r.setStateInformation(s.getData(),int(s.getSize()));}
            stop=true;audio.join();
            v.check(r.launchCount==0,"E2 4000 alternating ON/OFF recalls during "+std::to_string(blocks.load())+" concurrent blocks fabricate no launch (launches "+std::to_string(r.launchCount.load())+")");}
        // E5: starting the transport during a GUI-launched spin fades the voice instead of clicking.
        {EnergyHarvesterProcessor t;FakeHead head;t.setPlayHead(&head);t.prepareToPlay(48000,256);SineRun run;
            for(int i=0;i<10;++i)run.render(t,&head,256);set(t,"harvest",1);double inSpin=0;
            for(int i=0;i<60;++i){const double step=run.render(t,&head,256);if(i>0)inSpin=std::max(inSpin,step);}
            const bool spinning=t.visualState==1;head.playing=true;const double edge=std::max(run.render(t,&head,256),run.render(t,&head,256));
            v.check(spinning&&t.visualState==2&&edge<=.02,"E5 transport start mid-spin declicks (max step "+std::to_string(edge)+" vs in-spin "+std::to_string(inSpin)+"; state "+std::to_string(t.visualState.load())+")");}
        // E15: the tail report stays bounded at extreme sync settings.
        {EnergyHarvesterProcessor s;FakeHead head;head.bpm=10;head.numerator=7;head.denominator=4;s.setPlayHead(&head);s.prepareToPlay(48000,128);
            set(s,"syncMode",1);set(s,"syncDivision",5);process(s,128);const double tail=s.getTailLengthSeconds();
            v.check(tail>=8.01&&tail<=60.0,"E15 tail report capped for a 168 s four-bar 7/4 spin at 10 BPM (reported "+std::to_string(tail)+" s)");}
        // Animation feeds: the output peak is held until the UI consumes it, and the sample counter is published.
        {EnergyHarvesterProcessor f;f.prepareToPlay(48000,256);SineRun run;run.render(f,nullptr,256);run.render(f,nullptr,256);
            const auto samples=f.publishedSamples.load();const float level=f.visualLevel.exchange(0.f);const float again=f.visualLevel.load();
            v.check(samples==512&&std::abs(level-.5f)<.01f&&again==0.f,"visualLevel holds the 0.5 output peak until exchange(0); publishedSamples counts processed samples (level "+std::to_string(level)+", samples "+std::to_string(samples)+")");}
        if(v.failed)throw std::runtime_error(std::to_string(v.failed)+" processor regression check(s) failed");
    }
    {Verdicts v;
        const auto tree=[](const juce::MemoryBlock& block){return juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(block.getData(),int(block.getSize())));};
        const auto load=[](EnergyHarvesterProcessor& into,const juce::ValueTree& t){juce::MemoryBlock b;juce::AudioProcessor::copyXmlToBinary(*t.createXml(),b);into.setStateInformation(b.getData(),int(b.getSize()));};
        const auto rawOf=[](EnergyHarvesterProcessor& q,const char* id){return q.apvts.getRawParameterValue(id)->load();};
        // Projects from 0.1-0.3 keep every setting and the exact 0.3.2 sound; new instances start with the latest sound.
        {EnergyHarvesterProcessor fresh;v.check(rawOf(fresh,"latestSound")==1&&rawOf(fresh,"mode")==0&&rawOf(fresh,"subKeep")==0&&rawOf(fresh,"mix")==100,"new instances use the latest sound with Tape Start, Sub Keep off and 100 % mix");}
        auto schema3=tree(state);schema3.setProperty("schemaVersion",3,nullptr);schema3.setProperty("dspVersion",3,nullptr);schema3.removeProperty("presetName",nullptr);schema3.removeProperty("presetFolder",nullptr);
        for(int i=schema3.getNumChildren();--i>=0;)if(juce::StringArray{"mode","engine","curve","depth","filterMode","resonance","filterStart","subKeep","gridLand","landing","mix","latestSound"}.contains(schema3.getChild(i).getProperty("id").toString()))schema3.removeChild(i,nullptr);
        EnergyHarvesterProcessor old;load(old,schema3);
        v.check(rawOf(old,"harvest")==1&&std::abs(rawOf(old,"spinUpMs")-1234)<.01&&std::abs(rawOf(old,"fxAmount")-73.21f)<.001&&std::abs(rawOf(old,"outputDb")-6.2f)<.0001&&rawOf(old,"latestSound")==0
            &&rawOf(old,"mode")==0&&rawOf(old,"depth")==24&&rawOf(old,"mix")==100&&rawOf(old,"resonance")==0&&std::abs(rawOf(old,"filterStart")-350)<.05&&old.uiScale==125&&old.reducedMotion,
            "schema-3 (0.3.x) project keeps every setting and loads with Latest Sound off");
        v.check(rawOf(migrated,"latestSound")==0&&rawOf(migrated2,"latestSound")==0,"schema-1 and schema-2 projects also keep the 0.3.2 sound");
        // The deliberate upgrade is an ordinary undoable parameter gesture, saved with the project.
        {auto* latest=old.apvts.getParameter("latestSound");latest->beginChangeGesture();latest->setValueNotifyingHost(1);latest->endChangeGesture();
            juce::MemoryBlock upgraded;old.getStateInformation(upgraded);EnergyHarvesterProcessor again;again.setStateInformation(upgraded.getData(),int(upgraded.getSize()));
            const bool saved=rawOf(again,"latestSound")==1&&int(tree(upgraded).getProperty("schemaVersion"))==4;
            latest->beginChangeGesture();latest->setValueNotifyingHost(0);latest->endChangeGesture();
            v.check(saved&&rawOf(old,"latestSound")==0&&!latest->isAutomatable(),"Use Latest Sound upgrade saves as schema 4, can be reversed, and is not automatable");}
        // Size: 150 % is new; old sizes keep loading; unknown sizes fall back to 100 %.
        {auto sized=tree(state);sized.setProperty("uiScale",150,nullptr);EnergyHarvesterProcessor big;load(big,sized);sized.setProperty("uiScale",175,nullptr);EnergyHarvesterProcessor odd;load(odd,sized);
            v.check(big.uiScale==150&&odd.uiScale==100,"size 150 % is saved and restored; unknown sizes fall back to 100 %");}
        // Land On Grid: the spin ends exactly on the next beat, bar or two-bar line; no transport means TIME.
        for(auto [grid,ppq,expectedNext]:std::vector<std::tuple<int,double,double>>{{1,2.3,3},{2,2.5,4},{3,2.5,8},{2,5.0,8}}){
            EnergyHarvesterProcessor q;FakeHead head;head.playing=true;q.setPlayHead(&head);q.prepareToPlay(48000,512);set(q,"gridLand",float(grid));
            head.position=juce::int64(std::llround(ppq*24000));process(q,512);head.position+=512;
            set(q,"harvest",1);const double launchPpq=double(head.position)/24000;process(q,512);
            const double expected=(expectedNext-launchPpq)*500;
            v.check(std::abs(q.latchedTime.load()-expected)<.05,"Land On Grid "+juce::String(grid==1?"1/4":grid==2?"bar":"2 bars").toStdString()+" from beat "+std::to_string(launchPpq)+" lasts "+std::to_string(q.latchedTime.load())+" ms (expected "+std::to_string(expected)+")");}
        {EnergyHarvesterProcessor q;FakeHead head;q.setPlayHead(&head);q.prepareToPlay(48000,512);set(q,"gridLand",2);process(q,512);set(q,"harvest",1);process(q,512);
            v.check(std::abs(q.latchedTime.load()-800)<.05,"Land On Grid falls back to TIME when the transport is stopped");}
        // The light show's ratio follows the engine: down to 0 for a stop, negative while rewinding.
        {EnergyHarvesterProcessor q;q.prepareToPlay(48000,512);set(q,"mode",1);set(q,"spinUpMs",1000);process(q,512);set(q,"harvest",1);SineRun run;
            for(int i=0;i<47;++i)run.render(q,nullptr,512);const float mid=q.visualRatio;const int midState=q.visualState;for(int i=0;i<60;++i)run.render(q,nullptr,512);
            v.check(midState==1&&mid>.05f&&mid<.95f&&q.visualState==4&&q.visualRatio==0.f,"Tape Stop publishes a falling ratio ("+std::to_string(mid)+" half-way) and rests at 0 in the STOPPED state");}
        {EnergyHarvesterProcessor q;q.prepareToPlay(48000,512);set(q,"mode",4);set(q,"spinUpMs",1000);process(q,512);set(q,"harvest",1);SineRun run;
            float lowest=1;for(int i=0;i<100;++i){run.render(q,nullptr,512);lowest=std::min(lowest,q.visualRatio.load());}
            v.check(lowest<-2.5f&&q.visualState==2,"Rewind publishes a negative ratio (lowest "+std::to_string(lowest)+") and lands at speed");}
        // Presets: a large organised factory set, applied as complete undoable changes that never touch HARVEST.
        {const auto& list=energy::presets::factory();const auto folders=energy::presets::factoryFolders();
            v.check(list.size()>=50&&folders.size()==10,std::to_string(list.size())+" factory presets in "+std::to_string(folders.size())+" folders: "+folders.joinIntoString(", ").toStdString());
            EnergyHarvesterProcessor q;set(q,"harvest",1);bool exact=true,harvestKept=true;
            for(const auto& preset:list){q.applyPreset(preset.values,preset.folder,preset.name);harvestKept=harvestKept&&rawOf(q,"harvest")==1;
                for(const auto& [id,value]:preset.values){auto* parameter=q.apvts.getParameter(id);exact=exact&&parameter!=nullptr&&std::abs(rawOf(q,id.toRawUTF8())-parameter->getNormalisableRange().snapToLegalValue(value))<.02f;}}
            v.check(exact&&harvestKept&&q.presetLabel().second==list.back().name,"every factory preset applies exactly and leaves HARVEST alone");}
        {const auto root=energy::presets::userRoot();v.check(root.getFullPathName().startsWith(juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()),"tests use a scratch preset folder: "+root.getFullPathName().toStdString());
            const auto mine=root.getChildFile("My Folder");mine.createDirectory();const auto sentinel=mine.getChildFile("Keep.ehpreset");sentinel.replaceWithText("user file - must never change");
            EnergyHarvesterProcessor q;set(q,"mode",4);set(q,"depth",30);juce::File first,second;
            const bool saved=energy::presets::save(root,"My Folder","Keep",q.currentValues(),first).wasOk()&&energy::presets::save(root,"My Folder","Keep",q.currentValues(),second).wasOk();
            const auto users=energy::presets::userPresets(root);const auto readBack=energy::presets::read(first);
            v.check(saved&&first!=second&&first!=sentinel&&second!=sentinel&&sentinel.loadFileAsString()=="user file - must never change"&&readBack&&readBack->values.at("mode")==4&&readBack->values.at("depth")==30
                &&users.size()==2&&energy::presets::userFolders(root).contains("My Folder"),"user presets save into folders without overwriting existing files ("+first.getFileName().toStdString()+", "+second.getFileName().toStdString()+")");
            const char* home=std::getenv("HOME");const juce::String savedHome=home?home:"",savedRoot=root.getFullPathName();unsetenv("ENERGY_HARVESTER_PRESET_ROOT");setenv("HOME","/tmp/energy-home",1);
            const auto homed=energy::presets::userRoot();setenv("HOME",savedHome.toRawUTF8(),1);setenv("ENERGY_HARVESTER_PRESET_ROOT",savedRoot.toRawUTF8(),1);
            v.check(homed.getFullPathName()=="/tmp/energy-home/Library/Application Support/ENERGY HARVESTER/Presets","user preset root follows $HOME: "+homed.getFullPathName().toStdString());}
        renderPresets({},v);
        if(v.failed)throw std::runtime_error(std::to_string(v.failed)+" 0.4.0 check(s) failed");
    }
    set(p,"harvest",0);set(p,"spinUpMs",800);set(p,"fxAmount",45);set(p,"outputDb",0);p.uiScale=100;p.reducedMotion=false;p.prepareToPlay(48000,128);process(p,128);
    if(argc>1){juce::File folder(juce::String::fromUTF8(argv[1]));folder.createDirectory();
        EnergyHarvesterEditor editor(p);check(editor.getWidth()==518&&editor.getHeight()==691,"native portrait editor follows selected layout");
        auto* button=dynamic_cast<juce::Button*>(named(editor,"HARVEST"));auto* time=dynamic_cast<juce::Slider*>(named(editor,"SPIN-UP TIME"));auto* fx=dynamic_cast<juce::Slider*>(named(editor,"FX AMOUNT"));
        check(button&&time&&fx,"one harvest button, five effect buttons and TIME/SYNC sliders");time->setValue(850,juce::sendNotificationSync);
        check(std::abs(p.apvts.getRawParameterValue("spinUpMs")->load()-850)<.01,"native time slider updates automation parameter");set(p,"spinUpMs",800);
        auto* mode=dynamic_cast<juce::Button*>(named(editor,"Time / Sync"));auto* division=dynamic_cast<juce::Slider*>(named(editor,"SPIN-UP DIVISION"));check(mode&&division,"native TIME/SYNC control and division slider exist");
        for(const char* name:{"PANNING","CHORUS","PHASER","FLANGER","WASHOUT"})check(dynamic_cast<juce::Button*>(named(editor,name)),"native effect screen button exists");
        mode->setToggleState(true,juce::sendNotificationSync);division->setValue(3,juce::sendNotificationSync);process(p,128);editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-sync.png"));
        check(p.desiredTimeMs==2000 && p.apvts.getRawParameterValue("syncMode")->load()==1 && division->isVisible() && !time->isVisible(),"SYNC switches visible slider and selects one bar at fallback tempo");mode->setToggleState(false,juce::sendNotificationSync);process(p,128);
        check(time->isVisible() && p.desiredTimeMs==800,"TIME restores remembered milliseconds");
        auto* output=dynamic_cast<juce::Slider*>(named(editor,"OUTPUT"));check(output&&output->getValue()==0,"native output dial exists at unity");
        output->showTextBox();juce::TextEditor* text=nullptr;for(auto* child:output->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))text=label->getCurrentTextEditor();
        check(text!=nullptr,"output readout opens native text entry");text->setText("+6.2 dB",false);output->hideTextBox(false);
        check(std::abs(p.apvts.getRawParameterValue("outputDb")->load()-6.2f)<.0001,"typed dB readout updates output automation parameter");
        editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-output-boost.png"));output->setValue(0,juce::sendNotificationSync);
        editor.previewTime(0);auto off=snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-ready.png"));
        check(off.getPixelAt(0,0).getAlpha()==255&&off.getPixelAt(off.getWidth()-1,off.getHeight()-1).getAlpha()==255,"shell fills the window edge to edge");
        button->setToggleState(true,juce::sendNotificationSync);process(p,19200);editor.previewTime(.6);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-spinning.png"));
        check(p.launchCount==1&&p.visualState==1,"button click launches sound engine once");process(p,22000);editor.previewTime(2.6);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-at-speed.png"));
        check(p.visualGate&&p.visualState==2&&p.launchCount==1,"held ON lands clean and keeps light state active");
        editor.previewTime(1.5);auto first=editor.createComponentSnapshot(editor.getLocalBounds(),true);editor.previewTime(1.5);auto last=editor.createComponentSnapshot(editor.getLocalBounds(),true);
        bool equal=true;for(int y=0;y<first.getHeight();++y)for(int x=0;x<first.getWidth();++x)equal=equal&&first.getPixelAt(x,y)==last.getPixelAt(x,y);check(equal,"light show renders deterministically for a given state and time");
        auto frames=folder.getChildFile("animation-frames");frames.createDirectory();std::vector<double> costs;
        for(int i=0;i<72;++i){editor.previewTime(i/12.);auto start=std::chrono::steady_clock::now();auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true);costs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());juce::FileOutputStream s(frames.getChildFile(juce::String::formatted("frame-%03d.png",i)));juce::PNGImageFormat().writeImageToStream(image,s);}
        std::sort(costs.begin(),costs.end());std::cout<<"Native full snapshot p95 "<<costs[68]<<" ms (includes shell/controls; animation budget separately unmeasured)\n";
        std::vector<double> lightCosts;for(int i=0;i<360;++i){editor.previewTime(i/30.);lightCosts.push_back(editor.measureAnimationPaint());}std::sort(lightCosts.begin(),lightCosts.end());
        std::cout<<"Native animation paint p95 "<<lightCosts[342]<<" ms at 560 x 720; allocation of probe excluded, paths/masks/glows included\n";
        for(const char* name:{"PANNING","CHORUS","PHASER","FLANGER","WASHOUT"})dynamic_cast<juce::Button*>(named(editor,name))->setToggleState(true,juce::sendNotificationSync);editor.previewTime(2.6);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-all-fx.png"));
        mode->setToggleState(true,juce::sendNotificationSync);division->setValue(5,juce::sendNotificationSync);process(p,128);editor.previewTime(2.6);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-all-fx-sync.png"));mode->setToggleState(false,juce::sendNotificationSync);
        p.reducedMotion=true;editor.previewTime(1.3);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-reduced-motion.png"));p.reducedMotion=false;
        auto* info=dynamic_cast<juce::Button*>(named(editor,"INFO"));auto* overlay=named(editor,"VFD Info Page");check(info&&overlay,"shared VFD credits present");info->onClick();check(overlay->isVisible(),"INFO opens overlay");snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-info.png"));check(overlay->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&!overlay->isVisible(),"Escape closes credits");
        // 0.4.0 controls: MODE page, setup screen, sound version, the new modes in motion, presets and 150 %.
        editor.showPageForTesting(1);auto* stopKey=dynamic_cast<juce::Button*>(named(editor,"MODE STOP"));
        check(stopKey&&stopKey->isVisible()&&!named(editor,"PANNING")->isVisible(),"MODE page shows the five modes in the display");
        stopKey->onClick();check(p.apvts.getRawParameterValue("mode")->load()==1,"MODE key selects Tape Stop");
        editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-mode-page.png"));
        editor.showPageForTesting(2);check(named(editor,"Setup Screen")->isVisible()&&named(editor,"SPEED CURVE")&&named(editor,"START DEPTH")&&named(editor,"RESONANCE")&&named(editor,"FILTER START")&&named(editor,"MIX")
            &&named(editor,"ENGINE TRUE TAPE")&&named(editor,"LANDING HOLD")&&named(editor,"SUBKEEP 90")&&named(editor,"GRIDLAND BAR")&&named(editor,"FILTERMODE LP 24")&&named(editor,"LATEST SOUND"),"setup screen carries every 0.4.0 control");
        dynamic_cast<juce::Button*>(named(editor,"ENGINE TRUE TAPE"))->onClick();dynamic_cast<juce::Button*>(named(editor,"SUBKEEP 90"))->onClick();
        check(p.apvts.getRawParameterValue("engine")->load()==1&&p.apvts.getRawParameterValue("subKeep")->load()==2,"setup keys drive Engine and Sub Keep");
        editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-setup.png"));
        set(p,"latestSound",0);editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-setup-legacy.png"));
        dynamic_cast<juce::Button*>(named(editor,"LATEST SOUND"))->onClick();
        check(p.apvts.getRawParameterValue("latestSound")->load()==1,"USE LATEST SOUND key upgrades the project");
        set(p,"latestSound",0);editor.showPageForTesting(0);editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-legacy.png"));set(p,"latestSound",1);
        set(p,"engine",0);set(p,"subKeep",0);
        for(auto [modeIndex,file]:std::vector<std::pair<int,const char*>>{{1,"ENERGY-HARVESTER-stop-spinning.png"},{4,"ENERGY-HARVESTER-rewinding.png"},{2,"ENERGY-HARVESTER-braking.png"}}){
            set(p,"harvest",0);process(p,4800);set(p,"mode",float(modeIndex));set(p,"spinUpMs",1000);set(p,"harvest",1);process(p,24000);editor.showPageForTesting(1);editor.previewTime(.4);snapshot(editor,folder.getChildFile(file));}
        set(p,"mode",1);set(p,"harvest",0);process(p,4800);set(p,"harvest",1);process(p,60000);check(p.visualState==4,"Tape Stop rests in the STOPPED state while HARVEST is on");
        editor.previewTime(1.5);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-stopped.png"));set(p,"harvest",0);process(p,4800);
        for(const auto& preset:energy::presets::factory())if(preset.name=="Harvest Riser"){p.applyPreset(preset.values,preset.folder,preset.name);}
        editor.showPageForTesting(0);editor.previewTime(0);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-preset.png"));
        check(dynamic_cast<juce::Button*>(named(editor,"PRESET"))!=nullptr,"preset strip on the title plate");
        editor.setSize(777,1036);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-150.png"));
        editor.setSize(466,621);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-compact.png"));editor.setSize(647,863);snapshot(editor,folder.getChildFile("ENERGY-HARVESTER-large.png"));
        auto demos=folder.getParentDirectory().getChildFile("Demos");demos.createDirectory();demo(demos.getChildFile("01-bass-dry-basic-magnetic.wav"),0);demo(demos.getChildFile("02-chord-dry-basic-magnetic.wav"),1);demo(demos.getChildFile("03-pluck-dry-basic-magnetic.wav"),2);
        const char* labels[]{"panning","chorus","phaser","flanger","washout"};for(int i=0;i<5;++i)demo(demos.getChildFile(juce::String(i+4)+"-"+labels[i]+"-comparison.wav"),1,1u<<i);demo(demos.getChildFile("09-all-five-comparison.wav"),1,31);demo(demos.getChildFile("10-all-five-output-plus-6-dB.wav"),1,31,6);
    }
    std::cout<<"ENERGY HARVESTER host checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
