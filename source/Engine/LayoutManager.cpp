#include "LayoutManager.h"
#include <juce_core/juce_core.h>

MicLayout MicLayout::getDefault() {
    static const juce::String defaultYaml = R"yaml(
id: default
name: default
mics:
  - id: overhead/ons
    name: OH # 1-2
    code: ons
  - id: hihat1/dtm
    name: Hat1
    instrument: hihat1 # 3
    code: dtm
  - id: ride1/dtm
    name: Rid1
    instrument: ride1 # 4
    code: dtm
  - id: kick1/dim
    name: Kik In
    instrument: kick1 # 5
    code: dim
  - id: kick1/dom
    name: Kik Out
    instrument: kick1 # 6
    code: dom
  - id: snare1/dtm
    name: Snr1 Top # 7
    instrument: snare1
    code: dtm
  - id: snare1/dbm
    name: Snr1 Btm # 8
    instrument: snare1
    code: dbm
  - id: tom1/dtm
    name: Tom1 # 9
    instrument: tom1
    code: dtm
  - id: tom2/dtm
    name: Tom2 # 10
    instrument: tom2
    code: dtm
  - id: tom3/dtm
    name: Tom3 # 11
    instrument: tom3
    code: dtm
  - id: tom4/dtm
    name: Tom4 # 12
    instrument: tom4
    code: dtm
  - id: tom5/dtm
    name: Tom5 # 13
    instrument: tom5
    code: dtm
  - id: room/rcm
    name: Amb Cls # 14
    code: rcm
  - id: room/rfs
    name: Amb Far # 15-16
    code: rfs
  - id: overhead/oni
    name: OH MS # 17-18
    code: oni
  - id: overhead/ows
    name: OH wide # 19-20
    code: ows
  - id: hihat2/dtm
    name: Hat2
    instrument: hihat2 # 21
    code: dtm
  - id: ride2/dtm
    name: Rid2
    instrument: ride2 # 22
    code: dtm
  - id: octoban1/dtm
    name: Oct1 # 23
    instrument: octoban1
    code: dtm
  - id: octoban2/dtm
    name: Oct2 # 24
    instrument: octoban2
    code: dtm
  - id: aux1/**m
    name: Aux1 # 25
    code: **m
  - id: aux2/**m
    name: Aux2 # 26
    code: **m
  - id: aux3/**m
    name: Aux3 # 27
    code: **m
  - id: aux4/**m
    name: Aux4 # 28
    code: **m
  - id: aux1/**s
    name: LR Aux1 # 29-30
    code: **s
  - id: aux2/**s
    name: LR Aux2 # 31-32
    code: **s
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

  - id: octoban1
    display: Octoban 1
    articulations:
      - { notes: [50], code: center }

  - id: octoban2
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
      - { notes: [10], code: bow[open] }
      - { notes: [ 6], code: bow[tight] }
      - { notes: [ 7], code: edge[open] }
      - { notes: [ 3], code: edge[tight] }
      - { notes: [ 8], code: pedal }
    controllers:
      - { notes: [7,10], code: pdl_opn_cls, number: 4 }

  - id: ride1
    display: Ride 1
    position: right
    articulations:
      - { notes: [53], code: bell }
      - { notes: [51], code: bow }
      - { notes: [59], code: edge }
    controllers:
      - { notes: [51], code: zon_bow_edg, number: 17 }

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
      - { notes: [4], code: bow }
      - { notes: [52], code: edge }

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

  - id: crash3
    display: Crash 3
    position: right
    articulations:
      - { notes: [9], code: bow }
      - { notes: [32], code: edge }

  - id: stax1
    display: Stax 1
    position: right
    articulations:
      - { notes: [39], code: bow }
      - { notes: [15], code: edge }

  - id: perc1
    display: Vibraslap/Cowbel
    articulations:
      - { notes: [58], code: center }
      - { notes: [56], code: rim }

)yaml";
    auto result = LayoutTraits<KitLayout>::parseYaml(defaultYaml);
    if (!result.ok()) {
        jassertfalse;
        throw std::runtime_error(result.error().toStdString());
    }
    return result.value;
}
