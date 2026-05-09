# Drumgizmo to Karatala Kit Converter - Implementation Plan

## Overview
A Python script that converts Drumgizmo drum kit samples to Karatala kit format, including automated folder structure organization, file renaming, and sample variant management based on mic configurations.

**Default Behavior:** Dry-run mode (shows all commands without executing them)

---

## 1. Core Architecture

### 1.1 Main Components
```
converter.py
├── DGKitParser          # Parse Drumgizmo XML files
├── ChannelMapper        # Map DG channels to Karatala mic codes
├── InstrumentNamer      # Parse instrument names & infer articulations
├── KaratalaPathBuilder  # Generate Karatala filename/folder structure
├── FileOrganizer        # Generate move/rename commands
├── DryRunExecutor       # Output commands without executing
└── ReportGenerator      # Summary & debug info
```

### 1.2 Data Flow
```
Drumgizmo XML files
    ↓
Parse kit metadata (channels, instruments, samples)
    ↓
For each instrument:
  ├─ Parse name → instrument type + size + articulation
  ├─ Extract velocity/sample info
  ├─ Map channels to mic codes
  └─ Build Karatala filename
    ↓
Generate platform-specific move/rename commands
    ↓
Dry-run report with statistics
```

---

## 2. Detailed Algorithm

### 2.1 Phase 1: Kit Discovery & Parsing

**Input:** `/todo/` directory containing kit folders
**Output:** `KitContext` object with parsed structure

```python
class KitContext:
    kit_name: str               # e.g., "CrocellKit"
    kit_author: str            # From XML or folder name
    kit_license: str           # Default: unknown
    root_dir: Path             # Drumgizmo kit root
    
    channels: Dict[str, int]   # name -> channel_index
    instruments: List[InstrumentContext]
    
class InstrumentContext:
    dg_name: str               # e.g., "ChinaL"
    dg_folder: Path            # ChinaL/
    dg_xml_file: Path          # ChinaL/ChinaL.xml
    
    samples: List[SampleContext]  # parsed from XML
    
class SampleContext:
    dg_name: str               # e.g., "ChinaL-1", "ChinaL-2"
    velocity: int              # 0-127 (inferred from naming or power)
    power: float               # dB power from XML
    
    audio_files: List[AudioFileRef]
    
class AudioFileRef:
    channel_name: str          # e.g., "AmbLeft"
    file_path: Path            # samples/1-ChinaL.wav
    file_channel: int          # 1-based channel in file
```

**Algorithm:**
1. Walk `/todo/` for kit folders
2. For each kit folder:
   - Check for `{KitName}.xml` (main kit descriptor)
   - Parse `<drumkit>` → extract name, channels, instruments
   - For each `<instrument>`:
     - Load sub-XML file (e.g., `ChinaL/ChinaL.xml`)
     - Parse `<samples>` → velocity, power, audiofiles
3. Validate all referenced files exist

---

### 2.2 Phase 2: Channel-to-Mic-Code Mapping

**Input:** Channel name from Drumgizmo
**Output:** Karatala mic code (3-4 letters per mic)

#### Channel Mapping Dictionary

The mapping uses a multi-level strategy:

```python
CHANNEL_TO_MIC = {
    # Room mics (far)
    "AmbLeft": "rfs",      # room far stereo
    "AmbRight": "rfs",     # (paired, same code)
    
    # Overheads
    "OHLeft": "ons",       # overhead normal stereo
    "OHRight": "ons",      # (paired, same code)
    "OHCenter": "rcm",     # room close mono (or overhead center)
    
    # Drum-specific: skip (will be replaced with 'x')
    # Unless they're the primary mic for that drum, marked via main="true"
}

def identify_stereo_pairs():
    """
    Heuristic: if channel name ends in L/Left and Right versions exist,
    they form a stereo pair → single mic code for both.
    """
    # Group channels by base name (remove L/R/Left/Right)
```

#### Mapping Logic

