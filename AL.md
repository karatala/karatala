


need to plan instrument picker changes, currently it looks like this:
[filter field... ][v]related
list of instr [preview][use]
but need to modify it 
the idea is to have quick and convenient way to find/preview/select instruments, one of options i was thinking is to add category-filter buttons, like instead of '[v]related' checkbox add switch-button [drums|cymbals|other]
that will look something like 

[filter field... ][all|drums|cymbals|other] //cymbals selected, highlighted
[all     ]| list of instr [preview][use]
[hihat   ]| ...
[ride    ]| 
[crash   ]| 
[splash  ]| 
[china   ]| 
[bell    ]| 
[stack   ]| 
[chopper ]| 
[gong    ]| 
[karatala]| 




------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------

after this changes now hihat1 note 42 (hihat[tight]) plays hihat[open] and there are other problems like when applying for tom1 rim some instrument it seems applying it only randomly sometines - by randomly sometimes i mean you chose first layer instrument for tom1 rim, it does not play at all, you tune different knobs - no effect, you change the instrument to some another - same, then you changing again to 3rd, it suddently starts playing that one, then you change to first you chose before - it again plays, then you apply raw map with the same changes that are applied - it starts playing for tom1 rim tom1 head instrument like if fallback happened but with layered rim instrument layers. raw map is:  
snare1/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/snare-X1/center:tuning=20
snare1/rim:NakedDrums by WilkinsonAudio [CC-BY-4.0]/snare-X1/center
snare1/rim:NakedDrums by WilkinsonAudio [CC-BY-4.0]/snare-X2/center:tuning=60
tom1/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/tom-10/center:tuning=290
tom2/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/tom-12/center:tuning=110
tom3/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/tom-13/center:tuning=60
tom4/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/tom-14/center
tom5/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/tom-16/center:tuning=60
ride1/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/ride-X1/bow:decay=3749
ride1/bell:NakedDrums by WilkinsonAudio [CC-BY-4.0]/ride-X1/bell:decay=2919
splash1/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/splash-X1/edge:tuning=170
splash2/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/splash-X2/edge
china1/edge:NakedDrums by WilkinsonAudio [CC-BY-4.0]/china-X2/edge
china2/edge:NakedDrums by WilkinsonAudio [CC-BY-4.0]/china-X1/edge
hihat1/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/hihat-X1/edge
kick1/center:NakedDrums by WilkinsonAudio [CC-BY-4.0]/kick-22/center:tuning=-20
crash1/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/crash-X1/edge:tuning=20
crash2/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/crash-X2/edge
ride2/bell:NakedDrums by WilkinsonAudio [CC-BY-4.0]/ride-X1/bow:tuning=100,flipLR=1
hihat2/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/hihat-X1/edge:tuning=154,flipLR=1
hihat2/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/china-X2/edge:tuning=134,attack=25,flipLR=1
crash3/bow:aasimonster/crash-X1/edge:tuning=-19,flipLR=1
drum1/center:aasimonster2/tom-X1/center:tuning=614,decay=219,flipLR=1
drum1/center:CrocellKit/tom-X1/center:tuning=1171,attack=69,decay=939,flipLR=1
drum2/center:aasimonster2/tom-X1/center:tuning=420,decay=219,flipLR=1
drum2/center:CrocellKit/tom-X1/center:tuning=865,attack=69,decay=939,flipLR=1
stax1/bow:NakedDrums by WilkinsonAudio [CC-BY-4.0]/splash-X1/edge:tuning=-213,decay=379
stax1/bow:aasimonster/china-X1/edge:tuning=-346,decay=559
ride1/edge:NakedDrums by WilkinsonAudio [CC-BY-4.0]/ride-X1/bow:tuning=-176,attack=19,decay=789
ride1/edge:Salamander/ride-X2/edge:tuning=-192,decay=2799
snare2/center:tchackpoum_drumgizmo_13/snare-14x6.5/centerzon_ctr_rim064
snare2/rim:tchackpoum_drumgizmo_13/snare-14x6.5/rim
snare2/sidestick:tchackpoum_drumgizmo_13/snare-14/sidestick
tom2/rim:aasimonster/tom-X2/center
tom1/rim:aasimonster/tom-X1/center:tuning=-1958,attack=147
tom1/rim:tchackpoum_drumgizmo_13/snare-14/sidestick:tuning=-1728,attack=215


maybe we need to fully review the changes and  check for problems.

then redesign/rewrite overall approach for instrument map assignments, but first clarify what is happening now, and reimagine instrument assignment popup,
like add some flag for instrument popup similar to rolands H&R (head and rim sync) so we separate entire instrument change (together with all articulations) from dedicated articulation change somehow.
also we probably should always explicitly assign all articulations in instrument map, except cases when library instrument have only one articulation - then it assigns to all empty slot articulations of that instrument.

