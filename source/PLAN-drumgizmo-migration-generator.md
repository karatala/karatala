# ✅ COMPLETED: DrumGizmo Migration Generator Plan

## Summary

Revise `scripts/convert-drumgizmo2karatala.py` into a one-off, deterministic review and Python conversion-script generator. The generator itself should not execute conversion. It should generate readable Python files for clean rows, FLAC extraction commands for useful mic channels, and one consolidated review CSV where ambiguous rows are marked in context.

Default behavior remains non-destructive: inspect DrumGizmo XML/audio references, generate reports and scripts, and leave actual conversion to manually running reviewed Python scripts.

## Key Changes

### CLI

Add options:

- `--output-dir`, default `instruments`.
- `--audit-dir`, default `<output-dir>/<kit>/_audit`.
- `--tmp-root`, default `%TEMP%\karatala-drumgizmo`.
- `--ffmpeg`, default `ffmpeg`, emitted as `FFMPEG = ...` in generated settings.
- `--audio-format flac`, fixed/default for this migration.
- Runnable conversion scripts include non-quarantined rows. Rows with `keep: False` are present but skipped by default.

### Generated Files

Generate per kit:

- `convert_<kit>.py`: extraction/conversion operations plus review-refresh mode; dry-run by default, `--execute` to run ffmpeg, `--write-review` to rewrite the CSV after map edits.
- `map_<kit>.py`: editable per-kit instrument, articulation, parameter, keep/drop, and mic-code mapping dictionaries. It includes both useful mics and skipped bleed mics.
- `_audit/review-<kit>.csv`: main review file. Ready, skipped, and quarantined rows are together; filter the `status` column.
- `_audit/summary-<kit>.md`: readable counts, mappings, warnings, and next actions.

Generate shared helper files:

- `_common/drumgizmo_settings.py`: editable global paths and tool variables shared by every generated kit script.
- `_common/drumgizmo_runtime.py`: shared dry-run/execution, path building, and ffmpeg command construction.

### Python Script Format

Generated conversion scripts must not duplicate global path/tool variables. Each `convert_<kit>.py` imports shared settings/runtime helpers and the kit-local mapping module:

```python
from _common import drumgizmo_settings as settings
from _common.drumgizmo_runtime import Operation, run_operations
from map_aasimonster import INSTRUMENTS, MICS
```

`_common/drumgizmo_settings.py` owns shared variables:

```python
from pathlib import Path
import os

SRC_ROOT = Path(r"T:\source\repos\karatala\assets\drumgizmo")
OUT_ROOT = Path(r"T:\source\repos\karatala\assets\sample_library_pending")
TMP_ROOT = Path(os.path.expandvars(r"%TEMP%\karatala-drumgizmo"))
FFMPEG = r"ffmpeg"
AUDIO_FORMAT = "flac"
```

`map_<kit>.py` is the main human review/correction surface. The generator should create it with all source instruments, articulations, parameters, useful mic groups, and skipped bleed mic groups enumerated near the top. Existing map files should not be overwritten unless `--overwrite-map` is used.

Generated `convert_<kit>.py` scripts default to dry-run: they print ffmpeg commands and do not create folders or audio files. Passing `--write-review` rewrites the review CSV using the current edited map values. Passing `--execute` creates folders and runs ffmpeg.

Before each sample group, emit a readable `REM` block documenting:

- source XML path
- DrumGizmo sample name
- power, velocity, and round-robin letter
- source file and source channel count
- original source channels in file order
- keep/drop decision for each channel
- final Karatala output path

### Editable Kit Mapping Example

For an `aasimonster` kit, `map_aasimonster.py` should look like this style:

```python
# Auto-generated review map for aasimonster.
# Edit values here, then run convert_aasimonster.py.

INSTRUMENTS = {
    "china": {
        "instrument": "china-X1",
        "articulation": "edge",
        "parameter": "",
    },
    "hihat-closed": {
        "instrument": "hihat-X1",
        "articulation": "bow[close]",
        "parameter": "-pdl_opn_cls078",
    },
}

MICS = {
    ("china", "AMB"): {"keep": True, "code": "rfs"},
    ("china", "HIHAT"): {"keep": False, "code": "dtm"},
    ("china", "KICK_IN"): {"keep": False, "code": "dim"},
    ("china", "KICK_OUT"): {"keep": False, "code": "dom"},
    ("china", "OH"): {"keep": True, "code": "ons"},
    ("china", "RIDE"): {"keep": False, "code": "dtm"},
    ("china", "SNARE_BOTTOM"): {"keep": False, "code": "dbm"},
    ("china", "SNARE_TOP"): {"keep": False, "code": "dtm"},
    ("china", "SNARE_TRIGGER"): {"keep": False, "code": "dtm1"},
    ("china", "TOM1"): {"keep": False, "code": "dtm"},
    ("china", "TOM2"): {"keep": False, "code": "dtm"},
    ("china", "TOM3"): {"keep": False, "code": "dtm"},
    ("china", "TOM4"): {"keep": False, "code": "dtm"},
    ("hihat-closed", "DIRECT"): {"keep": True, "code": "dtm"},
    ("hihat-closed", "AMB"): {"keep": True, "code": "rfs"},
    ("hihat-closed", "OH"): {"keep": True, "code": "ons"},
}
```

