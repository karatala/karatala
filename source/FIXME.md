with hihats is unclear to me now what format should the line have, i thought to make instrumentMap to have definitions similar to codes in  source\Engine\LayoutManager.cpp: ```
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

