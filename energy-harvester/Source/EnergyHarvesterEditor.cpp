#include "EnergyHarvesterEditor.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include "Timing.h"
#include "Presets.h"
#include "Ui/HarvestLights.h"

namespace {
juce::Colour pink(0xffff52be), pearl(0xffffedf8), ink(0xff39102e), glass(0xff1f1427);
vfd::Theme theme() {return {juce::Colour(0xff280a22),pearl,pink,juce::Colour(0xff764061),juce::Colour(0xffd7abc5),pearl,
    juce::Colour(0xffc7a4b8),ink,juce::Colour(0xff4b163c)};}
// The device fills the window: this is the visible part of the 560 x 720 design canvas, inside the shell's
// straight edges (Assets/DeviceShell-fullbleed.png has tightened corners to match).
const juce::Rectangle<float> viewport{21,7,518,691};
constexpr int viewWidth=518,viewHeight=691;
class HarvestButton final:public juce::Button {
public:
    explicit HarvestButton(juce::Typeface::Ptr f):Button("HARVEST"),face(f) {
        setClickingTogglesState(true);setWantsKeyboardFocus(true);setTitle("Harvest");
        setDescription("Launch the selected mode when switched on. Switch off to rearm.");setTooltip(getDescription());
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    bool hitTest(int x,int y) override {return juce::Point<float>(float(x),float(y)).getDistanceFrom({120,120})<118;}
    void paintButton(juce::Graphics& g,bool hover,bool down) override {
        const bool on=getToggleState();const float y=(on||down)?32.f:29.f;
        auto cap=juce::Rectangle<float>(30,y,180,180);
        g.setColour(juce::Colours::black.withAlpha(.7f));g.fillEllipse(cap.translated(1,4));
        g.setGradientFill(juce::ColourGradient(hover?pearl.brighter(.08f):pearl,70,y,juce::Colour(0xffcbaabc),190,y+180,false));g.fillEllipse(cap);
        g.setColour(juce::Colours::white.withAlpha(.72f));g.drawEllipse(cap.reduced(2),2);
        g.setColour(ink.withAlpha(.12f));g.drawEllipse(cap.reduced(7),1);
        g.setColour(ink);
        for(int i=0;i<3;++i) {juce::Path p;p.startNewSubPath(120,88);p.cubicTo(112,72,122,61,129,65);p.cubicTo(142,70,144,91,132,99);p.closeSubPath();
            p.applyTransform(juce::AffineTransform::rotation(i*juce::MathConstants<float>::twoPi/3,120,100));g.fillPath(p);}
        g.setFont(vfd::font(face,27));g.drawText("HARVEST",36,119,168,36,juce::Justification::centred);
        g.setFont(vfd::font(face,16));g.drawText(on?"ON":"OFF",50,157,140,23,juce::Justification::centred);
        if(hasKeyboardFocus(true)){g.setColour(juce::Colours::white);g.drawEllipse(cap.expanded(3),2);}
    }
private:juce::Typeface::Ptr face;
};
// Touchscreen key drawn on the display glass. Small variants serve the page tabs and the setup screen.
class ScreenToggle final:public juce::Button {
public:
    ScreenToggle(const juce::String& name,juce::Typeface::Ptr f,bool mode=false,float textHeight=9.5f):Button(name),face(f),isMode(mode),height(textHeight){
        setClickingTogglesState(true);setWantsKeyboardFocus(true);setTitle(name);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
    juce::String label;   // shown text when it differs from the accessible name
    void paintButton(juce::Graphics& g,bool hover,bool) override {
        auto b=getLocalBounds().toFloat().reduced(1);const bool on=getToggleState();
        if(isMode){
            g.setColour(ink.withAlpha(.88f));g.fillRoundedRectangle(b,5);
            auto selected=b.withWidth(b.getWidth()*.5f);if(on)selected=selected.translated(selected.getWidth(),0);
            g.setColour(pearl);g.fillRoundedRectangle(selected.reduced(1.5f),3.5f);
            g.setFont(vfd::font(face,9.5f));g.setColour(on?pearl:ink);g.drawText("TIME",1,0,getWidth()/2,getHeight(),juce::Justification::centred);
            g.setColour(on?ink:pearl);g.drawText("SYNC",getWidth()/2,0,getWidth()/2,getHeight(),juce::Justification::centred);
        }else{
            g.setColour(on?pink.withAlpha(.23f):glass);g.fillRoundedRectangle(b,4);
            g.setColour(on?pink:pearl.withAlpha(hover?.65f:.23f));g.drawRoundedRectangle(b,4,1);
            g.setFont(vfd::font(face,height));g.setColour(on?pearl:pearl.withAlpha(.66f));
            g.drawFittedText(label.isNotEmpty()?label:getName(),getLocalBounds().reduced(2,0),juce::Justification::centred,1,.8f);
            if(on&&getHeight()>=22){g.setColour(pink);g.fillRoundedRectangle(b.getX()+9,b.getBottom()-3,b.getWidth()-18,1.5f,.75f);}
        }
        if(hasKeyboardFocus(true)){g.setColour(pearl);g.drawRoundedRectangle(b,4,1.5f);}
    }
private:juce::Typeface::Ptr face;bool isMode;float height;
};
// Radio row of screen keys driving one choice (or switch) parameter, undoable as a complete gesture.
class ChoiceRow {
public:
    ChoiceRow(juce::RangedAudioParameter& p,std::vector<ScreenToggle*> keys):buttons(std::move(keys)),attachment(p,[this](float v){show(v);}) {
        for(size_t i=0;i<buttons.size();++i){buttons[i]->setClickingTogglesState(false);buttons[i]->onClick=[this,i]{attachment.setValueAsCompleteGesture(float(i));};}
        attachment.sendInitialUpdate();
    }
private:
    void show(float v){const int index=juce::roundToInt(v);for(size_t i=0;i<buttons.size();++i)buttons[i]->setToggleState(int(i)==index,juce::dontSendNotification);}
    std::vector<ScreenToggle*> buttons;juce::ParameterAttachment attachment;
};
class FaderLook final:public juce::LookAndFeel_V4 {
public:
    juce::Typeface::Ptr face;
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float pos,float,float,juce::Slider::SliderStyle,juce::Slider& s) override {
        auto track=juce::Rectangle<float>(float(x),float(y+height/2-9),float(width),18);
        g.setColour(juce::Colour(0xff301123));g.fillRoundedRectangle(track,9);
        g.setColour(juce::Colour(0xffffaedb));g.fillRoundedRectangle(track.withWidth(juce::jlimit(0.f,float(width),pos-x)),9);
        auto cap=juce::Rectangle<float>(pos-15,float(y+height/2-14),30,28);
        g.setColour(juce::Colours::black.withAlpha(.45f));g.fillRoundedRectangle(cap.translated(1,3),6);
        g.setGradientFill(juce::ColourGradient(pearl,cap.getX(),cap.getY(),juce::Colour(0xffc7a8b5),cap.getX(),cap.getBottom(),false));g.fillRoundedRectangle(cap,6);
        g.setColour(juce::Colours::white.withAlpha(.65f));g.drawRoundedRectangle(cap,6,1);
        for(int i=-2;i<=2;++i){g.setColour(ink.withAlpha(.18f));g.drawVerticalLine(int(pos+i*3),cap.getY()+6,cap.getBottom()-6);}
        if(s.hasKeyboardFocus(true)){g.setColour(juce::Colours::white);g.drawRoundedRectangle(track.expanded(3),11,2);}
    }
    juce::Font getLabelFont(juce::Label&) override {return vfd::font(face,16);}
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* l=LookAndFeel_V4::createSliderTextBox(s);l->setColour(juce::Label::textColourId,ink);
        l->setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);l->setJustificationType(juce::Justification::centredRight);
        // Values stay on the resin fascia, including while typing.
        for(int id:std::array<int,7>{juce::Label::outlineColourId,juce::Label::outlineWhenEditingColourId,
                    juce::Label::backgroundWhenEditingColourId,juce::TextEditor::backgroundColourId,
                    juce::TextEditor::outlineColourId,juce::TextEditor::focusedOutlineColourId,
                    juce::TextEditor::shadowColourId})l->setColour(id,juce::Colours::transparentBlack);
        l->setColour(juce::Label::textWhenEditingColourId,ink);
        l->setColour(juce::TextEditor::textColourId,ink);
        l->setColour(juce::CaretComponent::caretColourId,ink);
        l->setColour(juce::TextEditor::highlightColourId,pink.withAlpha(.35f));
        l->setColour(juce::TextEditor::highlightedTextColourId,ink);return l;
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        juce::Slider::SliderLayout l;auto b=s.getLocalBounds();l.textBoxBounds=b.removeFromRight(74);l.sliderBounds=b.reduced(8,0);return l;
    }
};
// Thin touchscreen slider for the setup screen: pink track, pearl thumb, pearl readout.
class ScreenSliderLook final:public juce::LookAndFeel_V4 {
public:
    juce::Typeface::Ptr face;
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float pos,float,float,juce::Slider::SliderStyle,juce::Slider& s) override {
        auto track=juce::Rectangle<float>(float(x),float(y+height/2-3),float(width),6);
        g.setColour(juce::Colour(0xff3d1934));g.fillRoundedRectangle(track,3);
        const bool centred=s.getMinimum()<0;const float zero=centred?float(x)+float(width)*.5f:float(x);
        g.setColour(pink.withAlpha(.85f));g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(zero,pos),track.getY(),std::abs(pos-zero),6),3);
        auto thumb=juce::Rectangle<float>(pos-5,float(y+height/2-8),10,16);
        g.setColour(pearl);g.fillRoundedRectangle(thumb,3);g.setColour(ink.withAlpha(.3f));g.drawRoundedRectangle(thumb,3,.8f);
        if(s.hasKeyboardFocus(true)){g.setColour(pearl);g.drawRoundedRectangle(track.expanded(3,5),5,1.2f);}
    }
    juce::Font getLabelFont(juce::Label&) override {return vfd::font(face,10.5f);}
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* l=LookAndFeel_V4::createSliderTextBox(s);l->setJustificationType(juce::Justification::centredRight);
        for(int id:std::array<int,8>{juce::Label::backgroundColourId,juce::Label::outlineColourId,juce::Label::backgroundWhenEditingColourId,
            juce::Label::outlineWhenEditingColourId,juce::TextEditor::backgroundColourId,juce::TextEditor::outlineColourId,
            juce::TextEditor::focusedOutlineColourId,juce::TextEditor::shadowColourId})l->setColour(id,juce::Colours::transparentBlack);
        for(int id:std::array<int,4>{juce::Label::textColourId,juce::Label::textWhenEditingColourId,juce::TextEditor::textColourId,juce::CaretComponent::caretColourId})l->setColour(id,pearl);
        l->setColour(juce::TextEditor::highlightColourId,pink.withAlpha(.4f));l->setColour(juce::TextEditor::highlightedTextColourId,pearl);return l;
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        juce::Slider::SliderLayout l;auto b=s.getLocalBounds();l.textBoxBounds=b.removeFromRight(70);l.sliderBounds=b.reduced(6,0);return l;
    }
};
class OutputSlider final:public juce::Slider {
public:
    void mouseDown(const juce::MouseEvent& event) override {setMouseDragSensitivity(event.mods.isShiftDown()?2200:220);juce::Slider::mouseDown(event);}
};
class OutputLook final:public juce::LookAndFeel_V4 {
public:
    juce::Typeface::Ptr face;
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,float position,float start,float end,juce::Slider& slider) override {
        const float cx=x+width*.5f,cy=y+height*.5f,r=std::min(width,height)*.405f;
        const auto circle=[&](float radius){return juce::Rectangle<float>(cx-radius,cy-radius,radius*2,radius*2);};
        // A molded circular socket uses the same concentric resin lips as HARVEST and INFO.
        auto bezel=circle(r+8);
        g.setColour(juce::Colour(0xff260a1b).withAlpha(.75f));g.fillEllipse(bezel.translated(0,2));
        juce::ColourGradient resin(juce::Colour(0xffffb2d9),cx-r,cy-r-8,juce::Colour(0xff782048),cx+r,cy+r+8,false);
        resin.addColour(.30,juce::Colour(0xfff451a8));resin.addColour(.56,juce::Colour(0xff922452));resin.addColour(.90,juce::Colour(0xffdc488e));
        g.setGradientFill(resin);g.fillEllipse(bezel);
        g.setColour(pearl.withAlpha(.52f));g.drawEllipse(bezel.reduced(.8f),.8f);
        g.setColour(juce::Colour(0xff3b1029));g.fillEllipse(circle(r+4.5f));
        g.setColour(pink.withAlpha(.6f));g.drawEllipse(circle(r+6),1);
        const float zero=start+(end-start)*.5f,angle=start+(end-start)*position;
        juce::Path track;track.addCentredArc(cx,cy,r+3,r+3,0,start,end,true);
        g.setColour(juce::Colour(0xff9c557c));g.strokePath(track,juce::PathStrokeType(1.2f));
        juce::Path amount;amount.addCentredArc(cx,cy,r+3,r+3,0,std::min(zero,angle),std::max(zero,angle),true);
        g.setColour(juce::Colour(0xffffbee3));g.strokePath(amount,juce::PathStrokeType(1.6f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        auto cap=circle(r);
        g.setColour(juce::Colours::black.withAlpha(.6f));g.fillEllipse(cap.translated(0,1.5f));
        juce::ColourGradient ivory(pearl,cx-r*.4f,cy-r,juce::Colour(0xffb998aa),cx+r*.4f,cy+r,false);
        ivory.addColour(.4,juce::Colour(0xffecd4e0));g.setGradientFill(ivory);g.fillEllipse(cap);
        g.setColour(juce::Colour(0xff9a6c86));g.drawEllipse(cap,.8f);
        g.setColour(pearl.withAlpha(.8f));g.drawEllipse(cap.reduced(1.5f),.8f);
        g.setColour(ink.withAlpha(.18f));g.drawEllipse(cap.reduced(3),.7f);
        const auto tip=juce::Point<float>(cx+std::sin(angle)*r*.77f,cy-std::cos(angle)*r*.77f);
        const auto root=juce::Point<float>(cx+std::sin(angle)*r*.43f,cy-std::cos(angle)*r*.43f);
        g.setColour(ink);g.drawLine({root,tip},2.f);
        if(slider.hasKeyboardFocus(true)){g.setColour(pearl);g.drawEllipse(bezel.expanded(1.5f),1);}
    }
    juce::Font getLabelFont(juce::Label&) override {return vfd::font(face,13.f);}
    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label=LookAndFeel_V4::createSliderTextBox(slider);label->setJustificationType(juce::Justification::centred);
        for(int id:std::array<int,8>{juce::Label::backgroundColourId,juce::Label::outlineColourId,juce::Label::backgroundWhenEditingColourId,
            juce::Label::outlineWhenEditingColourId,juce::TextEditor::backgroundColourId,juce::TextEditor::outlineColourId,
            juce::TextEditor::focusedOutlineColourId,juce::TextEditor::shadowColourId})label->setColour(id,juce::Colours::transparentBlack);
        label->setColour(juce::Label::textColourId,pearl);label->setColour(juce::Label::textWhenEditingColourId,pearl);
        label->setColour(juce::TextEditor::textColourId,pearl);label->setColour(juce::CaretComponent::caretColourId,pearl);
        label->setColour(juce::TextEditor::highlightColourId,pink.withAlpha(.4f));label->setColour(juce::TextEditor::highlightedTextColourId,pearl);return label;
    }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
        juce::Slider::SliderLayout layout;auto bounds=slider.getLocalBounds();layout.textBoxBounds=bounds.removeFromBottom(22);layout.sliderBounds=bounds.reduced(10,3);return layout;
    }
};
// Small flat text key used on the title plate's preset strip.
class PlateKey final:public juce::Button {
public:
    PlateKey(const juce::String& name,juce::Typeface::Ptr f,juce::String text):Button(name),label(std::move(text)),face(f){setWantsKeyboardFocus(true);setTitle(name);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
    juce::String label;bool field=false;
    void paintButton(juce::Graphics& g,bool hover,bool down) override {
        auto b=getLocalBounds().toFloat().reduced(.5f);
        g.setColour(juce::Colour(0xff2a0b22).withAlpha(field?.78f:(down?.7f:hover?.55f:.38f)));g.fillRoundedRectangle(b,3);
        g.setColour(pearl.withAlpha(hover||field?.35f:.18f));g.drawRoundedRectangle(b,3,.8f);
        g.setColour(pearl.withAlpha(down?.75f:1.f));g.setFont(vfd::font(face,10.f));
        g.drawFittedText(label,getLocalBounds().reduced(field?7:2,0),field?juce::Justification::centredLeft:juce::Justification::centred,1,.75f);
        if(hasKeyboardFocus(true)){g.setColour(pink);g.drawRoundedRectangle(b,3,1.2f);}
    }
private:juce::Typeface::Ptr face;
};
juce::String modeVerb(int mode,int state) {
    if(state==4) return "STOPPED";
    if(state==3) return "RESETTING";
    if(state==1) { const char* words[]{"SPINNING","POWERING DOWN","BRAKING","STOP > START","REWINDING"}; return words[juce::jlimit(0,4,mode)]; }
    if(state==2) return mode==4?"BACK IN":"AT SPEED";
    return "READY";
}
}

