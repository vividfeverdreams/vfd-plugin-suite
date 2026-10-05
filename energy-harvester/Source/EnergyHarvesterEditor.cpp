#include "EnergyHarvesterEditor.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include "Timing.h"

namespace {
juce::Colour pink(0xffff52be), pearl(0xffffedf8), ink(0xff39102e);
vfd::Theme theme() {return {juce::Colour(0xff280a22),pearl,pink,juce::Colour(0xff764061),juce::Colour(0xffd7abc5),pearl,
    juce::Colour(0xffc7a4b8),ink,juce::Colour(0xff4b163c)};}
float periodic(float u,float center,float sharp=15) {return std::exp(sharp*(std::cos(juce::MathConstants<float>::twoPi*(u-center))-1));}
class HarvestButton final:public juce::Button {
public:
    explicit HarvestButton(juce::Typeface::Ptr f):Button("HARVEST"),face(f) {
        setClickingTogglesState(true);setWantsKeyboardFocus(true);setTitle("Harvest");
        setDescription("Launch one spin-up when switched on. Switch off to rearm.");setTooltip(getDescription());
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
class ScreenToggle final:public juce::Button {
public:
    ScreenToggle(const juce::String& name,juce::Typeface::Ptr f,bool mode=false):Button(name),face(f),isMode(mode){setClickingTogglesState(true);setWantsKeyboardFocus(true);setTitle(name);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
    void paintButton(juce::Graphics& g,bool hover,bool) override {
        auto b=getLocalBounds().toFloat().reduced(1);const bool on=getToggleState();
        if(isMode){
            g.setColour(ink.withAlpha(.88f));g.fillRoundedRectangle(b,5);
            auto selected=b.withWidth(b.getWidth()*.5f);if(on)selected=selected.translated(selected.getWidth(),0);
            g.setColour(pearl);g.fillRoundedRectangle(selected.reduced(1.5f),3.5f);
            g.setFont(vfd::font(face,9.5f));g.setColour(on?pearl:ink);g.drawText("TIME",1,0,getWidth()/2,getHeight(),juce::Justification::centred);
            g.setColour(on?ink:pearl);g.drawText("SYNC",getWidth()/2,0,getWidth()/2,getHeight(),juce::Justification::centred);
        }else{
            g.setColour(on?pink.withAlpha(.23f):juce::Colour(0xff1f1427));g.fillRoundedRectangle(b,4);
            g.setColour(on?pink:pearl.withAlpha(hover?.65f:.23f));g.drawRoundedRectangle(b,4,1);
            g.setFont(vfd::font(face,9.5f));g.setColour(on?pearl:pearl.withAlpha(.66f));g.drawText(getName(),getLocalBounds().reduced(2),juce::Justification::centred);
            if(on){g.setColour(pink);g.fillRoundedRectangle(b.getX()+9,b.getBottom()-3,b.getWidth()-18,1.5f,.75f);}
        }
        if(hasKeyboardFocus(true)){g.setColour(pearl);g.drawRoundedRectangle(b,4,1.5f);}
    }
private:juce::Typeface::Ptr face;bool isMode;
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

}

class HarvestCanvas final:public juce::Component,private juce::Timer {
public:
    HarvestCanvas(EnergyHarvesterProcessor& p,EnergyHarvesterEditor& e):processor(p),editor(e),
      normal(juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchSemiBold_ttf,BinaryData::ChakraPetchSemiBold_ttfSize)),
      italic(juce::Typeface::createSystemTypefaceFor(BinaryData::ChakraPetchBoldItalic_ttf,BinaryData::ChakraPetchBoldItalic_ttfSize)),
      harvest(italic),mode("Time / Sync",normal,true),info(theme(),normal),overlay("ENERGY HARVESTER",theme(),BinaryData::VfdLogo_svg,BinaryData::VfdLogo_svgSize,normal) {
        setOpaque(false);shell=juce::ImageCache::getFromMemory(BinaryData::DeviceShell_png,BinaryData::DeviceShell_pngSize).rescaled(560,720,juce::Graphics::highResamplingQuality);
        look.face=normal;addAndMakeVisible(harvest);harvest.setBounds(160,210,240,240);harvest.setExplicitFocusOrder(6);
        buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"harvest",harvest);
        configureSlider(time,"SPIN-UP TIME",8);configureSlider(division,"SPIN-UP DIVISION",8);configureSlider(fx,"FX AMOUNT",9);
        const char* names[]{"PANNING","CHORUS","PHASER","FLANGER","WASHOUT"};const char* ids[]{"fxPan","fxChorus","fxPhaser","fxFlanger","fxWashout"};
        for(int i=0;i<5;++i){auto b=std::make_unique<ScreenToggle>(names[i],normal);b->setBounds(120+i*64,148,60,28);b->setExplicitFocusOrder(i+1);b->setTooltip(juce::String("Include ")+names[i]+" in the spin-up. FX Amount controls every selected effect.");addAndMakeVisible(*b);effectAttachments[size_t(i)]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,ids[i],*b);effectButtons[size_t(i)]=std::move(b);}
        addAndMakeVisible(mode);mode.setBounds(78,494,79,21);mode.setExplicitFocusOrder(7);mode.setTooltip("Switch between milliseconds and tempo-synced note or bar lengths. Timing changes apply to the next spin-up.");
        modeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"syncMode",mode);mode.onClick=[this]{updateMode();};
        time.setBounds(168,474,320,42);division.setBounds(time.getBounds());fx.setBounds(168,529,320,42);
        timeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"spinUpMs",time);
        divisionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"syncDivision",division);
        fxAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"fxAmount",fx);
        time.textFromValueFunction=[](double v){return v<1000?juce::String(int(std::round(v)))+" ms":juce::String(v*.001,2)+" s";};
        time.valueFromTextFunction=[](const juce::String& t){auto v=t.getDoubleValue();return t.trim().endsWithIgnoreCase("ms")?v:t.trim().endsWithIgnoreCase("s")?v*1000:v;};
        fx.textFromValueFunction=[](double v){return juce::String(v, std::abs(v-std::round(v))<.001?0:1)+"%";};
        division.textFromValueFunction=[](double v){return juce::String(energy::divisions[juce::jlimit(0,5,int(std::round(v)))]);};
        division.valueFromTextFunction=[](const juce::String& text){for(int i=0;i<6;++i)if(text.trim().equalsIgnoreCase(energy::divisions[i]))return double(i);return 1.;};
        division.setDoubleClickReturnValue(true,1);division.setTooltip("Tempo-synced duration: 1/8, 1/4, 1/2, 1 bar, 2 bars, or 4 bars. Uses the host tempo and time signature at the start.");
        time.updateText();division.updateText();fx.updateText();updateMode();
        time.setDoubleClickReturnValue(true,800);fx.setDoubleClickReturnValue(true,45);
        time.setTooltip("Time to clean audio. Changes while spinning apply to the next start.");
        fx.setTooltip("Intensity of every selected screen effect. Zero keeps the basic tape start.");
        outputLook.face=normal;addAndMakeVisible(output);output.setName("OUTPUT");output.setTitle("Output Level");
        output.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);output.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,22);
        output.setLookAndFeel(&outputLook);output.setBounds(115,598,88,90);output.setWantsKeyboardFocus(true);output.setExplicitFocusOrder(10);
        output.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        output.setMouseDragSensitivity(220);output.setVelocityBasedMode(false);output.setVelocityModeParameters(1,1,0,false);
        outputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"outputDb",output);
        output.textFromValueFunction=[](double value){return (value>0?juce::String("+"):juce::String())+juce::String(value,1)+" dB";};
        output.valueFromTextFunction=[](const juce::String& text){return text.getDoubleValue();};output.updateText();output.setDoubleClickReturnValue(true,0);
        output.setTooltip("Level of the harvested spin-up. Clean audio keeps its original level. Shift-drag for fine adjustment; click the value to type dB. Double-click restores 0 dB.");
        addAndMakeVisible(info);info.setBounds(228,615,104,40);info.setExplicitFocusOrder(11);
        info.onClick=[this]{overlay.open(&info);};addChildComponent(overlay);overlay.setBounds(0,0,560,720);
        buildPaths();lastTime=juce::Time::getMillisecondCounterHiRes()*.001;startTimerHz(30);
        setSize(560,720);
    }
    ~HarvestCanvas() override {time.setLookAndFeel(nullptr);division.setLookAndFeel(nullptr);fx.setLookAndFeel(nullptr);output.setLookAndFeel(nullptr);}
    void configureSlider(juce::Slider& s,const char* name,int order) {
        s.setName(name);s.setTitle(name);s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight,false,74,30);s.setLookAndFeel(&look);
        s.setWantsKeyboardFocus(true);s.setExplicitFocusOrder(order);s.setVelocityBasedMode(false);
        addAndMakeVisible(s);
    }
    void preview(double seconds) {preview_=seconds;envelope=processor.visualGate?1.f:0.f;repaint();}
    double measureAnimationPaint(){juce::Image probe(juce::Image::ARGB,560,720,true);juce::Graphics g(probe);const double start=juce::Time::getMillisecondCounterHiRes();drawLights(g);return juce::Time::getMillisecondCounterHiRes()-start;}
    void mouseDown(const juce::MouseEvent& event) override {
        if(!event.mods.isPopupMenu())return;
        juce::PopupMenu m;m.addItem(1,"Reduced Motion",true,processor.reducedMotion.load());m.addSeparator();
        for(int v:{90,100,125})m.addItem(v,juce::String(v)+"% size",true,processor.uiScale==v);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe=juce::Component::SafePointer<HarvestCanvas>(this)](int choice){
            if(!safe)return;if(choice==1)safe->processor.reducedMotion=!safe->processor.reducedMotion.load();
            if(choice==90||choice==100||choice==125){safe->processor.uiScale=choice;safe->editor.setSize(560*choice/100,720*choice/100);}
        });
    }
    void paint(juce::Graphics& g) override {
        g.drawImage(shell,getLocalBounds().toFloat());
        g.setColour(juce::Colour(0xff16021a).withAlpha(.22f*(1-envelope)));g.fillRoundedRectangle(23,10,514,690,42);
        drawLights(g);
        g.setColour(pearl);g.setFont(vfd::font(italic,25));g.drawText("ENERGY HARVESTER",161,28,290,27,juce::Justification::centredLeft);
        for(int i=0;i<3;++i){const float x=126.f+i*9;juce::Path p;p.startNewSubPath(x,30);p.lineTo(x+6,30);p.lineTo(x-4,51);p.lineTo(x-10,51);p.closeSubPath();g.fillPath(p);}
        g.setFont(vfd::font(normal,10.5f));g.drawText("M A G N E T I C   T A P E   S T A R T",156,57,303,17,juce::Justification::centred);
        const int state=processor.visualState;const bool bypass=processor.visualBypass;
        const juce::String status=bypass?"BYPASSED":state==0?"READY":state==1?"SPINNING":state==2?"AT SPEED":"RESETTING";
        g.setColour(pearl);g.setFont(vfd::font(italic,23));g.drawText(status,120,103,238,30,juce::Justification::centredLeft);
        g.drawText(processor.visualGate?"ON":"OFF",379,103,62,30,juce::Justification::centredRight);
        g.setFont(vfd::font(normal,10));
        const bool sync=mode.getToggleState();
        const juce::String sub=state==2||bypass?"CLEAN AUDIO":sync?juce::String(processor.hostBpm.load(),1)+" BPM"+(processor.hasHostTempo.load()?"  /  SYNC":"  /  FALLBACK"):"SELECT YOUR FX";
        g.drawText(sub,121,128,316,16,juce::Justification::centredLeft);
        const float progress=processor.visualProgress;
        g.setColour(pink.withAlpha(.23f));g.fillRect(121.f,143.f,314.f,1.5f);g.setColour(pearl);g.fillRect(121.f,143.f,314.f*progress,1.5f);
        for(int i=0;i<40;++i){const float a=-juce::MathConstants<float>::halfPi+i*juce::MathConstants<float>::twoPi/40;
            juce::Path seg;seg.addCentredArc(280,330,102,102,0,a+.025f,a+.125f,true);
            g.setColour(i<progress*40?pink.brighter(.4f):juce::Colour(0xff6d244c));g.strokePath(seg,juce::PathStrokeType(3));}
        g.setFont(vfd::font(normal,11.f));
        g.setColour(juce::Colour(0xff260a1b));g.drawText("OUTPUT",115,586,88,16,juce::Justification::centred);
        g.setColour(pearl);g.drawText("OUTPUT",115,585,88,16,juce::Justification::centred);
        g.setFont(vfd::font(normal,12.f));g.setColour(ink);g.drawText("SPIN-UP",80,473,90,20,juce::Justification::centredLeft);
        g.setFont(vfd::font(normal,14.5f));g.drawText("FX AMOUNT",80,536,90,27,juce::Justification::centredLeft);

    }