like we rightclicked tom1 center to open popup, 
if checked H&R sync on (or better name it some different way with meaning of replacing entire instrument, or maybe that 'replace' checkbox better to install inside instrument picker popup on top of left instrument kind select-box) 
then list displays not all articulations of instruments but only instrument records, so when we will press use  (like kitA tom1 to kitB tom1) it will clean and all art of that instrument slot and fill them with available articulations of selected one
if checkbox unchecked - we see all available articulations (not only instruments records) and changing/adding only articulations. also another thing here is hihat articulations, it shows only base articulations in list (like kitName/hihat-x1/edge, not all kitName/hihat-x1/edge[close], kitName/hihat-x1/edge[half], kitName/hihat-x1/edge[open],... ).



also about previewing the instruments: what mics it selects now for preview? sounds like sometimes it takes some 1st available mic and previews it in left channel only, - we need to change this so on preview disk streaming plays overhead stereo mics (code ons) thru overheads out and direct mics thru available for that inst slot direct mics (if there are any)











------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------

next need to plan some  settings in ui for instruments when they have cc controlled articulations like particularly hihat has pdl_opn_cls or snare has zon_ctr_rim  and some cymbals can have zon_bow_edg

ExpressionDimension {
        static constexpr TypeEntry typeTable[] = {
            {"pdl_opn_cls", Type::PedalOpenClose},
            {"zon_ctr_rim", Type::ZoneCenterRim},
            {"zon_bow_edg", Type::ZoneBowEdge},
            {"zon_lft_rgt", Type::ZoneLeftRight},
        };
so for example instrument has articulations folders like this
hihat-X1\
    edge[open]-pdl_opn_cls24\
    edge[half]-pdl_opn_cls52\
    edge[close]-pdl_opn_cls78\
    edge[tight]-pdl_opn_cls85\
    pedal-pdl_opn_cls85\
we need to be able to have implicit range points for cc controlled articulation definitions that can be tuned in some global config or better in kit layout config, so we can simplify instrument's articulations folder naming to not have hardcoded cc values, but have optional steps of those values, example if instrument have only one level of each openness:
    edge[open]\
    edge[half]\
    edge[close]\
    edge[tight]\
    pedal\
and we have coresponding pdl_opn_cls default values in global config and it can be also overriden in kitLayout config
but need to think how should we do in cases when we have more detailed articulations like this:
    edge[open]-pdl_opn_cls013\
    edge[open]-pdl_opn_cls022\
    edge[open]-pdl_opn_cls031\
    edge[half]-pdl_opn_cls040\
    edge[half]-pdl_opn_cls049\
    edge[half]-pdl_opn_cls058\
    edge[close]-pdl_opn_cls067\
    edge[close]-pdl_opn_cls076\
    bow[tight]-pdl_opn_cls085\
    pedal-pdl_opn_cls085\







    pedal[splash]-pdl_opn_cls085\ // and this splash is separate case for other TODO - currently we have cc pedal controlled behavior where it sounds almost like real hihat including splashing because we have hihat pedal transition logic, so for hihat maybe its not big priority but would be nice to make feature if pedal[splash] articulation is available, then we extend that logic detecting footsplash for hihat (basically when pedal was hitted to close withoul resting in closed position for some timeout and almost instantly opened). 
also another TODO is a little similar to pedal[splash] but about cymbal choke - if instrument have choke articulation, then we need to play it on choke (in polyaftertouch logic) instead of just muting the sound.




then we also need to be able to adjust in ui (like in a pad instrument config popup) the overrides for this numbers
and this should change for all articulations of that instrument instance that use this expressionDimension
so we also probably need to rethink the implementation of popup, maybe like add there some controls like checkbox for changing all articulations of the instrument at same time, for example if we change mic bleed for one articulation we suppose to change also all other articulations of that same kit instrument loaded to other articulation slots and similar dedicated checkbox for toms so all articulations of all toms can have similar processing (layers of same kit at least) need to plan the ui



------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------

B	59	RD 1 )	RD 1 )		B	35	KICK 2
Bb	58	VSLP	TOM3 )		Bb	34	RD 2 )
A	57	CR 2 )	CR 2 ●		A	33	CR 2 ●
Ab	56	CWBL			Ab	32	KRTL O ●
G	55	SP 1 )	CR 1 )		G	31	SP 1 ●
F#	54	TAMB			F#	30	Sp 2 )
F	53	RD 1 ^	RD 1 ^		F	29	RD 2 ^
E	52	CH 1 )	CR 2 )		E	28	CH 1 ●
Eb	51	RD 1 ●	RD 1 ●		Eb	27	RD 2 ●
D	50	OCT1 ●	TOM1 )		D	26	OCT2 ●
C#	49	CR 1 )	CR 1 ●		C#	25	CR 1 ●
C	48	TOM1 ●	TOM1 ●		C	24	TOM1 )
B	47	TOM2 ●	TOM2 )		B	23	TOM2 )
Bb	46	HH O ●	HH O ●		Bb	22	HH O )
A	45	TOM3 ●	TOM2 ●		A	21	TOM3 )
Ab	44	HH P _	HH P _		Ab	20	KRTL C _
G	43	TOM4 ●	TOM3 ●		G	19	TOM4 )
F#	42	HH C ●	HH C ●		F#	18	HH C )
F	41	TOM5 ●	TOM4 ●		F	17	TOM5 )
E	40	SNR1 )	SNAR )		E	16	SNR2 )
Eb	39	CLAP	TOM4 )	-	Eb	15	KRTL C ●
D	38	SNR1 ●	SNAR ●		D	14	SNR2 ●
C#	37	SNR1 ×	SNAR ×		C#	13	SNR2 ×
C	36	KICK 1	KICK _		C	12	