```python
def map_drumgizmo_channels(
    instrument_name: str,
    channels: Dict[str, int],  # all kit channels
    audiofiles: List[AudioFileRef],  # this sample's channels
    main_channels: Set[str]  # marked as main="true"
) -> List[str]:
    """
    Returns list of mic codes in channel order.
    For channels not used by this instrument, use 'x'.
    """
    
    mic_codes = []
    
    for channel_name in channels.keys():
        if channel_name not in audiofiles:
            # Not used by this instrument
            mic_codes.append('x')
        elif channel_name in main_channels:
            # Primary mic for this instrument
            mic_codes.append(map_primary_channel(instrument_name, channel_name))
        else:
            # Secondary/ambient mic
            mic_codes.append(map_ambient_channel(channel_name))
    
    return mic_codes
```

#### Specific Channel Rules

```
AmbLeft / AmbRight        → "rfs" (room far stereo pair)
OHCenter                  → "rcm" (room close mono)
OHLeft / OHRight          → "ons" (overhead normal stereo pair)
Hihat (when instrument=hihat) → "dtm" (direct top mono) or "x" when instrument is not hihat (skip, n/a mic)
Ride (when instrument=ride) → "dtm" (direct top mono) or "x" when instrument is not ride (skip, n/a mic)
SnareTop / SnareBottom    → "dtm"/"dbm" for snare or "x" (for crash/cymbal instruments)
Tom1 / Tom2 / FTom1 / FTom2 → "dtm" or "x"
KDrumInside / KDrumOutside → "dim"/"dom" or "x"
```

---

### 2.3 Phase 3: Instrument Name Parsing

**Input:** Drumgizmo instrument name (e.g., "ChinaL", "HihatClosed", "KDrumR")
**Output:** `InstrumentParseResult`

```python
class InstrumentParseResult:
    instrument_type: str      # "kick", "snare", "hihat", "crash", "china", "ride", "tom", "splash"
    size: str                 # "L1", "R1", "14", "22", "F1", "X1", ...
    articulation: str         # "center", "edge", "bow", "pedal", "edge[half]", "edge[open]", "edge[close]", "edge[tight]", "bow[half]", "bow[open]", "bow[close]", "bow[tight]", "rim", "rimshot", "sidestick"
    description: str          # Additional notes (optional)
```

#### Parsing Dictionary

Use a case-insensitive keyword dictionary:

```python
INSTRUMENT_KEYWORDS = {
    # Exact or lead keywords
    "kick": "kick",
    "kdrum": "kick",
    "bass": "kick",
    
    "snare": "snare",
    
    "tom": "tom",
    "ftom": "tom",    # floor tom → distinguish by 'F' prefix
    
    "hihat": "hihat",
    
    "ride": "ride",
    
    "crash": "crash",
    
    "splash": "splash",
    
    "china": "china",
    
    # Size/position indicators
    "l": "L",
    "r": "R",
    "left": "L",
    "right": "R",

    # other mic indicators
    "inside": "inside", # for dim (direct inside mono) mic
    "outside": "outside", # for dom
    
    # Articulation keywords
    "rimshot": "rimshot",
    "rim": "rim",
    "bow": "bow",
    "edge": "edge",
    "bell": "bell",
    "pedal": "pedal",
    "open": "open",
    "closed": "close",
    "semiopen": "half",
    "half": "half",
    "tight": "tight",
    "choke": "choke",
    "stopped": "choke",
    "rest": "rest",
    "xtra": "extra",
    "splash": "splash",  # pedal splash
    "pedalhit": "splash",  # pedal splash
}

def parse_instrument_name(name: str) -> InstrumentParseResult:
    """
    Examples:
    - "ChinaL" → (china, size=L, articulation=edge)
    - "HihatClosed" → (hihat, articulation=close)  
    - "HihatSemiOpen" → (hihat, articulation=half)
    - "SnareRimShot" → (snare, articulation=rimshot)
    - "KDrumR" → (kick, size=R)
    - "RideRBell" → (ride, size=R, articulation=bell)
    - "CrashRXtra" → (crash, size=R, description=extra)
    - "CrashRStopped" → (crash, size=R, articulation=choke)
    - "FTom2" → (tom, size=F2 or tom-F2)
    """
```

#### Parsing Algorithm