private:
    void updateMode(){const bool sync=mode.getToggleState();time.setVisible(!sync);division.setVisible(sync);repaint();}
    struct Trace {juce::Path path;float offset,length;juce::Image glow;juce::Rectangle<int> bounds;};std::vector<Trace> traces;
    void buildPaths() {
        const std::array<std::array<juce::Point<float>,5>,12> points{{
            {{{42,74},{43,173},{52,248},{108,280},{166,299}}},{{{55,188},{61,249},{112,284},{124,308},{163,323}}},
            {{{519,87},{510,160},{500,226},{449,258},{394,291}}},{{{506,248},{485,262},{461,266},{423,287},{395,306}}},
            {{{43,405},{62,360},{111,356},{124,375},{166,375}}},{{{45,461},{98,438},{128,419},{155,409},{173,387}}},
            {{{519,423},{478,413},{452,385},{434,359},{400,350}}},{{{514,335},{501,351},{471,342},{444,321},{401,323}}},
            {{{62,666},{88,626},{111,609},{110,594},{170,590}}},{{{490,665},{467,637},{451,610},{428,594},{392,590}}},
            {{{184,449},{154,435},{137,405},{153,385},{174,368}}},{{{392,447},{420,428},{439,401},{427,378},{393,363}}}
        }};
        int i=0;for(auto route:points){juce::Path path;path.startNewSubPath(route[0]);for(size_t j=1;j<route.size();++j)path.lineTo(route[j]);const float length=path.getLength();
            const auto bounds=path.getBounds().expanded(6).getSmallestIntegerContainer();juce::Image glow(juce::Image::ARGB,bounds.getWidth(),bounds.getHeight(),true);juce::Graphics g(glow);g.addTransform(juce::AffineTransform::translation(-float(bounds.getX()),-float(bounds.getY())));
            g.setColour(pink.withAlpha(.10f));g.strokePath(path,juce::PathStrokeType(8,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
            g.setColour(pink.withAlpha(.55f));g.strokePath(path,juce::PathStrokeType(2.5f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
            g.setColour(pearl.withAlpha(.72f));g.strokePath(path,juce::PathStrokeType(.65f));
            traces.push_back({path,float(i++)*.137f,length,glow,bounds});}
        chargeGlow=juce::Image(juce::Image::ARGB,32,32,true);juce::Graphics glowGraphics(chargeGlow);
        juce::ColourGradient glow(pearl,16,16,pink.withAlpha(0.f),16,0,true);glow.addColour(.16,pearl.withAlpha(.9f));glow.addColour(.30,pink.withAlpha(.75f));glow.addColour(.62,pink.withAlpha(.18f));glowGraphics.setGradientFill(glow);glowGraphics.fillEllipse(0,0,32,32);
    }
    void drawLights(juce::Graphics& g) {
        if(envelope<.002f)return;g.saveState();
        juce::Path allowed;allowed.addRoundedRectangle(23,10,514,690,42);g.reduceClipRegion(allowed);
        for(auto r:{juce::Rectangle<int>(108,25,359,51),{94,93,372,104},{177,229,206,202},{63,459,433,124},{218,606,125,57},{111,583,96,106}})g.excludeClipRegion(r);
        const bool still=processor.reducedMotion||processor.visualBypass||overlay.isVisible();
        const double t=preview_>=0?preview_:phase;const float u=still?.766667f:float(std::fmod(t,6.0)/6.0);
        const float level=envelope*(processor.visualBypass?.18f:overlay.isVisible()?.12f:1.f);
        const std::array<juce::Point<int>,6> nodes{{{94,283},{477,282},{61,429},{500,447},{124,628},{433,627}}};
        for(size_t i=0;i<nodes.size();++i){const float intensity=level*periodic(u,float(i)*.173f,20);if(intensity>.01f){g.setOpacity(intensity);g.drawImageAt(chargeGlow,nodes[i].x-16,nodes[i].y-16);}}g.setOpacity(1);
        for(size_t i=0;i<traces.size();++i){auto& trace=traces[i];const float q=std::fmod(u*(i%3==0?2:1)+trace.offset,1.f);
            const float pulse=.35f+.65f*periodic(u,trace.offset);const float intensity=level*pulse;
            g.setOpacity(intensity);g.drawImageAt(trace.glow,trace.bounds.getX(),trace.bounds.getY());g.setOpacity(1);
            const float headAlpha=level*std::pow(std::sin(juce::MathConstants<float>::pi*q),2);
            for(int j=7;j>=0;--j){float at=juce::jmax(0.f,q-j*.018f);auto dot=trace.path.getPointAlongPath(at*trace.length);
                float a=headAlpha*float(8-j)/8;g.setColour(pink.withAlpha(a*.17f));g.fillEllipse(dot.x-5,dot.y-5,10,10);
                g.setColour(pearl.withAlpha(a));g.fillEllipse(dot.x-1.3f,dot.y-1.3f,2.6f,2.6f);}
        }
        juce::Path halo;for(int j=0;j<=120;++j){float a=j*juce::MathConstants<float>::twoPi/120;
            float r=114.5f+.8f*std::sin(5*a)*std::sin(u*juce::MathConstants<float>::twoPi)+.5f*std::sin(9*a+.7f)*std::sin(u*juce::MathConstants<float>::twoPi*2);
            juce::Point<float> v(280+std::cos(a)*r,330+std::sin(a)*r);if(j==0)halo.startNewSubPath(v);else halo.lineTo(v);}
        g.setColour(pink.withAlpha(.12f*level));g.strokePath(halo,juce::PathStrokeType(7));
        g.setColour(pink.withAlpha(.72f*level));g.strokePath(halo,juce::PathStrokeType(2.2f));
        g.setColour(pearl.withAlpha(.75f*level));g.strokePath(halo,juce::PathStrokeType(.65f));g.restoreState();
    }
    void timerCallback() override {
        if(division.isVisible()!=mode.getToggleState())updateMode();
        const auto now=juce::Time::getMillisecondCounterHiRes()*.001;const double dt=std::min(.1,now-lastTime);lastTime=now;
        if(!isShowing()&&getPeer()!=nullptr){if(getTimerInterval()!=250)startTimer(250);return;}
        const bool animated=(processor.visualGate||envelope>.001f)&&!processor.reducedMotion&&!processor.visualBypass&&!overlay.isVisible();
        const int interval=animated?33:100;if(getTimerInterval()!=interval)startTimer(interval);
        const float previous=envelope;
        const float target=processor.visualGate?1.f:0.f;
        if(target!=fadeTarget){fadeFrom=envelope;fadeTarget=target;fadeElapsed=0;}
        const double duration=target>fadeFrom?.120:.240;fadeElapsed=std::min(duration,fadeElapsed+dt);
        const float a=float(fadeElapsed/duration);envelope=fadeFrom+(target-fadeFrom)*a*a*(3-2*a);
        if(envelope>.001f&&!processor.reducedMotion&&!processor.visualBypass&&!overlay.isVisible())phase+=dt;
        if(envelope==0)phase=0;
        const int state=processor.visualState;const float progress=processor.visualProgress;
        const bool reduced=processor.reducedMotion,bypassed=processor.visualBypass;
        if(animated||std::abs(envelope-previous)>.00001f||state!=lastState||std::abs(progress-lastProgress)>.00001f||reduced!=lastReduced||bypassed!=lastBypassed)repaint();
        lastState=state;lastProgress=progress;lastReduced=reduced;lastBypassed=bypassed;
    }
    EnergyHarvesterProcessor& processor;EnergyHarvesterEditor& editor;
    juce::Typeface::Ptr normal,italic;juce::Image shell,chargeGlow;FaderLook look;OutputLook outputLook;
    HarvestButton harvest;ScreenToggle mode;juce::Slider time,division,fx;OutputSlider output;
    std::array<std::unique_ptr<ScreenToggle>,5> effectButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,5> effectAttachments;vfd::InfoButton info;vfd::InfoOverlay overlay;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment,modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment,divisionAttachment,fxAttachment,outputAttachment;
    double phase=0,lastTime=0,fadeElapsed=0,preview_=-1;
    float envelope=0,fadeFrom=0,fadeTarget=0;
    int lastState=-1;float lastProgress=-1;bool lastReduced=false,lastBypassed=false;
};
EnergyHarvesterEditor::EnergyHarvesterEditor(EnergyHarvesterProcessor& p):AudioProcessorEditor(p),canvas(std::make_unique<HarvestCanvas>(p,*this)) {
    setOpaque(false);addAndMakeVisible(*canvas);setResizable(true,false);setResizeLimits(504,648,700,900);
    if(auto* c=getConstrainer())c->setFixedAspectRatio(560.0/720.0);
    setSize(560*p.uiScale/100,720*p.uiScale/100);
}
EnergyHarvesterEditor::~EnergyHarvesterEditor()=default;
void EnergyHarvesterEditor::resized(){if(canvas){canvas->setBounds(0,0,560,720);canvas->setTransform(juce::AffineTransform::scale(float(getWidth())/560));}}
void EnergyHarvesterEditor::previewTime(double t){canvas->preview(t);}
double EnergyHarvesterEditor::measureAnimationPaint(){return canvas->measureAnimationPaint();}
