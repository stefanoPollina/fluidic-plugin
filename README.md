# FLUIDIC — drip synthesis (VST3 for Windows)

**Download (Windows):** [FLUIDIC-Windows-Setup.exe](https://github.com/stefanoPollina/fluidic-plugin/releases/latest/download/FLUIDIC-Windows-Setup.exe) (installer), or see [all releases](https://github.com/stefanoPollina/fluidic-plugin/releases/latest). No GitHub sign-in needed.

Two plugins from one project, for Ableton Live, FL Studio and any other VST3 host:

- **FLUIDIC** is an **instrument**: the drip synth. It also comes as a standalone app.
- **FLUIDIC FX** is an **audio effect**. Put it on an audio track and the track's audio (a breakbeat, a vocal, anything) becomes what the drips ring with. The pattern decides the rhythm:
  - **EVEN**: steady drops on the grid.
  - **RAIN**: random hits.
  - **SPLASH**: bursts and fills.
  - **STREAM**: rolls.
  - **STORM**: all of them at once.
  - **3D**: stereo chirps.

  FLUIDIC FX also has all of the synth's panels, re-purposed for the effect:
  - **EXCITER** is a tone layer, an oscillator that rings the drips alongside the track. LEVEL 0 means the track only.
  - **AMP** shapes each drip, with one grid step as its note length.
  - The filter's **KEY** follows the drip pitch.

  **AUTO ROLL** rolls the dice by itself on the grid, so the sound keeps re-shaping in time. The rhythms:
  - **Straight:** every step, 8ths, beats, half-time, bars.
  - **Syncopated:** off-beats, backbeat, tresillo, son and rumba clave, bossa nova, dotted 8ths, gallop, Euclidean 5/7/9, and a random bar.
  - **Your own pattern** of steps.

  **HOLD GRID** keeps the 1/4–1/32 grid and **HOLD PATTERN** keeps the drip style while the rest rolls. In a DAW the dice roll inside the plugin itself, so auto-roll keeps going with the plugin window closed. Rolls happen in real time: to get them into an export, record them as automation first (arm automation and play), or bounce the track in real time.

- **STEP SEQUENCER** (FLUIDIC FX, under AUTO ROLL): program where the drips fall. Switch on **SEQUENCER** (an empty bar starts on the beats).
  - **Click a step** to light it with the current **BRUSH**: DIAL (follows the PATTERN dial) or a pattern of its own (EVEN, RAIN, BOTH, 3D, SPLASH, STREAM, STORM). Click a lit step with the same brush to clear it; with another brush to repaint it.
  - **Drag a step up or down** to set its accent: how hard that drip hits.
  - **Right-click a step** to cycle its pattern.
  - Lit steps always drip; unlit steps stay dry. The grid (1/4 … 1/32) sets how many steps play (up to 32). CLEAR, BEATS and ACCENTS 75 help you start over.
  - **Dice lane** (under the steps): which die each step rolls. Pick a **DICE** brush (AUTO = the AUTO ROLL die, d4–d20, ☠, SET) and click a step's lane box; click again or right-click to remove it. Lighting a drip step gives it the AUTO die, so by default a lit step drips *and* rolls; you can also roll on a step that doesn't drip, or drip without rolling.
  - Set AUTO ROLL's style to **Sequencer steps** and the dice roll exactly where the lane says, each step with its own die. The lane lights up while it's driving the dice.
  - Saved with the project. FOLLOW still applies, so a silent track stays silent.
- **FLOW** (both plugins): the water as one body the drips fall into. All three start at 0 (off).
  - **SLOSH** rocks the body: drips lean left and right and their pitch swells with it. In FLUIDIC FX it sways in time with the beat; in FLUIDIC, **SWAY** sets its speed.
  - **TURB** (turbulence) knocks each drip's pitch, timing and place about, and keeps churning while it rings.
  - **CASCADE** spills drips into smaller, higher ones, which can spill again: water running down steps.
  - **RIPPLE**: every drip sends out a ripple, a train of soft pulses that re-ring the water: fast and gentle at first, then slower, softer, a touch lower and further out as they fade and spread. Cascades ripple too, and with SLOSH up the body hitting the wall at each end of a sway ripples back in.
  - **COLLIDE**: when two ripples meet they ring a harmonic of where they meet (a fifth, an octave or a major third), which ripples outwards in turn. Every collision is smaller than what caused it, so the feedback always dies away.
  - The dice can roll FLOW too (gently), and the FLUID visual sways with the sound's SLOSH.
- **FULL** (top right of the visual): the water fills the window for immersive playing; in a browser it goes truly full screen. Esc or EXIT comes back. **TOUCH** turns playing the water on or off (saved with the project).
- **Touch the water**: drag the mouse (or a finger) across the visual to play it. Left to right is low to high on a pentatonic scale; faster strokes drip harder. Each drip ripples where you touched, and FLOW (cascades, ripples, collisions) answers it like any other drip. In the plugins these play like the on-screen keys.
- **FLUID water** (the button next to SIDE / TOP, both plugins): the visual becomes a real wave simulation. Ripples interfere, splash droplets fall back and ripple again, the water sloshes in time with the beat, and the surface churns when it gets loud. Saved with the project.
- **Hold anything from the dice** (both plugins, manual and auto rolls). Everything you hold saves with the project.
  - **A whole panel:** its **HOLD** button.
  - **A row or group:** click its side caption: TONE, PATTERN, PITCH, DROP, SPREAD, SHAPE (filter type), CUT · RES, ENV, KEY · LFO, RISE, HOLD · FADE, DELAY, VERB · OUT.
  - **One dial:** right-click it → *Hold from dice*.

  The grid locks to your DAW's tempo and song position, and only drips while the DAW is playing: stop the transport and the drips ring out and stop. **FOLLOW** (on by default) only lets drips fall while the track has sound, so silent bars stay silent; turn it down to let the drips ping on their own. **CHOP** gates the track to the drip rhythm, re-cutting it, and **MIX** goes from dry to wet. With **SENSE** up, the track's own hits trigger drips too, and MIDI notes sent to the effect set the drips' pitch. In Ableton it's under *Audio Effects*; in FL Studio, load it in a Mixer slot.

- **Same sound as the prototype.** The engine is a line-by-line C++ port of the prototype's audio engine. Rendered with the same random seed, it matches the browser sample for sample. The effects bus (ping-pong delay, reverb, output limiter) matches the browser chain to within a fraction of a dB. See *Checking the sound* below.
- **Same interface.** The plugin window is the prototype's own page, running in Microsoft Edge WebView2, which is built into Windows 10 and 11. It has the drip visualiser (side and top views), dice with locks and history, the on-screen keyboard, the user bank, and full-screen-friendly sizing.
- **Every dial is automatable.** All of each plugin's controls are host parameters, with the same IDs as the prototype's dials:
  - In **FL Studio**, touch a dial, then use *Tools → Last tweaked → Create automation clip*, or *Edit events*.
  - In **Ableton**, every dial appears in the device's automation lanes, and *Configure* lets you pin dials to the device view.
  - Dice rolls and preset changes are recorded as automation when you're writing automation.
- **New instances open on Sine Meditation.** Your current sound, preset name and visualiser view are saved inside the DAW project.
- **Your bank** of saved sounds lives in `%APPDATA%\FLUIDIC\user-bank.json`, so it's shared by every project. Export and import it as `.json` from **BANK ▾**.

## Get the plugin: two ways

### 1. Let GitHub build it (nothing to install)
1. Create a new repository on GitHub, e.g. `fluidic`, and push this folder to it.
2. Open the **Actions** tab. The *Build FLUIDIC for Windows* run starts on every push and takes about 10–15 minutes the first time.
3. Open the finished run and download **FLUIDIC-windows** from *Artifacts*. The zip opens straight onto:
   - `FLUIDIC-0.3.0.N-Windows-Setup.exe`, the installer (N is the build number: higher is newer). It puts both plugins in `C:\Program Files\Common Files\VST3`.
   - `FLUIDIC.vst3`, `FLUIDIC FX.vst3` and `FLUIDIC.exe`, if you'd rather copy them yourself.

### 2. Build on your PC
1. Install **Visual Studio 2022 Community** with the *Desktop development with C++* workload. That includes CMake.
2. Double-click **`build-windows.bat`**. The first run downloads JUCE 8.0.4 and the WebView2 SDK automatically.
3. Right-click **`install-vst3.bat`** and choose *Run as administrator* to copy the VST3 into the system folder. You can also build the installer with Inno Setup: `ISCC installer\fluidic.iss`.

## Load it in your DAW
- **FL Studio:** *Options → Manage plugins → Find installed plugins*. FLUIDIC appears under Generators.
- **Ableton Live:** *Preferences → Plug-Ins*, turn on *Use VST3 Plug-In System Folders*, then *Rescan*. FLUIDIC appears under Plug-Ins → FLUIDIC.

The plugin reports 6 ms of latency, from the output limiter's look-ahead, and both DAWs compensate for it automatically.

## Project layout
| Path | What it is |
|---|---|
| `Source/dsp/FluidicEngine.*` | The drip engine: exciter, DRIP resonators (all 7 patterns, EDGE), filter, envelopes, 12 voices with click-free voice reuse. Pure C++17. |
| `Source/dsp/FluidicFxEngine.*` | The FLUIDIC FX engine: drips excited by the incoming audio, the tempo grid and patterns, CHOP, SENSE, and host sync. A port of `ui/fluidic-fx-worklet.js`. |
| `Source/dsp/FxBus.*` | The effects bus: ping-pong delay (with the browser's exact echo timing), reverb (a generated impulse with the Web Audio normalisation), and a limiter fitted to Chrome's compressor. |
| `Source/ParamTable.h` | The 44 host parameters, each marked as synth-only, FX-only or shared. Generated by `tools/gen_params.py` from the prototype's parameter table. **Never rename an ID:** DAWs store automation by ID. |
| `Source/PluginProcessor.*` | Host parameters, MIDI, the on-screen keyboard, project save/restore, and the visualiser feed. |
| `Source/PluginEditor.*` | The plugin window: WebView2 showing `ui/index.html`, plus the message bridge to the processor. |
| `ui/index.html` | The FLUIDIC page. In a normal browser it's the full prototype, with a SYNTH / FX switch and its own audio engines (`ui/fluidic-worklet.js`, `ui/fluidic-fx-worklet.js`). Inside the plugin it detects JUCE and hands the audio to the C++ engine. |
| `tests/` | Parity tests against the prototype (below). |
| `installer/fluidic.iss` | Inno Setup installer. |
| `.github/workflows/build-windows.yml` | The GitHub build. |

## Checking the sound (parity tests)
These compare the C++ against the web prototype in Chrome. They need Python with Playwright and Chrome. They're not needed to build the plugin.

```
python -m http.server 8766 --directory ui           # serves the prototype
g++ -std=c++17 -O2 -o render_engine tests/render_engine.cpp Source/dsp/FluidicEngine.cpp
RENDER_ENGINE=./render_engine python tests/compare_engine.py     # engine, sample by sample
cmake -S . -B build-tests -DFLUIDIC_BUILD_PLUGIN=OFF -DFLUIDIC_BUILD_TESTS=ON && cmake --build build-tests
RENDER_FULL=build-tests/fluidic_render_artefacts/fluidic_render python tests/compare_full.py   # engine + effects bus
g++ -std=c++17 -O2 -o render_fx tests/render_fx.cpp Source/dsp/FluidicFxEngine.cpp
RENDER_FX=./render_fx python tests/compare_fx.py                  # FX engine, sample by sample
```

Results when this version was made:

**FX engine:** 11 cases, covering every pattern, CHOP, SENSE, the filter and an overloaded STORM. 9 are bit-identical; the other two differ by at most 3×10⁻¹¹. The host-sync test lands every drip exactly on its grid step at any block size, and playback from bar 1 drops on the downbeat.

**Engine:** 10 cases, covering every pattern, wave and filter mode, EDGE, chirp, width, 3D and voice reuse. 9 are bit-identical. The 3D case differs by at most 2×10⁻⁹.

**Full chain, average difference in loudness per 100 ms:**

| Case | Average difference |
|---|---|
| Sine Meditation | 0.01 dB |
| Quiet dry, delay and reverb cases | 0.00 dB |
| Loud cases into the limiter | 0.02–0.15 dB |

## Changing the interface
Edit `ui/index.html`. It's the same page as the prototype. Rebuild and the plugin picks it up, because the page is compiled into the plugin. If you add or change a dial in the page's `PARAMS`, regenerate the C++ table:

1. Dump the table with `Fluidic.PARAMS` from the page into `tools/params.json`.
2. Run `python tools/gen_params.py tools/params.json Source/ParamTable.h`.

## Not in this version yet
- Mac (AU and VST3). The code is cross-platform, but the build setup and testing here are Windows-only for now.
- MIDI learn, sustain pedal and pitch bend.
- Motion lanes inside the plugin.
- Lighting the on-screen keys for notes played from the DAW.
