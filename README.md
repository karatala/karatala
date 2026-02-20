![Karatala](assets/images/karatala.png)
[![](https://github.com/karatala/karatala/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/karatala/karatala/actions)

Karatala is a drum kit engine instrument for e-Drummers and everyone.

FIXME: replace std::vector and other std shit like std::move with something better
FIXME: get rid of static_cast everywhere it is possible, change types
FIXME: sort properties in classes and structs so 1st go the heaviest for memory optimization

            const StringsDictSingleton::Id ID_bell = StringsDictSingleton::getId("bell");
            const StringsDictSingleton::Id ID_bow = StringsDictSingleton::getId("bow");
            const StringsDictSingleton::Id ID_edge = StringsDictSingleton::getId("edge");
            const StringsDictSingleton::Id ID_rim = StringsDictSingleton::getId("rim");
            const StringsDictSingleton::Id ID_sidestick = StringsDictSingleton::getId("sidestick");
            const StringsDictSingleton::Id ID_center = StringsDictSingleton::getId("center");

[FAIL] Reduce voices to use one multichannel voice instead of many 1-2 ch, and then cache multichannel variant from n files - FAILED (performance become worse and idea is logically wrong, cause for example, room channers have different attack/length, cannot wait for them)


TODO: peak offset for diffenetn mics (found minimal for example di and take it as 0, based on that calculate for all other mics of that sample variant relatively, for example far room = 10ms etc...) smart-parse from samples and save to filename





Loading of inst preset ( kick1(slit) kit inst (spec) params , support stacking samples differently to more extend rr variants (option)

// B	59	RD 1 )
// Bb	58	VSLP
// A	57	CR 2 )
// Ab	56	CWBL
// G	55	SP 1 )
// F#	54	TAMB
// F	53	RD 1 ^
// E	52	CH 1 )
// Eb	51	RD 1 ●
// D	50	OCT1 ●
// C#	49	CR 1 )
// C	48	TOM1 ●
// B	47	TOM2 ●
// Bb	46	HH O ●
// A	45	TOM3 ●
// Ab	44	HH P _
// G	43	TOM4 ●
// F#	42	HH C ●
// F	41	TOM5 ●
// E	40	SNR1 )
// Eb	39	CLAP
// D	38	SNR1 ●
// C#	37	SNR1 ×
// C	36	KICK 1
// B	35	KICK 2
// Bb	34	RD 2 )
// A	33	CR 2 ●
// Ab	32	KRTL O ●
// G	31	SP 1 ●
// F#	30	Sp 2 )
// F	29	RD 2 ^
// E	28	CH 1 ●
// Eb	27	RD 2 ●
// D	26	OCT2 ●
// C#	25	CR 1 ●
// C	24	TOM1 )
// B	23	TOM2 )
// Bb	22	HH O )
// A	21	TOM3 )
// Ab	20	KRTL C _
// G	19	TOM4 )
// F#	18	HH C )
// F	17	TOM5 )
// E	16	SNR2 )
// Eb	15	KRTL C ●
// D	14	SNR2 ●
// C#	13	SNR2 ×



how would be better to implement InstrumentSynth::loadInstrument so it creates very fast data structure that for each InstrumentSynth instance, that later will be queried from InstrumentSynth::handleMidiEvent with next requirements: it should very fast find a sound to play dependeing on several factors:
 - main factor is note (described in InstrumentSynth.instrumentSlot.articulations.notes), here we decide what articulation (KitLib::ArticulationSpec) to use.
 - latest cc values (for some instruments might be optional, used if the instrument have cc "subscriptions", that are defined in InstrumentSynth.instrumentSlot.controllers (type KitLayout::InstrumentSlot) - cc number is ContinuousController.id) latest cc values can be find from this->parent->getLatestContinuousControllerValues(), here we decide what KitLib::ArticulationSpec::ExpressionDimension to use if there was at least one defined
 - midi velocity value to find suitable velocityLayer (and neighbor layers)
 - and next starts the most difficult part: 
 -- if we have less then NAME_IT_BETTER_MIN_VARIANTS=3 sampleVariants for the velocityLayer, we need to look to neighbor velocity layers in they are not too far from selected layer velocity (NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS=32 (since midi velocity range is 0-127))
 --




const Sample* SampleSelection::get(level_t level, std::size_t pos) {
    const auto& samples = powerlist.getPowerListItems();
    if (!samples.size()) {
        return nullptr; // No samples to choose from.
    }

    std::size_t index_opt = 0;
    float power_opt { 0.f };
    float value_opt { std::numeric_limits<float>::max() };
    // the following three values are mostly for debugging
    float random_opt = 0.;
    float close_opt = 0.;
    float diverse_opt = 0.;

    // Note the magic values in front of the settings factors.
    const float f_close = 4. * settings.sample_selection_f_close.load();
    const float f_diverse = (1. / 2.) * settings.sample_selection_f_diverse.load();
    const float f_random = (1. / 3.) * settings.sample_selection_f_random.load();

    float power_range = powerlist.getMaxPower() - powerlist.getMinPower();
    // If all power values are the same then power_range is invalid but we want
    // to avoid division by zero.
    if (power_range == 0.) { power_range = 1.0; }

    // start with most promising power value and then stop when reaching far values
    // which cannot become opt anymore
    auto closest_it = std::lower_bound(samples.begin(), samples.end(), level);
    std::size_t up_index = std::distance(samples.begin(), closest_it);
    std::size_t down_index = (up_index == 0 ? 0 : up_index - 1);

    float up_value_lb;
    if (up_index < samples.size()) {
        auto const close_up = (samples[up_index].power - level) / power_range;
        up_value_lb = f_close * pow2(close_up);
    } else {
        --up_index;
        up_value_lb = std::numeric_limits<float>::max();
    }
    auto const close_down = (samples[down_index].power - level) / power_range;
    float down_value_lb = (up_index != 0 ? f_close * pow2(close_down) : std::numeric_limits<float>::max());

    std::size_t count = 0;
    do {
        DEBUG(rand, "%d %d", (int) up_index, (int) down_index);

        assert(down_index <= up_index);

        std::size_t current_index;
        if (up_value_lb < down_value_lb) {
            current_index = up_index;
            if (up_index != samples.size() - 1) {
                ++up_index;
                up_value_lb = f_close * pow2((samples[up_index].power - level) / power_range);
            } else {
                up_value_lb = std::numeric_limits<float>::max();
            }
        } else {
            current_index = down_index;
            if (down_index != 0) {
                --down_index;
                down_value_lb = f_close * pow2((samples[down_index].power - level) / power_range);
            } else {
                down_value_lb = std::numeric_limits<float>::max();
            }
        }

        auto random = rand.floatInRange(0., 1.);
        auto close = (samples[current_index].power - level) / power_range;
        auto diverse = 1. / (1. + (float) (pos - last[current_index]) / settings.samplerate);
        auto value = f_close * pow2(close) + f_diverse * diverse + f_random * random;

        if (value < value_opt) {
            index_opt = current_index;
            power_opt = samples[current_index].power;
            value_opt = value;
            random_opt = random;
            close_opt = close;
            diverse_opt = diverse;
        }
        ++count;
    } while (up_value_lb <= value_opt || down_value_lb <= value_opt);

    DEBUG(rand, "Chose sample with index: %d, value: %f, power %f, random: %f, close: %f, diverse: %f, count: %d", (int) index_opt, value_opt, power_opt, random_opt, close_opt, diverse_opt, (int) count);

    last[index_opt] = pos;
    return samples[index_opt].sample;
}




 

  how is better, do we need some multidimensional selection class like SampleSelector, or some different approach?
 




there will be some special rules for mapping midi notes to articulations per instrument and during play time in order to support choke messages for each cymbal instrument
   or to support hi-hat pedal cc messages to select specific articulation based on cc value+velocity,
   or to support multiple midi notes triggering same articulation (like snare center triggered by 38 and 40 notes should play rimshot articulation when center hit note received in a short window after edge hit note etc)
   
  need to make mvp of some global realtime logic (or maybe state machine) that monitors what midi note (=articulation, like hihat bow/edge/bell) was played and for how long (is it still < sustainTimeoutMs)
   and what is available cc state now and what wa before and did it raise or became lower and play crossfaded different articulation based on that, 
    examples:
      hihat pedal cc value rising should crossfade from closed articulation to open articulation based on cc value ranges etc, 
      polyphonic aftertouch for cymbal type instruments should choke them depend on aftertouch value, if it was not maxed out then just lower volume of played articulation and if aftertouch value became lower after that+cymbal still sounds, it should a little make up the volume back etc etc etc, 
        
        so need to think through all that logic and make some design for it




### MIC
need to make engine to use MicLayout with definition of mic destinations so any loaded into InstrumentSlot InstrumentSpec samples can be routed according to that

3) Implement mic-layout → bus routing

mics:
  - name: Overheads
    code: ons
  - name: Kick In
    instrument: kick1
    code: dim
  - name: Kick Out
    instrument: kick1
    code: dom
  - name: Snare Top
    instrument: snare1
    code: dtm
  - name: Snare Bottom
    instrument: snare1
    code: dbm


mic code examples (look about code format in MicSpec in KitLibModel.h):
ons=overhead normal stereo
oni=overhead normal midside
ows=overhead wide stereo
dtm=direct top mono
dbm=direct bottom mono
dim=direct in mono
dom=direct out mono
rcm=room close mono
rfs=room far stereo


 MicLayout describes MicSpec 
     map slot outputs to physical outputs.
samples need to be routed correctly to outputs according to the selected mic placement/layout.


Approach:
Interpret MicLayout descriptors into a routing table: slotId (or instrument mic) → destination bus(es).
Store routing info in engine-level structure so it can be changed at runtime and used by processBlock to perform mixing.


4) Implement dynamic runtime re-routing (applyMicLayout)
Goal: Allow switching mic layouts on the fly and update routing for all existing slots.
Why: Users should be able to change routing without restarting or reloading instruments.
Approach:
Recalculate routing table and update references in all slots.
If needed, resize per-slot buffers to match new mic channel counts.
5) Update processBlock: render into per-instrument mic buffers and mix to outputs 🎛️
Goal: Each slot should render to its own mic buffer(s). The engine then routes/mixes those to main outputs according to mic routing.
Why: This separates sound generation (slot) from routing/mixing, allowing more flexible output configurations and per-mic processing.
Approach:
For each slot: find its mic buffer, clear it, call slot.renderNextBlock into buffer.
After all slots rendered, iterate routing table and mix channels from slot buffers into the main output buffer (respect channel counts and levels).
Ensure sample counts and block sizes are respected.
6) Handle prepareToPlay and buffer sizing (move buffers to slots) ⚖️
Goal: Ensure buffers are sized based on sample rate and block size and decide where buffers are owned.
Why: Current hard-coded 2×1024 is unsafe. Buffers must match sample rate/block and mic channel counts.
Approach:
In prepareToPlay, set sampleRate and blockSize in engine and pass to slots.
Prefer moving mic buffers into InstrumentSlot so each slot owns its buffers. Slots can size their buffers according to number of mics assigned to them.
7) Implement presets / instrument assignment save & load 💾
Goal: Allow saving and restoring which kit + instrument is assigned to every instrument slot (e.g., "kick1:NakedDrums/kick-22").
Why: Users want to save and recall instrument assignments with presets or sessions.
Approach:
Define a simple format: lines or YAML entries mapping slotId → kitCode/instrumentCode.
When loading state, parse assignments and call a method on each slot to load the right InstrumentSpec from the LibraryManager.
Add validation and fallback when assignments reference missing kits or instruments.


 Analyze this codebase to generate or update `.github/copilot-instructions.md` for guiding AI coding agents.

