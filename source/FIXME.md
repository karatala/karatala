FIXME pad popup controls change nothing, instrument page controls works



i interupted implementation cause there are problems after one of changes that was not fixed by stage5 (not sure if it even was supposed to be fixed there)
in source/Engine/InstrumentSynthLoader.cpp:244
//FIXME: temporarily commented "InstrumentSynthLoader now uses those filters with mapFilterMatchesArticulation()" cause it broke many things

here is a pseudo code/table
instrumentMap|before plan started|with mapFilterMatchesArticulation inside collectResolvedLayoutArticulations
china1/bow:AasiMonster2/china-18/edge|ok|ok
china1/edge:NakedDrums/china-X2/edge|FAIL plays bow|FAIL silence

china2/edge:NakedDrums/china-X1/edge|ok|ok

crash1/bow:MuldjordKit3/ride-R1/bow:flipLR=1|ok|ok
crash1/edge:NakedDrums/crash-X1/edge|ok|FAIL silence

crash2/bow:MuldjordKit3/ride-L1/bow:flipLR=1|ok|ok
crash2/edge:NakedDrums/crash-X2/edge|ok|FAIL silence

crash3/bow:AasiMonster/crash-X1/edge:tuning=-19,flipLR=1|ok, edge plays bow|

hihat1/bow:NakedDrums/hihat-X1/edge|ok|FAIL transition from open to close kinda broken
hihat1/edge:NakedDrums/hihat-X1/edge|ok|FAIL silence
hihat1/pedal:NakedDrums/hihat-X1/pedal|ok|FAIL silence

hihat2/bow:TchackpoumDG1.3/hihat-14/bow|FAIL bow[tight] plays bow[open], opened articulations not transform to closed with cc change|
hihat2/edge:TchackpoumDG1.3/hihat-14/edge|FAIL opened articulations not transform to closed with cc change|
hihat2/pedal:TchackpoumDG1.3/hihat-14/pedal|ok|

kick1/center:NakedDrums/kick-22/center:tuning=-20|ok|ok

octoban1/center:AasiMonster2/tom-X1/center:tuning=1210|ok|ok

octoban2/center:AasiMonster/tom-X1/center:tuning=960|ok|ok

ride1/bell:NakedDrums/ride-X1/bell:decay=2359|ok|ok
ride1/bow:NakedDrums/ride-X1/bow:decay=3749,vol=-1.2|ok|ok
ride1/edge:TchackpoumDG1.3/ride-20/edge:decay=2920,vol=8.2|ok|ok

ride2/bell:Salamander/ride-20-R1-2-Paiste-pst5-medium-or-Stagg-SH-medium/bell:vol=-12.1|ok|ok
ride2/bow:Salamander/ride-20-R1-2-Paiste-pst5-medium-or-Stagg-SH-medium/bow:vol=-13.4|ok|ok
ride2/edge:Salamander/ride-20-R1-2-Paiste-pst5-medium-or-Stagg-SH-medium/edge:vol=-16.7|ok|ok

snare1/center:NakedDrums/snare-X1/center:tuning=20|ok|ok
snare1/rim:NakedDrums/snare-X1/center|ok|FAIL silence (plays only second layer of rim)
snare1/rim:NakedDrums/snare-X2/center:tuning=60|ok|ok
snare1/sidestick:NakedDrums/snare-X1/sidestick|ok|FAIL silence

snare2/center:TchackpoumDG1.3/snare-14x6.5/center:tuning=58|ok|ok
snare2/rim:TchackpoumDG1.3/snare-14x6.5/rim|ok|FAIL silence
snare2/sidestick:TchackpoumDG1.3/snare-14/sidestick|ok|FAIL silence

splash1/bow:NakedDrums/splash-X1/edge:tuning=170|ok|ok
splash1/edge:AasiMonster2/china-8/edge|FAIL edge plays bow|FAIL silence

splash2/bow:NakedDrums/splash-X2/edge|ok|ok
splash2/edge:AasiMonster2/china-10/edge|FAIL edge plays bow|FAIL silence

stax1/bow:NakedDrums/splash-X1/edge:tuning=-213,decay=379|ok, edge plays bow|ok, edge plays bow
stax1/bow:AasiMonster/china-X1/edge:tuning=-346,decay=559|ok, edge plays bow|ok, edge plays bow

tom1/center:NakedDrums/tom-10/center:tuning=463|ok|ok
tom1/rim:AasiMonster/tom-X1/center:tuning=-96,vol=3.5|ok|ok

tom2/center:NakedDrums/tom-12/center:tuning=110|ok|ok
tom2/rim:AasiMonster/tom-X2/center|FAIL rim plays center|FAIL silence

tom3/center:NakedDrums/tom-13/center:tuning=60|ok|ok
tom3/rim:AasiMonster/tom-X3/center|FAIL rim plays center|FAIL silence

tom4/center:NakedDrums/tom-14/center|ok|ok
tom4/rim:AasiMonster/tom-X4/center|FAIL rim plays center|FAIL silence

tom5/center:NakedDrums/tom-16/center:tuning=60|ok|ok
tom5/rim:AasiMonster/tom-X4/center|FAIL rim plays center|FAIL silence


also logs difference you can check in this files (if needed):
karatala-20260708-160630-before.log and before-after-patch.txt
(karatala-20260708-150804-after.log has too many duplicating lines, so maybe patch will be more informative to you)



