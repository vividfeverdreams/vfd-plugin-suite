#include "Presets.h"
#include <cstdlib>

namespace energy::presets {
namespace {
// One row per factory preset. sync: -1 = TIME (uses ms), otherwise the SYNC division index
// (0 1/8, 1 1/4, 2 1/2, 3 1 bar, 4 2 bars, 5 4 bars). fx mask: 1 pan, 2 chorus, 4 phaser, 8 flanger, 16 washout.
// mode: 0 start, 1 stop, 2 brake, 3 stop>start, 4 rewind. filter: 0 LP12, 1 LP24, 2 BP. keep: 0 off, 1 60, 2 90,
// 3 120, 4 150 Hz. grid: 0 off, 1 1/4, 2 bar, 3 2 bars. landing: 0 clean, 1 tail, 2 hold. Band-pass presets carry a
// little output trim because the band-pass takes the sub (most of a bass track's level) out of the effected voice.
struct Row { const char* folder; const char* name; const char* note; int mode, engine; float ms; int sync; float curve, depth, fx; unsigned mask;
    int filter; float resonance, start; int keep, grid, landing; float mix, outputDb; };
constexpr Row rows[] {
    {"Tape Starts","Classic Spin-Up","The original magnetic start, refined: 800 ms, S-curve, light phaser.",0,0,800,-1,0,24,45,4,0,0,350,0,0,0,100,0},
    {"Tape Starts","Reel-to-Reel","True varispeed reel start with a soft resonant opening.",0,1,1200,-1,20,24,0,0,0,25,300,0,0,0,100,0},
    {"Tape Starts","Cassette Wake","Short, wobbly deck start from half speed.",0,1,600,-1,-30,12,35,2,0,0,500,0,0,0,100,0},
    {"Tape Starts","Slow Motor","Two-bar True Tape crawl from three octaves down into a ringing washout.",0,1,800,4,50,36,40,16,0,0,350,0,0,1,100,0},
    {"Tape Starts","Kick-Start","Eighth-note snap from an octave down; the sub stays put.",0,0,800,0,-50,12,0,0,0,0,350,1,0,0,100,0},
    {"Tape Starts","Deep Iron","One bar from four octaves down through a growling 24 dB filter.",0,0,800,3,30,48,30,8,1,30,120,0,0,0,100,0},
    {"Tape Stops","Power Cut","Plain motor power-down to silence.",1,0,800,-1,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Tape Stops","Dying Battery","A bar of fading motor: quick sag, long drag, resonant dusk.",1,0,800,3,-40,24,30,2,0,35,250,0,0,0,100,0},
    {"Tape Stops","Reel Run-Out","Holds speed, then the reel runs out into reverb.",1,0,2000,-1,40,24,50,16,0,0,350,0,0,0,100,0},
    {"Tape Stops","Half-Bar Stop","Half-note power-down for quick fills.",1,0,800,2,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Tape Stops","Sub-Safe Stop","The top end powers down while the sub keeps going.",1,0,800,3,0,24,30,4,0,0,350,2,0,0,100,0},
    {"Tape Stops","Washed Power-Down","Two bars down into a dark wash.",1,0,800,4,10,24,70,16,1,20,200,0,0,0,100,0},
    {"Brakes","Turntable Brake","Platter brake: constant deceleration to a stop.",2,0,600,-1,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Brakes","Hard Brake","Quarter-second stop for hard cuts.",2,0,250,-1,-50,24,0,0,0,0,350,0,0,0,100,0},
    {"Brakes","Slow Platter","A lazy hand on the platter, with phase.",2,0,1500,-1,30,24,25,4,0,0,350,0,0,0,100,0},
    {"Brakes","Vinyl Drag","Quarter-note drag with chorus wobble and a resonant edge.",2,0,800,1,0,24,40,2,0,20,450,0,0,0,100,0},
    {"Brakes","Brake & Keep","Brakes the top end while the sub holds steady.",2,0,700,-1,0,24,0,0,0,0,350,1,0,0,100,0},
    {"Rewinds","Selecta Rewind","Classic pull-back: brake, spin back, snap to the drop.",4,0,1200,-1,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Rewinds","Pull-Up","One-bar rewind with a hall tail after the snap.",4,0,800,3,-30,24,30,16,0,0,350,0,0,1,100,0},
    {"Rewinds","Quick Spin-Back","Half-second spin-back for tight edits.",4,0,500,-1,-60,24,0,0,0,0,350,0,0,0,100,0},
    {"Rewinds","Backspin Wash","Rewind into a washed-out tail.",4,0,1500,-1,0,24,60,16,0,0,350,0,0,1,100,0},
    {"Rewinds","Siren Rewind","Rewind through a resonant band-pass.",4,0,1000,-1,0,24,20,8,2,50,600,0,0,0,100,6},
    {"Power-Ups","Stop-Start Stutter","Half-note stop, then straight back up to speed.",3,0,800,2,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Power-Ups","Brownout","A bar-long sag and recovery with a resonant sweep.",3,0,800,3,20,24,25,4,0,30,300,0,0,0,100,0},
    {"Power-Ups","Generator Restart","Two-second stall and restart with a light wash.",3,0,2000,-1,0,24,30,16,0,0,350,0,0,0,100,0},
    {"Power-Ups","Hiccup","Eighth-note dropout and recovery.",3,0,800,0,0,24,0,0,0,0,350,0,0,0,100,0},
    {"Power-Ups","Reactor Power-Up","Two bars from four octaves down with phase, flange and a resonant 24 dB rise.",0,1,800,4,20,48,50,12,1,40,150,0,0,0,100,0},
    {"Risers","Harvest Riser","Four bars, slam-late curve, phaser and a resonant climb.",0,0,800,5,60,36,60,4,0,50,200,0,0,0,100,0},
    {"Risers","Resonant Climb","Two-bar True Tape rise through a singing 24 dB filter.",0,1,800,4,0,24,0,0,1,70,150,0,0,0,100,-1},
    {"Risers","Band Riser","Two bars of band-pass sweep with chorus.",0,0,800,4,10,24,40,2,2,60,300,0,0,0,100,6},
    {"Risers","Phase Ascension","A bar of deep phaser as the tape climbs.",0,0,800,3,20,24,85,4,0,0,350,0,0,0,100,0},
    {"Risers","Space Bass Lift","Two bars of flange and wash above a steady 120 Hz sub, ringing on after the drop.",0,1,800,4,0,24,50,24,0,0,350,3,0,1,100,0},
    {"Risers","Slow Dawn","Eight seconds of light, washed climb, mixed in parallel.",0,0,8000,-1,30,24,50,16,0,0,350,0,0,0,80,0},
    {"Drops","Drop Slam","Quarter-note slam: stays low, then hits speed on the drop.",0,0,800,1,100,24,0,0,0,0,350,0,0,0,100,0},
    {"Drops","Riddim Snap","Eighth-note True Tape snap from an octave down, sub held.",0,1,800,0,80,12,0,0,0,0,350,1,0,0,100,0},
    {"Drops","Half-Time Drop","Half-note start from an octave down.",0,0,800,2,0,12,20,4,0,0,350,0,0,0,100,0},
    {"Drops","Wobble Wake","Quarter-note start through a resonant, phased low-pass.",0,0,800,1,50,24,70,4,0,45,250,0,0,0,100,0},
    {"Drops","Sub Punch","300 ms True Tape punch above a steady 90 Hz sub.",0,1,300,-1,60,24,0,0,0,0,350,2,0,0,100,0},
    {"Drops","Drop & Hold","Lands at speed and keeps the phaser on until HARVEST is switched off.",0,0,800,1,30,24,60,4,0,0,350,0,0,2,100,0},
    {"Washouts","Cathedral Start","A bar-long start drowned in hall, ringing on after the drop.",0,0,800,3,0,24,90,16,0,0,350,0,0,1,100,0},
    {"Washouts","Reverb Dive","Power-down into a deep wash.",1,0,800,3,0,24,85,16,0,0,350,0,0,0,100,0},
    {"Washouts","Wash & Rewind","Rewind with a hall tail after the snap.",4,0,800,3,0,24,70,16,0,0,350,0,0,1,100,0},
    {"Washouts","Fog Machine","Two bars of chorus and hall, mixed in parallel.",0,0,800,4,20,24,60,18,0,0,350,0,0,1,85,0},
    {"Washouts","Ghost Tail","Short start with a long ghostly tail.",0,0,600,-1,0,24,75,16,0,0,350,0,0,1,100,0},
    {"Lo-Fi","Broken Walkman","Wobbly True Tape start through a narrow band-pass.",0,1,700,-1,-20,12,60,2,2,40,400,0,0,0,100,4},
    {"Lo-Fi","Radio Tune-In","A bar of radio band-pass sweep with flange.",0,0,800,3,0,24,30,8,2,70,500,0,0,0,100,6},
    {"Lo-Fi","Worn Cassette","Chorus, flange and a soft resonant edge, in parallel.",0,1,1000,-1,0,24,50,10,0,25,600,0,0,0,85,0},
    {"Lo-Fi","Bitter Tape Stop","Band-passed power-down with chorus.",1,0,700,-1,0,24,30,2,2,50,600,0,0,0,100,6},
    {"Lo-Fi","Dusty Rewind","Band-passed rewind with gentle panning.",4,0,800,-1,0,24,40,1,2,30,450,0,0,0,100,6},
    {"Grid-Locked","Land on the Bar","Classic start that always lands on the next bar line.",0,0,800,-1,0,24,45,4,0,0,350,0,2,0,100,0},
    {"Grid-Locked","Quarter Lock","True Tape snap that lands on the next beat.",0,1,800,-1,0,12,0,0,0,0,350,0,1,0,100,0},
    {"Grid-Locked","Two-Bar Build","Lands on the next two-bar line with a resonant, phased climb.",0,0,800,-1,40,36,50,4,0,40,200,0,3,0,100,0},
    {"Grid-Locked","Bar-Line Stop","Power-down that reaches silence exactly on the bar line.",1,0,800,-1,0,24,0,0,0,0,350,0,2,0,100,0},
    {"Grid-Locked","Rewind to the One","Spin-back that snaps back on the next bar line.",4,0,800,-1,0,24,0,0,0,0,350,0,2,0,100,0},
    {"Grid-Locked","Stutter on the Beat","Stop-start that recovers on the next beat.",3,0,800,-1,0,24,0,0,0,0,350,0,1,0,100,0},
    {"Grid-Locked","Brake on the Bar","Turntable brake that stops on the bar line.",2,0,800,-1,0,24,0,0,0,0,350,0,2,0,100,0},
};
std::map<juce::String,float> valuesOf(const Row& r) {
    return {{"spinUpMs",r.ms},{"fxAmount",r.fx},{"fxPan",(r.mask&1u)?1.f:0.f},{"fxChorus",(r.mask&2u)?1.f:0.f},{"fxPhaser",(r.mask&4u)?1.f:0.f},
        {"fxFlanger",(r.mask&8u)?1.f:0.f},{"fxWashout",(r.mask&16u)?1.f:0.f},{"syncMode",r.sync>=0?1.f:0.f},{"syncDivision",float(r.sync>=0?r.sync:1)},
        {"outputDb",r.outputDb},{"mode",float(r.mode)},{"engine",float(r.engine)},{"curve",r.curve},{"depth",r.depth},{"filterMode",float(r.filter)},
        {"resonance",r.resonance},{"filterStart",r.start},{"subKeep",float(r.keep)},{"gridLand",float(r.grid)},{"landing",float(r.landing)},
        {"mix",r.mix},{"latestSound",1.f}};
}
}

const std::vector<Preset>& factory() {
    static const std::vector<Preset> list=[]{
        std::vector<Preset> v;
        for(const auto& r:rows) v.push_back({r.folder,r.name,r.note,valuesOf(r),true,{}});
        return v;
    }();
    return list;
}
juce::StringArray factoryFolders() {
    juce::StringArray folders;
    for(const auto& p:factory()) folders.addIfNotAlreadyThere(p.folder);
    return folders;
}
juce::File userRoot() {
    if(const char* overridden=std::getenv("ENERGY_HARVESTER_PRESET_ROOT"); overridden!=nullptr && *overridden!=0)
        return juce::File(juce::String::fromUTF8(overridden));
    const char* home=std::getenv("HOME");
    const juce::File base=home!=nullptr&&*home=='/'?juce::File(juce::String::fromUTF8(home)):juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    return base.getChildFile("Library").getChildFile("Application Support").getChildFile("ENERGY HARVESTER").getChildFile("Presets");
}
juce::String cleanName(const juce::String& text) {
    auto name=text.trim().replaceCharacters("/\\:","---").removeCharacters("\"*?<>|");
    while(name.startsWithChar('.')) name=name.substring(1);
    return name.substring(0,64).trim();
}
juce::StringArray userFolders(const juce::File& root) {
    juce::StringArray folders;
    for(const auto& entry:juce::RangedDirectoryIterator(root,false,"*",juce::File::findDirectories))
        if(!entry.isHidden()) folders.add(entry.getFile().getFileName());
    folders.sortNatural();
    return folders;
}
std::optional<Preset> read(const juce::File& file) {
    auto xml=juce::XmlDocument::parse(file);
    if(xml==nullptr || !xml->hasTagName("ENERGY_HARVESTER_PRESET")) return std::nullopt;
    Preset p; p.factory=false; p.file=file;
    p.name=xml->getStringAttribute("name",file.getFileNameWithoutExtension());
    p.folder=file.getParentDirectory().getFileName(); p.note=xml->getStringAttribute("note");
    for(auto* child:xml->getChildWithTagNameIterator("PARAM")) {
        const auto value=child->getDoubleAttribute("value",std::nan(""));
        if(child->hasAttribute("id") && std::isfinite(value)) p.values[child->getStringAttribute("id")]=float(value);
    }
    if(p.values.empty()) return std::nullopt;
    return p;
}
std::vector<Preset> userPresets(const juce::File& root) {
    std::vector<Preset> list;
    for(const auto& folder:userFolders(root)) {
        juce::Array<juce::File> files=root.getChildFile(folder).findChildFiles(juce::File::findFiles,false,juce::String("*")+extension);
        files.sort();
        for(const auto& f:files) if(auto p=read(f)) list.push_back(*p);
    }
    return list;
}
juce::Result save(const juce::File& root,const juce::String& folder,const juce::String& name,const std::map<juce::String,float>& values,juce::File& written) {
    const auto folderName=cleanName(folder), presetName=cleanName(name);
    if(folderName.isEmpty()) return juce::Result::fail("Choose or type a folder name.");
    if(presetName.isEmpty()) return juce::Result::fail("Type a preset name.");
    const auto directory=root.getChildFile(folderName);
    if(!directory.isDirectory()) if(auto created=directory.createDirectory(); created.failed()) return created;
    juce::XmlElement xml("ENERGY_HARVESTER_PRESET");
    xml.setAttribute("version",1); xml.setAttribute("name",presetName); xml.setAttribute("plugin","ENERGY HARVESTER");
    for(const auto& [id,value]:values) { auto* child=xml.createNewChildElement("PARAM"); child->setAttribute("id",id); child->setAttribute("value",double(value)); }
    // getNonexistentChildFile never returns an existing file, so user files are never replaced.
    written=directory.getNonexistentChildFile(presetName,extension,true);
    if(!xml.writeTo(written)) return juce::Result::fail("Could not write "+written.getFullPathName());
    return juce::Result::ok();
}
}