1. **Case Splitting:** Insert separators at case boundaries: `ChinaL` → `China L`
2. **Token Extraction:** Split by separators and numbers, keeping numbers
3. **Keyword Matching:** For each token, look up in `INSTRUMENT_KEYWORDS`
4. **Aggregation:**
   - First instrument keyword → `instrument_type`
   - Size indicators (L, R, numbers) → `size`
   - Articulation keywords → `articulation`
   - Remaining → `description`

#### Default Articulations (if not specified)

```python
DEFAULT_ARTICULATION = {
    "kick": "center",
    "snare": "center",
    "tom": "center",
    "hihat": "edge",      # or "bow" sometimes
    "ride": "bow",
    "crash": "edge",
    "splash": "edge",
    "china": "edge",
}
```

#### Special Hi-Hat Handling

HiHat samples need additional metadata based on closure level:

```python
HIHAT_VARIANTS = {
    "closed": ("bow", "pdl_opn_cls", 78),      # tight close
    "closednopedal": ("edge", "pdl_opn_cls", 64),  # half closed
    "semiopen": ("edge", "pdl_opn_cls", 52),  # half open
    "open": ("edge", "pdl_opn_cls", 43),      # open
    "pedal": ("pedal", "pdl_opn_cls", 85),    # full pedal
    "pedalhit": ("pedal", "splash", None),    # splash on pedal hit
}
```

---

### 2.4 Phase 4: Velocity & Sample Variant Detection

**Input:** Sample name and power, XML metadata
**Output:** Velocity level (0-127) and variant letter (a-z)

#### Velocity detection
Compute the instrument-wide maximum `power` first (max of all samples for that instrument)
and use it as a reference for velocity=127. The per-sample velocity is derived from
the sample power relative to that maximum (perceptual scaling using sqrt). If
no `max_power` is available, fall back to the numeric-suffix heuristic or mid velocity.
```python
def infer_velocity(
  sample_name: str,        # e.g., "ChinaL-1", "ChinaL-2"
  power: float,            # from XML <sample power="...">
  num_samples: int,        # total samples for this instrument
  max_power: float = None  # instrument max power (optional, compute once per instrument)
) -> int:
  """
  Derive a MIDI-style velocity in range 1..127.

  Strategy:
  - If `max_power` is provided (>0), treat it as reference → velocity 127.
    Map `power/max_power` to velocity using sqrt scaling: vel = 127 * sqrt(power/max_power).
  - Else if sample name ends with a numeric index, map index -> velocity across 1..127.
  - Fallback: return 64 (mid velocity).
  """
  import math, re

  # Use power relative to instrument max when available
  if max_power and max_power > 0.0:
    ratio = power / max_power
    ratio = max(0.0, min(1.0, ratio))
    vel = int(round(127.0 * math.sqrt(ratio)))
    return max(1, min(127, vel))

  # Fallback: numeric suffix heuristic
  m = re.search(r'(\d+)$', sample_name)
  if m and num_samples and num_samples > 0:
    idx = int(m.group(1))
    vel = int(round(127.0 * (idx / float(num_samples))))
    return max(1, min(127, vel))

  # Final fallback
  return 64
```

#### Sample Variant Assignment

```python
def assign_sample_variant(
    sample_index: int,    # 0, 1, 2, ... within same velocity level
) -> str:
    """
    Assign round-robin letter: a, b, c, ...
    """
    if sample_index > 25:
        print(f"WARNING: More than 26 variants, will reuse letters")
    return chr(ord('a') + (sample_index % 26))
```

---

### 2.5 Phase 5: Karatala Filename Generation

**Input:** Parsed instrument, articulation, velocity, sample info
**Output:** Karatala canonical filename

#### Naming Template

```
{instrument_type}-{size}-{articulation}[-{expression_shortname}]-{velocity:03d}{variant}-{mic_chain}-{optional_params}.{ext}

Examples:
  china-L-edge-127a-rfs_x_x_x_x_x_rcm_ons_x_x_x_x.wav
  kick-22-center-127a-dim_dom_ons_rfs.flac
  hihat-14-edge[half]-043a-oni.flac
```

#### Components