The generated `convert_aasimonster.py` should use only those dictionaries, so corrections in `map_aasimonster.py` affect every command consistently:

```python
OPERATIONS = [
    Operation(
        kit="aasimonster",
        source_instrument="china",
        mic_group="AMB",
        sample_name="1-china",
        source_xml="aasimonster\\china\\china.xml",
        power=0.1,
        velocity=34,
        round_robin="a",
        group_label="Amb L-20 + Amb R-20",
        sources=[
            {"path": "aasimonster\\china\\samples\\1-china-Amb L-20.wav", "channel": 0, "name": "Amb L-20"},
            {"path": "aasimonster\\china\\samples\\1-china-Amb R-20.wav", "channel": 0, "name": "Amb R-20"},
        ],
        output_channels="stereo",
    ),
]
```

The map variables intentionally separate folder parameters from filename articulations:

- Folder: `articulation + parameter` -> `bow[close]-pdl_opn_cls078`.
- Filename: `instrument-articulation-078a-mic.flac` -> parseable by `KitLibModel`.

For multichannel WAV kits, commands should use source channel extraction instead of separate mono inputs, while still using the same editable map variables.

### Audio Extraction

Replace current `copy` behavior with ffmpeg extraction to FLAC:

- Stereo pairs become one stereo FLAC mic file, for example `rfs` or `ons`.
- Useful mono channels become mono FLAC mic files, for example `dim`, `dom`, `dtm`, `dbm`, or `rcm`.
- Dropped bleed channels are omitted from conversion output and never represented as `x` in final filenames.
- No final Karatala filename may contain `x`.

For `aasimonster`, where sources are mostly separate mono files, the generator should either pair L/R mono files into stereo FLAC outputs or emit single useful mono outputs. For multichannel kits such as `CrocellKit`, `artstar`, and `MuldjordKit3`, extract selected channels from the multichannel WAV source.

### Filename and Mapping Rules

Fix filename/folder generation:

- Hi-hat expression codes stay in the articulation folder only, not duplicated into the sample filename velocity segment.
- All hi-hat openness variants for one physical hat should resolve to the same instrument slot unless source naming clearly indicates a different physical hat.
- Unknown instrument types are marked `QUARANTINED` in the review CSV unless mapped explicitly in a small mapping table.
- Enabled duplicate target filenames are marked `QUARANTINED`, not emitted into the runnable conversion script.
- Disabled duplicate target filenames stay `SKIPPED` with an action note, so the user can enable one or change mic codes manually.

Add explicit mapping tables near the top of the script:

- `KNOWN_INSTRUMENT_TYPES`
- `INSTRUMENT_TYPE_ALIASES`, including `zilbel -> percussion`, `cowbell -> percussion`, and `bellchime -> percussion`, unless later changed manually.
- `CHANNEL_KEEP_RULES` for common/overhead/room/direct mic handling.
- `HIHAT_VARIANTS` corrected so output filenames remain parseable by `KitLibModel`.

## Implementation Notes

Refactor around small data records:

- `SourceSample`
- `ChannelDecision`
- `OutputFilePlan`
- `KitReport`

Build the plan in memory first, then validate before writing any generated artifacts:

- source exists
- generated filename matches Karatala parser rules
- no invalid mic tokens
- no duplicate output paths
- no round-robin letter overflow in clean output

Review/quarantine policy:

- Clean rows go to `convert_<kit>.py`.
- Optional bleed rows go to `convert_<kit>.py` and `map_<kit>.py` with `keep: False`, and stay in `review-<kit>.csv` with `status=SKIPPED`.
- Ambiguous rows stay in `review-<kit>.csv` with `status=QUARANTINED`, `reasons`, `action`, source channels, proposed mic, and proposed target path.
- After editing `map_<kit>.py`, run `convert_<kit>.py --write-review` to regenerate the CSV from the edited map before executing conversion.
- A kit may still produce a runnable partial conversion script when only some rows are quarantined.

Because `ffmpeg` is not currently on `PATH`, generated conversion scripts must rely on the editable `FFMPEG = "ffmpeg"` setting. The summary should warn when local preflight cannot find ffmpeg.

## Test Plan

Add or run script-level dry-run checks against:

- `assets/drumgizmo/aasimonster`
- `assets/drumgizmo/CrocellKit`
- `assets/drumgizmo/Salamander`
- `assets/drumgizmo/tchackpoum_drumgizmo_13`

Validate generated clean filenames with a Python mirror of `InstrumentSpec::parseSampleFile` rules:

- no hi-hat velocity parse failures
- no `x` mic tokens
- no unknown mic codes
- no duplicate destinations in clean conversion script

Compare against working `assets/sample_library` naming rules:

- kit/instrument/articulation folder shape
- filename segment order
- mic-code validity

Run one small conversion script manually only after review, then scan the generated output with Karatala's library scanner or the focused `Tests` target.

## Assumptions

- The converter is a one-off migration helper, not a polished long-term tool.
- Generated audio target is lossless FLAC.
- The generator produces review artifacts and Python conversion scripts but does not execute ffmpeg itself.
- Quarantine is preferred over failing a whole kit when only some rows are ambiguous.
- Any implementation touching `T:/source/repos/karatala/scripts/convert-drumgizmo2karatala.py` may need write approval if current sandbox writable roots remain limited to `source`.