------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------

not sure what exactly this doing,very confusing naming, why applyAdaptiveTailShortening and setTailShorteningFactor,
code was generated but probably misunderstood, the idea is to  


DEBUG: Applying tail shortening to voice 36 age=29 ageFactor=0.58 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=28 ageFactor=0.56 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=27 ageFactor=0.54 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=26 ageFactor=0.52 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=25 ageFactor=0.5 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=24 ageFactor=0.48 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=11 ageFactor=0.22 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=10 ageFactor=0.2 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=9 ageFactor=0.18 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=8 ageFactor=0.16 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=7 ageFactor=0.14 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=6 ageFactor=0.12 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=5 ageFactor=0.1 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=4 ageFactor=0.08 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=3 ageFactor=0.06 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=2 ageFactor=0.04 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=1 ageFactor=0.02 load=0.7 compression=1 finalFactor=1
DEBUG: Applying tail shortening to voice 36 age=0 ageFactor=0 load=0.7 compression=1 finalFactor=1







------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
reread DrumKitEngine.cpp 

for now need to only plan and confirm with me implementation of task2. it is not done. it should not use InstrumentSynth::selectSampleFor since it is not working draft. InstrumentSynth::playNote




need to review the code and plan implementation of proper multi-mic audio rendering
the idea is:
first probably on DrumKitEngine level need to save once all the output channels ptrs of PluginProcessor (32 channels that described as 16 stereo outs) so those ptrs will be used in DrumKitEngine::applyMicLayout to rebuild channel mapping according to MicSpec and described below logic:

the logic example is when we have MicLayout.mics like:
1. name: Overheads, code: ons, instrument: '' specs: ... // ons - s=stereo (described in specs) so first 2 channels go here as left+right, since instrument is empty, there will be mapped all with same MicSpec
2. name: Hi-Hat, instrument: hihat1 code: dtm, specs: ... // dtm - m=mono - 1 channel, and since instrument=hihat1, all instruments>art>vel>SampleVariantSpec with the same MicSpec and same instrument id (from instrumentSpecsMap with same id) should output to that channel, or if there is not existing code like hihat2, then taking by same type=hihat.
3. name: Ride, instrument: ride1 code: dtm, specs: ...
4. name: Kick In, instrument: kick1 code: dim, specs: ...
... 
if same MicSpec not found - LOG(Warning, ...)

SampleVariantSpec has MicSpec that describes where to route audio of that sample

