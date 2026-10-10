# Sample library format

You do not need this guide to install the supplied library. Use the [install guide](INSTALL.md) for that.

Use this guide to add your own drum recordings. Karatala reads the folder names and sample file names. Start with one drum and one recording. Then add more sounds.

## 1. Make the folders

```text
(description copied from source/Engine/KitLibModel.h comment)

Sample Filename Convention:
   <InstrumentsRoot>/
      └─ <kitInfo>/
         └─ <instrumentType>-<size>[-<balance>][-<description>]/
             └─ <articulation>[optionalExpressionShortName]-<optionalExpressionCode><value>-<anotherExpressionCode><value>/
                 └─ <instrumentType>-<size>-<articulation>[optionalExpressionShortName]-<velocity><roundRobinLetter>-<micChain>-<otherOptionalSampleParams>.<ext>
otherOptionalSampleParams:
    v{n|p}<gainDb>                  manual gain, where vn is negative and vp is positive
    o{n|p}<milliseconds>[_micCode...]  playback timing, where on advances and op delays; optional mic codes limit the offset

    instruments/                                                 LibraryManager
      └─ Kit Name by Author Name  [CC-BY-4.0]/                   DrumKitSpec
         └─ kick-22-Tama Starclassic etc/                        InstrumentSpec
             └─ center/                                          ArticulationSpec
                 └─ kick-22-center-127a-dim.flac                                VelocityLevelSpec, LogicalHitSpec, SampleSpec, MicSpec
         └─ hihat-14-Sabian HHX/
             └─ bow[half]-pdl_opn_cls043-zon_bel_edg085/
                 └─ hihat-14-bow[half]-043a-oni-vp5.flac
                 └─ hihat-14-bow[half]-085a-oni-vp3.flac
                 ...
             └─ bow[close]-pdl_opn_cls085/
                 ...
             └─ edge[half]-pdl_opn_cls043/
             ...
         └─ snare-X1/
             └─ center-zon_ctr_rim043/
                 └─ snare-X1-center-043a-dtm_dbm_ons.flac
                 └─ snare-X1-center-085a-dtm_dbm_ons.flac
             └─ center-zon_ctr_rim085/
                 ...
             └─ edge/
                 ...
             └─ rimshot/
                 ...
Fields:
    kitInfo         = KitName by AuthorName [License]
    instrumentType  = kick snare tom hihat ride crash splash china stax cowbell tambourine shaker timbale octoban woodblock clap ...
    balance         = optional L1-L127 R1-R127 C0, used as instrument fallback when articulation has no balance; bare L/R/C is ignored with warning
    articulation    = center rim rimshot edge bell bow pedal
    dimensionCode   = pdl_opn_cls zon_bel_edg zon_ctr_edg zon_lft_rgt // or shorter codes poc zbe zce zlr?
    velocity        = 001-127
    roundRobinLetter = letter a-z
    micChain        = micCode("_"micCode)* to describe each channel for multichannel files
                        ex. dim_dom_ons_rfs for direct in mono, direct out mono, overhead normal stereo, room far stereo
                      type = d (direct) | o (overhead) | r (room)
                      position = (for direct) u (sub) | s (side) | t (top) | b (bottom) | i (in) | o (out)
                                 (for overhead) n (normal) | w (wide)
                                 (for room) c (close) | f (far)
                      format   = m (mono) | s (stereo) | i (mid-side)
                      variant  = optional number for any additional variant specification (ex. snare with 2 top mics: dtm1, dtm2)
Identity resolution for instrument maps:
    Kit IDs use the shortest unique value from: KitName, KitName by AuthorName, KitName by AuthorName [License], full folder name.
    Instrument IDs use the shortest unique value from: <instrumentType>-<size>, +<balance>, +<description>, full folder name.
    Articulation IDs use the shortest unique value from: <articulation>[optionalExpressionShortName], full articulation folder name.
    Full folder names stay valid so old maps can still resolve when possible.
```

In **Conf > Sample library > Browse...**, choose `My sample library`. This is the folder above the kits. Karatala reads kit folders, then drum folders, then articulation folders. It reads sample files directly inside each articulation folder.

An **articulation** is a way to play the instrument. For example, `center` is a hit in the center of a drum, and `rim` is a hit on its rim.

Keep the recordings and any other kit files in their original folders when you move a kit. Do not put all recordings in one folder.

## 2. Name the kit and drum folders

A kit folder can have a name, author, and license:

```text
My Kit by My Name [CC-BY-4.0]
```

Use the license that applies to your recordings. The author and license parts are optional. A folder named `My Kit` also works.

A drum folder uses this form:

```text
<instrument>-<size>[-<balance>][-<description>]
```

The parts in square brackets here are optional. Do not type those brackets into the drum folder name.

Examples:

- `kick-22`: a 22-inch kick drum.
- `snare-14`: a 14-inch snare drum.
- `tom-X1`: a tom with an unknown size, named X1.
- `tom-12-L40-My drum custom description`: a tom with optional left placement and a description.

