# Local assistance in the Qt client

The Qt client has a replaceable Python process backend using ONNX Runtime on CPU.
Select linked optical evidence in Mission Control, open local assistance, select
an inference Python and model contract, set the minimum model score and analyze
the preview. Select proposals to add as **Unreviewed** observations. The ordinary
observation review records a later analyst decision and rationale separately.
Nothing downloads a model automatically. No aerial detector accuracy,
construction progress, damage or calibrated probability is claimed.

The source is a bounded PNG display preview, not a scientific raster. A preview
does not add detail to a sensor's original pixels. Results retain the source,
original model, effective runtime graph and contract SHA256 hashes, dimensions,
preprocessing, boxes in preview pixels, normalized image boxes, geographic boxes,
threshold, backend/runtime/provider and elapsed time. The exact model contract and
analyzed preview bytes are retained with the result for later review; their hashes
match the recorded provenance. Acquisition time is copied
only when known; authored time uses the existing mission Foundation date epoch.
Source geographic accuracy still applies to every proposal. Dateline-crossing,
transparent/no-data, EXIF-oriented and animated previews are rejected explicitly.

## Optional environment

Use an isolated Python 3.12 or 3.13 environment; inference is not a map startup
dependency. The tested host used Python 3.12.14. The pinned ONNX Runtime 1.23.2
does not offer a Python 3.14 wheel, so Omarchy's system `python3` (3.14) is not
suitable for these pinned requirements. Choose an installed 3.12 interpreter:

```sh
/absolute/path/to/python3.12 -m venv /absolute/path/to/inference-env
/absolute/path/to/inference-env/bin/python -m pip install \
  -r native/desktop/Inference/requirements.txt
```

Select that Python in local assistance. Dependencies are pinned for the tested
Linux CPU path. A missing dependency produces a failed job with diagnostics and
does not alter the source evidence. The app invokes executables with argument
arrays through the same owned worker host as scientific processing. Cancellation
stops the owned process session and its descendants. Incomplete results are never
imported; completed job provenance stays in the profile's `inference/jobs/`.
The main window prevents quitting while this job runs, allowing cancellation.

## Generic static detector contract

A JSON contract and one regular `.onnx` file must live in the same directory.
`model` is a filename, not an external URL, directory or symbolic link. Example:

```json
{
  "version": 1,
  "backend": "onnx-cpu",
  "model": "detector.onnx",
  "modelSHA256": "replace with its 64-character lowercase SHA256",
  "input": "image",
  "boxes": "boxes",
  "scores": "scores",
  "classes": "classes",
  "preprocessing": "rgb-stretch-bilinear-0to1",
  "boxFormat": "xyxy-normalized-upper-left",
  "labels": ["declared class zero", "declared class one"]
}
```

The input is float32 `[1,3,H,W]` RGB, with fixed height/width at most 1024. The
whole preview is stretched with bilinear resizing, arranged NCHW and divided by
255. There is no crop, letterbox, arbitrary channel order, mean subtraction,
tiling or implicit model-specific preprocessing. Output is already postprocessed:
float32 boxes `[N,4]`, float32 scores `[N]` and int64 zero-based class IDs `[N]`.
`N` is fixed, between 1 and 2000. Boxes are normalized upper-left-origin xyxy;
scores must be finite within `[0,1]`; labels must be declared (1–512). All returned
rows are checked, including those below threshold. At most 200 proposals are
retained, sorted by score. Thresholding does not convert a score to probability.