```python
class KaratalaFilename:
    instrument_type: str      # kick, snare, tom, hihat, ride, crash, china
    size: str                 # 14, 22, L1, R1, X1, F1 (optional)
    articulation: str         # center, edge, bow, rim, pedal
    expression_name: str      # [half], [close], [open] (optional for hihat)
    expression_code: str      # pdl_opn_cls, zon_ctr_rim, etc. (optional)
    expression_value: int     # 43, 52, 64, 78, 85 (optional)
    velocity: int             # 001-127
    variant: str              # a-z
    mic_chain: str            # rfs_x_x_x_x_x_rcm_ons_x_x_x_x
```

#### Filename Building

```python
def build_karatala_filename(context) -> str:
    parts = [
        context.instrument_type,
        context.size,
        context.articulation,
    ]
    
    # Add expression (hihat only usually)
    if context.expression_name:
        parts.append(f"[{context.expression_name}]")
        if context.expression_code:
            parts.append(context.expression_code)
            parts.append(str(context.expression_value))
    
    # Velocity + variant
    velocity_part = f"{context.velocity:03d}{context.variant}"
    
    # Mic chain
    mic_chain = "_".join(context.mic_codes)
    
    # Optional metadata
    optional = []
    if context.peak_db and context.peak_ms:
        optional.append(f"p{int(context.peak_db*10)}_{int(context.peak_ms)}")
    if context.decay_ms:
        optional.append(f"d{int(context.decay_ms)}")
    if context.decay_code:
        optional.append(f"c{context.decay_code}")
    optional_str = "-".join(optional)
    
    # Assemble
    core = "-".join(parts) if context.size else "-".join(parts)
    filename = f"{core}-{velocity_part}-{mic_chain}"
    if optional_str:
        filename += f"-{optional_str}"
    filename += f".{context.extension}"
    
    return filename
```

---

### 2.6 Phase 6: Folder Structure Generation

**Output:** Karatala folder organization

```
/instruments/
  └─ {kit_name}/
     └─ {instrument_type}-{size}/             # InstrumentSpec folder
        ├─ {articulation}-{expression_code}{value}/  # ArticulationSpec folder
        │  ├─ {full_filename}.wav
        │  ├─ {full_filename}.wav
        │  └─ ...
        ├─ {other_articulation}/
        │  └─ ...
        └─ ...

Example:
/instruments/
  └─ CrocellKit/
     ├─ china-L/
     │  └─ edge/
     │     ├─ china-L1-edge-127a-rfs_x_x_x_x_x_rcm_ons_x_x_x_x.wav
     │     ├─ china-L1-edge-127b-rfs_x_x_x_x_x_rcm_ons_x_x_x_x.wav
     │     └─ ...
     ├─ kick-22/
     │  └─ center/
     │     ├─ kick-22-center-032a-dim_dom_ons_rfs.wav
     │     ├─ kick-22-center-032a-dim_dom_ons_rfs.wav
     │     └─ ...
     └─ ...
```

---

## 3. File Organization & Command Generation

### 3.1 Move/Rename Commands

Generate platform-specific commands:

```python
def generate_move_command(src: Path, dst: Path, os_type: str) -> str:
    """
    Windows (cmd):
      move "C:\path\source" "C:\path\dest"
    
    Linux/Mac (sh):
      mv "/path/source" "/path/dest"
    """
    if os_type.lower() == "windows":
        ...

def generate_rename_command(src: Path, new_name: str, os_type: str) -> str:
    """
    Windows (cmd):
      ren "C:\path\file.wav" "newname.wav"
    
    Linux/Mac (sh):
      mv "/path/file.wav" "/path/newname.wav"
    """
    if os_type.lower() == "windows":
        ...
```

### 3.2 Multi-Step Staging - optional, later.

Some samples are stored as multichannel files. Default behavior is **not** to modify source files; in dry-run mode the converter will only list extraction and move commands. When extraction is enabled (`--extract-channels`), follow these rules:

1. **Extract only useful channels** — do not extract channels mapped to `'x'` (ignored) unless `--keep-ignored` is specified. Prefer extracting:
   - stereo pairs (AmbLeft/AmbRight → single stereo file)
   - overhead stereo pair (OHLeft/OHRight)
   - important mono mics (OHCenter, KDrumInside/Outside, SnareTop/Bottom) when marked as `main="true"`