Focus on discovering the essential knowledge that would help an AI agents be immediately productive in this codebase. Consider aspects like:
- The "big picture" architecture that requires reading multiple files to understand - major components, service boundaries, data flows, and the "why" behind structural decisions
- Critical developer workflows (builds, tests, debugging) especially commands that aren't obvious from file inspection alone
- Project-specific conventions and patterns that differ from common practices
- Integration points, external dependencies, and cross-component communication patterns

Source existing AI conventions from `**/{.github/copilot-instructions.md,AGENT.md,AGENTS.md,CLAUDE.md,.cursorrules,.windsurfrules,.clinerules,.cursor/rules/**,.windsurf/rules/**,.clinerules/**,README.md}` (do one glob search).

Guidelines (read more at https://aka.ms/vscode-instructions-docs):
- If `.github/copilot-instructions.md` exists, merge intelligently - preserve valuable content while updating outdated sections
- Write concise, actionable instructions (~20-50 lines) using markdown structure
- Include specific examples from the codebase when describing patterns
- Avoid generic advice ("write tests", "handle errors") - focus on THIS project's specific approaches
- Document only discoverable patterns, not aspirational practices
- Reference key files/directories that exemplify important patterns

Update `.github/copilot-instructions.md` for the user, then ask for feedback on any unclear or incomplete sections to iterate.

GitHub Copilot: Chat took too long to get ready. Please ensure you are signed in to GitHub and that the extension `GitHub.copilot-chat` is installed and enabled.



## Development
based on Pamplejuce

init:
`git submodule update --init --recursive`

1. Runs C++23
2. Uses JUCE 8.x as a git submodule (tracking develop).
3. Uses CPM for dependency management.
3. Relies on CMake 3.25 and higher for cross-platform building.
4. Has [Catch2](https://github.com/catchorg/Catch2) v3.7.1 for the test framework and runner.
5. Includes a `Tests` target and a `Benchmarks` target with examples to get started quickly.
6. Has [Melatonin Inspector](https://github.com/sudara/melatonin_inspector) installed as a JUCE module to help relieve headaches when building plugin UI.

It also has integration with GitHub Actions, specifically:

1. Building and testing cross-platform (linux, macOS, Windows) binaries
2. Running tests and benchmarks in CI
3. Running [pluginval](http://github.com/tracktion/pluginval) 1.x against the binaries for plugin validation
4. Config for [installing Intel IPP](https://www.intel.com/content/www/us/en/developer/tools/oneapi/ipp.html)
5. [Code signing and notarization on macOS](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/)
6. [Windows code signing via Azure Trusted Signing](https://melatonin.dev/blog/code-signing-on-windows-with-azure-trusted-signing/)

It also contains:

1. A `.gitignore` for all platforms.
2. A `.clang-format` file for keeping code tidy.
3. A `VERSION` file that will propagate through JUCE and your app.
4. A ton of useful comments and options around the CMake config.

