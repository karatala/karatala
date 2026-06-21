# DrumGizmo Migration Summary: tchackpoum_drumgizmo_13

- Instruments: 45
- Samples: 813
- Channel decisions: 4271
- Output plans: 3371
- Ready outputs enabled by default: 1138
- Skipped optional outputs in convert script: 179
- Quarantined outputs: 2054
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-tchackpoum_drumgizmo_13.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_tchackpoum_drumgizmo_13.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_tchackpoum_drumgizmo_13.py`: rewrite review CSV, dry-run, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