2. **FFmpeg extraction examples** (choose based on file channel layout):

   - Extract individual channels by channel index (when each channel is a separate stream):
     ```bash
     ffmpeg -i "source.wav" -map 0:0 "temp/ch0.wav"
     ffmpeg -i "source.wav" -map 0:1 "temp/ch1.wav"
     ```

   - Extract individual channels from a single multichannel stream (useful for multichannel WAV):
     ```bash
     ffmpeg -i "source.wav" -map_channel 0.0.0 "temp/ch0.wav"
     ffmpeg -i "source.wav" -map_channel 0.0.1 "temp/ch1.wav"
     ```

   - Extract a stereo pair into a stereo file (keep both channels):
     ```bash
     ffmpeg -i "source.wav" -map_channel 0.0.0 -map_channel 0.0.1 "temp/amb_stereo.wav"
     ```

   - Windows cmd move (dry-run shows these commands):
     ```cmd
      move "temp\amb_stereo.wav" "instruments\Kit\...\amb-stereo.wav"
     ```

3. **Rename** extracted/temp files to the Karatala canonical filename (see filename builder) and place into the target folder structure.

4. **Move** the renamed file into the kit `instruments/{kit}/{instrument}-{size}/{articulation}/` folder.

Notes:
- Extraction can be slow; keep `--extract-channels-ffmpeg` optional.
- The converter will print the exact ffmpeg and move/rename commands in dry-run mode; nothing is executed unless `--apply` is given.

---

## 4. Dry-Run & Reporting

The converter is designed primarily as a dry-run tool for now. It will produce a complete set of platform-specific commands and a summary report (statistics, warnings, and file lists). Actual file operations and extractions are opt-in (`--apply`, `--extract-channels-ffmpeg`).

### 4.1 Dry-Run Mode (Default)

```
Dry-run mode. No changes to files will be made.
═══════════════════════════════════════════════════════════

Kit: CrocellKit
Source: /todo/CrocellKit/
Instruments: 15
Total Samples: 312

COMMANDS TO EXECUTE:
────────────────────
# Create directories
mkdir -p "instruments/CrocellKit/china-L/edge"
mkdir -p "instruments/CrocellKit/china-R/edge"
mkdir -p "instruments/CrocellKit/kick-22/center"
...

# Extract and rename samples
ffmpeg -i "CrocellKit/ChinaL/samples/1-ChinaL.wav" -map 0:0 "temp/china-L-127a-rfs.wav"
move "temp/china-L-1a-rfs.wav" -Destination "instruments/CrocellKit/china-L/edge/china-L-edge-001a-rfs_x_x_x_x_x_rcm_ons_x_x_x_x.wav"
...

═══════════════════════════════════════════════════════════

STATISTICS:
────────────────────
Instruments processed: 15
  ├─ kick: 1 (size: 22)  
    ├─ center: 6 channels 10 layers 2 variants
  ├─ snare: 1 (size: X1) 
    ├─ center 7 channels 20 layers 2 variants
    ├─ rimshot 7 channels 20 layers 2 variants
  ├─ tom: 4 (sizes: X1, X2, F1, F2) 
    ├─ center 5 channels 8 layers 2 variants
  ├─ hihat: 1 (size: 14) 
    ├─ edge[open] 5 channels 12 layers 2 variants
    ├─ edge[half] 5 channels 12 layers 2 variants
    ...
  ├─ ride: 1 (size: 14) 
    ├─ bow 5 channels 10 layers 2 variants
    ...
  ├─ crash: 2 (sizes: L1, R1) 
    ├─ edge 4 channels 5 layers 2 variants
  ├─ china: 2 (sizes: L1, R1) 
    ├─ edge 4 channels 5 layers 2 variants
  └─ splash: 2 (sizes: L1, R1) 
    ├─ edge 4 channels 5 layers 1 variants


Articulations detected:
  ├─ center: 3 instruments
  ├─ edge: 8 instruments
  ├─ bow: 1 instrument
  ├─ pedal: 1 instrument
  └─ rimshot: 1 instrument



WARNINGS:
────────────────────
[W] HihatSemiOpen: Velocity inference unclear from DG (using default 64)
[W] CrashRXtra: Extra crash variant—may need manual articulation review
[W] SnareRest: Rest articulation detected (keeping for now, can delete manually)

EXECUTION:
To apply these changes, run:
  python converter.py /todo/CrocellKit --apply

To generate for all kits:
  python converter.py /todo --apply-all
```

