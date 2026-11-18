#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "dsp/FluidicEngine.h"
#include "dsp/FxBus.h"
#include "ParamTable.h"

/* FLUIDIC — the plugin. Every dial is a host parameter (same IDs as the web prototype), so FL Studio's
   "Last tweaked" / automation clips and Ableton automation all work. The interface runs in a web view
   (see PluginEditor) and talks to this processor through parameters, notes and a visualiser feed. */
class FluidicProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FluidicProcessor();
    ~FluidicProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FLUIDIC"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- used by the editor (message thread) ----
    void addUiNote (int note, float velocity, bool on);   // on-screen / computer keyboard
    void requestPanic() { panicRequested = true; }
    struct Frame { std::vector<fluidic::DropEvent> events; float peak = 0; bool cut = false; int voices = 0; };
    Frame takeFrame();                                     // drops since the last call, for the visualiser
    juce::String getUiState() const { const juce::ScopedLock l (uiLock); return uiState; }
    void setUiState (const juce::String& s) { const juce::ScopedLock l (uiLock); uiState = s; }
    std::atomic<int> stateVersion { 0 };                   // bumped when the host restores a project

    static juce::File userBankFile();                      // %APPDATA%\FLUIDIC\user-bank.json

private:
    void timerCallback() override;                         // reverb size changes, like the prototype (debounced)
    void readParams();

    fluidic::FluidicEngine engine;
    fluidic::FxBus fx;
    juce::MidiMessageCollector uiNotes;
    std::atomic<bool> panicRequested { false };

    juce::CriticalSection uiLock; juce::String uiState;    // the interface's own state (current preset name, view…)
    // every parameter looked up once, so the audio thread never searches by name
    std::atomic<float> *pWave{}, *pLevel{}, *pHold{}, *pOct{}, *pPattern{}, *pCut{}, *pRes{}, *pKey{}, *pPing{}, *pSweep{}, *pFall{},
                       *pBubble{}, *pWobble{}, *pChirp{}, *pWidth{}, *pDensity{}, *pSize{}, *pEdge{}, *pFType{}, *pFCut{}, *pFRes{},
                       *pFDrive{}, *pFEnv{}, *pFA{}, *pFD{}, *pFKey{}, *pFRate{}, *pFDepth{}, *pA{}, *pD{}, *pS{}, *pR{}, *pOut{},
                       *pDMix{}, *pDTime{}, *pDFb{}, *pRMix{}, *rawRevSize{};

    juce::AbstractFifo dropFifo { 1024 };
    std::array<fluidic::DropEvent, 1024> dropBuf {};
    std::atomic<float> peak { 0 };
    std::atomic<bool> cut { false };
    std::atomic<int> voices { 0 };

    double pendingRevSize = -1; int revStableTicks = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidicProcessor)
};
