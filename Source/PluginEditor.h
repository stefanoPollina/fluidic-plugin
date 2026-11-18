#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

/* The plugin window: FLUIDIC's own interface (the web prototype's page, built into the plugin) in an
   Edge WebView2. Messages between page and plugin:
     page → plugin  ready · param {id,value,phase} · note {note,vel,on} · panic · ui {state}
                    bankSave {json} · bankExport {json} · bankImport
     plugin → page  init {values,ui,bank} · params {id:value} · frame {events,peak,cut,voices} · bankImported {json} */
class FluidicEditor : public juce::AudioProcessorEditor,
                      private juce::Timer,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit FluidicEditor (FluidicProcessor&);
    ~FluidicEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0e10)); }

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float) override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void sendInit (const juce::Identifier& eventId);
    void onParam (const juce::var&);
    juce::var valueForUi (int index) const;
    static juce::String readBank();

    FluidicProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<juce::RangedAudioParameter*> params;            // paramTable() order
    std::map<juce::String, int> indexOf;
    std::unique_ptr<std::atomic<bool>[]> dirty;
    std::vector<juce::uint32> uiTouched;                         // last time the page moved each one (echo guard)
    bool pageReady = false;
    int seenStateVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidicEditor)
};