### 4.2 Report Content

- Kit metadata (name, author, license)
- Instruments discovered with type/size
- Velocity distribution
- Sample variant counts
- Channel mapping matrix
- Articulation mapping
- Warnings/notes for manual review
- Estimated disk space changes
- Dry-run command list

---

## 5. CLI Interface

### 5.1 Usage

```bash
# Dry-run for single kit
python converter.py /path/to/CrocellKit

# Dry-run for all kits in /todo
python converter.py /todo

#TODO: maybe later
# # Actually apply changes
# python converter.py /todo --apply

# # Specific options
# python converter.py /todo \
#     --apply \
#     --output-dir /path/to/instruments \
#     --log-file converter.log \
#     --verbose

# # Extract channels (ffmpeg, slow)
# python converter.py /todo --extract-channels-ffmpeg

# # Keep temp files for review
# python converter.py /todo --keep-temp
```

### 5.2 Arguments

```python
parser = argparse.ArgumentParser(
    description="Convert Drumgizmo kits to Karatala format"
)
parser.add_argument(
    "kit_path",
    help="Path to kit folder or /todo root"
)

#TODO: maybe later
# parser.add_argument(
#     "--apply",
#     action="store_true",
#     help="Actually move files (default: dry-run)"
# )
# parser.add_argument(
#     "--output-dir",
#     default="./instruments",
#     help="Output directory for organized kits"
# )
# parser.add_argument(
#     "--extract-channels-ffmpeg",
#     action="store_true",
#     help="Use ffmpeg to extract multichannel (slow but lossless)"
# )
# parser.add_argument(
#     "--keep-temp",
#     action="store_true",
#     help="Don't delete temp extracted files"
# )
# parser.add_argument(
#     "--verbose", "-v",
#     action="store_true",
#     help="Verbose output"
# )
# parser.add_argument(
#     "--log-file",
#     help="Write detailed log to file"
# )
```

---

## 6. Error Handling & Validation

### 6.1 Validation Steps

1. **Kit Validation:**
   - Check XML exists and is valid
   - Verify all referenced files exist
   - Check channels are consistent

2. **Instrument Validation:**
   - Verify name can be parsed
   - Check at least one velocity layer exists
   - Validate all audiofiles reference existing samples

3. **Sample Validation:**
   - Verify file format is audio (wav, flac, ogg)
   - Check file is readable
   - Validate multichannel integrity

4. **Filename Validation:**
   - Check generated names don't exceed OS limits
   - Validate no duplicate filenames
   - Check characters are filesystem-safe

### 6.2 Error Classes

```python
class ConverterError(Exception): pass
class KitValidationError(ConverterError): pass
class InstrumentParseError(ConverterError): pass
class FilenameGenerationError(ConverterError): pass
class FileOperationError(ConverterError): pass
```

---

## 7. Implementation Order

### Phase A: Foundation
- [ ] XML parsing (kit + instrument specs)
- [ ] Data structures (KitContext, etc.)
- [ ] Basic CLI scaffold

### Phase B: Logic
- [ ] Channel-to-mic mapping
- [ ] Instrument name parsing
- [ ] Velocity/variant inference
- [ ] Karatala filename generation

### Phase C: Operations
- [ ] Folder structure generation
- [ ] Move/rename command generation
- [ ] Platform detection (Windows/Linux/Mac)
- [ ] File operation logic

### Phase D: Report & UX
- [ ] Dry-run report generation
- [ ] Logging system
- [ ] CLI refinements
- [ ] Integration tests with sample kits

### Phase E: Optimization & Polish
- [ ] Performance tuning for large kits
- [ ] ffmpeg integration (optional)
- [ ] Parallel processing (optional)
- [ ] Documentation

---
<!-- TODO: maybe later 
## 8. Testing Strategy

### 8.1 Unit Tests

