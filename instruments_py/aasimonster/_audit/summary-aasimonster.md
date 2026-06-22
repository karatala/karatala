# DrumGizmo Migration Summary: aasimonster

- Instruments: 17
- Samples: 375
- Channel decisions: 5679
- Output plans: 4959
- Ready outputs enabled by default: 1143
- Skipped optional outputs in convert script: 3557
- Quarantined outputs: 259
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-aasimonster.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_aasimonster.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_aasimonster.py`: rewrite review CSV, dry-run, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
