#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "UI/WindowActions.h"
#include "Standalone/DeviceSupport.h"

namespace yoiboi
{
class DeviceWindow final : public juce::DocumentWindow,
                           public DeviceWindowActions,
                           private juce::ChangeListener
{
public:
    explicit DeviceWindow (juce::StandalonePluginHolder& plugin)
        : DocumentWindow ("YOI-BOI", juce::Colours::transparentBlack, 0), holder (plugin)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);
        setOpaque (false);
        setDropShadowEnabled (false);
        setResizable (false, false);
        editor.reset (holder.processor->createEditorAndMakeActive());
        if (editor != nullptr)
        {
            setContentNonOwned (editor.get(), true);
            editor->addMouseListener (this, true);
        }
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        // Remember device and MIDI-input choices as soon as they change, not
        // only at a clean quit.
        holder.deviceManager.addChangeListener (this);
    }

    ~DeviceWindow() override
    {
        holder.deviceManager.removeChangeListener (this);
        holder.stopPlaying();
        if (editor != nullptr)
        {
            editor->removeMouseListener (this);
            clearContentComponent();
            holder.processor->editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    juce::BorderSize<int> getContentComponentBorder() const override { return {}; }
    void paint (juce::Graphics&) override {}
    void closeButtonPressed() override { closeDeviceWindow(); }
    void closeDeviceWindow() override { juce::JUCEApplicationBase::quit(); }
    void showDeviceAudioSettings() override { holder.showAudioSettingsDialog(); }

    void mouseDown (const juce::MouseEvent& event) override
    {
        // The wordmark/blank header is the grab area. Controls keep their own
        // gestures: the drag never starts from ZOOM, CLOSE, a knob or a tab.
        dragging = editor != nullptr
                   && startsDeviceWindowDrag (*editor, event.originalComponent,
                                              event.getEventRelativeTo (editor.get()).position,
                                              event.mods.isLeftButtonDown());
        if (dragging) dragger.startDraggingComponent (this, event);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragging) dragger.dragComponent (this, event, nullptr);
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { holder.saveAudioDeviceState(); }

    juce::StandalonePluginHolder& holder;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::ComponentDragger dragger;
    bool dragging = false;
};

class DeviceApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "YOI-BOI"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const juce::String&) override
    {
        // Audio/MIDI device choices persist; the sound itself is still never
        // saved or restored automatically (DeviceOnlySettings drops it).
        holder = std::make_unique<juce::StandalonePluginHolder> (
            new DeviceOnlySettings (DeviceOnlySettings::defaultFile()), true);
        window = std::make_unique<DeviceWindow> (*holder);
    }
    void shutdown() override { window.reset(); holder.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr) window->toFront (true);
    }
private:
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<DeviceWindow> window;
};
}

JUCE_CREATE_APPLICATION_DEFINE (yoiboi::DeviceApplication)