```python
class TestChannelMapping(unittest.TestCase):
    def test_amb_channels_map_to_rfs(self): ...
    def test_overhead_channels_map_correctly(self): ...
    def test_instrument_specific_mics_excluded(self): ...

class TestInstrumentParsing(unittest.TestCase):
    def test_parse_china_l(self): ...
    def test_parse_hihat_closed(self): ...
    def test_parse_kick_drum_r(self): ...
    def test_parse_snare_rim_shot(self): ...
    def test_parse_tom_f2(self): ...

class TestFilenameGeneration(unittest.TestCase):
    def test_basic_filename(self): ...
    def test_hihat_with_expression(self): ...
    def test_multichannel_mic_chain(self): ...

class TestFolderStructure(unittest.TestCase):
    def test_instrument_folder_creation(self): ...
    def test_articulation_subfolder(self): ...
```

### 8.2 Integration Tests

- Run on CrocellKit (simple, 15 instruments, ~300 samples)
- Run on MuldjordKit3 (medium, ~20-30 instruments)
- Run on full `/todo/` suite
- Manual audits of generated filenames

### 8.3 Test Kits

Use real kit samples from `/todo/`:
- CrocellKit/
- MuldjordKit3/
- tchackpoum_drumgizmo_13/
- LudwigBlackCortex/

--- -->

## 9. Configuration & Extensibility

### 9.1 Mappings as Config Files

Store mappings in YAML for easier updates:

```yaml
# same_as_py_script_name.yaml
channels_mapping:
  AmbLeft: rfs
  AmbRight: rfs
  OHLeft: ons
  OHRight: ons
  OHCenter: rcm
  ...
articulation_defaults:
  kick: center
  snare: center
  tom: center
  hihat: edge
  ride: bow
  crash: edge
  ...
hihat_variants:
  closed: [bow, pdl_opn_cls, 78]
  closednopedal: [edge, pdl_opn_cls, 64]
  semiopen: [edge, pdl_opn_cls, 52]
  open: [edge, pdl_opn_cls, 43]
  pedal: [pedal, pdl_opn_cls, 85]
  ...
```

---

## 10. Example Walkthrough: ChinaL Sample

**Input (Drumgizmo):**
```
CrocellKit/
├─ CrocellKit.xml
└─ ChinaL/
   ├─ ChinaL.xml
   └─ samples/
      └─ 1-ChinaL.wav  (15 channels, 48kHz, 16-bit)
         Channels: AmbL, AmbR, OHLeft, OHRight, OHCenter, Hihat, Ride, 
                   SnareTop, SnareBottom, Tom1, Tom2, FTom1, FTom2, 
                   KDrumInside, KDrumOutside
```

**Parsing:**
```
Instrument name: ChinaL
  ├─ Type: china (keyword match)
  ├─ Size: L1 (position indicator + found index)
  ├─ Max Power: 0.000105857
  └─ Articulation: edge (default for cymbals)

Sample: ChinaL-1
  ├─ Power: 0.000105857
  ├─ Velocity: 127
  └─ Variant: a (first for this velocity)

Channels used:
  AmbLeft (main=true) → rfs (room far stereo)
  AmbRight → rfs (paired)
  OHCenter (main=true) → rcm (room close mono)
  OHLeft → ons (overhead normal stereo)
  OHRight → ons (paired)
  Others (Hihat, Ride, SnareTop, SnareBottom, Tom1, Tom2, FTom1, FTom2, KDrumInside, KDrumOutside) → x (not used)

Mic Chain: rfs_rfs_ons_ons_rcm_x_x_x_x_x_x_x_x_x_x
           (but compress paired mics) → rfs_ons_rcm_x_x_x_x_x_x_x_x
```