// Flip-up settings screen: the extended controls on display glass over the socket.
class SetupScreen final:public juce::Component {
public:
    SetupScreen(EnergyHarvesterProcessor& p,juce::Typeface::Ptr f):processor(p),face(f) {
        setName("Setup Screen");setTitle("Setup");setWantsKeyboardFocus(false);
        look.face=f;
        addKeys(engineKeys,{"CLASSIC","TRUE TAPE"},"engine",{"Classic: the original two-head pitch climb.","True Tape: continuous varispeed from the recorded audio, no grain flutter; lands with a jump back to the live signal."});
        addKeys(landingKeys,{"CLEAN","TAIL","HOLD"},"landing",{"Clean: return exactly to the dry signal at the end.","Tail: the Washout hall keeps ringing over the dry signal after the landing.","Hold: stay effected at speed until HARVEST is switched off."});
        addKeys(filterKeys,{"LP 12","LP 24","BP"},"filterMode",{"12 dB low-pass sweep.","24 dB low-pass sweep.","Band-pass sweep."});
        addKeys(keepKeys,{"OFF","60","90","120","150"},"subKeep",{"Sub Keep off: the whole signal takes the effect.","Keep below 60 Hz dry.","Keep below 90 Hz dry.","Keep below 120 Hz dry.","Keep below 150 Hz dry."});
        addKeys(gridKeys,{"OFF","1/4","BAR","2 BARS"},"gridLand",{"Grid off: TIME or SYNC sets the length.","Land on the next beat (needs a playing transport).","Land on the next bar line (needs a playing transport).","Land on the next two-bar line (needs a playing transport)."});
        addSlider(curve,"SPEED CURVE","curve","Shape of the speed change: LOG moves early, S-CURVE is the original, SLAM LATE holds back and hits at the end.",0);
        addSlider(depth,"START DEPTH","depth","How far below normal speed a start begins, in semitones (24 = quarter speed).",24);
        addSlider(resonance,"RESONANCE","resonance","Resonance of the filter sweep. 0 % keeps the original soft brightness sweep.",0);
        addSlider(start,"FILTER START","filterStart","Where the filter sweep begins (start modes) or ends (stop modes).",350);
        addSlider(mix,"MIX","mix","Parallel mix of the effected signal. 100 % is the full effect.",100);
        resonance.setTextValueSuffix(" %");mix.setTextValueSuffix(" %");
        addAndMakeVisible(latest);latest.setName("LATEST SOUND");latest.setTitle("Latest Sound");latest.label="USE LATEST SOUND";
        latest.setClickingTogglesState(false);
        latestAttachment=std::make_unique<juce::ParameterAttachment>(*p.apvts.getParameter("latestSound"),[this](float v){latest.setToggleState(v>=.5f,juce::dontSendNotification);
            latest.label=v>=.5f?"LATEST SOUND":"USE LATEST SOUND";latest.setTooltip(v>=.5f?"This project uses the 0.4.0 sound (effects recede, sub-safe effects, single-tap landing). Click to return to the exact 0.3.2 sound; the host can undo either change.":
                "This project renders exactly like ENERGY HARVESTER 0.3.2. Click to use the 0.4.0 sound: effects recede with the spin, protect the sub and land with a single tap. The host can undo it.");repaint();});
        latest.onClick=[this]{latestAttachment->setValueAsCompleteGesture(latest.getToggleState()?0.f:1.f);};
        latestAttachment->sendInitialUpdate();
        addAndMakeVisible(done);done.label="DONE";done.setClickingTogglesState(false);done.setTooltip("Close the setup screen.");
    }
    ~SetupScreen() override {for(auto* s:{&curve,&depth,&resonance,&start,&mix})s->setLookAndFeel(nullptr);}
    ScreenToggle& doneKey() noexcept {return done;}
    void resized() override {
        const int left=92,right=getWidth()-12;
        done.setBounds(getWidth()-64,8,52,20);
        const auto row=[&](std::vector<std::unique_ptr<ScreenToggle>>& keys,int y,int x0,int width){const int w=(width-(int(keys.size())-1)*4)/int(keys.size());
            for(size_t i=0;i<keys.size();++i)keys[i]->setBounds(x0+int(i)*(w+4),y,w,22);};
        // One control per row, 27 px apart, so every key has room for its label.
        row(engineKeys,rowY[0],left,180);row(landingKeys,rowY[1],left,180);
        curve.setBounds(left-6,rowY[2],right-left+6,22);depth.setBounds(left-6,rowY[3],right-left+6,22);
        row(filterKeys,rowY[4],left,180);
        resonance.setBounds(left-6,rowY[5],right-left+6,22);start.setBounds(left-6,rowY[6],right-left+6,22);
        row(keepKeys,rowY[7],left,right-left);row(gridKeys,rowY[8],left,right-left);
        mix.setBounds(left-6,rowY[9],right-left+6,22);
        latest.setBounds(left,rowY[10],right-left,22);
    }
    void paint(juce::Graphics& g) override {
        auto b=getLocalBounds().toFloat().reduced(1);
        g.setColour(juce::Colours::black.withAlpha(.55f));g.fillRoundedRectangle(b.translated(0,3),14);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b1028),0,0,juce::Colour(0xff140814),0,b.getBottom(),false));g.fillRoundedRectangle(b,14);
        g.setColour(pink.withAlpha(.55f));g.drawRoundedRectangle(b,14,1.2f);g.setColour(pearl.withAlpha(.08f));g.drawRoundedRectangle(b.reduced(3),11,1);
        g.setColour(pearl);g.setFont(vfd::font(face,13.f));g.drawText("SETUP",16,8,120,20,juce::Justification::centredLeft);
        g.setColour(pink.withAlpha(.25f));g.fillRect(14.f,31.f,b.getWidth()-26,1.f);
        g.setFont(vfd::font(face,9.5f));g.setColour(pearl.withAlpha(.72f));
        const auto label=[&](const char* text,int row){g.drawText(text,16,rowY[size_t(row)],72,22,juce::Justification::centredLeft);};
        int row=0;for(const char* text:{"ENGINE","LANDING","CURVE","DEPTH","FILTER","RES","START","SUB KEEP","GRID","MIX","SOUND"})label(text,row++);
    }