when do InstrumentSynth::loadInstrument and looping thru each varSpec : velSpec->sampleVariantSpecs, varSpec there have all posible sample variants for all microphones, 
so maybe need create new entity class in KitLibModel as a child of SampleVariantSpec with latest entity for samples that will hold something like InstrumentSpec::SampleFileInfo data (or maybe some other better way?) or to "group" end samples somehow by SampleVariantSpec.letter, so for all coresponding micSpec channels should play corresponding sample variant later in InstrumentSynth::playNote (selectSampleFor with using some mic specifying param (maybe micIdx not the best implementation) 









------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------



it calculates micBufferChannelStart wrong.
td::vector<int> micStarts; // per-articulation mic buffer start indices for variants - does not make any sense
 it should check the engine current active micLayout and find corresponding output from mapping (that should happen 



>Round-robin / alternate variants: currently the code plays all samples in the layer. If you want round-robin for alternate samples for the same mic (e.g., multiple round-robin files per mic), we need to group samples by mic identity (micSpec.raw or letter) and apply round-robin within each group while still playing all groups. I can implement that grouping if you want.

now it does not play anything. why you removed core concept
// Select a sample using runtime tables
    SampledSound* chosen = this->selectSampleFor(artIdx, static_cast<int>(velocity));

    it was supposed to extend it to select many     vector<SampledSound*> chosen = this->selectSamplesFor and then play a selected round-robin samples  for all mics at the same time


------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------







InstrumentSynth::SampleEntry


DrumKitEngine::processBlock
InstrumentSynthVoice::renderNextBlock
-----------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------




1 Какое семантическое правило lookup?

d) строго диапазон (как сейчас), но наверное нужно переименовать стратегии ZoneSplitStrategy { Boundary, MidSplit } 

2 Boundary strategy — это:
предполагаемо MIDI-style zoning. код уже работает идеально, там есть и другие тесты которые не были отправлены

3 Допустим ли leaf с несколькими samples, или всегда должен быть 1?
это тоже нужно доработать.

глобальная идея такая:
нужно сделать класс реализующий быстрый производительный, читаемый и гибкий выбор семплов в нескольких dimensions, а также их предварительное добавление
этот класс будет использоваться в 2 фунциях другого класса,


выбор зависит от нескольких динамических факторов которые определяются в конструкторе класса при добавлении данных




 * LibraryManager
 *   └─ DrumKitSpec
 *      └─ Instrument
 *          └─ Articulation
 *              └─ VelocityLayerSpec
 *                  └─ SampleVariantSpec




void InstrumentSynth::loadInstrument(InstrumentSpec* kitInstrument) {
    if (kitInstrument == nullptr) return;
    this->unloadInstrument();
    this->instrumentSpecstPtr = kitInstrument;
    this->type = kitInstrument->meta.type;

    for (const KitLayout::Instrument::Articulation& layoutArt : this->instrumentSlot.articulations) {
        DBG("  layout articulation " + layoutArt.code);
        bool foundKitArt = false;
        for (const std::unique_ptr<ArticulationSpec>& kitArtPtr : kitInstrument->articulations) {
            if (layoutArt.code != kitArtPtr->name) continue;
            foundKitArt = true;
            DBG("    instr articulation " + kitArtPtr->name);

            for (const auto& velPtr : kitArtPtr->velocityLayers) {
                DBG("      velocity " + juce::String(velPtr->velocity) + " (" + juce::String(velPtr->sampleVariants.size()) + " samples)");
                for (const auto& variantPtr : velPtr->sampleVariants) {

                    auto* sound = new SampledSound(kitInstrument, variantPtr.get(), variantPtr->sampleFile, layoutArt.notes);
     
                }
            }
        }
        if (!foundKitArt) LOG(Warning, "no kit articulation found for layout art: " + layoutArt.code);
    }

в методе другого класса 






как лучше использовать класс SampleSelector или можно все сделать проще без него?






-----------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------



how to better name the namespaces, since now having 3 very similar class hierarchies and often getting confused in the code:


"Instrument" is kinda main concept entity, that have articulations and other sub stuff, and can be grouped to DrumKitSpec (on sample library level is like a "pack of instruments", on engine/layout level is like a "slot" for instrument of "sample lib" level)


here are sample library files related classes that provide files that are stored on latest level (SampleVariantSpec) and wrapped/groupped in all that hierarchy, that is parsed from a specific structure standartized named folders/files
source\Engine\LibraryManager.h:
   class LibraryManager
   class DrumKitSpec;
   class InstrumentSpec;
   class ArticulationSpec;
   class VelocityLayerSpec;
   class SampleVariantSpec;

here are mapping related classes that defines layout of engine's "slots" for instruments and possible midi notes and other controls that affect playback of samples in engine
source\Engine\LayoutManager.h:
  class LayoutManager
  struct KitLayout {
    struct Instrument {
        struct Articulation {
          struct Dimension {}
        }
        struct CC {}
    }
  }

here are fundamental classes that construct/load/reload and use almost all other classes inside itself in runtime
source\Engine\DrumKitEngine.h:
   class DrumKitEngine;
   class InstrumentSynth : public juce::Synthesiser
   class InstrumentSynthVoice : public juce::SynthesiserVoice
   class SampledSound : public juce::SynthesiserSound










------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------






need to organize thoughts and clarify plan
around convention ${kitInfo}/${instrumentName}-${size}-${description}/${instrumentType}-${articulation}-${velocityLayer}${sampleVariant}-${micChain}.${ext}
ex: kits/NakedDrums by WilkinsonAudio [CC-BY-4.0]/kick-22-YamahaRecordingCustom/kick-center-1a-dim.flac
need to create classes or structs? for  DrumKit, DrumKitInstrument, DrumKitInstrumentArticulation
{DrumKit}
  name
  author
  license
  instruments[]
      ? wtf findInstrument(name).
  {DrumKitInstrument}
    name
    type(drum|kick|tom|snare|cymbal|hihat|percussion)
    size
    tuning
    description
    kit(prt to parent)
    articulations[]
  *  effects[]
  **  micChain
    {DrumKitInstrumentArticulation}
      name
      instrument(prt to parent)
      velocityLayers[]
      {ArticulationVelocityLayer} 
        articulation(prt to parent)
        sampleVariants[]
        playCondition(s) (notes,cc,...)
        playState
        duration
        {LayerSampleVariant} 
          velocityLayer(prt to parent)
          sampleFile
          duration
          outputMic(DrumKitInstrumentMicOutput)

DrumKitInstrumentMicOutput
 to select what output channel(s) to route to based on



need fast optimized and compact fn to dynamically add/load kits with samples (refresh library)
 that go thru kitsDir and create kits for each kit folder
 and instruments for each subfolder 
    and then find and parse containing samples for each instrument





------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------







need to change the global approach on how plugin works, 
first need to define KitLayout yaml files for each supported drum module,

what about that KitLayout like ${pluginConfig}/layouts/roland-td50.yml like this (draft structure, maybe need some changes)

name: Roland TD-50
description: TD-50 and  compatible layout
instruments:
  - id: kick
    display: Kick
    articulations:
      - type: center
        notes: [36, 35]
    output_group: kick
    type: kick
    mics:
      - direct-in-mono
      - direct-out-mono
      - direct-sub-mono
      - overhead-normal-stereo
      - room-far-stereo
  - id: snare
    display: Snare
    articulations:
      - type: center
        notes: [38]
      - type: rim
        notes: [40]
      - type: sidestick
        notes: [37]
    CCs:
      - id: 16
        type: position
        name: Snare Position
        description: Adjust snare hit position from center to rim
    type: snare
    mics:
      - direct-top-mono
      - direct-bottom-mono
      - overhead-normal-stereo
      - room-far-stereo
  - id: tom1
    display: Tom 1
    articulations:
      - type: center
        notes:
          - 48
      - type: rim
        notes:
          - 50
    type: tom
    mics:
      - direct-top-mono
      - overhead-normal-stereo
      - room-far-stereo
  - id: hihat
    display: Hi-Hat
    articulations:
      - type: bow
        notes: [42,46]
      - type: edge
        notes: [22,26]
      - type: pedal
        notes: [44]
    CCs:
      - id: 4
        type: pedal
        name: Hi-Hat Pedal
    type: hihat
    mics:
      - direct-top-mono
      - overhead-normal-stereo
      - room-far-stereo
...






then probably better to make it map available mics per instrument based on selected kit layout
then probably better to make it populate ui for drum component ui buttons based on selected kit layout 


Instrument mapper must 
Implement choke-group handling: identify and stop/choke other instruments' voices when an articulation belongs to a choke group. (InstrumentMapper should produce a list of instruments/stop-midi-notes to choke; synth voices should be stopped.)
Implement hi-hat pedal CC handling and crossfade logic in ArticulationStateMachine using InstrumentSlotCC mapping ranges to articulation ids.
Add interaction for layout-driven per-instrument controller definitions (controller idString like "hh_pedal") and map incoming CC messages in processBlock() into mInstrumentMapper->setControllerValue(...).
Add unit tests for parsing and runtime mapping (e.g., MicDescriptor, InstrumentMapper, ArticulationStateMachine).
Replace the simple heuristic in ArticulationStateMachine::chooseArticulationForNote() with a full priority/CC/range evaluation to select articulations precisely.
Add choke-config-driven voice stopping: either return stop-midi-notes from InstrumentMapper or add a callback to tell synth to stop voices matching specific midi notes.
Questions / choices for you

A) Choke-group handling (stop other instrument voices when needed).
B) Hi-hat pedal CC crossfading (controller mapping + articulation switching).
C) Scan the codebase and migrate remaining legacy mic-char checks to MicDescriptor enums/helpers (we already did some earlier, but there may be more).
D) Add unit tests for MicDescriptor::tryCreateFromCode, InstrumentMapper::mapNoteOn, and ArticulationStateMachine.
E) I can implement A+B together (higher effort, more invasive).
If you'd like, I can start with choke groups (A) next — I'll:

update InstrumentMapper::mapNoteOn() to return a list of stopMidiNotes or instrument ids to choke,
update processBlock() to call synth->noteOff() (or provide a helper) to stop those voices immediately before starting the newly-triggered voice,
add a few defensive log messages to help validate behavior.






!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

DrumKitInstrument and related DrumKit classes are already implemented well in DrumKitModel.h you can use it as style and pattern guide
KitLayout need some rethinking/improvement, the purpose is to
p
OutputLayout is used for defining sequence of virtual outputs like:
  








------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------


  
need to make my vst plugin written in JUCE map incoming midi notes to coresponding DrumKitInstrument/InstrumentArticulation based on selected KitLayout - that probably will need some major refactoring, since now there is 5 channels for instrument groups (like kick, snare, toms, hihat, cymbals) rather than separate DrumKitInstrument (idk is it ok) with one juce::Synthesiser for each need to implement in code next features:
- some special rules for mapping midi notes to InstrumentArticulation per DrumKitInstrument and during play time in order to support things like
    -- choke messages for each cymbal DrumKitInstrument (that need to choke all sounding notes (all articulations) for that exact instrument (bell, bow, edge,...)) 
    -- hi-hat pedal cc messages to select specific articulation based on cc value+velocity, and if note is still playing, then continuous pedal cc value change, rather than switching sample, perform a fast crossfade between closed and open samples. Crossfade length 20–80 ms configurable.
    -- support multiple midi notes triggering same articulation / fallback for missing articulations/instruments, if instrument does not have some articulation (or there is no instrument in the kit for that note, ex. when play note 59=ride edge, but current instrument does not have 'edge' articulation, then need to play some different articulation or maybe even different instrument's articulation, there should be some object for rule definition)
    -- extended support for rimshot (like snare center triggered by note 38, and 40 notes should play rimshot articulation when center hit note received in a short window after edge hit note etc) "if rim note occurs within 30 ms of center note and velocities satisfy rule, treat as rimshot" 

so probably there should be not just plain synth-note mapping but some realtime logic to handle that, that monitors what midi note of available in the instrument was played and for how long, and what is available cc state now and what it was before and did it raise or became lower and play crossfaded different articulation based on that, ex. hihat pedal cc value rising should crossfade from closed articulation to open articulation based on cc value ranges etc, polyphonic aftertouch for cymbal type instruments should choke them depend on aftertouch value, if it was not maxed out then just lower volume of played articulation and if aftertouch value became lower after that+cymbal still sounds, it should a little make up the volume back etc etc etc, so need to think through all that logic and make some design for it.

made some major refactoring of DrumKitModel.h and other files, check it out and pls implement performance engine:
Implement InstrumentMapper to use lookup table and handle choke, CC state, simple hi-hat mapping (discrete ranges depending on pedal CC4 openness value).
Implement poly-aftertouch behavior for cymbal instruments note choke.
Implement robust note history and short-window rules for rimshot detection ("if rim note occurs within 30 ms of center note and velocities satisfy rule, treat as rimshot" 






implement a realtime InstrumentMapper and performance engin. The plugin must map incoming MIDI to DrumKitInstrument and InstrumentArticulation based on the selected KitLayout. The current system uses seeveral fixed instrument-group channels with a syntesizer on each (right?); the new design should treat each DrumKitInstrument as its own object and routing target. The agent should inspect DrumKitModel.h and update or extend it where needed.

InstrumentMapper requirements:
• Provide a fast lookup table that maps MIDI NoteOn → {instrumentId, articulationId} based on the active kit.
• Maintain runtime state: hi-hat CC value (default CC4), note history for rimshot detection, currently sounding voices for choke, and poly-aftertouch state.
• APIs should include: mapNoteOn, noteOff, controllerChanged, polyAftertouch, setKit, configuration setters. Must be audio-thread safe (no allocations).

Choke rules:
• Cymbal instruments must support choke: hitting a choke-capable note must stop all sounding voices of the same instrument (bell, bow, edge) or of the same . Default behavior: per-instrument choke, with optional shared choke groups. Use a short fade (5–20 ms) unless a hard stop is specified.

Hi-hat CC mapping:
• Use CC4 by default. Discrete openness ranges: 0–25 closed, 26–70 half-open, 71–127 open.
• NoteOn picks articulation based on CC value and velocity.
• If a hi-hat note is already sounding and CC changes, do NOT retrigger. Instead, crossfade to the appropriate articulation. Configurable crossfade length 20–80 ms (default 40 ms). Crossfade implemented inside voices, not as new notes.

Polyphonic aftertouch for cymbals:
• Cymbal voices with supportsAftertouch respond to poly-aftertouch.
• Aftertouch >= 100 equals full choke (immediate or short fade).
• Lower aftertouch values attenuate volume proportionally, with recovery if aftertouch decreases.

Rimshot detection:
• Keep a small ring buffer of recent NoteOn events with timestamps and velocities.
• Default rule: If a rim note (e.g., MIDI 40) occurs within 30 ms of a center snare note (e.g., MIDI 38) and rimVel >= 0.7 * centerVel or rimVel > 50, treat as rimshot.
• Map to rimshot articulation; if missing, fall back using fallback rules.

Fallback rules and aliases:
• Articulations may define multiple MIDI aliases.
• When a requested articulation is missing, search instrument’s fallback order; if still missing, search kit-level fallbacks.
• Return the resolved articulation to the synth layer.

Voice updates:
• DrumVoice class must support startCrossfade(targetSample, ms), setAftertouch(value), and chokeImmediateOrFade(ms).
• Ensure no heap allocations on audio thread.

Default parameters:
• Hihat CC number 4; ranges 0–25/26–70/71–127.
• Crossfade 40 ms default.
• Rim window 30 ms.
• Rim velocity rule: rimVel >= 0.7 * centerVel or rimVel > 50.
• Aftertouch full choke threshold 100.
• Choke fade 5–20 ms default.

Deliverables:
• InstrumentMapper.h/.cpp implementing all logic above.
• Required updates to DrumKitModel structures (chokeGroupId, midiAliases, fallbackOrder, supportsAftertouch, hihat flags).
• DrumVoice updates for crossfade, aftertouch, choke.
• Tests or test harness covering mapping, hi-hat CC, choke, aftertouch, rimshot, and fallbacks.
• Integration notes on how to call InstrumentMapper inside processBlock and how to safely set a new kit from the message thread.










------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------




depends on samples directory structure and more instruments can be dynamically added/loaded (by some kind of refresh library button) in future.

Add a system for instruments root dir
void setSampleDirectory(juce::File dir);





Reader lifetime must be wrapped in a unique_ptr?

4. Update PluginSynthesiserSound
4.1. Ensure PluginSynthesiserSound does not assume MemoryInputStream

If it currently stores a pointer to the MemoryInputStream, change it to store:

A unique_ptr<AudioFormatReader> OR a

Sample Reader instance copied to an AudioSampleBuffer.

4.2. Decide whether samples should:

Stay on disk and stream (not recommended for drum hits), or

Be fully loaded into memory on creation (recommended).

LLM should implement “fully load into memory”.

5. Update the sample-registration logic

The new flow:

File → AudioFormatReader → AudioSampleBuffer → PluginSynthesiserSound → PluginSynthesiserVoice

5.1. Make sure nested data structure is preserved

mMidiNoteToInstruments

intensities/variations/microphones

stopsMidiNotes

currentVariationIndex

5.2. Confirm indexing logic still works

intensity index = velocity → intensity layer

variation index = round-robin

mic id mapping unchanged

6. Remove hard-coded sample list

BinaryData auto-generate sample names; disk loading requires explicit enumeration.

Add:

6.1. A sample manifest loader

Options:

A. JSON file describing all samples
Example:

{
  "36": {
    "intensities": [
      { "variations": [
        { "mics": { "0": "kick/vel1/var1/close.wav", "1": "kick/vel1/var1/room.wav" } }
      ]}
    ]
  }
}


B. Automatic folder scan
Detect folders → map intensity → map variation → map mic → load.

7. Implement dynamic reload support
7.1. Add:
void reloadAllSamples();
void unloadAllSamples();

7.2. Ensure:

Voices are stopped before unloading.

Old sounds removed via removeSound().

New sounds added via addSound().

Optional:
Add file watcher to auto-reload.

8. Preserve noteOn behavior

Do not modify:

velocity → intensity selection

variation cycling

stops-other-notes behavior

mic-specific noteOn() overload

Confirm that sample loading changes do not affect:

Sound::appliesToNote

Sound::appliesToChannel

Voice start/stop logic

9. Threading & performance
9.1. Disk → memory loading must not block the message thread.

LLM should:

Load samples on a background thread.

Then safely transfer prepared AudioSampleBuffer to the main thread for synth registration.

9.2. Optional

Add a simple async loader pool.

10. Remove unused BinaryData code

Delete BinaryData resource references.

Remove CMake/Projucer BinaryData generation.

11. Testing checklist

LLM should ensure tests for:

Proper note mapping for all loaded samples

Correct velocity layer selection

Variation cycling works

Microphone-specific noteOn works

Stopping logic works

Samples reload without crash

Voices don’t reference freed memory after unloading

12. Migration & Compatibility

Provide migration helpers:

If someone still calls old addSample(name, ...), produce compile error or redirect to disk-based API.

13. Deliverables LLM should generate

Refactored PluginSynthesiser.h / .cpp

Updated PluginSynthesiserSound

Updated PluginSynthesiserVoice if needed

Sample manifest loader or folder-scanner

New addSampleFromFile()

Disk-loading pipeline

Optional async loader

Documentation describing new sample layout



------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------

here is the script, need to check implementation and fix problems. original description was
py converter script for Drumgizmos kits (under this workspace in /todo/* subfolders) to Karatala kit format that will generate commands to organize/copy with different name samples and some debug info and summary for review
according to Sample Filename Convention and folder structure of Karatala described in KitLibModel comment (attached)

basically it will need to check xml files like this

some/path/kitname/ChinaL/ChinaL.xml:
<?xml version='1.0' encoding='UTF-8'?>
<instrument version="2.0" name="ChinaL">
<samples>
<sample name="ChinaL-1" power="0.000105857">
<audiofile channel="AmbLeft" file="samples/1-ChinaL.wav" filechannel="1"/>
<audiofile channel="AmbRight" file="samples/1-ChinaL.wav" filechannel="2"/>
<audiofile channel="FTom1" file="samples/1-ChinaL.wav" filechannel="3"/>
<audiofile channel="FTom2" file="samples/1-ChinaL.wav" filechannel="4"/>
<audiofile channel="Hihat" file="samples/1-ChinaL.wav" filechannel="5"/>
<audiofile channel="KDrumInside" file="samples/1-ChinaL.wav" filechannel="6"/>
<audiofile channel="KDrumOutside" file="samples/1-ChinaL.wav" filechannel="7"/>
<audiofile channel="OHCenter" file="samples/1-ChinaL.wav" filechannel="8"/>
<audiofile channel="OHLeft" file="samples/1-ChinaL.wav" filechannel="9"/>
<audiofile channel="OHRight" file="samples/1-ChinaL.wav" filechannel="10"/>
<audiofile channel="Ride" file="samples/1-ChinaL.wav" filechannel="11"/>
<audiofile channel="SnareBottom" file="samples/1-ChinaL.wav" filechannel="12"/>
<audiofile channel="SnareTop" file="samples/1-ChinaL.wav" filechannel="13"/>
<audiofile channel="Tom1" file="samples/1-ChinaL.wav" filechannel="14"/>
<audiofile channel="Tom2" file="samples/1-ChinaL.wav" filechannel="15"/>
...
and organize all the stuff to instruments/articulations/sample names using some search mappings like
for channel mic codes parsed based on:
L or Left, R or Right means we have stereo pair,
amb: room, probably far, so rfs (room far stereo)
OHCenter = rcm (room close mono)
OH - overheads, so ons (overheads normal stereo)
if containing instrument name and its not the current instrument's mic - ignoring with x
etc so for example xml we should get:
move ChinaL/samples/1-ChinaL.wav to /CrocellKit/china-L/edge/china-L-edge-1a-rfs_x_x_x_x_x_rcm_ons_x_x_x_x-w000010585.wav

word extraction is basically by case change or by some kind of dictionary lookup or number like

Tom2 - tom 2
Tom1 - tom 1
SplashR - splash R
SplashL - splash L
SnareRimShot - snare rimshot
SnareRim - snare rim
SnareRest - snare rest ("rest" articulation can be ignored, but lets keep it for manual processing - still make some moving commands, can be removed later manually)
Snare - snare
RideRBell - ride R bell
RideR - ride R
KDrumR - kick R
KDrumL - kick L
HihatSemiOpen - hihat half (naming here is fucked, here its probably is like more closed edge[half]-pdl_opn_cls52)
HihatPedalHit - hihat pedal[splash] (probably)
HihatPedal - hihat pedal
HihatOpen - hihat edge[open]
HihatClosedNoPedal - hihat half (naming here is fucked, here 'ClosedNoPedal' probably is like more closed edge[half]-pdl_opn_cls64)
HihatClosed - hihat bow[close]
FTom2 - tom F2
FTom1 - tom F1
CrashRXtra - crash X1
CrashRStopped - crash X1 choke
CrashR - crash R
CrashLStopped - crash R choke
CrashL - crash L
ChinaR - china R
ChinaL - china L
etc

if no articulation specified, defaults are different for instruments:
ride: bow
crash,splash,china: edge
snare,kick,tom:center
hihat: edge, but could be bow sometimes

hihats have this kinda curve by default, if there are more open/half variations they can go between these values of pdl_opn_cls
edge[close]-pdl_opn_cls78
edge[half]-pdl_opn_cls52
edge[open]-pdl_opn_cls43
edge[tight]-pdl_opn_cls85
pedal-pdl_opn_cls85
