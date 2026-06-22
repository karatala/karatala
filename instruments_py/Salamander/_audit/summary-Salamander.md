# DrumGizmo Migration Summary: Salamander

- Instruments: 39
- Samples: 525
- Channel decisions: 1050
- Output plans: 959
- Ready outputs enabled by default: 91
- Skipped optional outputs in convert script: 426
- Quarantined outputs: 442
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-Salamander.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_Salamander.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_Salamander.py`: rewrite review CSV, dry-run, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
