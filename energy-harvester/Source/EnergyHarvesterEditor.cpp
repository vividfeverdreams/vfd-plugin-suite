#include "EnergyHarvesterEditor.h"
#include "BinaryData.h"
#include "VfdInfo.h"
#include "Timing.h"
#include "Ui/HarvestLights.h"

namespace {
juce::Colour pink(0xffff52be), pearl(0xffffedf8), ink(0xff39102e);
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
        setOpaque(true);shellSource=juce::ImageCache::getFromMemory(BinaryData::DeviceShellfullbleed_png,BinaryData::DeviceShellfullbleed_pngSize);
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
        division.valueFromTextFunction=[this](const juce::String& text){for(int i=0;i<6;++i)if(text.trim().equalsIgnoreCase(energy::divisions[i]))return double(i);return division.getValue();};
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
        repaint();
    }
    void step(double seconds){lights.advance(seconds,inputs(true));repaint();}
    double measureAnimationPaint(){juce::Image probe(juce::Image::ARGB,560,720,true);juce::Graphics g(probe);const double start=juce::Time::getMillisecondCounterHiRes();
        drawLights(g);lights.paintRing(g,processor.visualProgress);return juce::Time::getMillisecondCounterHiRes()-start;}
    void mouseDown(const juce::MouseEvent& event) override {
        if(!event.mods.isPopupMenu())return;
        juce::PopupMenu m;m.addItem(1,"Reduced Motion",true,processor.reducedMotion.load());m.addSeparator();
        for(int v:{90,100,125})m.addItem(v,juce::String(v)+"% size",true,processor.uiScale==v);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe=juce::Component::SafePointer<HarvestCanvas>(this)](int choice){
            if(!safe)return;if(choice==1)safe->processor.reducedMotion=!safe->processor.reducedMotion.load();
            if(choice==90||choice==100||choice==125){safe->processor.uiScale=choice;safe->editor.setSize(viewWidth*choice/100,viewHeight*choice/100);}
            // Size and Reduced Motion are saved with the project; let the host know the session changed.
            if(choice!=0)safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
        });
    }
    void paint(juce::Graphics& g) override {
        paintShell(g);
        g.setColour(juce::Colour(0xff16021a).withAlpha(.22f*(1-lights.charge())));g.fillRect(viewport);
        drawLights(g);
        g.setColour(pearl);g.setFont(vfd::font(italic,25));g.drawText("ENERGY HARVESTER",161,28,290,27,juce::Justification::centredLeft);
        for(int i=0;i<3;++i){const float x=126.f+i*9;juce::Path p;p.startNewSubPath(x,30);p.lineTo(x+6,30);p.lineTo(x-4,51);p.lineTo(x-10,51);p.closeSubPath();g.fillPath(p);}
        g.setFont(vfd::font(normal,10.5f));g.drawText("M A G N E T I C   T A P E   S T A R T",156,57,303,17,juce::Justification::centred);
        const auto shown=inputs(false);const int state=shown.state;const bool bypass=shown.bypassed;
        const juce::String status=bypass?"BYPASSED":state==0?"READY":state==1?"SPINNING":state==2?"AT SPEED":"RESETTING";
        g.setColour(pearl);g.setFont(vfd::font(italic,23));g.drawText(status,120,103,238,30,juce::Justification::centredLeft);
        g.drawText(shown.gate?"ON":"OFF",379,103,62,30,juce::Justification::centredRight);
        g.setFont(vfd::font(normal,10));
        const bool sync=mode.getToggleState();
        const juce::String sub=state==2||bypass?"CLEAN AUDIO":sync?juce::String(processor.hostBpm.load(),1)+" BPM"+(processor.hasHostTempo.load()?"  /  SYNC":"  /  FALLBACK"):"SELECT YOUR FX";
        g.drawText(sub,121,128,316,16,juce::Justification::centredLeft);
        const float progress=shown.progress;
        g.setColour(pink.withAlpha(.23f));g.fillRect(121.f,143.f,314.f,1.5f);g.setColour(pearl);g.fillRect(121.f,143.f,314.f*progress,1.5f);
        lights.paintRing(g,progress);
        g.setFont(vfd::font(normal,11.f));
        g.setColour(juce::Colour(0xff260a1b));g.drawText("OUTPUT",115,586,88,16,juce::Justification::centred);
        g.setColour(pearl);g.drawText("OUTPUT",115,585,88,16,juce::Justification::centred);
        g.setFont(vfd::font(normal,12.f));g.setColour(ink);g.drawText("SPIN-UP",80,473,90,20,juce::Justification::centredLeft);
        g.setFont(vfd::font(normal,14.5f));g.drawText("FX AMOUNT",80,536,90,27,juce::Justification::centredLeft);

    }
