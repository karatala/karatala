#pragma once

#include "LayoutManager.h"
#include <juce_core/juce_core.h>

MicLayout MicLayout::getDefault() {
    static const juce::String defaultYaml = R"yaml(
id: default
name: default
mics:
  - name: Overheads # 1-2
    code: ons
  - name: Hi-Hat
    instrument: hihat1 # 3
    code: dtm
  - name: Ride
    instrument: ride1 # 4
    code: dtm
  - name: Kick In
    instrument: kick1 # 5
    code: dim
  - name: Kick Out
    instrument: kick1 # 6
    code: dom
  - name: Snare Top # 7
    instrument: snare1
    code: dtm
  - name: Snare Bottom # 8
    instrument: snare1
    code: dbm
  - name: Tom1 # 9
    instrument: tom1
    code: dtm
  - name: Tom2 # 10
    instrument: tom2
    code: dtm
  - name: Tom3 # 11
    instrument: tom3
    code: dtm
  - name: Tom4 # 12
    instrument: tom4
    code: dtm
  - name: Tom5 # 13
    instrument: tom5
    code: dtm
  - name: Room Close # 14
    code: rcm
  - name: Room Far # 15-16
    code: rfs
  - name: Overheads Mid-Side # 17-18
    code: oni
  - name: Overheads wide # 19-20
    code: ows
  # TODO utilize remaining 12 channels
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
    display: Kick 1
    articulations:
      - { notes: [35,36], code: center }

  - id: snare1
    display: Snare 1
    articulations:
      - { notes: [38], code: center }
      - { notes: [40], code: rim }
      - { notes: [37], code: sidestick }
    controllers:
      - { notes: [38], code: zon_ctr_rim, number: 16 }

  - id: snare2
    display: Snare 2
    articulations:
      - { notes: [14], code: center }
      - { notes: [16], code: rim }
      - { notes: [13], code: sidestick }

  - id: tom1
    display: Tom 1
    articulations:
      - { notes: [48], code: center }
      - { notes: [24], code: rim }
    controllers:
      - { notes: [48], code: zon_ctr_rim, number: 18 }

  - id: tom2
    display: Tom 2
    articulations:
      - { notes: [47], code: center }
      - { notes: [23], code: rim }
    controllers:
      - { notes: [47], code: zon_ctr_rim, number: 18 }

  - id: tom3
    display: Tom 3
    articulations:
      - { notes: [45], code: center }
      - { notes: [21], code: rim }
    controllers:
      - { notes: [45], code: zon_ctr_rim, number: 18 }

  - id: tom4
    display: Tom 4
    articulations:
      - { notes: [43], code: center }
      - { notes: [19], code: rim }
    controllers:
      - { notes: [43], code: zon_ctr_rim, number: 18 }

  - id: tom5
    display: Tom 5
    articulations:
      - { notes: [41], code: center }
      - { notes: [17], code: rim }
    controllers:
      - { notes: [41], code: zon_ctr_rim, number: 18 }

  - id: drum1
    display: Octoban 1
    articulations:
      - { notes: [50], code: center }

  - id: drum2
    display: Octoban 2
    articulations:
      - { notes: [26], code: center }

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
      - { notes: [ 6], code: bow[tight] }
      - { notes: [10], code: bow[open] }
      - { notes: [ 3], code: edge[tight] }
      - { notes: [ 7], code: edge[open] }
      - { notes: [ 8], code: pedal }
    controllers:
      - { notes: [ 6,10], code: pdl_opn_cls, number: 4 }

  - id: ride1
    display: Ride 1
    position: right
    articulations:
      - { notes: [53], code: bell }
      - { notes: [51], code: bow }
      - { notes: [59], code: edge }
    controllers:
      - { code: zon_bow_edg, number: 17 }

  - id: ride2
    display: Ride 2
    position: left
    articulations:
      - { notes: [29], code: bell }
      - { notes: [27], code: bow }
      - { notes: [11], code: edge }

  - id: crash1
    display: Crash 1
    position: left
    articulations:
      - { notes: [25], code: bow }
      - { notes: [49], code: edge }

  - id: crash2
    display: Crash 2
    position: right
    articulations:
      - { notes: [33], code: bow }
      - { notes: [57], code: edge }

  - id: china1
    display: China 1
    position: right
    articulations:
      - { notes: [52], code: edge }
      - { notes: [4], code: bow }

  - id: china2
    display: China 2
    position: left
    articulations:
      - { notes: [28], code: edge }

  - id: splash1
    display: Splash 1
    position: left
    articulations:
      - { notes: [54], code: bow }
      - { notes: [55], code: edge }

  - id: splash2
    display: Splash 2
    position: right
    articulations:
      - { notes: [30], code: bow }
      - { notes: [31], code: edge }

  - id: cymbal1
    display: Cymbal 1
    position: right
    articulations:
      - { notes: [32], code: bow }
      - { notes: [32], code: edge }

  - id: perc1
    display: Vibraslap/Sticks
    articulations:
      - { notes: [34], code: center }
      - { notes: [58], code: rim }

  - id: perc2
    display: Timbale/Block 1
    articulations:
      - { notes: [65], code: center }
      - { notes: [76], code: rim }

  - id: perc3
    display: Timbale/Block 2
    articulations:
      - { notes: [66], code: center }
      - { notes: [77], code: rim }

)yaml";
    auto result = LayoutTraits<KitLayout>::parseYaml(defaultYaml);
    if (!result.ok()) {
        jassertfalse;
        throw std::runtime_error(result.error().toStdString());
    }
    return result.value;
}