Instrument names include `kick`, `snare`, `tom`, `hihat`, `ride`, `crash`, `splash`, `china`, and `stax`. Use the instrument type as the first part. Keep descriptions in the drum folder name, not in the sample's first five parts.

Optional balance codes are `L1` to `L127`, `R1` to `R127`, and `C0`. They describe left, right, or center placement. An articulation folder can also end in a balance code, such as `center-L40`. This takes priority over the drum folder balance. Do not use a bare `L`, `R`, or `C`.

## 3. Name the articulation folders

Common names are `center`, `rim`, `rimshot`, `sidestick`, `bow`, `edge`, `bell`, and `pedal`.

For a simple drum, use `center`. For a cymbal, you might use `bow`, `edge`, and `bell`.

You can add a state in brackets. For example, `bow[half]` names a half-open hi-hat bow sound. These brackets are part of the actual name.

## 4. Name the sample files

Use FLAC or WAV audio files with this name form:

```text
<instrument>-<size>-<articulation>-<velocity><roundRobinLetter>-<micCodes>.<extension>
```

Example:

```text
kick-22-center-080a-dim.flac
```

| Part | Meaning |
| --- | --- |
| `kick` | The instrument type |
| `22` | The size, matching the drum folder |
| `center` | The articulation, matching the folder's articulation name |
| `080` | Hit strength from `001` to `127`: low numbers for soft hits, high numbers for hard hits |
| `a` | Round robin: one recording of this hit strength |
| `dim` | Microphone code: direct, inside, mono |
| `flac` | Audio file type |

**Round robin** means different recordings of the same kind of hit. Use `a` to `z` for these recordings. For example, `127a` and `127b` are two hard hits. Karatala can use them in turn. The letter is optional when you have only one recording at that strength.

The number in a hi-hat sample name is still hit strength. It is separate from the pedal number in the folder name.

## 5. Name the microphones

A microphone code has three characters, with an optional variant number such as `dtm1` or `dtm2`.

| Part | Codes |
| --- | --- |
| Type | `d` = direct, `o` = overhead, `r` = room |
| Direct position | `u` = sub, `s` = side, `t` = top, `b` = bottom, `i` = inside, `o` = outside |
| Overhead position | `n` = normal, `w` = wide |
| Room position | `c` = close, `f` = far |
| Audio format | `m` = mono, `s` = stereo, `i` = mid-side |

Examples: `dtm` is a direct top mono microphone. `ons` is normal overhead stereo. `rfs` is far room stereo. Stereo and mid-side codes each describe two audio channels.

For separate microphone files from one hit, use the same velocity and round robin letter:

```text
snare-14-center-127a-dtm.flac
snare-14-center-127a-dbm.flac
snare-14-center-127a-ons.flac
```

For one audio file with several microphones, join their codes with `_` in audio channel order:

```text
snare-14-center-127a-dtm_dbm_ons.flac
```

This example has four channels: top mono, bottom mono, and two overhead stereo channels. Use concrete microphone codes in sample names. Do not use `*`.

## Optional hi-hat and playing-zone values

An articulation folder can have extra control values from `0` to `127`:

```text
bow[half]-pdl_opn_cls043/
    hihat-14-bow[half]-080a-ons.flac
    hihat-14-bow[half]-127a-ons.flac
```

The `043` after `pdl_opn_cls` describes the pedal state for this sound. The `080` and `127` in the files describe hit strength.

Supported control names are:

| Code | What it describes |
| --- | --- |
| `pdl_opn_cls` | Hi-hat pedal open/close state |
| `zon_ctr_rim` | Center/rim playing zone |
| `zon_bow_edg` | Bow/edge playing zone |
| `zon_lft_rgt` | Left/right playing zone |

For example, `center-zon_ctr_rim043` adds a center/rim value to a center sound. Several controls can be joined with `-`, such as `bow[half]-pdl_opn_cls043-zon_bow_edg085`.

## Optional volume and timing

Add these parts after the microphone codes, before the file extension:

| Part | Meaning |
| --- | --- |
| `vn4` | Reduce volume by 4 dB |
| `vp3` | Increase volume by 3 dB |
| `on5.5` | Skip the first 5.5 milliseconds of the recording |
| `op2` | Wait 2 milliseconds before playing |
| `op2_ons_rfs` | Apply that delay only to the named microphones |

Example:

```text
snare-14-center-127a-dtm_dbm_ons-vn4-op2_ons.flac
```

This reduces the sample's volume by 4 dB and delays only its overhead channels by 2 milliseconds. Volume changes affect the whole file. Timing changes can affect the whole file or named microphones.

## Other kit files and preset names

A kit can contain `README.md`, `metadata.csv`, and `kit-overrides.yml`. Keep these files with the kit. They contain kit information, measured sample data, or saved sound changes. You do not need to write them by hand to make a simple kit.

Presets refer to sounds by `kit/instrument/articulation`. Karatala uses short names when they identify only one sound. If names repeat, it uses more of the folder name, such as author, license, balance, or description. Full folder names can also identify the sound when the match is unique.

Renaming or removing a kit, drum, or articulation can break a preset that uses it. The supplied `Default` preset combines sounds from several kits in the full sample library. It does not contain the recordings itself.
