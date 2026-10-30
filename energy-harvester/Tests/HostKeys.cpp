#include "EnergyHarvesterEditor.h"
#include "Presets.h"
#include "Tests/HostKeysTest.h"
#include <iostream>

int runEnergyHostKeys();

namespace {
juce::Component* named(juce::Component& parent,const char* name) {
    if(parent.getName()==name)return &parent;
    for(auto* child:parent.getChildren())if(auto* found=named(*child,name))return found;
    return nullptr;
}
void click(juce::ComponentPeer& peer,juce::Point<float> at) {
    const auto now=juce::Time::currentTimeMillis();
    peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse,at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                         juce::MouseInputSource::defaultPressure,juce::MouseInputSource::defaultOrientation,now);
    peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse,at,{},
                         juce::MouseInputSource::defaultPressure,juce::MouseInputSource::defaultOrientation,now+30);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}
}

int runEnergyHostKeys() {
    EnergyHarvesterProcessor processor;
    auto presets=energy::presets::factory();
    auto values=processor.currentValues();values["outputDb"]=3.2f;
    juce::File written;
    if(energy::presets::save(energy::presets::userRoot(),"Host Keys","User Preset",values,written).failed())return 1;
    for(const auto& user:energy::presets::userPresets(energy::presets::userRoot()))presets.push_back(user);
    const auto load=[&](size_t index){const auto& p=presets[index];processor.applyPreset(p.values,p.folder,p.name);};
    load(0);
    auto* latest=processor.apvts.getParameter("latestSound");latest->setValueNotifyingHost(0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    vfd::test::HostKeysProbe probe;
    // Read the actual title plate, including the folder, rather than just processor bookkeeping.
    probe.currentPreset=[&]{return named(*editor,"PRESET")->getTitle();};
    probe.blankSpot={25,425};
    for(const char* name:{"SPIN-UP TIME","FX AMOUNT","OUTPUT","PANNING"}){
        auto* control=named(*editor,name);
        probe.controlSpots.push_back(editor->getLocalPoint(control,juce::Point<int>(control->getWidth()/3,control->getHeight()/3)));
    }
    probe.openTextField=[&]() -> juce::TextEditor* {
        auto* output=named(*editor,"OUTPUT");
        for(auto* child:output->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child)){
            auto* peer=editor->getPeer();
            click(*peer,peer->getComponent().getLocalPoint(label,label->getLocalBounds().getCentre()).toFloat());
            return label->getCurrentTextEditor();
        }
        return nullptr;
    };
    int failures=vfd::test::runHostKeys(*editor,probe,std::cout);
    if(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()==nullptr)return failures;
    const auto expect=[&](bool ok,const char* text){std::cout<<(ok?"  ok   ":"  FAIL ")<<text<<'\n';if(!ok)++failures;};
    juce::Component holder;holder.setSize(editor->getWidth(),editor->getHeight());holder.addAndMakeVisible(*editor);
    holder.setTopLeftPosition(-6000,-6000);holder.addToDesktop(juce::ComponentPeer::windowIsTemporary);holder.setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* peer=holder.getPeer();
    if(peer==nullptr){expect(false,"EH-F1 extra checks open the editor");return failures;}
    click(*peer,probe.blankSpot.toFloat());
    const auto press=[&](int code,juce::ModifierKeys mods=juce::ModifierKeys()){return peer->handleKeyPress(juce::KeyPress(code,mods,0));};
    const auto matches=[&](size_t index){return processor.presetLabel()==std::make_pair(presets[index].folder,presets[index].name);};
    for(const int key:{juce::KeyPress::rightKey,juce::KeyPress::downKey,juce::KeyPress::leftKey,juce::KeyPress::upKey}){
        const bool forward=key==juce::KeyPress::rightKey||key==juce::KeyPress::downKey;
        load(0);bool ordered=true;
        for(size_t i=1;i<=presets.size();++i){const bool used=press(key);ordered=ordered&&used&&matches(forward?i%presets.size():(presets.size()-i)%presets.size());}
        expect(ordered,forward?"EH-F1 forward arrow follows the full factory/user list and wraps":"EH-F1 backward arrow follows the full factory/user list and wraps");
    }
    const auto before=processor.currentValues();const auto label=processor.presetLabel();bool modifiersPass=true;
    for(int mod:{juce::ModifierKeys::shiftModifier,juce::ModifierKeys::commandModifier,juce::ModifierKeys::altModifier,juce::ModifierKeys::ctrlModifier})
        for(int key:{juce::KeyPress::leftKey,juce::KeyPress::rightKey,juce::KeyPress::upKey,juce::KeyPress::downKey})
            modifiersPass=!press(key,juce::ModifierKeys(mod))&&modifiersPass;
    expect(modifiersPass&&processor.currentValues()==before&&processor.presetLabel()==label,"EH-F1 all 16 modified arrows reach the host without changing controls or presets");
    expect(!press(juce::KeyPress::escapeKey),"EH-F1 Escape reaches the host when nothing is being cancelled");
    expect(processor.apvts.getRawParameterValue("harvest")->load()<.5f&&latest->getValue()<.5f,"EH-F1 keyboard browsing leaves HARVEST and original Sound unchanged");
    holder.removeFromDesktop();holder.removeChildComponent(editor.get());
    std::cout<<"ENERGY HOST KEYS: "<<(failures==0?"ALL PASSED":"FAILED")<<'\n';
    return failures;
}
