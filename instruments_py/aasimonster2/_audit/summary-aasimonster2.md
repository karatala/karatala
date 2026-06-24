# DrumGizmo Migration Summary: aasimonster2

- Instruments: 24
- Samples: 536
- Channel decisions: 8576
- Output plans: 7504
- Ready outputs enabled by default: 1438
- Skipped optional outputs in convert script: 5774
- Quarantined outputs: 292
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-aasimonster2.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_aasimonster2.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_aasimonster2.py`: rewrite review CSV, dry-run, print batch text, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands, or with `--bat` to write a clean batch file.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
