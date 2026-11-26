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
    if (u == "times") return juce::String (v, 2) + "x";
    if (u == "bpm") return juce::String (juce::roundToInt (v)) + " BPM";
    if (u == "pctoff") return v < 0.01f ? juce::String ("OFF") : juce::String (juce::roundToInt (v * 100)) + "%";
    return juce::String (juce::roundToInt (v * 100)) + "%";
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : paramTable())
    {
        if (FluidicProcessor::isFx ? s.synthOnly : s.fxOnly) continue;   // each plugin only shows its own dials
        const float def = FluidicProcessor::isFx ? s.defFx : s.def;
        const juce::ParameterID pid { s.id, 1 };
        if (s.kind == Kind::choice)
        {
            juce::StringArray labels; for (auto* l : s.labels) labels.add (l);
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, labels, (int) def));
        }
        else if (s.kind == Kind::integer)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.min, (int) s.max, (int) def));
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
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([sp] (float v, int) { return formatValue (*sp, v); })));
        }
    }
    return layout;
}
} // namespace

FluidicProcessor::FluidicProcessor()
    : AudioProcessor (isFx ? BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                           : BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FLUIDIC", makeLayout())
{
    auto P = [this] (const char* id)
    {
        if (auto* a = apvts.getRawParameterValue (id)) return a;
        // a parameter only the other plugin has: read its default instead
        float d = 0; for (auto& s : paramTable()) if (juce::String (s.id) == id) d = isFx ? s.defFx : s.def;
        fallbacks.push_back (std::make_unique<std::atomic<float>> (d)); return fallbacks.back().get();
    };
    pWave = P ("exc.wave"); pLevel = P ("exc.level"); pHold = P ("exc.hold"); pOct = P ("exc.octave");
    pPattern = P ("drip.pattern"); pCut = P ("drip.cutoff"); pRes = P ("drip.res"); pKey = P ("drip.key"); pPing = P ("drip.ping");
    pSweep = P ("drip.sweep"); pFall = P ("drip.fall"); pBubble = P ("drip.bubble"); pWobble = P ("drip.wobble"); pChirp = P ("drip.chirp");
    pWidth = P ("drip.width"); pDensity = P ("drip.density"); pSize = P ("drip.size"); pEdge = P ("drip.edge");
    pFType = P ("filt.type"); pFCut = P ("filt.cutoff"); pFRes = P ("filt.res"); pFDrive = P ("filt.drive"); pFEnv = P ("filt.env");
    pFA = P ("filt.attack"); pFD = P ("filt.decay"); pFKey = P ("filt.key"); pFRate = P ("filt.lfoRate"); pFDepth = P ("filt.lfoDepth");
    pA = P ("amp.a"); pD = P ("amp.d"); pS = P ("amp.s"); pR = P ("amp.r"); pOut = P ("out.level");
    pDMix = P ("fx.delayMix"); pDTime = P ("fx.delayTime"); pDFb = P ("fx.delayFb"); pRMix = P ("fx.revMix"); rawRevSize = P ("fx.revSize");
    pRate = P ("rhy.rate"); pBpm = P ("rhy.bpm"); pSense = P ("rhy.sense"); pMix = P ("in.mix"); pChop = P ("in.chop"); pDrive = P ("in.drive"); pFollow = P ("in.follow");
    pSlosh = P ("flow.slosh"); pSway = P ("flow.sway"); pTurb = P ("flow.turb"); pCasc = P ("flow.cascade"); pRipple = P ("flow.ripple"); pCollide = P ("flow.collide");
    uiState = isFx ? R"({"preset":"f:Drippiness"})" : R"({"preset":"f:Sine Meditation"})";
    engine.seed ((uint32_t) juce::Random::getSystemRandom().nextInt());
    fxEngine.seed ((uint32_t) juce::Random::getSystemRandom().nextInt());
    if (isFx)
    {
        std::vector<std::string> mine;
        for (auto& s : paramTable()) if (apvts.getParameter (s.id) != nullptr) mine.push_back (s.id);
        dice = std::make_unique<DiceRoller> (mine, true, (uint32_t) juce::Random::getSystemRandom().nextInt());
    }
    setUiState (uiState);
    startTimerHz (60);
}

namespace
{
// the window's saved state → AUTO ROLL settings and HOLDs (same JSON the page writes)
AutoConfig parseAuto (const juce::String& json)
{
    AutoConfig c;
    const auto v = juce::JSON::parse (json);
    if (const auto a = v["auto"]; a.isObject())
    {
        c.on = (bool) a["on"];
        if (a["die"].isString()) c.die = a["die"].toString().toStdString();
        if (a["mode"].isString()) c.mode = a["mode"].toString().toStdString();
        if (a["mask"].isString()) c.customMask = a["mask"].toString().toStdString();
        c.holdRate = (bool) a["holdRate"]; c.holdPattern = (bool) a["holdPattern"];
    }
    if (auto* st = v["seq"]["steps"].getArray())   // the sequencer's dice lane
        for (auto& x : *st) c.seqDice.push_back (x["die"].toString().toStdString());
    if (const auto h = v["holds"]; h.isObject())
    {
        if (auto* s = h["secs"].getArray()) for (auto& x : *s) c.holdSecs.insert (x.toString().toStdString());
        if (auto* s = h["ids"].getArray()) for (auto& x : *s) c.holdIds.insert (x.toString().toStdString());
    }
    return c;
}
} // namespace

void FluidicProcessor::setUiState (const juce::String& s)
{
    const juce::ScopedLock l (uiLock);
    uiState = s;
    if (isFx)
    {
        pendingAuto = parseAuto (s); autoDirty = true;
        Sequence q;   // STEP SEQUENCER: { on, steps: [{ on, pat (pattern key, '' = the dial), acc }] }
        const auto v = juce::JSON::parse (s)["seq"];
        if (v.isObject())
        {
            q.on = (bool) v["on"];
            const ParamSpec* pat = nullptr; for (auto& t : paramTable()) if (juce::String (t.id) == "drip.pattern") pat = &t;
            if (auto* st = v["steps"].getArray())
                for (int i = 0; i < juce::jmin (32, st->size()); ++i)
                {
                    const auto& x = st->getReference (i);
                    auto& d = q.steps[(size_t) i];
                    d.on = (bool) x["on"];
                    d.acc = x.hasProperty ("acc") ? juce::jlimit (0.0, 1.0, (double) x["acc"]) : 0.75;
                    d.pat = -1;
                    const auto key = x["pat"].toString();
                    if (pat != nullptr) for (size_t k = 0; k < pat->keys.size(); ++k) if (key == juce::String (pat->keys[k])) d.pat = (int) k;
                }
        }
        const juce::SpinLock::ScopedLockType sl (seqLock);
        pendingSeq = q; seqDirty = true;
    }
}

void FluidicProcessor::runAutoRoll()
{
    if (dice == nullptr) return;
    {
        const juce::ScopedLock l (uiLock);
        if (autoDirty) { dice->setConfig (pendingAuto); autoDirty = false; }
    }
    const long long k = gridStep.load();
    if (k < 0) return;
    auto get = [this] (const std::string& id) -> float
    {
        auto* p = apvts.getParameter (id); return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    };
    auto set = [this] (const std::string& id, float v)
    {
        if (auto* p = apvts.getParameter (id))   // one gesture per change, like a dial the page turns once
        {
            p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
        }
    };
    dice->onGridStep (k, get, set);
    for (auto& n : dice->takeNotices()) if (rollNotices.size() < 32) rollNotices.push_back (n);
}

FluidicProcessor::~FluidicProcessor() { stopTimer(); }

bool FluidicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (! isFx) return true;
    const auto in = layouts.getMainInputChannelSet();   // the effect takes a stereo or mono track
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void FluidicProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    fxEngine.prepare (sampleRate);
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
    p.slosh = v (pSlosh); p.sway = v (pSway); p.turb = v (pTurb); p.cascade = v (pCasc); p.ripple = v (pRipple); p.collide = v (pCollide);   // FLOW
    if (isFx)
    {
        auto& q = fxEngine.params();
        static constexpr int rates[] = { 1, 2, 4, 8 };   // 1/4, 1/8, 1/16, 1/32
        q.pattern = p.pattern; q.rate = rates[ix (pRate, 3)]; q.bpm = v (pBpm); q.density = p.density; q.sense = v (pSense);
        q.cutoff = p.cutoff; q.res = p.res; q.ping = p.ping; q.sweep = p.sweep; q.fall = p.fall; q.bubble = p.bubble; q.wobble = p.wobble;
        q.chirp = p.chirp; q.width = p.width; q.size = p.size; q.edge = p.edge; q.key = p.key;
        q.mix = v (pMix); q.chop = v (pChop); q.drive = v (pDrive); q.follow = v (pFollow);
        q.fType = p.fType; q.fCut = p.fCut; q.fRes = p.fRes; q.fDrive = p.fDrive; q.fEnv = p.fEnv; q.fA = p.fA; q.fD = p.fD;
        q.fRate = p.fRate; q.fDepth = p.fDepth; q.level = p.level;
        q.wave = p.wave; q.oscLevel = p.oscLevel; q.hold = p.hold; q.octave = p.octave;   // tone layer
        q.a = p.a; q.d = p.d; q.s = p.s; q.r = p.r; q.fKey = p.fKey;                     // AMP per drip, filter KEY
        q.slosh = p.slosh; q.turb = p.turb; q.cascade = p.cascade; q.ripple = p.ripple; q.collide = p.collide;                      // FLOW (sways with the beat)
    }
    FxParams f;
    f.delayMix = v (pDMix); f.delayTime = v (pDTime); f.delayFb = v (pDFb); f.revMix = v (pRMix); f.revSize = v (rawRevSize);
    fx.setParams (f);
}

void FluidicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! isFx) buffer.clear();
    if (! prepared || buffer.getNumChannels() < 2) { buffer.clear(); return; }

    readParams();
    if (panicRequested.exchange (false)) { engine.panic(); fxEngine.panic(); }
    uiNotes.removeNextBlockOfMessages (midi, n);   // merge notes from the on-screen keyboard

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    if (isFx)
    {
        // a mono track arrives on channel 0 only
        if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
        if (seqDirty.load())   // a new STEP SEQUENCER pattern from the window (or the project)
        {
            const juce::SpinLock::ScopedTryLockType sl (seqLock);
            if (sl.isLocked()) { fxEngine.setSequence (pendingSeq); seqDirty = false; }
        }
        // follow the host: its tempo, and while it plays, its position (so the drip grid sits on the song's grid)
        auto& q = fxEngine.params();
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
            {
                if (const auto bpm = pos->getBpm()) if (*bpm > 1) q.bpm = *bpm;
                if (pos->getIsPlaying()) { if (const auto ppq = pos->getPpqPosition()) fxEngine.setGridPosition (*ppq * q.rate); }
                else fxEngine.hostStopped();
            }
        int at0 = 0;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int at = juce::jlimit (0, n, meta.samplePosition);
            if (at > at0) { fxEngine.process (L + at0, R + at0, L + at0, R + at0, at - at0); at0 = at; }
            if (m.isNoteOn()) fxEngine.noteOn (m.getNoteNumber());
            else if (m.isNoteOff()) fxEngine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) fxEngine.panic();
        }
        if (at0 < n) fxEngine.process (L + at0, R + at0, L + at0, R + at0, n - at0);
        fx.process (L, R, n);
        for (auto& e : fxEngine.events) { const auto w = dropFifo.write (1); if (w.blockSize1 > 0) dropBuf[(size_t) w.startIndex1] = e; }
        fxEngine.events.clear();
        peak.store ((float) fxEngine.peak);
        if (fxEngine.cutFlash) { cut.store (true); fxEngine.cutFlash = false; }
        voices.store (fxEngine.activeDrops());
        gridStep.store (fxEngine.gridStep());
        sloshNow.store ((float) fxEngine.slosh());
        return;
    }
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
    sloshNow.store ((float) engine.slosh());
}

