from __future__ import annotations

import csv
import hashlib
import tempfile
import unittest
from pathlib import Path

from apply_kit_overrides import plan_kit
from kit_overrides_format import load_kit_overrides, parse_sample_name, replace_manual_gain, replace_playback_offsets, sample_name_text


class KitOverrideFormatTest(unittest.TestCase):
    def test_generated_yaml_shape_and_decimal_tokens(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            override_path = Path(temporary_directory) / "kit-overrides.yml"
            override_path.write_text(
                'version: 1\n\nsamples:\n  "snare-X1/center/snare-X1-center-043a-dtm_ons.flac":\n    manualGainDb: -1.5\n',
                encoding="utf-8",
            )
            overrides = load_kit_overrides(override_path)
            self.assertEqual(overrides["samples"]["snare-X1/center/snare-X1-center-043a-dtm_ons.flac"]["manualGainDb"], -1.5)

            sample_name = parse_sample_name(Path("snare-X1-center-043a-dtm_ons-vn4-on2.flac"))
            sample_name = replace_manual_gain(sample_name, -1.5)
            sample_name = replace_playback_offsets(sample_name, 0.25, {"dtm": -0.5})
            self.assertEqual(sample_name_text(sample_name), "snare-X1-center-043a-dtm_ons-vn1.5-on0.5_dtm-op0.25_ons.flac")

    def test_dry_run_plans_changes_without_touching_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            kit_root = Path(temporary_directory) / "KitA"
            articulation_folder = kit_root / "snare-X1-L10" / "center-L5"
            articulation_folder.mkdir(parents=True)
            sample_path = articulation_folder / "snare-X1-center-043a-dtm_ons-vn4.flac"
            sample_bytes = b"not real audio, sufficient for a dry-run identity fixture"
            sample_path.write_bytes(sample_bytes)
            content_hash = hashlib.blake2b(sample_bytes, digest_size=16).hexdigest()

            metadata_path = kit_root / "metadata.csv"
            with metadata_path.open("w", encoding="utf-8", newline="") as metadata_file:
                writer = csv.DictWriter(metadata_file, fieldnames=("relative_path", "mic_code", "content_hash", "file_size_bytes", "true_peak_db"))
                writer.writeheader()
                writer.writerow(
                    {
                        "relative_path": sample_path.relative_to(kit_root).as_posix(),
                        "mic_code": "dtm",
                        "content_hash": content_hash,
                        "file_size_bytes": len(sample_bytes),
                        "true_peak_db": "-3",
                    }
                )

            (kit_root / "kit-overrides.yml").write_text(
                f'''version: 1

instruments:
  "snare-X1-L10":
    balance: C0
    articulations:
      "center":
        balance: R8
        velocityUpperBounds:
          "043": 50

samples:
  "{sample_path.relative_to(kit_root).as_posix()}":
    contentHash: "{content_hash}"
    manualGainDb: -1.5
''',
                encoding="utf-8",
            )

            plan = plan_kit(kit_root)
            self.assertTrue(plan["readyForReviewedApply"])
            self.assertEqual(len(plan["sampleChanges"]), 1)
            self.assertIn("-050a-", plan["sampleChanges"][0]["destination"])
            self.assertTrue(sample_path.is_file())
            self.assertFalse((kit_root / plan["sampleChanges"][0]["destination"]).exists())


if __name__ == "__main__":
    unittest.main()
