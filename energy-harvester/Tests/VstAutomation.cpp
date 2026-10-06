// macOS regression host for the actual packaged VST3. The matching JUCE wrapper
// supplies the concrete controller type solely to reach its public
// getPluginInstance() test hook; all tested objects come from the loaded bundle.
// Build as Objective-C++ with the same plugin definitions and JUCE version.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp>
#undef Component
#include "EnergyHarvesterProcessor.h"
#include "Timing.h"
#include <iostream>
#include <vector>
#include <set>
#include <cmath>
#include <thread>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

template <typename Interface>
Interface* query(FUnknown* from)
{
    Interface* result = nullptr;
    require(from != nullptr && from->queryInterface(Interface::iid, reinterpret_cast<void**>(&result)) == kResultOk
            && result != nullptr, "VST3 interface available");
    return result;
}

class Host final : public IHostApplication, public IComponentHandler, public IComponentHandler2 {
public:
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override
    {
        *object = nullptr;
        if (juce::doUIDsMatch(FUnknown::iid, id) || juce::doUIDsMatch(IHostApplication::iid, id)) *object = static_cast<IHostApplication*>(this);
        else if (juce::doUIDsMatch(IComponentHandler::iid, id)) *object = static_cast<IComponentHandler*>(this);
        else if (juce::doUIDsMatch(IComponentHandler2::iid, id)) *object = static_cast<IComponentHandler2*>(this);
        if (*object == nullptr) return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return --references; }
    tresult PLUGIN_API getName(String128 name) override
    {
        juce::String("ENERGY HARVESTER regression host").copyToUTF16(reinterpret_cast<juce::CharPointer_UTF16::CharType*>(name), sizeof(String128));
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void** object) override { *object = nullptr; return kNotImplemented; }
    tresult PLUGIN_API beginEdit(ParamID id) override { begin.push_back(id); return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue) override { edits.push_back(id); return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID id) override { end.push_back(id); return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { restartFlags |= flags; return kResultOk; }
    tresult PLUGIN_API setDirty(TBool state) override { if (state) ++dirty; return kResultOk; }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kNotImplemented; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }
    void clear() { begin.clear(); edits.clear(); end.clear(); restartFlags = 0; dirty = 0; }
    uint32 references = 1;
    std::vector<ParamID> begin, edits, end;
    int32 restartFlags = 0;
    int dirty = 0;
};

