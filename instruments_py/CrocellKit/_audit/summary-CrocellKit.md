# DrumGizmo Migration Summary: CrocellKit

- Instruments: 27
- Samples: 758
- Channel decisions: 11370
- Output plans: 9854
- Ready outputs enabled by default: 2634
- Skipped optional outputs in convert script: 6414
- Quarantined outputs: 806
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-CrocellKit.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_CrocellKit.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_CrocellKit.py`: rewrite review CSV, dry-run, print batch text, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands, or with `--bat` to write a clean batch file.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