private:
    static constexpr std::array<int,11> rowY{37,64,91,118,145,172,199,226,253,280,310};
    void addKeys(std::vector<std::unique_ptr<ScreenToggle>>& keys,std::initializer_list<const char*> names,const char* id,std::initializer_list<const char*> tips) {
        std::vector<ScreenToggle*> raw;auto tip=tips.begin();
        for(auto* name:names){auto k=std::make_unique<ScreenToggle>(juce::String(id).toUpperCase()+" "+name,face,false,9.f);k->label=name;
            if(tip!=tips.end())k->setTooltip(*tip++);addAndMakeVisible(*k);raw.push_back(k.get());keys.push_back(std::move(k));}
        rows.push_back(std::make_unique<ChoiceRow>(*processor.apvts.getParameter(id),raw));
    }
    void addSlider(juce::Slider& s,const char* name,const char* id,const char* tip,double reset) {
        s.setName(name);s.setTitle(name);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,20);
        s.setLookAndFeel(&look);s.setTooltip(tip);s.setVelocityBasedMode(false);addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,id,s));
        s.setDoubleClickReturnValue(true,reset);
    }
    EnergyHarvesterProcessor& processor;juce::Typeface::Ptr face;ScreenSliderLook look;
    std::vector<std::unique_ptr<ScreenToggle>> engineKeys,landingKeys,filterKeys,keepKeys,gridKeys;
    std::vector<std::unique_ptr<ChoiceRow>> rows;
    juce::Slider curve,depth,resonance,start,mix;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    ScreenToggle latest{"LATEST SOUND",face,false,9.5f},done{"SETUP DONE",face,false,9.5f};std::unique_ptr<juce::ParameterAttachment> latestAttachment;
};