Models are limited to 64 MiB, 1000 nodes, 2000 dense initializers, ONNX IR ≤10 and
standard opsets 7–22. Only a bounded allowlist of ordinary numeric operators is
accepted. Every intermediate must have a statically inferred dense numeric shape;
one tensor is limited to 16 million elements, aggregate inferred values to 64
million. External tensor files, sparse tensors, custom operators/domains, local
functions, control flow, dynamic shapes, unsupported types, classifiers, raw YOLO
tensors and rotated boxes are rejected. This deliberately limited adapter does
not promise compatibility with arbitrary ONNX exports. ONNX Runtime's official
[Python API](https://onnxruntime.ai/docs/api/python/api_summary.html) describes
explicit execution providers; this worker requests only `CPUExecutionProvider`
and disables fallback. Shape admission uses the official
[ONNX checker and shape inference](https://onnx.ai/onnx/api/shape_inference.html).

The worker also sets two numerical threads, sequential execution, disabled CPU
arena/graph optimization, 120 CPU seconds, a 180-second wall deadline and a 4 GiB
virtual-address ceiling on Linux. These bound individual jobs; they are not a
security sandbox. Use trusted model files. No model code or pickle is imported.

## Pinned Tiny YOLOv2 VOC adapter

The separate `onnx-tinyyolov2-voc-cpu` contract supports exactly the ONNX Model Zoo
Tiny YOLOv2 opset 8 model:

- Repository: `onnxmodelzoo/tinyyolov2-8`.
- Revision: `869707e16e57006f97d98af54cfdc8a1d388ae61`.
- File: `tinyyolov2-8.onnx`, 63,480,982 bytes.
- SHA256: `583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae`.
- Contract: `native/desktop/Inference/tinyyolov2-voc.contract.json`.

For an explicit development download using the Hugging Face CLI:

```sh
hf download onnxmodelzoo/tinyyolov2-8 tinyyolov2-8.onnx README.md \
  --revision 869707e16e57006f97d98af54cfdc8a1d388ae61 \
  --local-dir /absolute/path/to/local-detector
```

Copy the contract beside the downloaded model, then choose that contract. The
model is an ordinary Pascal VOC 20-class everyday-image detector. Its vocabulary
has no generic building, construction or damage class. It is a compatibility
example, not an aerial recommendation. The upstream
[model documentation](https://github.com/onnx/models/tree/main/validated/vision/object_detection_segmentation/tiny-yolov2)
describes its fixed 416×416 input and 125×13×13 output. The legacy graph includes
division by 255, so this adapter supplies RGB values 0–255. It freezes its symbolic
batch to one and removes overridable initializer inputs without altering the
original file. The runtime graph hash and normalization are recorded separately.
The reviewed exact SHA256 prevents other raw models from entering this adapter.

The decoder considers 845 cells/anchors, uses the model's VOC anchor sizes,
sigmoid centers/objectness, softmax class scores, multiplies objectness by class
score, clips boxes to image edges and applies class-aware IoU 0.5 suppression.
It retains at most 200 axis-aligned proposals. Other raw YOLO and OBB exports
remain unsupported. The original
[conversion/decoder project](https://github.com/hollance/YOLO-CoreML-MPSNNGraph)
and upstream ONNX documentation state MIT; the migrated
[Hugging Face card](https://huggingface.co/onnxmodelzoo/tinyyolov2-8) tags Apache-2.0.
This source discrepancy is recorded rather than silently relicensing weights.
No weights are bundled with Helios. Review the original model/conversion/data
licenses before any redistribution.

## Evidence and limits

On 2026-10-15, the isolated Linux Python 3.12 environment ran ONNX Runtime 1.23.2
with only `CPUExecutionProvider`. The pinned trained Tiny YOLOv2 model executed
against the previously retrieved public Sentinel-2 product
`S2B_T32TNR_20261012T102038_L2A` (40×56 preview, bbox
9.18,45.455,9.185,45.46). At threshold 0.5 it returned **zero proposals**. The initial
run measured 0.119 seconds for session creation/inference/postprocessing. The
current retained result (`trained-product-current/result.json`) measured 0.201
seconds and 454,590,464 bytes peak worker RSS, and includes the effective runtime
graph hash and retained source preview. That small scene's integration result establishes
local CPU execution, not useful detection accuracy or a performance benchmark.
Reports and worker diagnostics are retained under
`output/qa/linux-migration-20261015/local-inference/` (ignored development output).

Fifteen Python tests exercise actual input-dependent generated ONNX execution,
preprocessing, image/pixel/geographic transforms, empty results, thresholds,
model/source hashes, malformed classes/scores/boxes, dynamic/large/custom/external
tensors, no-data/oversized imagery, atomic output and no-overwrite behavior.
Generated fixtures are explicitly named and contain no trained detector.
The Qt core test also exercises the owned worker boundary, frozen references,
proposal admission and cancellation when the optional environment is supplied:

```sh
HELIOS_INFERENCE_PYTHON=/absolute/path/to/inference-env/bin/python \
  ./output/desktop/build/native/desktop/HeliosInferenceTests
/absolute/path/to/inference-env/bin/python -m unittest discover \
  -s native/desktop/Tests -p test_inference.py -v
```

The separate owned C++ ASAN/UBSAN core run passed all four checks, including
immediate startup cancellation, with leak detection enabled after the processing
host lifecycle fix. The actual Wayland native assistance window passed three
checks for generated CPU analysis, source/mission context invalidation, pending
acceptance, failure/retry and success. The analyzed source preview is retained,
hash checked and decoded off the UI thread; a red box shows the selected model
proposal directly on that preview. An acceptance remains pending until the main
application verifies the current source hash and atomically stores the unreviewed
observation. A source or mission switch cannot redirect an old proposal.

NVIDIA acceleration has **not** been tested on the GTX 1070. This implementation
offers CPU only and neither changes drivers nor claims CUDA/kernel compatibility.
The Mac Python/Qt path and Metal map interaction still require a real Mac check.
No Apple Vision/Core ML adapter is used; the inference boundary is replaceable.
