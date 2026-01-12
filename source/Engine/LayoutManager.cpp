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
      - type: center
        notes: [35, 36]

  - id: snare1
    display: Snare
    type: snare
    articulations:
      - type: center
        notes: [38]
      - type: rim
        notes: [40]
      - type: sidestick
        notes: [37]
    controllers:
      - id: 16
        type: position
        name: Snare Position
        description: Adjust snare hit position from center to rim

  - id: hihat1
    display: Hi-Hat
    type: hihat
    articulations:
      - type: closed
        notes: [42]
      - type: pedal
        notes: [44]
      - type: bow
        notes: [46]
    controllers:
      - id: 4
        type: pedal
        name: Hi-Hat Pedal
        mapping:
          - range: [0, 20]
            articulation: closed
          - range: [21, 80]
            articulation: half_open
          - range: [81, 127]
            articulation: open
)yaml";
    auto result = LayoutTraits<KitLayout>::parseYaml(defaultYaml);
    if (!result.ok()) {
        jassertfalse;
        throw std::runtime_error(result.error().toStdString());
    }
    return result.value;
}