class HarvestCanvas final:public juce::Component,private juce::Timer {
public:
    HarvestCanvas(EnergyHarvesterProcessor& p,EnergyHarvesterEditor& e):processor(p),editor(e),
      normal(juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchSemiBold_ttf,BinaryData::ChakraPetchSemiBold_ttfSize)),
      italic(juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchBoldItalic_ttf,BinaryData::ChakraPetchBoldItalic_ttfSize)),
      harvest(italic),mode("Time / Sync",normal,true),info(theme(),normal),overlay("ENERGY HARVESTER",theme(),BinaryData::VfdLogo_svg,BinaryData::VfdLogo_svgSize,normal),setup(p,normal) {
        setOpaque(true);shellSource=juce::ImageCache::getFromMemory(BinaryData::DeviceShellfullbleed_png,BinaryData::DeviceShellfullbleed_pngSize);
        look.face=normal;addAndMakeVisible(harvest);harvest.setBounds(160,210,240,240);harvest.setExplicitFocusOrder(6);
        buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"harvest",harvest);
        configureSlider(time,"SPIN-UP TIME",8);configureSlider(division,"SPIN-UP DIVISION",8);configureSlider(fx,"FX AMOUNT",9);
        const char* names[]{"PANNING","CHORUS","PHASER","FLANGER","WASHOUT"};const char* ids[]{"fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout"};
        for(int i=0;i<5;++i){auto b=std::make_unique<ScreenToggle>(names[i],normal);b->setBounds(120+i*64,150,60,28);b->setExplicitFocusOrder(i+1);b->setTooltip(juce::String("Include ")+names[i]+" in the spin. FX Amount controls every selected effect.");addAndMakeVisible(*b);effectAttachments[size_t(i)]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,ids[i],*b);effectButtons[size_t(i)]=std::move(b);}
        // MODE page: the five gestures as display keys.
        const char* modeLabels[]{"START","STOP","BRAKE","STOP>GO","REWIND"};
        const char* modeTips[]{"Tape Start: rise from below speed to the original pitch.","Tape Stop: power down to silence; stays silent while HARVEST is on.",
            "Brake: a turntable brake with constant deceleration to a stop.","Stop > Start: stop over the first half, restart over the second.","Rewind: brake, spin backwards and snap back at the end."};
        std::vector<ScreenToggle*> modeRaw;
        for(int i=0;i<5;++i){auto b=std::make_unique<ScreenToggle>(juce::String("MODE ")+modeLabels[i],normal);b->label=modeLabels[i];b->setBounds(120+i*64,150,60,28);
            b->setTooltip(modeTips[i]);addChildComponent(*b);modeRaw.push_back(b.get());modeButtons[size_t(i)]=std::move(b);}
        modeRow=std::make_unique<ChoiceRow>(*p.apvts.getParameter("mode"),modeRaw);
        // Page tabs on the display glass.
        const char* tabNames[]{"FX","MODE","SETUP"};const char* tabTips[]{"Show the five screen effects.","Choose Tape Start, Tape Stop, Brake, Stop > Start or Rewind.","Open the setup screen: engine, landing, curve, depth, filter, Sub Keep, grid, mix and sound version."};
        for(int i=0;i<3;++i){auto t=std::make_unique<ScreenToggle>(juce::String("PAGE ")+tabNames[i],normal,false,8.5f);t->label=tabNames[i];t->setClickingTogglesState(false);
            t->setBounds(322+i*46,105,43,18);t->setTooltip(tabTips[i]);t->onClick=[this,i]{if(i==2)showSetup(true);else showPage(i);};addAndMakeVisible(*t);tabs[size_t(i)]=std::move(t);}
        showPage(0);
        addAndMakeVisible(mode);mode.setBounds(78,494,79,21);mode.setExplicitFocusOrder(7);mode.setTooltip("Switch between milliseconds and tempo-synced note or bar lengths. Timing changes apply to the next launch.");
        modeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"syncMode",mode);mode.onClick=[this]{updateMode();};
        time.setBounds(168,474,320,42);division.setBounds(time.getBounds());fx.setBounds(168,529,320,42);
        timeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"spinUpMs",time);
        divisionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"syncDivision",division);
        fxAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"fxAmount",fx);
        time.textFromValueFunction=[](double v){return v<1000?juce::String(int(std::round(v)))+" ms":juce::String(v*.001,2)+" s";};
        time.valueFromTextFunction=[](const juce::String& t){auto v=t.getDoubleValue();return t.trim().endsWithIgnoreCase("ms")?v:t.trim().endsWithIgnoreCase("s")?v*1000:v;};
        fx.textFromValueFunction=[](double v){return juce::String(v, std::abs(v-std::round(v))<.001?0:1)+"%";};
        division.textFromValueFunction=[](double v){return juce::String(energy::divisions[juce::jlimit(0,5,int(std::round(v)))]);};
        division.valueFromTextFunction=[this](const juce::String& text){for(int i=0;i<6;++i)if(text.trim().equalsIgnoreCase(energy::divisions[i]))return double(i);return division.getValue();};
        division.setDoubleClickReturnValue(true,1);division.setTooltip("Tempo-synced length: 1/8, 1/4, 1/2, 1 bar, 2 bars or 4 bars, from the host tempo and time signature at the launch. Land On Grid overrides it.");
        time.updateText();division.updateText();fx.updateText();updateMode();
        time.setDoubleClickReturnValue(true,800);fx.setDoubleClickReturnValue(true,45);
        time.setTooltip("Length of the gesture. Changes while spinning apply to the next launch. Land On Grid overrides it.");
        fx.setTooltip("Intensity of every selected screen effect. Zero keeps the basic tape gesture.");
        outputLook.face=normal;addAndMakeVisible(output);output.setName("OUTPUT");output.setTitle("Output Level");
        output.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);output.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,22);
        output.setLookAndFeel(&outputLook);output.setBounds(115,598,88,90);output.setWantsKeyboardFocus(true);output.setExplicitFocusOrder(10);
        output.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        output.setMouseDragSensitivity(220);output.setVelocityBasedMode(false);output.setVelocityModeParameters(1,1,0,false);
        outputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"outputDb",output);
        output.textFromValueFunction=[](double value){return (value>0?juce::String("+"):juce::String())+juce::String(value,1)+" dB";};
        output.valueFromTextFunction=[](const juce::String& text){return text.getDoubleValue();};output.updateText();output.setDoubleClickReturnValue(true,0);
        output.setTooltip("Level of the effected voice. Clean audio keeps its original level. Shift-drag for fine adjustment; click the value to type dB. Double-click restores 0 dB.");
        // Preset strip on the title plate.
        for(auto* k:{&previous,&presetField,&next,&save})addAndMakeVisible(*k);
        presetField.field=true;presetField.setTooltip("Choose a factory or user preset.");previous.setTooltip("Previous preset");next.setTooltip("Next preset");
        save.setTooltip("Save the current settings as a user preset (choose or create a folder). Existing presets are never overwritten.");
        previous.setBounds(124,58,20,16);presetField.setBounds(148,58,234,16);next.setBounds(386,58,20,16);save.setBounds(412,58,46,16);
        previous.onClick=[this]{stepPreset(-1);};next.onClick=[this]{stepPreset(1);};presetField.onClick=[this]{showPresetMenu();};save.onClick=[this]{savePreset();};
        updatePresetLabel();
        addAndMakeVisible(info);info.setBounds(228,615,104,40);info.setExplicitFocusOrder(11);
        addChildComponent(setup);setup.setBounds(94,88,372,364);setup.doneKey().onClick=[this]{showSetup(false);};
        info.onClick=[this]{overlay.open(&info);};addChildComponent(overlay);overlay.setBounds(viewport.toNearestInt());
        tooltips.setColour(juce::TooltipWindow::backgroundColourId,ink);tooltips.setColour(juce::TooltipWindow::textColourId,pearl);
        tooltips.setColour(juce::TooltipWindow::outlineColourId,pink.withAlpha(.6f));
        lastTime=juce::Time::getMillisecondCounterHiRes()*.001;lastPublished=processor.publishedSamples.load();lastPublishTime=lastTime;startTimerHz(30);
        setSize(560,720);
    }
    ~HarvestCanvas() override {time.setLookAndFeel(nullptr);division.setLookAndFeel(nullptr);fx.setLookAndFeel(nullptr);output.setLookAndFeel(nullptr);}
    void configureSlider(juce::Slider& s,const char* name,int order) {
        s.setName(name);s.setTitle(name);s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight,false,74,30);s.setLookAndFeel(&look);
        s.setWantsKeyboardFocus(true);s.setExplicitFocusOrder(order);s.setVelocityBasedMode(false);
        addAndMakeVisible(s);
    }
    // Deterministic frame for tests and screenshots: settle the lights for the current processor state, then
    // run `seconds` of motion at 60 fps with a steady programme level.
    void preview(double seconds) {
        lights.reset();auto in=inputs(false);in.level=.35f;
        for(int i=0,n=36+int(std::round(seconds*60));i<n;++i)lights.advance(1./60,in);
        updatePresetLabel();repaint();
    }
    void step(double seconds){lights.advance(seconds,inputs(true));updatePresetLabel();repaint();}
    double measureAnimationPaint(){juce::Image probe(juce::Image::ARGB,560,720,true);juce::Graphics g(probe);const double start=juce::Time::getMillisecondCounterHiRes();
        drawLights(g);lights.paintRing(g,processor.visualProgress);return juce::Time::getMillisecondCounterHiRes()-start;}
    void showPage(int page){
        for(auto& b:effectButtons)b->setVisible(page==0);for(auto& b:modeButtons)b->setVisible(page==1);
        for(int i=0;i<2;++i)tabs[size_t(i)]->setToggleState(i==page,juce::dontSendNotification);currentPage=page;repaint();
    }
    void showSetup(bool open){setup.setVisible(open);tabs[2]->setToggleState(open,juce::dontSendNotification);harvest.setVisible(!open);if(open)setup.toFront(false);repaint();}
    void mouseDown(const juce::MouseEvent& event) override {
        if(!event.mods.isPopupMenu())return;
        juce::PopupMenu m;m.addItem(1,"Reduced Motion",true,processor.reducedMotion.load());m.addSeparator();
        const auto area=availableArea();
        for(int v:EnergyHarvesterProcessor::uiScales)m.addItem(v,"Size "+juce::String(v)+"%",fits(v,area),processor.uiScale==v);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe=juce::Component::SafePointer<HarvestCanvas>(this)](int choice){
            if(!safe)return;if(choice==1)safe->processor.reducedMotion=!safe->processor.reducedMotion.load();
            if(std::find(EnergyHarvesterProcessor::uiScales.begin(),EnergyHarvesterProcessor::uiScales.end(),choice)!=EnergyHarvesterProcessor::uiScales.end()){
                safe->processor.uiScale=choice;safe->editor.setSize(viewWidth*choice/100,viewHeight*choice/100);}
            // Size and Reduced Motion are saved with the project; let the host know the session changed.
            if(choice!=0)safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
        });
    }
    static bool fits(int scale,juce::Rectangle<int> area){return area.isEmpty()||(viewWidth*scale/100<=area.getWidth()&&viewHeight*scale/100+60<=area.getHeight());}
    juce::Rectangle<int> availableArea() const {
        const auto& displays=juce::Desktop::getInstance().getDisplays();
        if(isShowing())if(auto* d=displays.getDisplayForRect(getScreenBounds()))return d->userBounds.toNearestInt();
        if(auto* d=displays.getPrimaryDisplay())return d->userBounds.toNearestInt();
        return {};
    }
    void paint(juce::Graphics& g) override {
        paintShell(g);
        g.setColour(juce::Colour(0xff16021a).withAlpha(.22f*(1-lights.charge())));g.fillRect(viewport);
        drawLights(g);
        g.setColour(pearl);g.setFont(vfd::font(italic,25));g.drawText("ENERGY HARVESTER",161,26,290,27,juce::Justification::centredLeft);
        for(int i=0;i<3;++i){const float x=126.f+i*9;juce::Path p;p.startNewSubPath(x,28);p.lineTo(x+6,28);p.lineTo(x-4,49);p.lineTo(x-10,49);p.closeSubPath();g.fillPath(p);}
        const auto shown=inputs(false);const int state=shown.state;const bool bypass=shown.bypassed;
        const int modeIndex=juce::roundToInt(processor.apvts.getRawParameterValue("mode")->load());
        const int voiceMode=state==0?modeIndex:processor.visualMode.load();
        const juce::String status=bypass?"BYPASSED":modeVerb(voiceMode,state);
        g.setColour(pearl);g.setFont(vfd::font(italic,21));g.drawText(status,120,101,200,26,juce::Justification::centredLeft);
        g.setFont(vfd::font(normal,10));
        const bool sync=mode.getToggleState();const auto raw=[this](const char* id){return processor.apvts.getRawParameterValue(id)->load();};
        const char* modeNames[]{"TAPE START","TAPE STOP","BRAKE","STOP > START","REWIND"};
        juce::String sub=modeNames[juce::jlimit(0,4,modeIndex)];
        const int grid=juce::roundToInt(raw("gridLand"));
        if(state==2&&!bypass)sub=juce::roundToInt(raw("landing"))==2?"HOLDING AT SPEED":juce::roundToInt(raw("landing"))==1?"CLEAN AUDIO + TAIL":"CLEAN AUDIO";
        else if(state==4)sub="SILENT UNTIL HARVEST IS OFF";
        else if(bypass)sub="CLEAN AUDIO";
        else if(grid>0){const char* g2[]{"","BEAT","BAR","2 BARS"};sub+="  /  LANDS ON THE NEXT "+juce::String(g2[grid]);}
        else if(sync)sub+="  /  "+juce::String(processor.hostBpm.load(),1)+" BPM"+(processor.hasHostTempo.load()?" SYNC":" FALLBACK");
        if(raw("latestSound")<.5f)sub+="  /  0.3.2 SOUND";
        g.drawText(sub,121,128,330,15,juce::Justification::centredLeft);
        const float progress=shown.progress;
        g.setColour(pink.withAlpha(.23f));g.fillRect(121.f,145.f,314.f,1.5f);g.setColour(pearl);g.fillRect(121.f,145.f,314.f*progress,1.5f);
        if(!setup.isVisible())lights.paintRing(g,progress);
        g.setFont(vfd::font(normal,11.f));
        g.setColour(juce::Colour(0xff260a1b));g.drawText("OUTPUT",115,586,88,16,juce::Justification::centred);
        g.setColour(pearl);g.drawText("OUTPUT",115,585,88,16,juce::Justification::centred);
        g.setFont(vfd::font(normal,12.f));g.setColour(ink);g.drawText(modeIndex==0?"SPIN-UP":"LENGTH",80,473,90,20,juce::Justification::centredLeft);
        g.setFont(vfd::font(normal,14.5f));g.drawText("FX AMOUNT",80,536,90,27,juce::Justification::centredLeft);
    }
