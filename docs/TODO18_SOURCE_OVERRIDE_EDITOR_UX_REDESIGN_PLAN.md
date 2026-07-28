# TODO18: Source Override Editor UX Redesign Plan

Date: 2026-07-28  
Status: Implemented in code. Build, tests, and visual QA are pending by request.

## ✅ Goal

Make source overrides easy to understand and adjust live without searching through a long sample dropdown.

## ✅ Implemented layout

- Use normal UI font sizes.
- Show a short scope hint: `Changes this instrument on every pad that uses it.`
- Put balance controls on one line:
  `BALANCE  Instrument [kick-22] [C0] [×]  Articulation [center] [C0] [×]`
- Show `VELOCITY RANGES` with `Even` and `Reset` actions.
- Show each compact range as two lines: normal `052` and smaller `28-58`.
- `Even` divides MIDI `1-127` between all named layers as evenly as integer ranges allow, saves every boundary in one operation, and replays the last preview once.
- Remove the separate boundary editor and the verbose effective-range line.
- Put the lower editor into two columns:
  - left: `SAMPLE OVERRIDES`, the RR by velocity matrix, and `MIC` buttons directly below it;
  - right: a scrollable list headed `SELECTED 6 SAMPLES`.
- Put the three batch fields below the sample area:
  - `File gain correction, dB`
  - `File start offset, ms`
  - `Mic offset, ms`
- Use the `×` symbol for reset actions.
- Remove the bottom metadata sentence.

## ✅ Selection model

- Matrix cells select logical hits for batch editing.
- Mic buttons select which files from those hits are included in the batch.
- The selected-file list is the existing combination of selected hits and selected mics.
- The last previewed matrix cell is also the active hit used by mic preview.
- Batch fields show a value when all selected files match, or `Mixed` when they do not.
- Each batch change is saved as one complete operation.

## ✅ Preview rules

- Hovering a matrix cell for a short delay previews that hit with all its microphones.
- Clicking a matrix cell changes its batch selection and immediately previews that hit with all its microphones.
- Hovering a mic button previews only that mic for the active hit.
- Clicking a mic button changes its batch selection and immediately previews only that mic for the active hit.
- A click cancels the queued hover for the same target. That target cannot schedule another hover until the pointer leaves it.
- Clicking inside a velocity-range block previews the velocity under the pointer using the current ranges.
- Dragging a range boundary is silent. Releasing it previews the velocity under the pointer once.
- Typing in a balance or override field is silent. Enter, focus loss, or `×` saves the value and replays the last preview target once.
- Automatic replay uses the same target as the last manual preview: the full hit or one solo mic.
- If no hit has been previewed yet, the value is saved without automatic audio.

## ✅ Sample measurements

`Onset`, `Intensity`, and `Peak` are diagnostic data, not required for selecting or editing samples:

- `Onset` helps explain or tune a file start offset.
- `Intensity` helps compare measured loudness between samples.
- `Peak` shows remaining level headroom before gain correction.

To keep the main form simple, the selected-sample list should show filenames only. These measurements should remain available in a row tooltip or another small details view.

## Completion checks

- ✅ The dropdown, boundary editor, verbose readout, and bottom metadata row were removed.
- ✅ The matrix, mic filters, active-hit outline, selected-file list, and batch fields were added.
- ✅ Balance and sample overrides use the source override metadata. Each batch field saves all selected targets through one complete operation.
- ✅ Matrix and velocity previews use a complete logical hit. Mic hover and click use exact solo preview.
- ✅ Matrix and mic clicks consume the queued hover for the same target, so one click produces one preview.
- ✅ All five value editors use the standard 20 px control and 14 px text style with vertically centred text.
- ✅ Velocity blocks use a compact two-line layout with a smaller range line.
- ✅ `Even` distributes and saves every range boundary as one complete operation; `Reset` restores filename values.
- Pending: build and run the C++ tests when requested.
- Pending: open a large real kit and verify scrolling, text size, hover timing, layout, and audio behavior.