FluidicProcessor::Frame FluidicProcessor::takeFrame()
{
    Frame f;
    const auto r = dropFifo.read (dropFifo.getNumReady());
    for (int i = 0; i < r.blockSize1; ++i) f.events.push_back (dropBuf[(size_t) (r.startIndex1 + i)]);
    for (int i = 0; i < r.blockSize2; ++i) f.events.push_back (dropBuf[(size_t) (r.startIndex2 + i)]);
    f.peak = peak.load(); f.cut = cut.exchange (false); f.voices = voices.load(); f.step = gridStep.load(); f.slosh = sloshNow.load();
    if (dice != nullptr)   // same thread as the auto-roll (message thread)
    {
        f.rolls = std::move (rollNotices); rollNotices.clear();
        f.autoPos = dice->config().on ? dice->position() : -1; f.autoMask = dice->maskString();
    }
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
    if (! prepared) return;
    runAutoRoll();
    // Reverb SIZE: rebuild the impulse once the dial has settled (~160 ms), as the prototype does
    const double want = rawRevSize->load();
    if (std::abs (want - fx.loadedReverbSize()) < 1e-4) { pendingRevSize = -1; return; }
    if (std::abs (want - pendingRevSize) > 1e-4) { pendingRevSize = want; revStableTicks = 0; return; }
    if (++revStableTicks >= 9) { fx.loadReverb (want, (uint32_t) juce::Random::getSystemRandom().nextInt()); pendingRevSize = -1; }
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
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")   // ~/Library/Application Support/FLUIDIC
#endif
        .getChildFile ("FLUIDIC").getChildFile (isFx ? "user-bank-fx.json" : "user-bank.json");
}

juce::AudioProcessorEditor* FluidicProcessor::createEditor() { return new FluidicEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FluidicProcessor(); }
