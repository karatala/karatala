![Karatala](assets/images/karatala.png)
[![](https://github.com/karatala/karatala/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/karatala/karatala/actions)

Karatala is a drum kit engine instrument for e-Drummers and everyone.


DO NOT CHANGE THIS FILE, its for my manual temporary editing only!



remove base62ToInt with related stuff, we obsoleted it, instead for decay envelope we now use library kit owned cvs files





do we need to reorganize code better, like move out from drumKitEngine.cpp to some separate file(s?) and rethink how to better name namespaces:
namespace DrumKitEngineIdentityMapHelpers
namespace DrumKitEnginePreviewDetail
namespace DrumKitEngineMapTextDetail
namespace DrumKitEngineVelocityResponseHelpers
namespace DrumKitEngineLibraryOverrideDetail
namespace DrumKitEngineMapDetail
namespace DrumKitEngineMapHelpers
also check for usage of the code, like might be questionable stuff like below or even worse
std::optional<bool> inferAutomaticFlipLR(const KitLayout::InstrumentSlot& slot, const KitLib::InstrumentSpec& libraryInstrument, const InstrumentMap::Layer& mapLayer) { //FIXME: what is this wrapper for?
    return DrumKitEngineMapDetail::inferAutomaticFlipLR(slot, libraryInstrument, mapLayer);
}

similar in KitLibModel:
namespace KitLibIdentityHelpers
namespace KitLibSampleMetadataHelpers
namespace KitLibCalibrationHelpers
in InstrumentMap:
namespace InstrumentMapDetail
namespace InstrumentMapDetail



here is unrelated tasks description but maybe we need to somehow keep them in mind designing this architecture, and maybe made some corrections to plan:

according "Use the median response of round robins at each recorded velocity first. Report outliers instead of automatically flattening each round robin." - TODO: + probably separate unrelated task: 


there might be instruments with 2 or less round robins but with many velocity levels, so for them only we need to have some special logic - instead of exact velocity matching there we need to use something similar to drumgizmo approach - see docs/drumgizmoFloatingVelocityGroups.cpp where we use primarily list of available samples by their 'power' maybe even ignoring velocity levels and round robins, need to plan well







according "it analyzes every file separately instead of grouping one hit across mic files" - it should do  every file separately, we write to each sample filename its calculated metadata with --rename and use that in engine, what is wrong about it? 
does it clarify plan a little? should we include that to this plan? let’s negotiate, dont make md changes yet




app need to use everywhere only 1 font with 2 possible sizes (normal and small) and limited colors selection (primary secondary danger success info) and standartized buttons sizes etc


the original idea was to review the codebase and make plan for improvements like:
repeating code that can be moved to reusable helper
inoptimal emory alignment, is there places where we can reorder some props to save some ram?
memory allocations that can be optimized
check where string comparison used and where could be better to replace strings with StringsDictSingleton

so can u modify the plan md doc, maybe split/remove something from it and add there proper tests and benchmarks planning. main goal is very readable, reusable and performant code, since currently project got pretty bloated. its unreleased yet so there are some experimental unfinished ui, but it works kinda well now, except some places, so i wanted to make the code base more compact and readable

TODO: apply processing to cache - plan how to save cpu and time by applying sample reference volume/curve and future effects to cached sample data, make dedicated 'bake cache' button that applies effects to cached data (and maybe even merge layers - but that probably not so easy or even impossible/pointless, since layers can have different roundrobins and velocity level numbers)
 - when we have sample trim volume compensation, wouldn't it be better to also apply that to cache when reading the sample.


todo: build is too slow and often rebuilds too many .cpp files, also after building vst building tests rebuilds same files and then benchmarks also rebuilds many same files, can it be optimized somehow to better reuse stuff

FIXME:
- statusbar shows some text that sometimes dont fit if its multiline, what text it can show and why/when? 




- need to standartize fonts and their sizes, now there are to many different, need to keep 2 or 3 at maximum, one is normal and small (title size maybe no need)

only there are some unfinished/bad ux on some tabs, many fixme/todo comments by me in the code to be reviewed/addressed 










questionable features:
- in inst tab table header need to add switch button in top right corner( instead of + and x buttons column empty header that will switch compact controls to bleed controls and back, so 
- inst tab need to have some 20px width interactive 'link' area between instrument picker and tuning control, so clicking ot that should work similar to ckechbox, but ou can select many underlying rows by moving mouse up/down while left button pressed, and that selected layers should have their controls linked together, so adjusting one change same thing for all selected




docs\TODO_LIBRARY_SNAPSHOT_PLAN.md
Phase 4 - Verify Reload Behavior
Phase 5 - Optional Articulation-Level Reload, only if later needed

docs\TODO0X_PER_INSTANCE_MIC_LAYOUT_PLAN.md
Phase 2 - VST Wrapper State-Driven Reconstruction
Phase 3 - Bus Rename Fallback / Dynamic Bus Name Refresh

docs\TODO05_INSTRUMENT_MAP_PLAN.md
VST Automatable Parameters / APVTS, deferred
Some optional params like hold/curve/solo/mute/fx chain remain future work

docs\TODO10_UI_REVIEW_REFACTOR_PLAN.md
Phase 3 - Architecture split, still partial
Phase 4 - Performance improvements, partly done
Phase 5 - Polish and consistency, still ongoing

docs\TODO12_HIHAT_TRANSITION_FIX_PLAN.md
Phase 4 - Original preload/group lookup path was not completed, it was mostly removed/reworked
Known remaining perf debt: bounded 1024-sample validation scan

docs\UI_REVIEW_PLAN.md
Phase 3 - Architecture
Phase 4 - Polish
Phase 1 and Phase 2 look mostly done.

docs\AUDIO_THREAD_PERF_PLAN.md
P1-A - Aftertouch choke gain still appears per-sample/per-call sensitive
P4 - Hi-hat CC target RMS / validation scan remains
P5 - Voice estimated dB work was improved/throttled, but not fully gone

need to think about next tasks and write md plan
therminology: layer - instrument articulation with params assigned for some pad thru pad popup or inst tab.
tasks rough description: instruments for different kits can be recorded with different volume and samples of them can be named using different velocity 'curve', sometimes missrepresenting the loudness, so midi velocity can have different effect on them even if we compensate gain of samples accordint velocities and overall sample gains. main goal here is to have all kits instruments to play max velocity notes globally with the same loudness and lover velocity notes also follow some globally defined loudness curve for predictable user controlled velocity curve config. so need to think about way to intelegently apply/save some custom curves to specific instruments/articulations automatically (with ability to change it manually for any instrument/articulation), even when instrument slot is layered (have several different instruments articulations layered for that slot), need to make it with combination with analyze_drum_samples.py (which will need some improvements for that, so it can analyze sample library and apply to folders/filenames some engine-compatible parsable extra params (which can also later be used for modifying sample audio files by applying gain and maybe offset changes etc (we have some kits with samples recorded using different audio interfaces, so direct/room/oh mics can be out of sync),  also new features for py like pan detection (as an extra code in range L127-C0-R127) for stereo samples and instrument folders, based on overhead mic, since its the most common one. pan we should use for layer panning - mainly on overhead mic types and mono mics that routed to stereo out/aux, since room mics are usually far and direct mono mics are mono and panned outside of the plugin on DAW level, we can probably skip them, only maybe room mics can be flipped sometimes if panning dramatically changed (like from LXXX to RXXX or opposite)





ui changes:


check and briefly list what knobs/sliders/interactive number inputs we have, what custom components are they using



need to think how should we pan extra mics for manual routing



can u review the codebase and make plan for improvements like:
- repeating code that can be moved to reusable helper
- inoptimal emory alignment, is there places where we can reorder some props to save some ram?
- memory allocations that can be optimized
- check where string comparison used and where could be better to replace strings with StringsDictSingleton


replace em dash characters with hyphens



there are plenty ugly namespace { ... } blocks in the middle of code, need to analyze use cases and is there already repeating functionality somewhere and reorganize first those empty namespace wrapped functions to some helper file, if there are  too many lines then maybe to several files< so helpers are a little categorized and not too bloated (<700  lines or about that)







thanks, half fixed, now only remained problem with fallback of empty articulation, maybe this we better review -how it implemented and plan changes/improvements:
normally if instrument have >1 articulations and nothing asigned for them, then we assigned something to one articulation, so playing that note plays it, and playing any other instrument notes if corresponding articulation dont have assigned layers it should play same layers as main, so in our example we have stax1 with bow and edge (both empty), assigned for bow some instrument then no mater what articulation (bow/edge) we play it should make sound of assigned, until there is something else got assigned, so playing empty edge it plays all layers of bow (maybe we should do this in some different hook so we just reuse existing stuff). currently stax1 has bow with 2 layers (splash+china in this order), and when playing bow all good, but when playing edge - edge plays only 1st layer (splash). also we have more advanced cc-controlled instruments like hihat, there is some more complex stuff happens with pdl_opn_cls cc values and articulation names like edge[close] bow[close] edge[open] bow[open] etc - for hihat now all works good but maybe its a little confusing from user perspective - 



TODO0A: get rid of juce completely, decide on what plugin format is needed, maybe only clap or keep also vst3, find better framework/tools to replace juce quirkiness or give up and use ui https://github.com/ImJimmi/JIVE

TODO0X: avpts on plugin init to decide what micLayout to use?


TODO: after routing-plan done, need to make preview of instrument articulation layers (can be found in pads context popup)  play not first found mic (or how its doing it now) but select mic to play using rule: first - direct mono mics (d*m, like dtm/dim/dom) if present and overhead stereo mics (codes like ons) and room stereo mics (rfs) - all 5 channels together, preview from disk (like its probably already doing now) - need to check/confirm





TODO: ignore low velocity notes on polyaftertouch changes (min velocity to conf ui, 1-20, default 10
TODO: add crosstalk support - midi filter for instruments since crash or splash can be near tom and hitting one always can produce low velocity hit for another instrument. need to add some button on Map page to record users drumkit x-talk profile womething like this (or suggest better ideas for better ux) user interactively following instructions and hit with max velocity all the notes for instruments sequentially, one at time, so plugin reads hit and records other sounding notes, so this data later getting saved and used in runtime to filter those crasstalking midi from e-kit)





TODO: review hihat openness level sensitivity, think what control to add on ui for adjusting choice of hihat openness articulation between fully opened and tight together with pdl_opn_cls level if provided, mayube this even should be defined per hihat instrument, so maybe will need some user overrides config for instruments


TODO: add to kit map  array setting with neighbor pads that can trigger crosstalk and add fn in midi processor for cancelling it so for example hitting toms rim wont trigger low velocity crash if difference in velocity is less then configurable threshold (that is also to add in conf ui, like 10-100, default 70)


TODO: make ui code more compact overall by using some kind of ui components to less repeat stuff and make it more clear/readable

git submodule add -f -b develop https://github.com/juce-framework/JUCE/ JUCE
git submodule add -f -b main https://github.com/sudara/melatonin_inspector.git modules/melatonin_inspector
git submodule add -f -b main https://github.com/sudara/cmake-includes.git cmake
git submodule add -f -b main https://github.com/free-audio/clap-juce-extensions.git modules/clap-juce-extensions

git add .gitmodules JUCE modules/melatonin_inspector cmake modules/clap-juce-extensions
git commit -m "Add project submodules"
git submodule status --recursive


having realistic multiarticulation/expression/velocity/rr/etc drumkit engine that works with many sampled audio files of namy instruments (drums/cymbals). now in order to make it even more realistic and smart, and also save cpu and ram
by ending voices smart way, for example when playing 

for example make realistic hihat pedal changes after hit on closed/opened hihat, so if closed hitted and pedal got more opened (cc value went down) and sound is not much decayed yet, it should crossfade switch to more opened sample in the same offset that was already played, and if hat is closing after hit on opened it should switch to more close, but need to find a way to detect offset to where to switch in all cases, to avoid double-attack sound or choke+fadein after almost silence, since length of played sounds for different openness of hihat is different. based on that thinking, what params can be detected from a cymbal sound file by analysing its waveform, so those params can be hardcoded in sample file name and used in the drumkit engine for achieving that hihat transition realist (currently have a py script that can detect attack/decay ms times for samples, etc, but it is yet very approximate and not very much helping on seamless sound construction of different samples even just in a daw, by placing them serially and crossfading, it only works when visually looking for a matching amplitude level (since samples decay exponentally but still a little different depending on openness), need some logic/formula to organise it right)

optional: is it possible to add round magic button for mouse click+hold so u can play on pads using pc keyboard and after pressing that and moving mouse up/down to control velocity or control hihat opennes by moving left/right and right click to choke?


FIXME: replace std::vector and other std shit like std::move with something better
FIXME: get rid of static_cast everywhere it is possible, change types
FIXME: sort properties in classes and structs so 1st go the heaviest for memory optimization

            const StringsDictSingleton::Id ID_bell = StringsDictSingleton::getId("bell");
            const StringsDictSingleton::Id ID_bow = StringsDictSingleton::getId("bow");
            const StringsDictSingleton::Id ID_edge = StringsDictSingleton::getId("edge");
            const StringsDictSingleton::Id ID_rim = StringsDictSingleton::getId("rim");
            const StringsDictSingleton::Id ID_sidestick = StringsDictSingleton::getId("sidestick");
            const StringsDictSingleton::Id ID_center = StringsDictSingleton::getId("center");

[FAIL] Reduce voices to use one multichannel voice instead of many 1-2 ch, and then cache one logical hit from multiple files - FAILED (performance become worse and idea is logically wrong, cause for example, room channers have different attack/length, cannot wait for them)



TODO05: need to decide what vst params to expose for avpts. instrument map - probably no, since its formatted string, nothing to automate, but some params from it maybe, (like what? pitch? attack? vol/pan/phase/flipRL of each instrument?)


TODO cache baking, for example when we have instrument articulation layered and each of layers have some fx processing and pitch and adsr applied, maybe it will be cpu+ram win if sample buffer data will be recached with already applied fx-es/attack/decays and mixed waveforms, also metadata of applied processing will remain somewhere in instrument map and on some of those params change that can be recached with new params, since there will probably be no automations on those basic instrument params (or if it will ever be needed for some reason, then we can maybe make cache baking  toggle  in instrument map params)

TODO: option: preview playback limit to 1 round robin
show vel ranges in preview wide button with diff colors, kit settings saving in yaml in instr dir


TODO: learn hit+articulations, dropdown with note name/midi number, learn for choke (polyaft or note), load/save  kitLayout, 


invert cc, current cc position, cc curve?

TODO: vel curve (global/per instr)

TODO: preview velocity sliders on drum pad square (horizontal zones)

optional:
global decay?

room reverb: amount, decay, predelay for short/long?


TODO: peak offset for diffenetn mics (found minimal for example di and take it as 0, based on that calculate for all other mics of that logical hit relatively, for example far room = 10ms etc...) smart-parse from samples and save to filename



python .\scripts\analyze_drum_samples.py -i C:\Users\Public\Documents\Karatala\sample_library 


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



how would be better to implement InstrumentPlayer::loadInstrument so it creates very fast data structure that for each InstrumentPlayer instance, that later will be queried from InstrumentPlayer::handleMidiEvent with next requirements: it should very fast find a sound to play dependeing on several factors:
 - main factor is note (described in InstrumentPlayer.instrumentSlot.articulations.notes), here we decide what articulation (KitLib::ArticulationSpec) to use.
 - latest cc values (for some instruments might be optional, used if the instrument have cc "subscriptions", that are defined in InstrumentPlayer.instrumentSlot.controllers (type KitLayout::InstrumentSlot) - cc number is ContinuousController.id) latest cc values can be find from this->parent->getLatestContinuousControllerValues(), here we decide what KitLib::ArticulationSpec::ExpressionDimension to use if there was at least one defined
 - midi velocity value to find a suitable velocity level (and neighbor levels)
 - and next starts the most difficult part: 
 -- if we have fewer than NAME_IT_BETTER_MIN_VARIANTS=3 logical hits for the velocity level, we need to look at neighbor velocity levels if they are not too far from the selected level velocity (NAME_IT_BETTER_MAX_VELOCITY_DISTANCE_TO_USE_SAMPLES_FROM_NEARBY_VELOCITY_LAYERS=32 (since midi velocity range is 0-127))
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
  - id: overhead/ons
    name: Overheads
    code: ons
  - id: kick1/dim
    name: Kick In
    instrument: kick1
    code: dim
  - id: kick1/dom
    name: Kick Out
    instrument: kick1
    code: dom
  - id: snare1/dtm
    name: Snare Top
    instrument: snare1
    code: dtm
  - id: snare1/dbm
    name: Snare Bottom
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

need to make next changes to Map page (source\UI\MidiMapTable.cpp and related):
1. dropdown on top should also show item for 'default' layout (hardcoded in KitLayout::getDefault)
2. dropdown dont need rounded corners and need to be half of its current width, position more on right side of top toolbar, have "Layout" label before it
3. more ('...') button showing context menu with default juce theme colors, need to make it follow UITheme colors instead
4. some instruments in the Map page table have CC controls, currently those controls are the same as for note assignment (4 buttons [note1][learn1][note2][learn2] where 'noteN' showing for example "38 / D 1" and midi key popup on click), need to change it to have different controls, for now just one dropdown with numbers 0-127 / GM cc name to assign control number, no learn buttons needed for cc or second alternative assignment buttons




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

# python3 scripts/generate_sample_metadata.py -i ./sample_library --report-folder ./reports
