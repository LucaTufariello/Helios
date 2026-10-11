# Choosing a local detector for GodEye

## Recommendation

For an **aerial object detection evaluation**, start with **YOLO11s-OBB**, rather
than a generic camera-image detector. The small model is a reasonable first
candidate to evaluate on Apple Silicon; GodEye has not benchmarked its latency,
memory or accuracy. The development Mac was verified as an Apple M2 with 8 GB RAM;
start with the small variant and measure peak memory alongside the running map.
This is a proposed next adapter, **not a model that the current
Choose detector button can run directly**.

The pretrained DOTA vocabulary includes aircraft, ships, storage tanks, bridges,
harbors, small/large vehicles and several sports/road features. It does **not**
include a generic building/warehouse class, construction progress or structural
damage. Those require suitable labeled data and a separately evaluated model.
See the publisher's [YOLO11 model documentation](https://docs.ultralytics.com/models/yolo11/),
[oriented detection documentation](https://docs.ultralytics.com/tasks/obb/), and
[exact DOTA class list](https://github.com/ultralytics/ultralytics/blob/main/ultralytics/cfg/datasets/DOTAv1.yaml).

YOLO11 weights/tooling are offered under the publisher's AGPL-3.0 or Enterprise
terms. Review that distribution choice before bundling a model with this MIT-licensed
project; this implementation neither changes GodEye's license nor bundles weights.
[Publisher licensing](https://www.ultralytics.com/license).
If permissive detector tooling is required, [MMRotate](https://github.com/open-mmlab/mmrotate)
is an Apache-2.0 alternative to investigate; individual checkpoints and training
sources still need their own review, and native conversion is additional work.

## What can be tried in the current app

Apple's **YOLOv3 FP16 Core ML model** is a documented, ready-to-download format
candidate for testing the existing Vision adapter. It is an older everyday-object
detector, not a validated overhead infrastructure model. A successful run would
verify integration, not establish useful satellite detection accuracy.

1. Obtain `YOLOv3FP16.mlmodel` from the [official Apple model repository](https://huggingface.co/apple/coreml-YOLOv3).
2. Link a saved optical image to a mission under **Evidence**.
3. Open **Objects → Local model assistance → Choose detector…** and select it.
4. Choose the linked optical evidence, set a minimum model score, then **Analyze image**.
5. Inspect proposals before adding them to the mission. Review accepted proposals
   against the image and record a rationale. A score is not a calibrated probability.

The current adapter accepts `.mlmodel`, `.mlpackage` and `.mlmodelc` models whose
Vision request returns `VNRecognizedObjectObservation` boxes. Classifiers and raw
tensor outputs are rejected. A `.pt` download cannot be selected as a Core ML model.
Apple's card describes a Core ML object detector, but actual inference with this
trained model has **not yet been verified in GodEye**. No trained weights are installed
automatically. The app's disclosure explains these choices beside the model picker.

## Why the aerial model needs more work

YOLO11s-OBB produces oriented boxes; the current adapter expects Vision's ordinary
normalized object boxes. Core ML conversion alone does not implement rotated-box
postprocessing, class decoding, score filtering or geographic polygons. A future
adapter should preserve the rotated shape and model provenance, add tiled inference
with overlap/deduplication, and test the whole pixel-to-map transform. Do not label
raw exported tensors as supported output. The publisher documents
[Core ML export](https://docs.ultralytics.com/integrations/coreml/); ordinary detection
exports with native NMS differ from the OBB path.

## Imagery and evaluation come first

Sentinel-2 RGB has 10 m source pixels. A car or small building may occupy less than
a pixel; a different detector or enlarged preview cannot recover missing detail.
Use sufficiently detailed, dated optical imagery for object-scale work. A 2 m
panchromatic image also differs from RGB training data, so source-specific evaluation
is necessary. The existing detector analyzes a bounded display preview, not the
full-resolution scientific raster, and resizes the entire image to model input.
Large regions can therefore lose small objects even when the original data is sharp.

Before depending on an aerial model, evaluate labeled images from the actual sources
and viewing conditions used in GodEye. Measure precision/recall per class, missed
objects, duplicate boxes, geolocation error, latency and memory on the target Mac.
Keep a held-out test set and include empty scenes. Construction/damage/change
claims need independent dated evidence and a separate validation protocol. The
current model adapter only proposes optical objects for human review; it does not
classify SAR or perform validated infrastructure change detection.