private:
    void updateMode(){const bool sync=mode.getToggleState();time.setVisible(!sync);division.setVisible(sync);repaint();}
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
        in.gate=processor.visualGate;in.state=processor.visualState;in.progress=processor.visualProgress;
        in.bypassed=processor.visualBypass;in.reducedMotion=processor.reducedMotion;in.dimmed=overlay.isVisible();
        if(consumeLevel)in.level=processor.visualLevel.exchange(0.f,std::memory_order_relaxed);
        // When the host isn't running audio, nothing is published: follow the HARVEST switch itself so the
        // screen and lights still answer the click.
        if(audioStalled){in.gate=processor.apvts.getRawParameterValue("harvest")->load()>=.5f;in.state=in.gate?2:0;in.progress=in.gate?1.f:0.f;in.bypassed=false;}
        return in;
    }
    void timerCallback() override {
        if(division.isVisible()!=mode.getToggleState())updateMode();
        const auto now=juce::Time::getMillisecondCounterHiRes()*.001;const double dt=std::min(.1,now-lastTime);lastTime=now;
        const auto published=processor.publishedSamples.load(std::memory_order_relaxed);
        if(published!=lastPublished){lastPublished=published;lastPublishTime=now;}
        audioStalled=now-lastPublishTime>.25;
        if(!isShowing()&&getPeer()!=nullptr){if(getTimerInterval()!=250)startTimer(250);return;}
        const auto in=inputs(true);
        const float previousCharge=lights.charge();
        lights.advance(dt,in);
        const bool animated=lights.needsFrames();
        const int interval=animated?16:100;if(getTimerInterval()!=interval)startTimer(interval);
        const bool changed=in.state!=lastState||std::abs(in.progress-lastProgress)>.00001f||in.reducedMotion!=lastReduced
            ||in.bypassed!=lastBypassed||in.gate!=lastGate||std::abs(lights.charge()-previousCharge)>.00001f;
        if(animated||changed)repaint();
        lastState=in.state;lastProgress=in.progress;lastReduced=in.reducedMotion;lastBypassed=in.bypassed;lastGate=in.gate;
    }
    EnergyHarvesterProcessor& processor;EnergyHarvesterEditor& editor;
    juce::Typeface::Ptr normal,italic;juce::Image shellSource,shellCache;FaderLook look;OutputLook outputLook;
    HarvestButton harvest;ScreenToggle mode;juce::Slider time,division,fx;OutputSlider output;
    std::array<std::unique_ptr<ScreenToggle>,5> effectButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,5> effectAttachments;vfd::InfoButton info;vfd::InfoOverlay overlay;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment,modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment,divisionAttachment,fxAttachment,outputAttachment;
    energy::ui::HarvestLights lights;juce::TooltipWindow tooltips{this,600};
    double lastTime=0,lastPublishTime=0;std::uint64_t lastPublished=0;
    int lastState=-1;float lastProgress=-1;bool lastReduced=false,lastBypassed=false,lastGate=false,audioStalled=false;
};
EnergyHarvesterEditor::EnergyHarvesterEditor(EnergyHarvesterProcessor& p):AudioProcessorEditor(p),canvas(std::make_unique<HarvestCanvas>(p,*this)) {
    setOpaque(true);addAndMakeVisible(*canvas);setResizable(true,false);
    setResizeLimits(viewWidth*90/100,viewHeight*90/100,viewWidth*125/100,viewHeight*125/100);
    if(auto* c=getConstrainer())c->setFixedAspectRatio(double(viewWidth)/viewHeight);
    setSize(viewWidth*p.uiScale/100,viewHeight*p.uiScale/100);
}
EnergyHarvesterEditor::~EnergyHarvesterEditor()=default;
void EnergyHarvesterEditor::resized(){if(canvas){canvas->setBounds(0,0,560,720);
    canvas->setTransform(juce::AffineTransform::translation(-viewport.getX(),-viewport.getY()).scaled(float(getWidth())/viewWidth));}}
void EnergyHarvesterEditor::previewTime(double t){canvas->preview(t);}
void EnergyHarvesterEditor::stepAnimationForTesting(double seconds){canvas->step(seconds);}
double EnergyHarvesterEditor::measureAnimationPaint(){return canvas->measureAnimationPaint();}