private:
    void updateMode(){const bool sync=mode.getToggleState();time.setVisible(!sync);division.setVisible(sync);repaint();}
    // Preset strip -----------------------------------------------------------------------------------------
    std::vector<energy::presets::Preset> allPresets() const {
        auto list=energy::presets::factory();
        for(auto& p:energy::presets::userPresets(energy::presets::userRoot()))list.push_back(p);
        return list;
    }
    void updatePresetLabel(){
        const auto [folder,name]=processor.presetLabel();
        const juce::String text=folder.isEmpty()?name:folder+"  /  "+name;
        if(text!=presetField.label){presetField.label=text;presetField.setTitle("Preset: "+text);presetField.repaint();}
    }
    void load(const energy::presets::Preset& p){processor.applyPreset(p.values,p.folder,p.name);updatePresetLabel();updateMode();repaint();}
    void stepPreset(int direction){
        const auto list=allPresets();if(list.empty())return;
        const auto [folder,name]=processor.presetLabel();int index=-1;
        for(size_t i=0;i<list.size();++i)if(list[i].folder==folder&&list[i].name==name)index=int(i);
        index=index<0?(direction>0?0:int(list.size())-1):(index+direction+int(list.size()))%int(list.size());
        load(list[size_t(index)]);
    }
    void showPresetMenu(){
        menuPresets=allPresets();juce::PopupMenu menu,user;std::map<juce::String,juce::PopupMenu> folders,userFolders;juce::StringArray order,userOrder;
        const auto [currentFolder,currentName]=processor.presetLabel();
        for(size_t i=0;i<menuPresets.size();++i){const auto& p=menuPresets[i];auto& target=p.factory?folders[p.folder]:userFolders[p.folder];
            (p.factory?order:userOrder).addIfNotAlreadyThere(p.folder);target.addItem(int(i)+1,p.name,true,p.folder==currentFolder&&p.name==currentName);}
        menu.addSectionHeader("FACTORY");for(auto& f:order)menu.addSubMenu(f,folders[f]);
        menu.addSectionHeader("USER");
        if(userOrder.isEmpty())menu.addItem(-3,"No user presets yet",false);
        for(auto& f:userOrder)menu.addSubMenu(f,userFolders[f]);
        menu.addSeparator();menu.addItem(-1,"Save Preset...");menu.addItem(-2,"Show User Presets Folder",energy::presets::userRoot().isDirectory());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetField),[safe=juce::Component::SafePointer<HarvestCanvas>(this)](int choice){
            if(!safe)return;
            if(choice>0&&size_t(choice)<=safe->menuPresets.size())safe->load(safe->menuPresets[size_t(choice-1)]);
            if(choice==-1)safe->savePreset();
            if(choice==-2)energy::presets::userRoot().revealToUser();
        });
    }
    void savePreset(){
        auto root=energy::presets::userRoot();auto folders=energy::presets::userFolders(root);
        for(auto& f:energy::presets::factoryFolders())folders.addIfNotAlreadyThere(f);
        const auto [folder,name]=processor.presetLabel();
        dialog=std::make_unique<juce::AlertWindow>("Save Preset","Save the current settings as a user preset. Choose a folder or type a new one; existing presets are never overwritten.",juce::MessageBoxIconType::NoIcon,this);
        dialog->addTextEditor("name",name=="Default"||name=="Project"?juce::String("My Preset"):name,"Name");
        dialog->addComboBox("folder",folders,"Folder");
        if(auto* box=dialog->getComboBoxComponent("folder")){box->setEditableText(true);box->setText(folder.isNotEmpty()?folder:juce::String("My Presets"),juce::dontSendNotification);}
        dialog->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<HarvestCanvas>(this)](int result){
            if(!safe||!safe->dialog)return;
            auto window=std::move(safe->dialog);
            if(result!=1)return;
            const auto presetName=window->getTextEditorContents("name");
            const auto folderName=window->getComboBoxComponent("folder")!=nullptr?window->getComboBoxComponent("folder")->getText():juce::String();
            juce::File written;const auto saved=energy::presets::save(energy::presets::userRoot(),folderName,presetName,safe->processor.currentValues(),written);
            if(saved.failed()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preset not saved",saved.getErrorMessage());return;}
            safe->processor.setPresetLabel(written.getParentDirectory().getFileName(),written.getFileNameWithoutExtension());
            safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
            safe->updatePresetLabel();
        }),false);
    }
    void paintShell(juce::Graphics& g) {
        // Resample the full-resolution shell once per screen density and zoom, then blit it: sharp on Retina
        // and at 125%, and cheap at 60 fps.
        const float scale=g.getInternalContext().getPhysicalPixelScaleFactor();
        const int w=juce::jmax(1,juce::roundToInt(560*scale)),h=juce::jmax(1,juce::roundToInt(720*scale));
        if(shellCache.getWidth()!=w||shellCache.getHeight()!=h){
            shellCache=juce::Image(juce::Image::RGB,w,h,false);juce::Graphics cache(shellCache);
            cache.setImageResamplingQuality(juce::Graphics::highResamplingQuality);cache.drawImage(shellSource,juce::Rectangle<float>(0,0,float(w),float(h)));
        }
        g.drawImage(shellCache,juce::Rectangle<float>(0,0,560,720));
    }
    void drawLights(juce::Graphics& g) {
        // Board light lives in the PCB under the resin: clip away the socket and the glass and resin panels.
        juce::Graphics::ScopedSaveState state(g);
        juce::Path board;board.setUsingNonZeroWinding(false);board.addRectangle(viewport);
        const float r=energy::ui::HarvestLights::lipRadius+2;
        board.addEllipse(energy::ui::HarvestLights::centreX-r,energy::ui::HarvestLights::centreY-r,r*2,r*2);
        g.reduceClipRegion(board);
        for(auto rect:{juce::Rectangle<int>(108,25,359,51),{94,88,372,110},{63,459,433,124},{218,606,125,57},{111,583,96,106}})g.excludeClipRegion(rect);
        lights.paintBoard(g);
    }
    energy::ui::LightInputs inputs(bool consumeLevel) {
        energy::ui::LightInputs in;
        in.gate=processor.visualGate;in.state=processor.visualState;in.progress=processor.visualProgress;in.ratio=processor.visualRatio;
        in.bypassed=processor.visualBypass;in.reducedMotion=processor.reducedMotion;in.dimmed=overlay.isVisible();
        if(consumeLevel)in.level=processor.visualLevel.exchange(0.f,std::memory_order_relaxed);
        // When the host isn't running audio, nothing is published: follow the HARVEST switch itself so the
        // screen and lights still answer the click.
        if(audioStalled){in.gate=processor.apvts.getRawParameterValue("harvest")->load()>=.5f;in.state=in.gate?2:0;in.progress=in.gate?1.f:0.f;in.ratio=1;in.bypassed=false;}
        return in;
    }
    void timerCallback() override {
        if(division.isVisible()!=mode.getToggleState())updateMode();
        const auto now=juce::Time::getMillisecondCounterHiRes()*.001;const double dt=std::min(.1,now-lastTime);lastTime=now;
        const auto published=processor.publishedSamples.load(std::memory_order_relaxed);
        if(published!=lastPublished){lastPublished=published;lastPublishTime=now;}
        audioStalled=now-lastPublishTime>.25;
        if(!isShowing()&&getPeer()!=nullptr){if(getTimerInterval()!=250)startTimer(250);return;}
        updatePresetLabel();
        const auto in=inputs(true);
        const float previousCharge=lights.charge();
        lights.advance(dt,in);
        const bool animated=lights.needsFrames();
        const int interval=animated?16:100;if(getTimerInterval()!=interval)startTimer(interval);
        const int modeNow=juce::roundToInt(processor.apvts.getRawParameterValue("mode")->load());
        const bool changed=in.state!=lastState||std::abs(in.progress-lastProgress)>.00001f||in.reducedMotion!=lastReduced
            ||in.bypassed!=lastBypassed||in.gate!=lastGate||std::abs(lights.charge()-previousCharge)>.00001f||modeNow!=lastMode;
        if(animated||changed)repaint();
        lastState=in.state;lastProgress=in.progress;lastReduced=in.reducedMotion;lastBypassed=in.bypassed;lastGate=in.gate;lastMode=modeNow;
    }
    EnergyHarvesterProcessor& processor;EnergyHarvesterEditor& editor;
    juce::Typeface::Ptr normal,italic;juce::Image shellSource,shellCache;FaderLook look;OutputLook outputLook;
    HarvestButton harvest;ScreenToggle mode;juce::Slider time,division,fx;OutputSlider output;
    std::array<std::unique_ptr<ScreenToggle>,5> effectButtons,modeButtons;std::array<std::unique_ptr<ScreenToggle>,3> tabs;std::unique_ptr<ChoiceRow> modeRow;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,5> effectAttachments;vfd::InfoButton info;vfd::InfoOverlay overlay;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment,modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment,divisionAttachment,fxAttachment,outputAttachment;
    PlateKey previous{"PREVIOUS PRESET",normal,"<"},presetField{"PRESET",normal,"Default"},next{"NEXT PRESET",normal,">"},save{"SAVE PRESET",normal,"SAVE"};
    SetupScreen setup;std::unique_ptr<juce::AlertWindow> dialog;std::vector<energy::presets::Preset> menuPresets;
    energy::ui::HarvestLights lights;juce::TooltipWindow tooltips{this,600};
    double lastTime=0,lastPublishTime=0;std::uint64_t lastPublished=0;
    int lastState=-1,lastMode=-1,currentPage=0;float lastProgress=-1;bool lastReduced=false,lastBypassed=false,lastGate=false,audioStalled=false;
    friend class EnergyHarvesterEditor;
};
EnergyHarvesterEditor::EnergyHarvesterEditor(EnergyHarvesterProcessor& p):AudioProcessorEditor(p),canvas(std::make_unique<HarvestCanvas>(p,*this)) {
    setOpaque(true);addAndMakeVisible(*canvas);setResizable(true,false);
    setResizeLimits(viewWidth*90/100,viewHeight*90/100,viewWidth*150/100,viewHeight*150/100);
    if(auto* c=getConstrainer())c->setFixedAspectRatio(double(viewWidth)/viewHeight);
    // Open at the saved size, or the largest size that fits this display.
    int scale=p.uiScale;const auto area=canvas->availableArea();
    if(!HarvestCanvas::fits(scale,area)){scale=EnergyHarvesterProcessor::uiScales.front();for(int v:EnergyHarvesterProcessor::uiScales)if(HarvestCanvas::fits(v,area))scale=v;}
    setSize(viewWidth*scale/100,viewHeight*scale/100);
}
EnergyHarvesterEditor::~EnergyHarvesterEditor()=default;
void EnergyHarvesterEditor::resized(){if(canvas){canvas->setBounds(0,0,560,720);
    canvas->setTransform(juce::AffineTransform::translation(-viewport.getX(),-viewport.getY()).scaled(float(getWidth())/viewWidth));}}
void EnergyHarvesterEditor::previewTime(double t){canvas->preview(t);}
void EnergyHarvesterEditor::stepAnimationForTesting(double seconds){canvas->step(seconds);}
double EnergyHarvesterEditor::measureAnimationPaint(){return canvas->measureAnimationPaint();}
void EnergyHarvesterEditor::showPageForTesting(int page){if(page==2)canvas->showSetup(true);else{canvas->showSetup(false);canvas->showPage(page);}}
