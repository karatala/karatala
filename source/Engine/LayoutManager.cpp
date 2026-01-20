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
    type: kick
    articulations:
      - code: center
        notes: [35, 36]

  - id: snare1
    display: Snare
    type: snare
    articulations:
      - code: center
        notes: [38]
      - code: rim
        notes: [40]
      - code: sidestick
        notes: [37]
    controllers:
      - id: 16
        code: zone-center-rim
        notes: [38]

  - id: hihat1
    display: Hi-Hat
    type: hihat
    articulations:
      - code: tight-edge
        notes: [18]
      - code: open-edge
        notes: [22]
      - code: tight-bow
        notes: [42]
      - code: pedal
        notes: [44]
      - code: foot-splash
        notes: [44]
      - code: open-bow
        notes: [46]
    controllers:
      - id: 4
        code: pedal-open-close
        notes: [22,46]
      - id: 19
        code: zone-bow-edge
        notes: [42,46]
      - id: 83
        code: zone-left-right
        notes: [18,22,42,46]
        
  - id: hihat2
    display: Hi-Hat Stax
    type: hihat
    articulations:
      - code: tight-bow
        notes: [15]
      - code: tight-edge
        notes: []
      - code: pedal
        notes: [20]
      - code: open-bow
        notes: [32]
      - code: open-edge
        notes: []
    controllers:
      - id: 4
        code: pedal-open-close
        notes: [22,46]

  - id: ride1
    display: Ride
    type: ride
    articulations:
      - code: bow
        notes: [51]
      - code: edge
        notes: [59]
      - code: bell
        notes: [53]
    controllers:
      - id: 17
        code: zone-bow-edge
        
  - id: tom1
    display: Tom 1
    type: tom
    articulations:
      - code: center
        notes: [48]
      - code: rim
        notes: [24]
    controllers:
      - id: 18
        code: zone-center-rim

  - id: tom2
    display: Tom 2
    type: tom
    articulations:
      - code: center
        notes: [47]
      - code: rim
        notes: [23]
    controllers:
      - id: 18
        code: zone-center-rim
        
  - id: tom3
    display: Tom 3
    type: tom
    articulations:
      - code: center
        notes: [45]
      - code: rim
        notes: [21]
    controllers:
      - id: 18
        code: zone-center-rim

  - id: tom4
    display: Tom 4
    type: tom
    articulations:
      - code: center
        notes: [43]
      - code: rim
        notes: [19]
    controllers:
      - id: 18
        code: zone-center-rim
        notes: [43]

  - id: tom5
    display: Tom 5
    type: tom
    articulations:
      - code: center
        notes: [41]
      - code: rim
        notes: [17]
    controllers:
      - id: 18
        code: zone-center-rim

  - id: crash1
    display: Crash 1
    type: crash
    articulations:
      - code: bow
        notes: [25]
      - code: edge
        notes: [49]
  
  - id: crash2
    display: Crash 2
    type: crash
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: splash1
    display: Splash 1
    type: splash
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: splash2
    display: Splash 2
    type: splash
    articulations:
      - code: bow
        notes: [33]
      - code: edge
        notes: [57]

  - id: china1
    display: China 1
    type: china
    articulations:
      - code: bow
        notes: [30]
      - code: edge
        notes: [52]

  - id: china2
    display: China 2
    type: china
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