struct Bundle {
    explicit Bundle(const juce::File& file)
    {
        auto path = file.getFullPathName().toRawUTF8();
        auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path),
                                                          static_cast<CFIndex>(std::strlen(path)), true);
        bundle = CFBundleCreate(nullptr, url); CFRelease(url);
        require(bundle != nullptr && CFBundleLoadExecutable(bundle), "packaged VST3 bundle loads");
        const auto entry = reinterpret_cast<bool (*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
        exit = reinterpret_cast<bool (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
        const auto getFactory = reinterpret_cast<IPluginFactory* (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
        require(entry != nullptr && exit != nullptr && getFactory != nullptr && entry(bundle), "VST3 bundle entry succeeds");
        factory = getFactory(); require(factory != nullptr, "VST3 factory available");
    }
    ~Bundle() { if (factory) factory->release(); if (exit) exit(); if (bundle) CFRelease(bundle); }
    CFBundleRef bundle = nullptr;
    IPluginFactory* factory = nullptr;
    bool (*exit)() = nullptr;
};

class Queue final:public IParamValueQueue {
public:
    ParamID id;std::vector<std::pair<int32,ParamValue>> points;
    Queue(ParamID parameter,std::vector<std::pair<int32,ParamValue>> data):id(parameter),points(std::move(data)){}
    tresult PLUGIN_API queryInterface(const TUID uid,void** result) override {*result=nullptr;if(juce::doUIDsMatch(uid,FUnknown::iid)||juce::doUIDsMatch(uid,IParamValueQueue::iid)){*result=this;return kResultOk;}return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}uint32 PLUGIN_API release() override{return 1;}
    ParamID PLUGIN_API getParameterId() override{return id;}int32 PLUGIN_API getPointCount() override{return int32(points.size());}
    tresult PLUGIN_API getPoint(int32 i,int32& at,ParamValue& value) override{if(i<0||size_t(i)>=points.size())return kInvalidArgument;at=points[size_t(i)].first;value=points[size_t(i)].second;return kResultOk;}
    tresult PLUGIN_API addPoint(int32,ParamValue,int32&) override{return kNotImplemented;}
};
class Changes final:public IParameterChanges {
public:
    std::vector<std::unique_ptr<Queue>> queues;
    void add(ParamID id,std::vector<std::pair<int32,ParamValue>> points){queues.push_back(std::make_unique<Queue>(id,std::move(points)));}
    tresult PLUGIN_API queryInterface(const TUID uid,void** result) override{*result=nullptr;if(juce::doUIDsMatch(uid,FUnknown::iid)||juce::doUIDsMatch(uid,IParameterChanges::iid)){*result=this;return kResultOk;}return kNoInterface;}
    uint32 PLUGIN_API addRef() override{return 1;}uint32 PLUGIN_API release() override{return 1;}
    int32 PLUGIN_API getParameterCount() override{return int32(queues.size());}
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override{return i>=0&&size_t(i)<queues.size()?queues[size_t(i)].get():nullptr;}
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&,int32&) override{return nullptr;}
};
thread_local bool quiet=false;
void pass(bool value,const char* message){require(value,message);if(!quiet)std::cout<<"PASS "<<message<<'\n';}
void test(Bundle& bundle,Host& host){
    IComponent* component=nullptr;int found=0;
    for(int32 i=0;i<bundle.factory->countClasses();++i){PClassInfo info{};bundle.factory->getClassInfo(i,&info);if(std::strcmp(info.category,kVstAudioEffectClass)==0){++found;require(bundle.factory->createInstance(info.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"component creation");}}
    pass(found==1&&component,"one audio component");TUID controllerId{};
    require(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk&&component->getControllerClassId(controllerId)==kResultOk,"component initialization");
    IEditController* controller=nullptr;require(bundle.factory->createInstance(controllerId,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"controller creation");
    controller->initialize(static_cast<IHostApplication*>(&host));controller->setComponentHandler(&host);
    auto* a=query<IConnectionPoint>(component);auto* b=query<IConnectionPoint>(controller);a->connect(b);b->connect(a);
    auto* concrete=query<juce::JuceVST3EditController>(controller);auto* actual=static_cast<EnergyHarvesterProcessor*>(concrete->getPluginInstance());
    pass(actual&&actual->getName()=="ENERGY HARVESTER","test inspects processor from shipping bundle");auto* processor=query<IAudioProcessor>(component);
    struct Cleanup{IComponent* c;IEditController* e;IConnectionPoint* a;IConnectionPoint* b;juce::JuceVST3EditController* concrete;IAudioProcessor* p;
        ~Cleanup(){p->setProcessing(false);c->setActive(false);a->disconnect(b);b->disconnect(a);a->release();b->release();e->setComponentHandler(nullptr);e->terminate();c->terminate();concrete->release();p->release();e->release();c->release();}} cleanup{component,controller,a,b,concrete,processor};
    const ParamID gate=juce::String("harvest").hashCode()&0x7fffffff,time=juce::String("spinUpMs").hashCode()&0x7fffffff,fx=juce::String("fxAmount").hashCode()&0x7fffffff;
    const ParamID outputDb=juce::String("outputDb").hashCode()&0x7fffffff;
    ParamID bypass=0;std::set<ParamID> ids;
    for(int i=0;i<controller->getParameterCount();++i){ParameterInfo info{};controller->getParameterInfo(i,info);ids.insert(info.id);if(info.flags&ParameterInfo::kIsBypass)bypass=info.id;}
    bool originals=true;for(const char* id:{"harvest","spinUpMs","fxAmount","fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout","syncMode","syncDivision","outputDb"})originals=originals&&ids.count(ParamID(juce::String(id).hashCode()&0x7fffffff));
    // 0.4.0 appends twelve controls; the original eleven keep their IDs.
    pass(ids.size()==24&&originals&&ids.count(bypass),"23 named controls (original eleven IDs unchanged, twelve appended) plus VST3 host bypass");
    constexpr int block=1024;ProcessSetup setup{};setup.maxSamplesPerBlock=block;setup.sampleRate=48000;setup.symbolicSampleSize=kSample32;setup.processMode=kRealtime;
    require(component->activateBus(kAudio,kInput,0,true)==kResultOk&&component->activateBus(kAudio,kOutput,0,true)==kResultOk,"main buses activation");
    require(processor->setupProcessing(setup)==kResultOk&&component->setActive(true)==kResultOk&&processor->setProcessing(true)==kResultOk,"processor activation");
    juce::AudioBuffer<float> audio(2,block);std::vector<energy::Frame> expected(block);Steinberg::int64 position=0;
    energy::HarvestEngine reference;reference.setTime(800);reference.setFx(45);reference.prepare(48000,2);
    bool referenceGate=false,referenceBypass=false;bool compare=true;double maximum=0;bool tempoValid=false;double tempo=120;int numerator=4,denominator=4;
    auto render=[&](Changes* changes,bool playing=true,bool reset=false,int count=128){
        if(reset)reference.reset(referenceGate);
        std::array<size_t,12> cursor{};
        for(int s=0;s<count;++s){float input=float(.3*std::sin(2*juce::MathConstants<double>::pi*220*(position+s)/48000.));audio.setSample(0,s,input);audio.setSample(1,s,input);
            if(changes)for(ParamID id:{time,fx,outputDb,bypass,gate})for(size_t q=0;q<changes->queues.size();++q){auto& queue=*changes->queues[q];if(queue.id!=id)continue;
                // Test queues cover all eleven controls plus bypass; keep each queue's cursor independently.
                auto& index=cursor[q];while(index<queue.points.size()&&queue.points[index].first<=s){const auto value=queue.points[index++].second;
                    if(id==time)reference.setTime(float(80*std::pow(100.,value)));
                    if(id==fx)reference.setFx(float(value*100));
                    if(id==outputDb)reference.setOutputDb(float(-18+36*value));
                    if(id==bypass){referenceBypass=value>=.5;reference.setBypass(referenceBypass);}
                    if(id==gate){referenceGate=value>=.5;reference.setGate(referenceGate);}
                }
            }
            expected[size_t(s)]=reference.process({input,input},referenceBypass);
        }
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;float* channels[]{audio.getWritePointer(0),audio.getWritePointer(1)};in.channelBuffers32=out.channelBuffers32=channels;
        ProcessContext context{};context.sampleRate=48000;context.state=playing?ProcessContext::kPlaying:0;context.projectTimeSamples=position;if(tempoValid){context.state|=ProcessContext::kTempoValid|ProcessContext::kTimeSigValid;context.tempo=tempo;context.timeSigNumerator=numerator;context.timeSigDenominator=denominator;}
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=count;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.inputParameterChanges=changes;data.processContext=&context;
        require(processor->process(data)==kResultOk,"raw VST3 processing");
        for(int s=0;s<count;++s){require(std::isfinite(audio.getSample(0,s)),"finite actual VST3 output");if(compare)maximum=std::max(maximum,double(std::abs(audio.getSample(0,s)-expected[size_t(s)].left)));}
        position+=count;
    };
    render(nullptr);for(int i=0;i<30;++i)render(nullptr);
    Changes shortPulse;shortPulse.add(gate,{{17,1},{24,0},{97,1}});render(&shortPulse);
    pass(actual->launchCount==2&&actual->visualGate,"17/24/97 intra-block gate points all survive wrapper");
    pass(maximum<2e-6,"actual VST3 sample offsets match independent DSP reference");
    Changes repeatedBypass;repeatedBypass.add(bypass,{{0,0},{31,0},{127,0}});render(&repeatedBypass);
    pass(actual->visualState==1&&maximum<2e-6,"repeated bypass OFF points preserve running spin-up");
    Changes release;release.add(gate,{{0,0}});render(&release);for(int i=0;i<5;++i)render(nullptr);
    Changes sameSample;sameSample.add(gate,{{31,1}});sameSample.add(time,{{31,0}});sameSample.add(fx,{{31,1}});render(&sameSample);
    pass(std::abs(actual->latchedTime.load()-80)<.001,"same-sample duration applied before gate regardless of host queue order");
    Changes future;future.add(time,{{5,1}});render(&future);pass(std::abs(actual->latchedTime.load()-80)<.001,"active spin-up duration remains latched");
    for(int i=0;i<40;++i)render(nullptr);pass(actual->visualState==2&&actual->launchCount==3,"held ON reaches clean speed without retrigger");
    Changes off;off.add(gate,{{0,0}});render(&off);for(int i=0;i<5;++i)render(nullptr);
    auto before=actual->launchCount.load();Changes dense;std::vector<std::pair<int32,ParamValue>> points;
    for(int i=0;i<5001;++i)points.push_back({64,i%2==0?1.:0.});dense.add(gate,std::move(points));render(&dense);
    pass(actual->launchCount==before+2501,"5001 same-offset events stream without fixed-buffer overflow");
    pass(maximum<3e-6,"dense queue output still matches reference");
    Changes bypassed;bypassed.add(bypass,{{9,1}});bypassed.add(gate,{{10,0},{11,1}});before=actual->launchCount;render(&bypassed);
    pass(actual->visualBypass&&actual->launchCount==before,"host bypass cancels effect and suppresses launches");
    Changes resume;resume.add(bypass,{{4,0}});render(&resume);pass(!actual->visualBypass&&actual->visualState==2&&actual->launchCount==before,"unbypass while ON returns clean without synthetic start");
    Changes reenter;reenter.add(bypass,{{3,1}});render(&reenter);before=actual->launchCount;
    Changes explicitExit;explicitExit.add(gate,{{8,0},{8,1}});explicitExit.add(bypass,{{8,0}});render(&explicitExit);
    pass(!actual->visualBypass&&actual->visualState==1&&actual->launchCount==before+1&&maximum<3e-6,"explicit OFF then ON at bypass-exit sample launches correctly");
    Changes trim;trim.add(outputDb,{{11,1},{64,0},{92,.5}});render(&trim);
    pass(maximum<3e-6,"actual VST3 output dB automation follows sample offsets and smoothing");
    compare=false;render(nullptr,false);position+=48000;before=actual->launchCount;render(nullptr,true);
    pass(actual->visualState==2&&actual->launchCount==before,"transport start and seek while ON prime clean state");
    render(nullptr,false);Changes baseline;baseline.add(gate,{{0,1},{0,0},{0,1}});before=actual->launchCount;render(&baseline,true);
    pass(actual->launchCount==before+1,"transport baseline consumes first point and preserves same-offset OFF then ON");
    position=0;before=actual->launchCount;Changes loopStart;loopStart.add(gate,{{0,1}});render(&loopStart);
    pass(actual->launchCount==before&&actual->visualState==2,"loop restart ON baseline stays dry");
    Changes preroll;preroll.add(gate,{{0,0},{64,1}});before=actual->launchCount;render(&preroll);
    pass(actual->launchCount==before+1,"OFF pre-roll then ON retriggers correctly after loop reset");
    Changes flush;flush.add(gate,{{0,0}});flush.add(time,{{0,0}});before=actual->launchCount;
    ProcessContext flushContext{};flushContext.sampleRate=48000;flushContext.state=ProcessContext::kPlaying;flushContext.projectTimeSamples=position;
    ProcessData empty{};empty.processMode=kRealtime;empty.symbolicSampleSize=kSample32;empty.inputParameterChanges=&flush;empty.processContext=&flushContext;
    require(processor->process(empty)==kResultOk,"zero-length control flush");pass(!actual->visualGate&&actual->launchCount==before,"zero-length flush accepts controls without new launch or audio time");
    const std::vector<std::pair<int32,ParamValue>> gateEvents{{0,0},{1024,1},{1025,0},{2087,1},{2100,1},{2150,0},{3333,1},{5000,0},{5001,1}};
    const std::vector<std::pair<int32,ParamValue>> outputEvents{{0,.5},{1500,1},{2100,0},{5001,2./3},{7500,.5}};
    const std::vector<std::pair<int32,ParamValue>> fxEvents{{0,0},{1281,1},{3872,.45}};
    for(int size:{1,64,257,1024,-1}){
        processor->setProcessing(false);component->setActive(false);
        actual->apvts.getParameter("harvest")->setValueNotifyingHost(0);actual->apvts.getParameter("spinUpMs")->setValueNotifyingHost(0);actual->apvts.getParameter("fxAmount")->setValueNotifyingHost(0);
        require(component->setActive(true)==kResultOk&&processor->setProcessing(true)==kResultOk,"matrix activation");
        reference.setTime(80);reference.setFx(0);reference.setOutputDb(0);actual->apvts.getParameter("outputDb")->setValueNotifyingHost(.5);reference.prepare(48000,2);referenceGate=false;referenceBypass=false;reference.reset(false);position=0;compare=true;maximum=0;int iteration=0;
        while(position<12000){const int variable[]={1,257,64,1024,17};const int count=std::min<int>(size<0?variable[iteration++%5]:size,int(12000-position));Changes events;
            for(auto entry:{std::pair{gate,&gateEvents},std::pair{fx,&fxEvents},std::pair{outputDb,&outputEvents}}){std::vector<std::pair<int32,ParamValue>> points;for(auto [at,value]:*entry.second)if(at>=position&&at<position+count)points.push_back({int32(at-position),value});if(!points.empty())events.add(entry.first,std::move(points));}
            render(events.queues.empty()?nullptr:&events,true,false,count);
        }
        require(maximum<3e-6,"block size invariant actual VST3 audio");require(actual->launchCount==4,"single-sample pulses and repeated ON yield four launches");
        if(!quiet)std::cout<<"PASS raw VST3 block size "<<size<<" (variable=-1), maximum difference "<<maximum<<'\n';
    }
    compare=false;
    const auto pid=[](const char* name){return ParamID(juce::String(name).hashCode()&0x7fffffff);};
    tempoValid=true;
    for(double hostTempo:{60.,90.,120.,150.})for(int hostNumerator:{3,4})for(int division=0;division<6;++division){tempo=hostTempo;numerator=hostNumerator;Changes off;off.add(gate,{{0,0}});render(&off);for(int i=0;i<5;++i)render(nullptr);
        Changes selected;selected.add(gate,{{31,1}});selected.add(pid("syncMode"),{{31,1}});selected.add(pid("syncDivision"),{{31,division/5.}});
        for(const char* id:{"fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout"})selected.add(pid(id),{{31,1}});selected.add(fx,{{31,.85}});render(&selected);
        const float latched=actual->latchedTime.load();require(std::abs(latched-energy::syncMilliseconds(division,tempo,numerator,denominator))<.001,"raw queue sync controls applied before same-offset launch");
        tempo=hostTempo*1.2;render(nullptr);require(actual->latchedTime.load()==latched,"mid-spin host tempo change keeps latched endpoint");
    }
    pass(true,"all five FX and 48 host BPM/time-signature/division launches; tempo latch and same-offset automation");
    if(!quiet)std::cout<<"VST3 queue points "<<actual->queuePoints<<'\n';
}
// 0.3.2 regressions through the shipping wrapper. Measured values print before each verdict.
void regressions(Bundle& bundle){
    Host host;IComponent* component=nullptr;
    for(int32 i=0;i<bundle.factory->countClasses();++i){PClassInfo info{};bundle.factory->getClassInfo(i,&info);if(std::strcmp(info.category,kVstAudioEffectClass)==0)bundle.factory->createInstance(info.cid,IComponent::iid,reinterpret_cast<void**>(&component));}
    require(component!=nullptr,"regression component");TUID controllerId{};
    require(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk&&component->getControllerClassId(controllerId)==kResultOk,"regression component initialisation");
    IEditController* controller=nullptr;require(bundle.factory->createInstance(controllerId,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"regression controller");
    controller->initialize(static_cast<IHostApplication*>(&host));controller->setComponentHandler(&host);
    auto* a=query<IConnectionPoint>(component);auto* b=query<IConnectionPoint>(controller);a->connect(b);b->connect(a);
    auto* concrete=query<juce::JuceVST3EditController>(controller);auto* actual=static_cast<EnergyHarvesterProcessor*>(concrete->getPluginInstance());auto* processor=query<IAudioProcessor>(component);
    struct Cleanup{IComponent* c;IEditController* e;IConnectionPoint* a;IConnectionPoint* b;juce::JuceVST3EditController* concrete;IAudioProcessor* p;
        ~Cleanup(){p->setProcessing(false);c->setActive(false);a->disconnect(b);b->disconnect(a);a->release();b->release();e->setComponentHandler(nullptr);e->terminate();c->terminate();concrete->release();p->release();e->release();c->release();}} cleanup{component,controller,a,b,concrete,processor};
    const auto pid=[](const char* name){return ParamID(juce::String(name).hashCode()&0x7fffffff);};
    const ParamID gate=pid("harvest"),time=pid("spinUpMs"),sync=pid("syncMode"),division=pid("syncDivision");
    ProcessSetup setup{};setup.maxSamplesPerBlock=512;setup.sampleRate=48000;setup.symbolicSampleSize=kSample32;setup.processMode=kRealtime;
    require(component->activateBus(kAudio,kInput,0,true)==kResultOk&&component->activateBus(kAudio,kOutput,0,true)==kResultOk&&processor->setupProcessing(setup)==kResultOk
            &&component->setActive(true)==kResultOk&&processor->setProcessing(true)==kResultOk,"regression activation");
    juce::AudioBuffer<float> audio(2,512);Steinberg::int64 position=0;float last=0;bool primed=false;const double tempo=150;
    const auto render=[&](Changes* changes,int count)->double{
        for(int s=0;s<count;++s){const float x=float(.5*std::sin(2*juce::MathConstants<double>::pi*60*double(position+s)/48000.+1.1));audio.setSample(0,s,x);audio.setSample(1,s,x);}
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;float* channels[]{audio.getWritePointer(0),audio.getWritePointer(1)};in.channelBuffers32=out.channelBuffers32=channels;
        ProcessContext context{};context.sampleRate=48000;context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid|ProcessContext::kTimeSigValid;
        context.tempo=tempo;context.timeSigNumerator=4;context.timeSigDenominator=4;context.projectTimeSamples=position;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=count;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.inputParameterChanges=changes;data.processContext=&context;
        require(processor->process(data)==kResultOk,"regression processing");double step=0;
        for(int s=0;s<count;++s){const float y=audio.getSample(0,s);if(primed)step=std::max(step,double(std::abs(y-last)));last=y;primed=true;}
        position+=count;return step;
    };
    // The VST3 spec allows parameter flushes with no ProcessContext at all.
    const auto flush=[&](Changes* changes){ProcessData empty{};empty.processMode=kRealtime;empty.symbolicSampleSize=kSample32;empty.inputParameterChanges=changes;empty.processContext=nullptr;require(processor->process(empty)==kResultOk,"null-context flush");};
    int failed=0;const auto verdict=[&](bool ok,const std::string& text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failed;};
    for(int i=0;i<10;++i)render(nullptr,512);
    // E1: a null-context zero-length flush in the middle of a spin must not look like a transport start.
    {Changes start;start.add(sync,{{0,0}});start.add(time,{{0,std::log(1600./80.)/std::log(100.)}});start.add(gate,{{0,0},{1,1}});render(&start,512);
        for(int i=0;i<20;++i)render(nullptr,512);const float before=actual->visualProgress;Changes none;flush(&none);const double edge=render(nullptr,512);
        verdict(actual->visualState==1&&actual->visualProgress>before,"E1 null-context flush mid-spin keeps spinning (state "+std::to_string(actual->visualState.load())+", progress "+std::to_string(before)
                +" -> "+std::to_string(actual->visualProgress.load())+", next-block max step "+std::to_string(edge)+")");}
    // E1: controls delivered in such a flush keep the last host tempo for SYNC launches.
    {Changes off;off.add(gate,{{0,0}});render(&off,512);for(int i=0;i<4;++i)render(nullptr,512);
        Changes launch;launch.add(sync,{{0,1}});launch.add(division,{{0,3/5.}});launch.add(gate,{{0,1}});flush(&launch);
        const float latched=actual->latchedTime;const double expected=energy::syncMilliseconds(3,tempo,4,4);
        verdict(std::abs(latched-expected)<.01&&actual->hasHostTempo,"E1 SYNC launch inside a null-context flush uses the host's "+std::to_string(int(tempo))+" BPM (latched "+std::to_string(latched)
                +" ms, expected "+std::to_string(expected)+", host tempo flag "+(actual->hasHostTempo?"kept":"lost")+")");
        render(nullptr,512);}
    // E15: a point at offset == numSamples is clamped into the block instead of being dropped.
    {Changes off;off.add(sync,{{0,0}});off.add(gate,{{0,0}});render(&off,128);for(int i=0;i<4;++i)render(nullptr,128);const auto before=actual->launchCount.load();
        Changes late;late.add(gate,{{128,1}});render(&late,128);
        verdict(actual->launchCount==before+1&&actual->visualGate,"E15 gate point at offset 128 of a 128-sample block launches inside that block (launches +"+std::to_string(actual->launchCount.load()-before)+")");}
    if(failed)throw std::runtime_error(std::to_string(failed)+" VST3 regression check(s) failed");
    std::cout<<"PASS 0.3.2 VST3 regression suite\n";
}
// --compat <0.3.2 bundle> <new bundle>: a state saved by the shipped 0.3.2 plug-in must render identically here.
class Stream final:public IBStream {
public:
    std::vector<char> bytes;int64 position=0;
    tresult PLUGIN_API queryInterface(const TUID id,void** object) override {*object=nullptr;if(juce::doUIDsMatch(id,FUnknown::iid)||juce::doUIDsMatch(id,IBStream::iid)){*object=this;return kResultOk;}return kNoInterface;}
    uint32 PLUGIN_API addRef() override {return 1;} uint32 PLUGIN_API release() override {return 1;}
    tresult PLUGIN_API read(void* buffer,int32 count,int32* done) override {const auto n=std::max<int64>(0,std::min<int64>(count,int64(bytes.size())-position));
        if(n>0)std::memcpy(buffer,bytes.data()+position,size_t(n));position+=n;if(done)*done=int32(n);return kResultOk;}
    tresult PLUGIN_API write(void* buffer,int32 count,int32* done) override {if(position+count>int64(bytes.size()))bytes.resize(size_t(position+count));
        std::memcpy(bytes.data()+position,buffer,size_t(count));position+=count;if(done)*done=count;return kResultOk;}
    tresult PLUGIN_API seek(int64 to,int32 mode,int64* result) override {position=mode==kIBSeekSet?to:mode==kIBSeekCur?position+to:int64(bytes.size())+to;if(result)*result=position;return kResultOk;}
    tresult PLUGIN_API tell(int64* at) override {*at=position;return kResultOk;}
};
struct Instance {
    Host host;IComponent* component=nullptr;IEditController* controller=nullptr;IAudioProcessor* processor=nullptr;IConnectionPoint *a=nullptr,*b=nullptr;
    juce::AudioBuffer<float> audio{2,512};Steinberg::int64 position=0;
    explicit Instance(Bundle& bundle){
        for(int32 i=0;i<bundle.factory->countClasses();++i){PClassInfo info{};bundle.factory->getClassInfo(i,&info);if(std::strcmp(info.category,kVstAudioEffectClass)==0)bundle.factory->createInstance(info.cid,IComponent::iid,reinterpret_cast<void**>(&component));}
        require(component!=nullptr,"compat component");TUID controllerId{};
        require(component->initialize(static_cast<IHostApplication*>(&host))==kResultOk&&component->getControllerClassId(controllerId)==kResultOk,"compat component initialisation");
        require(bundle.factory->createInstance(controllerId,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"compat controller");
        controller->initialize(static_cast<IHostApplication*>(&host));controller->setComponentHandler(&host);
        a=query<IConnectionPoint>(component);b=query<IConnectionPoint>(controller);a->connect(b);b->connect(a);processor=query<IAudioProcessor>(component);
        ProcessSetup setup{};setup.maxSamplesPerBlock=512;setup.sampleRate=48000;setup.symbolicSampleSize=kSample32;setup.processMode=kRealtime;
        require(component->activateBus(kAudio,kInput,0,true)==kResultOk&&component->activateBus(kAudio,kOutput,0,true)==kResultOk&&processor->setupProcessing(setup)==kResultOk
                &&component->setActive(true)==kResultOk&&processor->setProcessing(true)==kResultOk,"compat activation");
    }
    ~Instance(){processor->setProcessing(false);component->setActive(false);a->disconnect(b);b->disconnect(a);a->release();b->release();controller->setComponentHandler(nullptr);
        controller->terminate();component->terminate();processor->release();controller->release();component->release();}
    void block(Changes* changes,int count,const std::function<float(Steinberg::int64,int)>& input,std::vector<float>* out){
        for(int s=0;s<count;++s){audio.setSample(0,s,input(position+s,0));audio.setSample(1,s,input(position+s,1));}
        AudioBusBuffers in{},outBus{};in.numChannels=outBus.numChannels=2;float* ch[]{audio.getWritePointer(0),audio.getWritePointer(1)};in.channelBuffers32=outBus.channelBuffers32=ch;
        ProcessContext context{};context.sampleRate=48000;context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid|ProcessContext::kTimeSigValid;context.tempo=140;
        context.timeSigNumerator=4;context.timeSigDenominator=4;context.projectTimeSamples=position;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=count;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&outBus;data.inputParameterChanges=changes;data.processContext=&context;
        require(processor->process(data)==kResultOk,"compat processing");
        if(out)for(int s=0;s<count;++s){out->push_back(audio.getSample(0,s));out->push_back(audio.getSample(1,s));}
        position+=count;
    }
};
int compat(const juce::File& oldPath,const juce::File& newPath){
    Bundle old(oldPath),current(newPath);
    const auto pid=[](const char* name){return ParamID(juce::String(name).hashCode()&0x7fffffff);};
    const auto ms=[](double v){return std::log(v/80.)/std::log(100.);};
    struct Scenario{const char* name;std::vector<std::pair<const char*,double>> values;bool retrigger;};
    const std::vector<Scenario> scenarios{
        {"defaults (800 ms, FX 45 %, phaser)",{},false},
        {"2.5 s, FX 100 %, all five effects, +6 dB",{{"spinUpMs",ms(2500)},{"fxAmount",1},{"fxPan",1},{"fxChorus",1},{"fxPhaser",1},{"fxFlanger",1},{"fxWashout",1},{"outputDb",(6+18)/36.}},false},
        {"SYNC 1 bar at 140 BPM, washout + chorus, cancel and relaunch",{{"syncMode",1},{"syncDivision",3/5.},{"fxAmount",.7},{"fxPhaser",0},{"fxChorus",1},{"fxWashout",1}},true},
        {"80 ms, FX 0 %, -12 dB",{{"spinUpMs",0},{"fxAmount",0},{"outputDb",(-12+18)/36.}},false},
        {"1.2 s, flanger + panning at 85 %",{{"spinUpMs",ms(1200)},{"fxAmount",.85},{"fxPhaser",0},{"fxFlanger",1},{"fxPan",1}},true},
    };
    const auto input=[](Steinberg::int64 n,int channel){const double t=double(n)/48000;
        return float(.3*std::sin(2*juce::MathConstants<double>::pi*49*t)+.18*std::sin(2*juce::MathConstants<double>::pi*(98+channel*3)*t)*(.5+.5*std::sin(9.4*t))
            +.08*std::sin(2*juce::MathConstants<double>::pi*1760*t)*std::exp(-std::fmod(t,.2142857)*20));};
    int failed=0;
    for(const auto& scenario:scenarios){
        Stream saved;
        {Instance source(old);Changes setup;setup.add(pid("harvest"),{{0,0}});for(auto [id,value]:scenario.values)setup.add(pid(id),{{0,value}});
            source.block(&setup,512,[](Steinberg::int64,int){return 0.f;},nullptr);require(source.component->getState(&saved)==kResultOk,"0.3.2 getState");}
        const auto session=[&](Instance& instance){
            saved.position=0;require(instance.component->setState(&saved)==kResultOk,"setState");saved.position=0;instance.controller->setComponentState(&saved);
            std::vector<float> out;const int total=48000*3;
            while(instance.position<total){const int count=int(std::min<Steinberg::int64>(512,total-instance.position));Changes events;std::vector<std::pair<int32,ParamValue>> gate;
                for(auto [at,value]:std::vector<std::pair<Steinberg::int64,double>>{{24000,1},{43200,scenario.retrigger?0.:1.},{45600,1}})
                    if(at>=instance.position&&at<instance.position+count)gate.push_back({int32(at-instance.position),value});
                if(!gate.empty())events.add(pid("harvest"),gate);instance.block(gate.empty()?nullptr:&events,count,input,&out);}
            return out;};
        std::vector<float> before,after;bool legacy=false;
        {Instance reference(old);before=session(reference);}
        {Instance updated(current);after=session(updated);
            auto* concrete=query<juce::JuceVST3EditController>(updated.controller);auto* actual=static_cast<EnergyHarvesterProcessor*>(concrete->getPluginInstance());
            legacy=actual->apvts.getRawParameterValue("latestSound")->load()<.5f&&actual->apvts.getRawParameterValue("mode")->load()==0;concrete->release();}
        double worst=0;size_t identical=0;
        for(size_t i=0;i<before.size();++i){worst=std::max(worst,double(std::abs(before[i]-after[i])));identical+=before[i]==after[i]?1:0;}
        const bool ok=before.size()==after.size()&&worst<=1e-6&&legacy;if(!ok)++failed;
        std::cout<<(ok?"PASS ":"FAIL ")<<"0.3.2 state renders the same in 0.4.0: "<<scenario.name<<" ("<<identical<<"/"<<before.size()<<" samples bit-identical, max difference "<<worst
                 <<", Latest Sound "<<(legacy?"off":"on")<<")\n";
    }
    if(failed)throw std::runtime_error(std::to_string(failed)+" compatibility scenario(s) failed");
    std::cout<<"ENERGY HARVESTER 0.3.2 compatibility checks passed\n";return 0;
}
}
int main(int argc,char** argv){try{juce::ScopedJuceInitialiser_GUI gui;require(argc>=2,"Supply VST3 bundle path");
    if(juce::String(argv[1])=="--compat"){require(argc>=4,"--compat <0.3.2 bundle> <new bundle>");return compat(juce::File(juce::String::fromUTF8(argv[2])),juce::File(juce::String::fromUTF8(argv[3])));}
    Bundle bundle(juce::File(juce::String::fromUTF8(argv[1])));
    std::vector<std::unique_ptr<Bundle>> siblings;for(int i=2;i<argc;++i)siblings.push_back(std::make_unique<Bundle>(juce::File(juce::String::fromUTF8(argv[i]))));
    if(!siblings.empty())std::cout<<"PASS ENERGY loads while "<<siblings.size()<<" sibling VST3 modules remain loaded\n";
    Host host;test(bundle,host);regressions(bundle);
    // Fresh sequential instances and concurrently processed instances use the same loaded module.
    quiet=true;Host second;test(bundle,second);quiet=false;std::cout<<"PASS second instance on same thread has independent delivery\n";
    std::exception_ptr errors[2];std::thread threads[2];for(int i=0;i<2;++i)threads[i]=std::thread([&,i]{try{quiet=true;Host h;test(bundle,h);}catch(...){errors[i]=std::current_exception();}});for(auto& t:threads)t.join();for(auto error:errors)if(error)std::rethrow_exception(error);
    std::cout<<"PASS simultaneous instances on separate processing threads have independent delivery\nENERGY HARVESTER shipping VST3 automation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
