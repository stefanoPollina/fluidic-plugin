#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace fluidic;

namespace
{
juce::String formatValue (const ParamSpec& s, float v)
{
    const juce::String u (s.unit);
    if (u == "Hz") return v >= 1000 ? juce::String (v / 1000, 2) + " kHz" : juce::String (v, v < 10 ? 2 : 0) + " Hz";
    if (u == "ms") return juce::String (juce::roundToInt (v * 1000)) + " ms";
    if (u == "s")  return v < 1 ? juce::String (juce::roundToInt (v * 1000)) + " ms" : juce::String (v, 2) + " s";
    if (u == "oct") return (v > 0 ? "+" : "") + juce::String (v, 2);
    if (u == "bi") return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100));
    if (u == "x")  return juce::String (std::pow (2.0f, (v - 0.5f) * (juce::String (s.id) == "drip.size" ? 2.4f : 4.0f)), 2) + "x";
    if (juce::String (s.id) == "drip.edge" && v < 0.01f) return "OFF";
    return juce::String (juce::roundToInt (v * 100)) + "%";
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : paramTable())
    {
        const juce::ParameterID pid { s.id, 1 };
        if (s.kind == Kind::choice)
        {
            juce::StringArray labels; for (auto* l : s.labels) labels.add (l);
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, labels, (int) s.def));
        }
        else if (s.kind == Kind::integer)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.min, (int) s.max, (int) s.def));
        }
        else
        {
            juce::NormalisableRange<float> range (s.min, s.max);
            if (s.log)   // same curve as the prototype's dials: t = log(v/min) / log(max/min)
            {
                const float lo = s.min, hi = s.max;
                range = juce::NormalisableRange<float> (lo, hi,
                    [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                    [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); });
            }
            const ParamSpec* sp = &s;
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, s.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([sp] (float v, int) { return formatValue (*sp, v); })));
        }
    }
    return layout;
}
} // namespace

FluidicProcessor::FluidicProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FLUIDIC", makeLayout())
{
    auto P = [this] (const char* id) { auto* a = apvts.getRawParameterValue (id); jassert (a != nullptr); return a; };
    pWave = P ("exc.wave"); pLevel = P ("exc.level"); pHold = P ("exc.hold"); pOct = P ("exc.octave");
    pPattern = P ("drip.pattern"); pCut = P ("drip.cutoff"); pRes = P ("drip.res"); pKey = P ("drip.key"); pPing = P ("drip.ping");
    pSweep = P ("drip.sweep"); pFall = P ("drip.fall"); pBubble = P ("drip.bubble"); pWobble = P ("drip.wobble"); pChirp = P ("drip.chirp");
    pWidth = P ("drip.width"); pDensity = P ("drip.density"); pSize = P ("drip.size"); pEdge = P ("drip.edge");
    pFType = P ("filt.type"); pFCut = P ("filt.cutoff"); pFRes = P ("filt.res"); pFDrive = P ("filt.drive"); pFEnv = P ("filt.env");
    pFA = P ("filt.attack"); pFD = P ("filt.decay"); pFKey = P ("filt.key"); pFRate = P ("filt.lfoRate"); pFDepth = P ("filt.lfoDepth");
    pA = P ("amp.a"); pD = P ("amp.d"); pS = P ("amp.s"); pR = P ("amp.r"); pOut = P ("out.level");
    pDMix = P ("fx.delayMix"); pDTime = P ("fx.delayTime"); pDFb = P ("fx.delayFb"); pRMix = P ("fx.revMix"); rawRevSize = P ("fx.revSize");
    uiState = R"({"preset":"f:Sine Meditation"})";
    engine.seed ((uint32_t) juce::Random::getSystemRandom().nextInt());
    startTimerHz (20);
}

FluidicProcessor::~FluidicProcessor() { stopTimer(); }

bool FluidicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FluidicProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    fx.prepare (sampleRate, std::max (1, samplesPerBlock));
    fx.loadReverb (rawRevSize->load(), (uint32_t) juce::Random::getSystemRandom().nextInt());
    setLatencySamples (fx.latencySamples());   // the limiter's 6 ms look-ahead; DAWs compensate for it
    uiNotes.reset (sampleRate);
    readParams();
    prepared = true;
}

