# DrumGizmo Migration Generator Plan

## Summary

Revise `scripts/convert-drumgizmo2karatala.py` into a one-off, deterministic audit and batch generator. It should not execute conversion itself. It should generate safe `.bat` files for clean rows, FLAC extraction commands for useful mic channels, and separate audit/quarantine artifacts for anything ambiguous.

Default behavior remains non-destructive: inspect DrumGizmo XML/audio references, generate reports and scripts, and leave actual conversion to manually running reviewed batches.

## Key Changes

### CLI

Add options:

- `--output-dir`, default `instruments`.
- `--audit-dir`, default `<output-dir>/<kit>/_audit`.
- `--tmp-root`, default `%TEMP%\karatala-drumgizmo`.
- `--ffmpeg`, default `ffmpeg`, emitted as `set FFMPEG=...` in generated `.bat` files.
- `--audio-format flac`, fixed/default for this migration.
- `--clean-only-bat`, default true: runnable batch includes only non-quarantined rows.

### Generated Files

Generate per kit:

- `convert-<kit>.bat`: clean extraction/conversion commands only.
- `map-<kit>.bat`: editable per-kit instrument, articulation, parameter, keep/drop, and mic-code mapping variables.
- `_audit/audit-<kit>.csv`: one row per source sample/channel decision.
- `_audit/manifest-<kit>.json`: structured source XML to Karatala mapping.
- `_audit/quarantine-<kit>.csv`: duplicate targets, unknown types, bad filenames, ambiguous channels.
- `_audit/summary-<kit>.md`: readable counts, mappings, warnings, and next actions.

Generate one shared include file:

- `_common/drumgizmo-env.bat`: editable global paths and tool variables shared by every generated kit batch.

### Batch Format

Batch files must not duplicate global path/tool variables. Each `convert-<kit>.bat` starts by calling a shared env include and then the kit-local mapping include:

```bat
@echo off
setlocal EnableExtensions
call "%~dp0..\_common\drumgizmo-env.bat"
call "%~dp0map-aasimonster.bat"
```

`_common/drumgizmo-env.bat` owns shared variables:

```bat
set "SRC_ROOT=T:\source\repos\karatala\assets\drumgizmo"
set "OUT_ROOT=T:\source\repos\karatala\assets\sample_library_pending"
set "TMP_ROOT=%TEMP%\karatala-drumgizmo"
set "FFMPEG=ffmpeg"
```

`map-<kit>.bat` is the main human review/correction surface. The generator should create it with all source instruments, articulations, parameters, and planned mic groups enumerated near the top. Existing map files should not be overwritten unless an explicit overwrite option is added later.

Use active `mkdir`, not commented `mkdir`. Prefer `if not exist` guards for folders.

Before each sample group, emit a readable `REM` block documenting:

- source XML path
- DrumGizmo sample name
- power, velocity, and round-robin letter
- source file and source channel count
- original source channels in file order
- keep/drop decision for each channel
- final Karatala output path

### Editable Kit Mapping Example

For an `aasimonster` kit, `map-aasimonster.bat` should look like this style:

```bat
REM Auto-generated review map for aasimonster.
REM Edit values here, then run convert-aasimonster.bat.

REM ######### source instrument: china #########
REM Planned slot: china -> china-X1\edge
set "__INS_CHINA=china-X1"
set "__ART_CHINA=edge"
set "__PRM_CHINA="

REM Microphone groups for china.
REM Amb L-20 + Amb R-20 -> room far stereo
set "__KEEP_CHINA_AMB=1"
set "__MIC_CHINA_AMB=rfs"

REM OH L-20 + OH R-20 -> overhead normal stereo
set "__KEEP_CHINA_OH=1"
set "__MIC_CHINA_OH=ons"

REM Hihat-20, Kick L-20, Kick R-20, Ride-20, Snare*, Tom* are bleed for china.
REM They are recorded in audit files and intentionally not emitted to output filenames.

REM ######### source instrument: hihat-closed #########
REM Planned slot: hihat-closed -> hihat-X1\bow[close]-pdl_opn_cls078
set "__INS_HIHAT_CLOSED=hihat-X1"
set "__ART_HIHAT_CLOSED=bow[close]"
set "__PRM_HIHAT_CLOSED=-pdl_opn_cls078"

REM Hihat-15 -> direct mono
set "__KEEP_HIHAT_CLOSED_DIRECT=1"
set "__MIC_HIHAT_CLOSED_DIRECT=dtm"

REM Amb L-15 + Amb R-15 -> room far stereo
set "__KEEP_HIHAT_CLOSED_AMB=1"
set "__MIC_HIHAT_CLOSED_AMB=rfs"

REM OH L-15 + OH R-15 -> overhead normal stereo
set "__KEEP_HIHAT_CLOSED_OH=1"
set "__MIC_HIHAT_CLOSED_OH=ons"
```

The generated `convert-aasimonster.bat` should use only those variables, so corrections in `map-aasimonster.bat` affect every command consistently:

```bat
REM XML: %SRC_ROOT%\aasimonster\china\china.xml
REM sample: 1-china power=... velocity=034 rr=a
REM channels:
REM   Amb L-20 + Amb R-20 -> %__MIC_CHINA_AMB% keep=%__KEEP_CHINA_AMB%
REM   OH L-20 + OH R-20   -> %__MIC_CHINA_OH% keep=%__KEEP_CHINA_OH%

if not exist "%OUT_ROOT%\aasimonster\%__INS_CHINA%\%__ART_CHINA%%__PRM_CHINA%" mkdir "%OUT_ROOT%\aasimonster\%__INS_CHINA%\%__ART_CHINA%%__PRM_CHINA%"

if "%__KEEP_CHINA_AMB%"=="1" "%FFMPEG%" -y -i "%SRC_ROOT%\aasimonster\china\samples\1-china-Amb L-20.wav" -i "%SRC_ROOT%\aasimonster\china\samples\1-china-Amb R-20.wav" -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" -c:a flac "%OUT_ROOT%\aasimonster\%__INS_CHINA%\%__ART_CHINA%%__PRM_CHINA%\%__INS_CHINA%-%__ART_CHINA%-034a-%__MIC_CHINA_AMB%.flac"

if "%__KEEP_CHINA_OH%"=="1" "%FFMPEG%" -y -i "%SRC_ROOT%\aasimonster\china\samples\1-china-OH L-20.wav" -i "%SRC_ROOT%\aasimonster\china\samples\1-china-OH R-20.wav" -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" -c:a flac "%OUT_ROOT%\aasimonster\%__INS_CHINA%\%__ART_CHINA%%__PRM_CHINA%\%__INS_CHINA%-%__ART_CHINA%-034a-%__MIC_CHINA_OH%.flac"

REM XML: %SRC_ROOT%\aasimonster\hihat-closed\hihat-closed.xml
REM sample: 1-hihat-closed power=... velocity=078 rr=a
REM folder includes %__PRM_HIHAT_CLOSED%; filename keeps only articulation name so KitLibModel parses velocity correctly.

if not exist "%OUT_ROOT%\aasimonster\%__INS_HIHAT_CLOSED%\%__ART_HIHAT_CLOSED%%__PRM_HIHAT_CLOSED%" mkdir "%OUT_ROOT%\aasimonster\%__INS_HIHAT_CLOSED%\%__ART_HIHAT_CLOSED%%__PRM_HIHAT_CLOSED%"

if "%__KEEP_HIHAT_CLOSED_DIRECT%"=="1" "%FFMPEG%" -y -i "%SRC_ROOT%\aasimonster\hihat-closed\samples\1-hihat-closed-Hihat-15.wav" -c:a flac "%OUT_ROOT%\aasimonster\%__INS_HIHAT_CLOSED%\%__ART_HIHAT_CLOSED%%__PRM_HIHAT_CLOSED%\%__INS_HIHAT_CLOSED%-%__ART_HIHAT_CLOSED%-078a-%__MIC_HIHAT_CLOSED_DIRECT%.flac"
```

The map variables intentionally separate folder parameters from filename articulations:

- Folder: `%__ART_HIHAT_CLOSED%%__PRM_HIHAT_CLOSED%` -> `bow[close]-pdl_opn_cls078`.
- Filename: `%__INS_HIHAT_CLOSED%-%__ART_HIHAT_CLOSED%-078a-...` -> parseable by `KitLibModel`.

For multichannel WAV kits, commands should use source channel extraction instead of separate mono inputs, while still using the same editable map variables.

### Audio Extraction

Replace current `copy` behavior with ffmpeg extraction to FLAC:

- Stereo pairs become one stereo FLAC mic file, for example `rfs` or `ons`.
- Useful mono channels become mono FLAC mic files, for example `dim`, `dom`, `dtm`, `dbm`, or `rcm`.
- Dropped bleed channels are recorded in audit artifacts but are never represented as `x` in final filenames.
- No final Karatala filename may contain `x`.

For `aasimonster`, where sources are mostly separate mono files, the generator should either pair L/R mono files into stereo FLAC outputs or emit single useful mono outputs. For multichannel kits such as `CrocellKit`, `artstar`, and `MuldjordKit3`, extract selected channels from the multichannel WAV source.

### Filename and Mapping Rules

Fix filename/folder generation:

- Hi-hat expression codes stay in the articulation folder only, not duplicated into the sample filename velocity segment.
- All hi-hat openness variants for one physical hat should resolve to the same instrument slot unless source naming clearly indicates a different physical hat.
- Unknown instrument types are quarantined unless mapped explicitly in a small mapping table.
- Duplicate target filenames are quarantined, not emitted into the runnable batch.

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
- `QuarantineIssue`
- `KitReport`

Build the plan in memory first, then validate before writing any generated artifacts:

- source exists
- generated filename matches Karatala parser rules
- no invalid mic tokens
- no duplicate output paths
- no round-robin letter overflow in clean output

Quarantine policy:

- Clean rows go to `.bat`.
- Ambiguous rows go to quarantine with reason and enough source context to edit manually.
- A kit may still produce a runnable partial `.bat` when only some rows are quarantined.

Because `ffmpeg` is not currently on `PATH`, generated batches must rely on the editable `set FFMPEG=ffmpeg` line. The summary should warn when local preflight cannot find ffmpeg.

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
- no duplicate destinations in clean batch

Compare against working `assets/sample_library` naming rules:

- kit/instrument/articulation folder shape
- filename segment order
- mic-code validity

Run one small batch manually only after review, then scan the generated output with Karatala's library scanner or the focused `Tests` target.

## Assumptions

- The converter is a one-off migration helper, not a polished long-term tool.
- Generated audio target is lossless FLAC.
- The Python script produces audit artifacts and `.bat` files but does not execute ffmpeg itself.
- Quarantine is preferred over failing a whole kit when only some rows are ambiguous.
- Any implementation touching `T:/source/repos/karatala/scripts/convert-drumgizmo2karatala.py` may need write approval if current sandbox writable roots remain limited to `source`.
