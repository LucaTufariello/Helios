import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
import onnx
from PIL import Image
from inference_fixture import make_fixture

WORKER = Path(__file__).resolve().parents[1] / "Inference" / "infer.py"
spec = importlib.util.spec_from_file_location("godeye_infer", WORKER)
infer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(infer)


class InferenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.contract, self.request, self.request_path = make_fixture(self.root)
        self.value = self.contract | self.request
        self.value["model"] = str(self.root / self.contract["model"])

    def tearDown(self):
        self.temporary.cleanup()

    def detect(self):
        with patch.object(infer, "event"):
            return infer.detect(self.value, hashlib.sha256((self.root / "GENERATED_QA.contract.json").read_bytes()).hexdigest())

    def test_real_cpu_execution_preprocessing_transforms_and_hashes(self):
        result = self.detect()
        self.assertEqual(result["providers"], ["CPUExecutionProvider"])
        self.assertEqual(result["inputShape"], [1, 3, 8, 8])
        self.assertEqual(result["sourcePixels"], [40, 20])
        self.assertEqual(result["sourceSHA256"], hashlib.sha256((self.root / "GENERATED_QA.png").read_bytes()).hexdigest())
        self.assertEqual(result["proposalCount"], 1)
        p = result["proposals"][0]
        self.assertAlmostEqual(p["modelScore"], 1)
        np.testing.assert_allclose(p["bounds"], [10.2, 21.2, 11.2, 23.2], atol=1e-6)
        np.testing.assert_allclose(p["imagePixelBox"], [4, 4, 24, 14], atol=1e-5)
        self.assertAlmostEqual(p["coordinate"]["latitude"], 22.2)
        self.assertEqual(p["state"], "Unreviewed")
        self.assertFalse(result["accuracyValidated"])
        self.assertGreater(result["peakRSSBytes"], 0)

    def test_empty_results_are_input_dependent(self):
        Image.new("RGB", (40, 20), "black").save(self.root / "GENERATED_QA.png")
        self.assertEqual(self.detect()["proposals"], [])

    def test_threshold_endpoints(self):
        self.value["threshold"] = 1
        self.assertEqual(self.detect()["proposalCount"], 1)
        self.value["threshold"] = 1.01
        with self.assertRaises(infer.AdmissionError): self.detect()

    def test_reject_hash_mismatch(self):
        self.value["modelSHA256"] = "0" * 64
        with self.assertRaisesRegex(infer.AdmissionError, "SHA256"): self.detect()

    def test_reject_model_cap_and_symlink(self):
        target = self.root / "too-large.onnx"
        with target.open("wb") as handle: handle.truncate(infer.MAX_MODEL+1)
        self.value["model"] = str(target)
        with self.assertRaises(infer.AdmissionError): self.detect()
        link = self.root / "linked.onnx"
        link.symlink_to(self.root / self.contract["model"])
        self.value["model"] = str(link)
        with self.assertRaises(infer.AdmissionError): self.detect()

    def mutate_model(self, mutate):
        path = Path(self.value["model"])
        model = onnx.load(path)
        mutate(model)
        data = model.SerializeToString()
        path.write_bytes(data)
        self.value["modelSHA256"] = hashlib.sha256(data).hexdigest()

    def test_reject_dynamic_classifier_and_rotated(self):
        self.mutate_model(lambda m: setattr(m.graph.input[0].type.tensor_type.shape.dim[0], "dim_param", "batch"))
        with self.assertRaises(infer.AdmissionError): self.detect()
        self.value["boxFormat"] = "rotated-xywha"
        with self.assertRaises(infer.AdmissionError): self.detect()

    def test_reject_external_weights_and_custom_operator(self):
        def external(m):
            m.graph.initializer[0].data_location = onnx.TensorProto.EXTERNAL
            m.graph.initializer[0].external_data.add(key="location", value="/etc/passwd")
        self.mutate_model(external)
        with self.assertRaisesRegex(infer.AdmissionError, "External"): self.detect()

    def test_reject_custom_domain(self):
        self.mutate_model(lambda m: setattr(m.graph.node[0], "domain", "untrusted.library"))
        with self.assertRaises(infer.AdmissionError): self.detect()

    def test_reject_large_tensor(self):
        self.mutate_model(lambda m: setattr(m.graph.input[0].type.tensor_type.shape.dim[2], "dim_value", 100000000))
        with self.assertRaises((infer.AdmissionError, onnx.checker.ValidationError, onnx.shape_inference.InferenceError)): self.detect()

    def test_reject_unknown_class_and_nonfinite_score(self):
        self.contract, self.request, _ = make_fixture(self.root, label=2)
        self.value.update(self.contract)
        self.value["model"] = str(self.root / self.contract["model"])
        with self.assertRaisesRegex(infer.AdmissionError, "undeclared"): self.detect()
        self.contract, _, _ = make_fixture(self.root, score=float("nan"))
        self.value.update(self.contract)
        self.value["model"] = str(self.root / self.contract["model"])
        with self.assertRaisesRegex(infer.AdmissionError, "scores"): self.detect()

    def test_reject_invalid_box_even_below_threshold(self):
        self.contract, _, _ = make_fixture(self.root, score=0, box=[-.1, .2, .5, .7])
        self.value.update(self.contract)
        self.value["model"] = str(self.root / self.contract["model"])
        with self.assertRaisesRegex(infer.AdmissionError, "box"): self.detect()

    def test_reject_nodata_and_oversized_image(self):
        Image.new("RGBA", (40, 20), (255,255,255,0)).save(self.root / "GENERATED_QA.png")
        with self.assertRaisesRegex(infer.AdmissionError, "no-data"): self.detect()
        Image.new("RGB", (2049, 1)).save(self.root / "GENERATED_QA.png")
        with self.assertRaisesRegex(infer.AdmissionError, "2048"): self.detect()

    def test_reject_dateline_bounds(self):
        self.value["bounds"] = [179, 0, -179, 1]
        with self.assertRaises(infer.AdmissionError): self.detect()

    def test_cli_atomic_output_and_no_overwrite(self):
        output = self.root / "product"
        argv = [sys.executable, "-I", str(WORKER), "render", "--input", str(self.request_path), "--output", str(output)]
        first = subprocess.run(argv, capture_output=True, text=True, timeout=30)
        self.assertEqual(first.returncode, 0, first.stdout+first.stderr)
        lines = [json.loads(line) for line in first.stdout.splitlines()]
        self.assertEqual([x["state"] for x in lines], ["validating", "processing", "exporting", "completed"])
        original = (output / "result.json").read_bytes()
        result = json.loads(original)
        self.assertEqual(hashlib.sha256((output / "model-contract.json").read_bytes()).hexdigest(), result["contractSHA256"])
        self.assertEqual(hashlib.sha256((output / "preview.png").read_bytes()).hexdigest(), result["sourceSHA256"])
        self.assertFalse((output / "result.tmp").exists())
        second = subprocess.run(argv, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(second.returncode, 0)
        self.assertEqual((output / "result.json").read_bytes(), original)

    def test_pinned_tiny_decode_nms_and_empty(self):
        grid = np.full((1,125,13,13), -80., dtype=np.float32)
        boxes, scores, classes = infer.decode_tiny(grid, .5)
        self.assertEqual(len(boxes), 0)
        for anchor in (0,1):
            grid[0,anchor*25:anchor*25+4,6,6] = 0
            grid[0,anchor*25+4,6,6] = 10
            grid[0,anchor*25+5:anchor*25+25,6,6] = 0
            grid[0,anchor*25+5+14,6,6] = 10
        grid[0,27,6,6] = np.log(1.08/3.42)
        grid[0,28,6,6] = np.log(1.19/4.41)
        boxes, scores, classes = infer.decode_tiny(grid, .5)
        self.assertEqual(set(classes), {14})
        self.assertEqual(len(boxes), 1)  # Same class/box is suppressed.
        grid[0,25+5+14,6,6] = 0
        grid[0,25+5+6,6,6] = 10
        boxes, scores, classes = infer.decode_tiny(grid, .5)
        self.assertEqual(len(boxes), 2)  # Distinct classes survive overlap.
        for box in boxes: infer.geographic_box(box, [10,20,12,24])
        self.value["backend"] = "onnx-tinyyolov2-voc-cpu"
        with self.assertRaises(infer.AdmissionError): self.detect()


if __name__ == "__main__": unittest.main()