**Output (Karatala):**
```
# Source:  CrocellKit/ChinaL/samples/1-ChinaL.wav
#          ├─ Channel 0: AmbLeft → ...
#          ├─ Channel 1: AmbRight → ...
#          ├─ Channel 2: OHLeft → ...
#          ├─ Channel 3: OHRight → ...
#          ├─ Channel 4: OHCenter → ...
#          └─ Channels 5-14: [skip]
#
# Extract commands:
  ffmpeg -i "CrocellKit/ChinaL/samples/1-ChinaL.wav" -map 0:0 "temp/ch0.wav"
  ffmpeg -i "CrocellKit/ChinaL/samples/1-ChinaL.wav" -map 0:1 "temp/ch1.wav"
#   ...
# 
# Move command:
  mkdir -p "instruments/CrocellKit/china-L1/edge"
  move "temp/ch0_ch1.wav" "instruments/CrocellKit/china-L1/edge/china-L1-edge-127a-rfs_ons_rcm_x_x_x_x_x_x_x.wav"
#   ...

```

---

## 11. Known Challenges & Mitigations

| Challenge | Root Cause | Mitigation |
|-----------|-----------|-----------|
| Hi-hat closure level ambiguity | DG doesn't explicitly state closure % | Use sample count + hardcoded aliases (Closed=78, Half=52, Open=43) |
| Multichannel extraction | DG uses single files with many channels | Pre-extract with ffmpeg in temp folder |
| Velocity inference | DG may not have velocity in filename | Use power dB + sample ordering heuristic |
| Stereo pair detection | L/R naming inconsistent | Case-insensitive keyword + broad pattern matching |
| License metadata | DG kits may lack license info | Default to "unknown", allow override in config |
| Filesystem path limits | Windows 260-char limit | Abbreviate long mic codes (rfs→rfs, not room_far_stereo) |
| Round-robin limits | Only 26 letters (a-z) | Allow wraparound with warning, support 26+ variants |

---

## 12. Future Enhancements //TODO: maybe later

- [ ] Auto-generate FLAC conversion
- [ ] Parallel processing for large kits
- [ ] GUI tool wrapper
- [ ] Integration with LibraryManager (C++ side)
- [ ] Validation pass before apply (checksum, file counts)
- [ ] Rollback capability (transaction log)
- [ ] Web-based converter (Flask/FastAPI)



* original propmt

can u plan in .md file py converter script for Drumgizmos kits (under this workspace in /todo/* subfolders) to Karatala kit format that will organize/rename/move samples (by-default making do dry run without acrual changes, just showing commands (mostly move/remane, need to be platform specific depends on current os) what should run and some debug info and summary for review)
according to Sample Filename Convention and folder structure of Karatala described in KitLibModel comment (attached)

basically it will need to check xml files for
<drumkit name="CrocellKit" ...>
  <channels>
   <channel name="AmbLeft"/>
   <channel name="AmbRight"/>
   <channel name="OHLeft"/>
   <channel name="OHRight"/>
   <channel name="OHCenter"/>
   <channel name="Hihat"/>
   <channel name="Ride"/>
   <channel name="SnareTop"/>
   <channel name="SnareBottom"/>
   <channel name="Tom1"/>
   <channel name="Tom2"/>
   <channel name="FTom1"/>
   <channel name="FTom2"/>
   <channel name="KDrumInside"/>
   <channel name="KDrumOutside"/>
   <instruments>
    <instrument name="ChinaL" file="ChinaL/ChinaL.xml">
      <channelmap in="AmbLeft" out="AmbLeft" main="true"/>
      <channelmap in="AmbRight" out="AmbRight" main="true"/>
      <channelmap in="OHLeft" out="OHLeft" main="true"/>
      <channelmap in="OHRight" out="OHRight" main="true"/>
      <channelmap in="OHCenter" out="OHCenter" main="true"/>
      <channelmap in="Hihat" out="Hihat"/>
      <channelmap in="Ride" out="Ride"/>
      <channelmap in="SnareTop" out="SnareTop"/>
      <channelmap in="SnareBottom" out="SnareBottom"/>
      <channelmap in="Tom1" out="Tom1"/>
      <channelmap in="Tom2" out="Tom2"/>
      <channelmap in="FTom1" out="FTom1"/>
      <channelmap in="FTom2" out="FTom2"/>
      <channelmap in="KDrumInside" out="KDrumInside"/>
      <channelmap in="KDrumOutside" out="KDrumOutside"/>
    </instrument>
...
ChinaL/ChinaL.xml:
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
HihatSemiOpen - hihat half  (naming here is fucked, here its probably is like more closed edge[half]-pdl_opn_cls52)
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