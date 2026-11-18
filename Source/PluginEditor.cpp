#include "PluginEditor.h"
#include "BinaryData.h"

using namespace fluidic;

FluidicEditor::FluidicEditor (FluidicProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    const auto& T = paramTable();
    dirty.reset (new std::atomic<bool>[T.size()]);
    for (size_t i = 0; i < T.size(); ++i)
    {
        params.push_back (proc.apvts.getParameter (T[i].id));
        indexOf[T[i].id] = (int) i;
        dirty[i] = false;
        proc.apvts.addParameterListener (T[i].id, this);
    }
    uiTouched.assign (T.size(), 0);
    seenStateVersion = proc.stateVersion.load();

    using Opts = juce::WebBrowserComponent::Options;
    auto options = Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FLUIDIC-webview"))
                                     .withBackgroundColour (juce::Colour (0xff0d0e10))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("ready", [this] (const juce::var&) { pageReady = true; sendInit ("init"); })
        .withEventListener ("param", [this] (const juce::var& v) { onParam (v); })
        .withEventListener ("note", [this] (const juce::var& v)
        {
            proc.addUiNote ((int) v["note"], (float) (double) v["vel"], (bool) v["on"]);
        })
        .withEventListener ("panic", [this] (const juce::var&) { proc.requestPanic(); })
        .withEventListener ("ui", [this] (const juce::var& v) { proc.setUiState (v["state"].toString()); })
        .withEventListener ("bankSave", [] (const juce::var& v)
        {
            auto f = FluidicProcessor::userBankFile();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (v["json"].toString());
        })
        .withEventListener ("bankExport", [this] (const juce::var& v)
        {
            const auto json = v["json"].toString();
            chooser = std::make_unique<juce::FileChooser> ("Export FLUIDIC bank",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("fluidic-bank.json"), "*.json");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [json] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File{}) f.replaceWithText (json); });
        })
        .withEventListener ("bankImport", [this] (const juce::var&)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import FLUIDIC bank",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
            juce::Component::SafePointer<FluidicEditor> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || ! f.existsAsFile() || safe->browser == nullptr) return;
                auto* o = new juce::DynamicObject(); o->setProperty ("json", f.loadFileAsString());
                safe->browser->emitEventIfBrowserIsVisible ("bankImported", juce::var (o));
            });
        });

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (900, 620, 2600, 1800);
    setSize (1280, 860);
    startTimerHz (60);
}

FluidicEditor::~FluidicEditor()
{
    stopTimer();
    for (auto& s : paramTable()) proc.apvts.removeParameterListener (s.id, this);
}

void FluidicEditor::resized() { if (browser) browser->setBounds (getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> FluidicEditor::getResource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    if (path == "/" || path == "/index.html")
    {
        juce::WebBrowserComponent::Resource r;
        r.data.resize ((size_t) BinaryData::index_htmlSize);
        std::memcpy (r.data.data(), BinaryData::index_html, r.data.size());
        r.mimeType = "text/html";
        return r;
    }
    return std::nullopt;
}

juce::var FluidicEditor::valueForUi (int i) const
{
    const auto& s = paramTable()[(size_t) i];
    auto* prm = params[(size_t) i];
    const float real = prm->convertFrom0to1 (prm->getValue());
    if (s.kind == Kind::choice) return juce::String (s.keys[(size_t) juce::jlimit (0, (int) s.keys.size() - 1, juce::roundToInt (real))]);
    return (double) real;
}

juce::String FluidicEditor::readBank()
{
    auto f = FluidicProcessor::userBankFile();
    return f.existsAsFile() ? f.loadFileAsString() : juce::String ("{}");
}

void FluidicEditor::sendInit (const juce::Identifier& eventId)
{
    auto* values = new juce::DynamicObject();
    for (size_t i = 0; i < params.size(); ++i) { values->setProperty (paramTable()[i].id, valueForUi ((int) i)); dirty[i] = false; }
    auto* o = new juce::DynamicObject();
    o->setProperty ("values", juce::var (values));
    o->setProperty ("ui", proc.getUiState());
    o->setProperty ("bank", readBank());
    browser->emitEventIfBrowserIsVisible (eventId, juce::var (o));
}

void FluidicEditor::onParam (const juce::var& v)
{
    const auto id = v["id"].toString();
    const auto it = indexOf.find (id);
    if (it == indexOf.end()) return;
    const int i = it->second;
    const auto& s = paramTable()[(size_t) i];
    auto* prm = params[(size_t) i];
    const auto phase = v["phase"].toString();
    uiTouched[(size_t) i] = juce::Time::getMillisecondCounter();

    if (phase == "begin") { prm->beginChangeGesture(); return; }
    if (phase == "end")   { prm->endChangeGesture(); return; }

    float real;
    if (s.kind == Kind::choice)
    {
        const auto key = v["value"].toString(); int idx = 0;
        for (size_t k = 0; k < s.keys.size(); ++k) if (key == s.keys[k]) idx = (int) k;
        real = (float) idx;
    }
    else real = (float) (double) v["value"];
    const float norm = prm->convertTo0to1 (juce::jlimit (s.min, s.max, real));
    if (phase == "once") prm->beginChangeGesture();
    prm->setValueNotifyingHost (norm);
    if (phase == "once") prm->endChangeGesture();
}

void FluidicEditor::parameterChanged (const juce::String& id, float)
{
    // may arrive on the audio thread (automation): only flag it; the timer sends it to the page
    const auto it = indexOf.find (id);
    if (it != indexOf.end()) dirty[(size_t) it->second] = true;
}

void FluidicEditor::timerCallback()
{
    if (! pageReady || browser == nullptr) return;

    // the host restored a project while the window was open
    if (proc.stateVersion.load() != seenStateVersion) { seenStateVersion = proc.stateVersion.load(); sendInit ("restored"); }

    // host automation → page (skipping the page's own recent moves, so a dragged dial never jumps)
    const auto now = juce::Time::getMillisecondCounter();
    juce::DynamicObject::Ptr changed;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (! dirty[i].load()) continue;
        if (now - uiTouched[i] < 400) continue;   // stays flagged; sent once the page has let go
        dirty[i] = false;
        if (changed == nullptr) changed = new juce::DynamicObject();
        changed->setProperty (paramTable()[i].id, valueForUi ((int) i));
    }
    if (changed != nullptr) browser->emitEventIfBrowserIsVisible ("params", juce::var (changed.get()));

    // visualiser
    auto f = proc.takeFrame();
    static const char* kinds[] = { "note", "rain", "splash", "stream" };
    juce::Array<juce::var> evs;
    for (auto& e : f.events)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", e.x); o->setProperty ("size", e.size); o->setProperty ("kind", kinds[(int) e.kind]);
        o->setProperty ("note", e.note); o->setProperty ("sweep", e.sweep); o->setProperty ("chirp", e.chirp);
        evs.add (juce::var (o));
    }
    auto* fr = new juce::DynamicObject();
    fr->setProperty ("events", evs); fr->setProperty ("peak", f.peak); fr->setProperty ("cut", f.cut); fr->setProperty ("voices", f.voices);
    browser->emitEventIfBrowserIsVisible ("frame", juce::var (fr));
}
