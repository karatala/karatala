#pragma once

#include "LayoutManager.h"
#include <juce_core/juce_core.h>

MicLayout MicLayout::getDefault() {
    static const juce::String defaultYaml = R"yaml(
id: default
name: default
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
)yaml";
    auto result = LayoutTraits<MicLayout>::parseYaml(defaultYaml);
    if (!result.ok()) {
        jassertfalse;
        throw std::runtime_error(result.error().toStdString());
    }
    return result.value;
}

KitLayout KitLayout::getDefault() {
    static const juce::String defaultYaml = R"yaml(
id: default
name: default
description: default layout
vendor: Haribol
version: 1.0
instruments:
  - id: kick1
    display: Kick
    articulations:
      - code: center
        notes: [35, 36]

  - id: snare1
    display: Snare
    articulations:
      - code: center
        notes: [38]
      - code: rim
        notes: [40]
      - code: sidestick
        notes: [37]
    controllers:
      - id: 16
        code: zon_ctr_rim
        notes: [38]

  - id: hihat1
    display: Hi-Hat
    articulations:
      - code: edge[tight]
        notes: [18]
      - code: edge[open]
        notes: [22]
      - code: bow[tight]
        notes: [42]
      - code: pedal
        notes: [44]
      - code: pedal[splash]
        notes: [44]
      - code: bow[open]
        notes: [46]
    controllers:
      - id: 4
        code: pdl_opn_cls
        notes: [22,46]
      - id: 19
        code: zon_bow_edg
        notes: [42,46]
      - id: 83
        code: zon_lft_rgt
        notes: [18,22,42,46]
        
  - id: hihat2
    display: Hi-Hat Stax
    articulations:
      - code: bow[tight]
        notes: [15]
      - code: edge[tight]
        notes: []
      - code: pedal
        notes: [20]
      - code: bow[open]
        notes: [32]
      - code: edge[open]
        notes: []
    controllers:
      - id: 4
        code: pdl_opn_cls
        notes: [22,46]

  - id: ride1
    display: Ride
    articulations:
      - code: bow
        notes: [51]
      - code: edge
        notes: [59]
      - code: bell
        notes: [53]
    controllers:
      - id: 17
        code: zon_bow_edg
        
  - id: tom1
    display: Tom 1
    articulations:
      - code: center
        notes: [48]
      - code: rim
        notes: [24]
    controllers:
      - id: 18
        code: zon_ctr_rim

  - id: tom2
    display: Tom 2
    articulations:
      - code: center
        notes: [47]
      - code: rim
        notes: [23]
    controllers:
      - id: 18
        code: zon_ctr_rim
        
  - id: tom3
    display: Tom 3
    articulations:
      - code: center
        notes: [45]
      - code: rim
        notes: [21]
    controllers:
      - id: 18
        code: zon_ctr_rim

  - id: tom4
    display: Tom 4
    articulations:
      - code: center
        notes: [43]
      - code: rim
        notes: [19]
    controllers:
      - id: 18
        code: zon_ctr_rim
        notes: [43]

  - id: tom5
    display: Tom 5
    articulations:
      - code: center
        notes: [41]
      - code: rim
        notes: [17]
    controllers:
      - id: 18
        code: zon_ctr_rim

  - id: crash1
    display: Crash 1
    articulations:
      - code: bow
        notes: [25]
      - code: edge
        notes: [49]
  
  - id: crash2
    display: Crash 2
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: splash1
    display: Splash 1
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: splash2
    display: Splash 2
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: china1
    display: China 1
    articulations:
      - code: bow
        notes: [30]
      - code: edge
        notes: [52]

  - id: china2
    display: China 2
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]


)yaml";
    auto result = LayoutTraits<KitLayout>::parseYaml(defaultYaml);
    if (!result.ok()) {
        jassertfalse;
        throw std::runtime_error(result.error().toStdString());
    }
    return result.value;
}