also with hihats is unclear to me now what format should the line have, i thought to make instrumentMap to have definitions similar to codes in  source\Engine\LayoutManager.cpp: ```
  - id: hihat1
    display: Hi-Hat 1
    position: left
    articulations:
      - { notes: [46], code: bow[open] }
      - { notes: [42], code: bow[tight] }
      - { notes: [22], code: edge[open] }
      - { notes: [18], code: edge[tight] }
      - { notes: [44], code: pedal }
    controllers:
      - { notes: [22,46], code: pdl_opn_cls, number: 1 }
      - { notes: [42,46], code: zon_bow_edg, number: 19 }
      - { notes: [18,22,42,46], code: zon_lft_rgt, number: 83 }

  - id: hihat2
    display: Hi-Hat Stax
    position: right
    articulations:
      - { notes: [10], code: bow[open] }
      - { notes: [ 6], code: bow[tight] }
      - { notes: [ 7], code: edge[open] }
      - { notes: [ 3], code: edge[tight] }
      - { notes: [ 8], code: pedal }
    controllers:
      - { notes: [ 6,10], code: pdl_opn_cls, number: 4 }
```
so we will define instrumentMap as: ```
hihat1/bow[open]:NakedDrums/hihat-X1/edge[open]
hihat1/bow[close]:NakedDrums/hihat-X1/edge[close]
hihat1/edge[open]:NakedDrums/hihat-X1/edge[open]
hihat1/edge[close]:NakedDrums/hihat-X1/edge[close]
hihat1/pedal:NakedDrums/hihat-X1/pedal
```
and internally we have implicit hihat logic with cc-controlled transitions open>half>close>tight abd backwards, that already kinda worked well (before we introduced mapFilterMatchesArticulation inside collectResolvedLayoutArticulations)


## Test plan for instrument-map routing regression

Goal:

- First write tests that describe the current temporarily working behavior.
- Then uncomment `mapFilterMatchesArticulation` inside `InstrumentSynth::collectResolvedLayoutArticulations()` and confirm the tests reproduce the broken behavior from the table.
- Then fix routing and update/add tests for the wanted behavior.

### Step 1 - Capture current behavior before changing code

Add focused tests using the map rows from the table. The tests should check routing ownership and note output, not only parser success.

Important cases:

- Cross-art source remap should still make sound on the destination note:
  - `snare1/rim:NakedDrums/snare-X1/center`
  - Expected current behavior: rim note plays source `center`.
  - Reason: destination is `rim`, source sample is `center`.
- Separate layers for the same slot should not steal each other:
  - `crash1/bow:MuldjordKit3/ride-R1/bow:flipLR=1`
  - `crash1/edge:NakedDrums/crash-X1/edge`
  - Expected current behavior: bow note plays bow layer, edge note plays edge layer.
- Known old conflict should be captured too:
  - `china1/bow:AasiMonster2/china-18/edge`
  - `china1/edge:NakedDrums/china-X2/edge`
  - Expected current behavior from table: edge can wrongly play bow. Mark as known bad current behavior.
- Hihat should load destination rows and controller siblings:
  - `hihat1/bow:NakedDrums/hihat-X1/edge`
  - `hihat1/edge:NakedDrums/hihat-X1/edge`
  - `hihat1/pedal:NakedDrums/hihat-X1/pedal`
  - Expected current behavior: bow, edge, and pedal are not silent; open/close CC transition still works enough to prove CC routes exist.

Suggested test shape:

- Build a small engine with the normal layout and test libraries.
- Apply a mini map for one slot at a time.
- Inspect the synth route list or public route ownership where possible.
- If needed, render a note and assert non-silence.
- For known bad current behavior, make the test name explicit, for example `documents_current_china_edge_route_conflict`.

### Step 2 - Reproduce the broken strict-filter behavior

Temporarily uncomment the `allowedArticulationFilters` + `mapFilterMatchesArticulation()` block in `InstrumentSynth::collectResolvedLayoutArticulations()`.

Expected failures from the table:

- `crash1/edge:NakedDrums/crash-X1/edge` becomes silent when it is the second row in the same slot.
- `snare1/rim:NakedDrums/snare-X1/center` becomes silent because destination `rim` is filtered out or remap is not applied correctly.
- `snare1/sidestick:NakedDrums/snare-X1/sidestick` becomes silent when it is after other rows in same slot.
- `hihat1/edge:NakedDrums/hihat-X1/edge` becomes silent.
- `hihat1/pedal:NakedDrums/hihat-X1/pedal` becomes silent.

This confirms the regression before fixing it.

### Step 3 - Fix and test wanted behavior

Wanted behavior:

- Each map row owns its destination side from the left side of the map.
  - Example: `snare1/rim:Kit/snare/center` owns destination `rim`.
- Source remap is separate.
  - Example: destination `rim` can play source `center`.
- A primary synth for a slot may need the combined destination filters from all non-shadow rows in that slot.
  - Example: primary allowed destinations should be `[bow, edge]`, not only first row `[bow]`.
- A shadow synth should own only its own destination rows.
- Hihat route rows should follow layout-style names where possible:
  - `hihat1/bow[open]:Kit/hat/bow[open]`
  - `hihat1/bow[tight]:Kit/hat/bow[tight]`
  - `hihat1/edge[open]:Kit/hat/edge[open]`
  - `hihat1/edge[tight]:Kit/hat/edge[tight]`
  - `hihat1/pedal:Kit/hat/pedal`
- Hihat CC may load same-family controller siblings, for example `edge[open]`, `edge[half]`, `edge[close]`, `edge[tight]`, but those sibling rows must not own MIDI notes by themselves.

Expected fixed results:

- `crash1/edge` is not silent.
- `snare1/rim -> center` is not silent and still belongs to destination `rim`.
- `snare1/sidestick` is not silent.
- `china1/edge` no longer plays the bow layer.
- `tom2/rim -> center` no longer gets stolen by center notes unless that is the explicit destination route.
- `hihat1/edge` and `hihat1/pedal` are not silent.
- `hihat2/bow[tight]` should not always play `bow[open]`; CC should choose the best open/half/close/tight source state.
