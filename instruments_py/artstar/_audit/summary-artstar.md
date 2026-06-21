# DrumGizmo Migration Summary: artstar

- Instruments: 21
- Samples: 470
- Channel decisions: 7050
- Output plans: 6110
- Ready outputs enabled by default: 1380
- Skipped optional outputs in convert script: 4618
- Quarantined outputs: 112
- Missing sources: 0
- ffmpeg on PATH: yes

## Review Files

- `review-artstar.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.
- `../map_artstar.py`: edit instrument/articulation/mic keep/code values here.
- `../convert_artstar.py`: rewrite review CSV, dry-run, or execute rows after map review.

## Warnings

- None

## Next Actions

1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.
2. Fix mapping mistakes or flip skipped mic groups in the map Python file.
3. Run the convert script with `--write-review` and inspect the refreshed CSV.
4. Run the convert script without `--execute` to inspect commands.
5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.
