"""Fixtures indépendantes synthetic_fixture ; aucune mesure ni donnée Line 6."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
import contextlib
import io
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


analysis = load("echo_analysis", "tools/analysis/echo_tools.py")
source_import = load("echo_import", "tools/import/import_sources.py")


class SyntheticFixtureToolsTests(unittest.TestCase):
    """Tous les signaux sont construits ici à partir de valeurs analytiques connues."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="echo_synthetic_fixture_")
        self.path = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def args(self, text):
        return analysis.parser().parse_args(text + ([] if text[0] == "generate_stimuli" else ["--synthetic-fixture"]))

    def test_pcm24_roundtrip_preserves_channels_and_absolute_gain(self):
        values = np.array([[0, .25], [-.5, .125], [.75, -.25]])
        path = self.path / "synthetic_fixture.wav"
        analysis.write_wav(path, values, 48000)
        decoded, fs, fmt = analysis.read_wav(path)
        self.assertEqual((fs, fmt["bits"], fmt["channels"]), (48000, 24, 2))
        np.testing.assert_allclose(decoded, values, atol=1 / 8388608)

    def test_float32_wav_and_non_finite_rejection(self):
        values = np.array([.25, np.nan], dtype="<f4").tobytes()
        fmt = struct.pack("<HHIIHH", 3, 1, 48000, 192000, 4, 32)
        body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(values)) + values
        path = self.path / "synthetic_fixture_float.wav"
        path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        decoded, _, _ = analysis.read_wav(path)
        self.assertEqual(decoded[0, 0], .25)
        with self.assertRaises(ValueError):
            analysis.assert_finite(decoded)
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            analysis.read_wav(path)

    def test_zero_bit_depth_and_zero_alignment_are_rejected(self):
        fmt = struct.pack("<HHIIHH", 1, 1, 48000, 0, 0, 0)
        body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", 0)
        path = self.path / "synthetic_fixture_invalid.wav"
        path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        with self.assertRaisesRegex(ValueError, "incohérent"):
            analysis.read_wav(path)

    def test_cli_refuses_parent_overwrite_without_altering_capture(self):
        path = self.path / "synthetic_fixture.wav"
        analysis.write_wav(path, np.array([.25, -.125]), 48000)
        before = path.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            code = analysis.main(["compare_render", str(path), str(path), "--output", str(path), "--synthetic-fixture"])
        self.assertEqual(code, 2)
        self.assertEqual(path.read_bytes(), before)
        manifest = self.path / "parent.json"
        manifest.write_text('{"status":"synthetic_fixture"}', encoding="utf-8")
        before = manifest.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            code = analysis.main(["validate_capture", str(path), "--manifest", str(manifest), "--output", str(manifest)])
        self.assertEqual(code, 2)
        self.assertEqual(manifest.read_bytes(), before)

    def test_cli_rejects_generated_output_collision(self):
        output = self.path / "synthetic_fixture.json"
        with contextlib.redirect_stderr(io.StringIO()):
            code = analysis.main(["generate_stimuli", "--output", str(output)])
        self.assertEqual(code, 2)
        self.assertFalse(output.exists())

    def test_cli_rejects_non_finite_parameter_before_output(self):
        output = self.path / "synthetic_fixture.wav"
        with contextlib.redirect_stderr(io.StringIO()):
            code = analysis.main(["generate_stimuli", "--frequency", "nan", "--output", str(output)])
        self.assertEqual(code, 2)
        self.assertFalse(output.exists())

    def test_import_rejects_traversal_and_case_collision_before_write(self):
        for names in [("valid.txt", "../escape.txt"), ("safe.txt", "SAFE.txt"), ("C:/escape",)]:
            archive = self.path / "synthetic_fixture.zip"
            with zipfile.ZipFile(archive, "w") as z:
                for name in names:
                    z.writestr(name, b"synthetic_fixture")
            target = self.path / "output"
            with self.assertRaises(ValueError):
                source_import.extract_safe(archive, target)
            self.assertFalse(target.exists())

    def test_generation_metadata_and_no_channel_crosstalk(self):
        path = self.path / "synthetic_fixture_stereo.wav"
        args = self.args(["generate_stimuli", "--kind", "impulse", "--duration", "1", "--channels", "2",
                          "--active-channel", "1", "--output", str(path)])
        meta = analysis.generate_stimuli(args)
        x, fs, _ = analysis.read_wav(path)
        self.assertEqual(meta["status"], "synthetic_fixture")
        self.assertEqual(np.count_nonzero(x[:, 0]), 0)
        self.assertEqual(np.flatnonzero(x[:, 1])[0], fs // 4)
        self.assertAlmostEqual(float(x[:, 1].max()), 10 ** (-18 / 20), places=6)

    def test_first_pass_recovers_independent_known_fir_and_latency(self):
        # Independent analytical convolution, no echo engine is used as oracle.
        fs = 8000
        x = np.zeros(1024)
        x[0] = .25
        expected = np.array([.6, .2, -.1])
        y = np.zeros(2048)
        y[137:140] = x[0] * expected
        excitation, capture = self.path / "x.wav", self.path / "y.wav"
        analysis.write_wav(excitation, x, fs)
        analysis.write_wav(capture, y, fs)
        output = self.path / "first.json"
        args = self.args(["estimate_first_pass", str(excitation), str(capture), "--start", "0", "--end", ".256",
                          "--min-hz", "0", "--max-hz", "4000", "--regularization", "1e-12", "--output", str(output)])
        result = analysis.estimate_first_pass(args)
        self.assertEqual(result["status"], "synthetic_fixture")
        with np.load(output.with_suffix(".npz")) as data:
            self.assertEqual(int(np.argmax(abs(data["response"]))), 137)
            np.testing.assert_allclose(data["response"][137:140], expected, atol=5e-7)
        self.assertEqual(result["removed_delay_samples"], 0)
        self.assertEqual(result["ir_role"], "first_pass")

    def test_loop_estimate_retains_nominal_spacing_and_gain(self):
        fs = 8000
        y = np.zeros(fs)
        for index, gain in [(800, .2), (2400, .1), (4000, .05), (5600, .025)]:
            y[index] = gain
        capture = self.path / "synthetic_fixture_echoes.wav"
        analysis.write_wav(capture, y, fs)
        args = self.args(["estimate_loop", str(capture), "--echo-starts", ".1,.3,.5,.7", "--window", ".1",
                          "--regularization", "1e-9", "--topology", "single_path", "--output", str(self.path / "loop.json")])
        result = analysis.estimate_loop(args)
        np.testing.assert_allclose(result["scalar_pair_gains"], [.5, .5, .5], atol=3e-6)
        np.testing.assert_allclose(result["removed_nominal_intervals_seconds"], [.2, .2, .2], atol=1e-12)
        args.topology = "coupled"
        with self.assertRaises(ValueError):
            analysis.estimate_loop(args)

    def test_compare_keeps_gain_error_and_does_not_free_align(self):
        x = np.array([.25, -.125, .0625, 0])
        a, b = self.path / "a.wav", self.path / "b.wav"
        analysis.write_wav(a, x, 48000)
        analysis.write_wav(b, x * .5, 48000)
        args = self.args(["compare_render", str(a), str(b), "--output", str(self.path / "compare.json")])
        result = analysis.compare_render(args)
        self.assertFalse(result["exact_match"])
        self.assertAlmostEqual(result["diagnostic_linear_gain"][0], .5)
        self.assertGreater(result["rmse_absolute"][0], 0)
        self.assertIsNone(result["gain_normalization"])

    def test_calibrate_known_delay_and_gain(self):
        rng = np.random.default_rng(127)
        x = rng.uniform(-.2, .2, 2048)
        y = np.zeros(4096)
        y[127:127 + len(x)] = -.75 * x
        a, b = self.path / "x.wav", self.path / "y.wav"
        analysis.write_wav(a, x, 8000)
        analysis.write_wav(b, y, 8000)
        args = self.args(["calibrate", str(a), str(b), "--output", str(self.path / "cal.json")])
        result = analysis.calibrate(args)
        self.assertEqual(result["delay_samples"], 127)
        self.assertAlmostEqual(result["linear_gain"], -.75, places=6)

    def test_validate_manifest_hash_mismatch_and_silent_channel(self):
        capture = self.path / "synthetic_fixture.wav"
        analysis.write_wav(capture, np.zeros((1000, 2)), 48000)
        manifest = self.path / "capture.json"
        manifest.write_text(json.dumps({"capture": {"sha256": "incorrect"}, "status": "synthetic_fixture"}), encoding="utf-8")
        args = self.args(["validate_capture", str(capture), "--manifest", str(manifest), "--output", str(self.path / "validation.json")])
        result = analysis.validate_capture(args)
        self.assertFalse(result["valid"])
        self.assertIn("capture_sha256_mismatch", result["issues"])
        self.assertIn("silent_channel", result["issues"])
        self.assertIn("missing_metadata:sample_rates", result["issues"])


if __name__ == "__main__":
    unittest.main()