void FluidicProcessor::readParams()
{
    auto& p = engine.params();
    auto v = [] (std::atomic<float>* a) { return (double) a->load(); };
    auto ix = [] (std::atomic<float>* a, int hi) { return juce::jlimit (0, hi, juce::roundToInt (a->load())); };
    p.wave = (Wave) ix (pWave, 3);            // choice order matches: sine, saw, square, noise
    p.oscLevel = v (pLevel); p.hold = v (pHold); p.octave = v (pOct);
    p.pattern = (Pattern) ix (pPattern, 6);   // even, rain, both, 3d, splash, stream, storm
    p.cutoff = v (pCut); p.res = v (pRes); p.key = v (pKey); p.ping = v (pPing); p.sweep = v (pSweep); p.fall = v (pFall);
    p.bubble = v (pBubble); p.wobble = v (pWobble); p.chirp = v (pChirp); p.width = v (pWidth); p.density = v (pDensity);
    p.size = v (pSize); p.edge = v (pEdge);
    p.fType = (FilterType) ix (pFType, 5);    // off, lp, lp24, hp, bp, notch
    p.fCut = v (pFCut); p.fRes = v (pFRes); p.fDrive = v (pFDrive); p.fEnv = v (pFEnv); p.fA = v (pFA); p.fD = v (pFD);
    p.fKey = v (pFKey); p.fRate = v (pFRate); p.fDepth = v (pFDepth);
    p.a = v (pA); p.d = v (pD); p.s = v (pS); p.r = v (pR); p.level = v (pOut);
    FxParams f;
    f.delayMix = v (pDMix); f.delayTime = v (pDTime); f.delayFb = v (pDFb); f.revMix = v (pRMix); f.revSize = v (rawRevSize);
    fx.setParams (f);
}

void FluidicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! prepared || buffer.getNumChannels() < 2) return;

    readParams();
    if (panicRequested.exchange (false)) engine.panic();
    uiNotes.removeNextBlockOfMessages (midi, n);   // merge notes from the on-screen keyboard

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { engine.process (L + pos, R + pos, at - pos); pos = at; }
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.panic();
    }
    if (pos < n) engine.process (L + pos, R + pos, n - pos);

    fx.process (L, R, n);

    // visualiser feed
    for (auto& e : engine.events)
    {
        const auto w = dropFifo.write (1);
        if (w.blockSize1 > 0) dropBuf[(size_t) w.startIndex1] = e;
    }
    engine.events.clear();
    peak.store ((float) engine.peak);
    if (engine.cutFlash) { cut.store (true); engine.cutFlash = false; }
    voices.store (engine.activeVoices());
}

FluidicProcessor::Frame FluidicProcessor::takeFrame()
{
    Frame f;
    const auto r = dropFifo.read (dropFifo.getNumReady());
    for (int i = 0; i < r.blockSize1; ++i) f.events.push_back (dropBuf[(size_t) (r.startIndex1 + i)]);
    for (int i = 0; i < r.blockSize2; ++i) f.events.push_back (dropBuf[(size_t) (r.startIndex2 + i)]);
    f.peak = peak.load(); f.cut = cut.exchange (false); f.voices = voices.load();
    return f;
}

void FluidicProcessor::addUiNote (int note, float velocity, bool on)
{
    auto m = on ? juce::MidiMessage::noteOn (1, note, velocity) : juce::MidiMessage::noteOff (1, note);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    uiNotes.addMessageToQueue (m);
}

void FluidicProcessor::timerCallback()
{
    // Reverb SIZE: rebuild the impulse once the dial has settled (~160 ms), as the prototype does
    if (! prepared) return;
    const double want = rawRevSize->load();
    if (std::abs (want - fx.loadedReverbSize()) < 1e-4) { pendingRevSize = -1; return; }
    if (std::abs (want - pendingRevSize) > 1e-4) { pendingRevSize = want; revStableTicks = 0; return; }
    if (++revStableTicks >= 3) { fx.loadReverb (want, (uint32_t) juce::Random::getSystemRandom().nextInt()); pendingRevSize = -1; }
}

void FluidicProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiState", getUiState(), nullptr);
    state.setProperty ("version", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FluidicProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.hasProperty ("uiState")) setUiState (tree.getProperty ("uiState").toString());
            apvts.replaceState (tree);
            ++stateVersion;
        }
}

juce::File FluidicProcessor::userBankFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FLUIDIC").getChildFile ("user-bank.json");
}

juce::AudioProcessorEditor* FluidicProcessor::createEditor() { return new FluidicEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FluidicProcessor(); }
