#!/usr/bin/env python3
"""Bounded local ONNX proposals. Run through HeliosWorkerHost; never imports model code."""
import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import resource
import signal
import sys
import time
import uuid

# Set these before numerical libraries load. This process has no GPU provider.
for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ[key] = "2"
os.environ["CUDA_VISIBLE_DEVICES"] = ""

MAX_MODEL = 64 * 1024 * 1024
MAX_IMAGE = 32 * 1024 * 1024
MAX_TENSOR = 16 * 1024 * 1024
MAX_TOTAL = 64 * 1024 * 1024
OPS = set("Abs Add AveragePool BatchNormalization Cast Clip Concat Constant Conv Div Exp Flatten Gather Gemm GlobalAveragePool Identity LeakyRelu MatMul Max MaxPool Min Mul Neg Pad Pow ReduceMax ReduceMean ReduceSum Relu Reshape Resize Sigmoid Slice Softmax Split Sqrt Squeeze Sub Transpose Unsqueeze Where Equal Greater Less And Or Not".split())
TINY_SHA = "583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae"
VOC = "aeroplane bicycle bird boat bottle bus car cat chair cow diningtable dog horse motorbike person pottedplant sheep sofa train tvmonitor".split()


class AdmissionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise AdmissionError(message)


def event(state, stage, progress=None):
    print(json.dumps(dict(state=state, stage=stage, progress=progress)), flush=True)


def bounded_file(path, maximum):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), "Select a regular local file; symbolic links are unsupported.")
    with path.open("rb") as handle:
        data = handle.read(maximum + 1)
    require(0 < len(data) <= maximum, "A selected local file exceeds its size limit or is empty.")
    return data


def load_json(path, maximum=1024 * 1024):
    data = bounded_file(path, maximum)
    try:
        value = json.loads(data, parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except (ValueError, UnicodeError) as error:
        raise AdmissionError("Invalid JSON request or model contract.") from error
    require(isinstance(value, dict), "Expected a JSON object.")
    return value, data


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_request(request):
    tiny = request.get("backend") == "onnx-tinyyolov2-voc-cpu"
    require(request.get("version") == 1 and (tiny or request.get("backend") == "onnx-cpu"), "Only version 1 ONNX CPU contracts are supported.")
    require(request.get("boxFormat") == "xyxy-normalized-upper-left", "Rotated, pixel, raw detector and lower-left boxes are unsupported.")
    require(request.get("preprocessing") == ("rgb-stretch-bilinear-0to255" if tiny else "rgb-stretch-bilinear-0to1"), "Unsupported preprocessing for this model backend.")
    require(isinstance(request.get("labels"), list) and 1 <= len(request["labels"]) <= 512, "Declare 1–512 class labels in the contract.")
    require(all(isinstance(x, str) and 0 < len(x.strip()) <= 120 and not any(ord(c) < 32 for c in x) for x in request["labels"]), "Class labels are empty, too long or contain control characters.")
    require(len(set(request["labels"])) == len(request["labels"]), "Class labels must be unique.")
    for name in (("input", "grid") if tiny else ("input", "boxes", "scores", "classes")):
        require(isinstance(request.get(name), str) and 0 < len(request[name]) <= 200, "Declare input and output tensor names.")
    if not tiny:
        require(len({request[x] for x in ("input", "boxes", "scores", "classes")}) == 4, "Tensor names must differ.")
    else:
        require(request["labels"] == VOC and request.get("input") == "image" and request.get("grid") == "grid" and request.get("modelSHA256") == TINY_SHA, "The Tiny YOLOv2 adapter supports only the pinned ONNX Model Zoo VOC model and its 20 classes.")
    require(re.fullmatch("[0-9a-f]{64}", str(request.get("modelSHA256", ""))), "The model contract requires its lowercase SHA256.")
    bounds = request.get("bounds")
    require(isinstance(bounds, list) and len(bounds) == 4 and all(number(x) for x in bounds), "The source preview needs EPSG:4326 outer-edge bounds.")
    w, s, e, n = bounds
    require(-180 <= w < e <= 180 and -90 <= s < n <= 90, "The source preview bounds are invalid; dateline-crossing previews are unsupported.")
    threshold = request.get("threshold")
    require(number(threshold) and 0 <= threshold <= 1, "Minimum model score must be between zero and one.")
    for field in ("missionID", "evidenceID"):
        try:
            require(str(uuid.UUID(request[field])) == request[field].lower(), "Invalid mission or evidence reference.")
        except (ValueError, TypeError, KeyError) as error:
            raise AdmissionError("A linked mission and evidence reference is required.") from error


def tensor_count(info):
    require(info.type.HasField("tensor_type"), "Only dense numeric tensors are supported.")
    tensor = info.type.tensor_type
    require(tensor.elem_type in (1, 6, 7, 9), "Unsupported tensor type; only float32, int32, int64 and bool are admitted.")
    require(tensor.HasField("shape"), "Dynamic tensors are unsupported.")
    shape = []
    for dim in tensor.shape.dim:
        require(dim.HasField("dim_value") and 0 < dim.dim_value <= MAX_TENSOR, "Dynamic or empty tensor dimensions are unsupported.")
        shape.append(dim.dim_value)
    count = math.prod(shape)
    require(count <= MAX_TENSOR, "An inferred tensor exceeds 16 million elements.")
    return shape, count


def validate_model(data, request):
    import onnx
    require(hashlib.sha256(data).hexdigest() == request["modelSHA256"], "The model SHA256 does not match its contract.")
    model = onnx.load_model_from_string(data)
    tiny = request["backend"] == "onnx-tinyyolov2-voc-cpu"
    if tiny:
        # The pinned legacy graph declares initializers as overridable inputs and
        # a symbolic batch. Freeze exactly that reviewed model to batch one.
        initializer_names = {x.name for x in model.graph.initializer}
        inputs = [x for x in model.graph.input if x.name not in initializer_names]
        del model.graph.input[:]
        model.graph.input.extend(inputs)
        for info in list(model.graph.input) + list(model.graph.output):
            dim = info.type.tensor_type.shape.dim[0]
            dim.ClearField("dim_param")
            dim.dim_value = 1
    require(0 < model.ir_version <= 10 and not model.functions, "Unsupported ONNX IR version or local functions.")
    require(all(x.domain in ("", "ai.onnx") and 7 <= x.version <= 22 for x in model.opset_import), "Only standard ONNX operators (opsets 7–22) are supported.")
    graph = model.graph
    require(0 < len(graph.node) <= 1000 and len(graph.initializer) <= 2000 and not graph.sparse_initializer, "The model graph exceeds the bounded dense contract.")
    tensors = list(graph.initializer)
    for node in graph.node:
        require(node.domain in ("", "ai.onnx") and node.op_type in OPS, "Unsupported operator, control flow, raw postprocessing or custom domain.")
        for attr in node.attribute:
            require(attr.type not in (onnx.AttributeProto.GRAPH, onnx.AttributeProto.GRAPHS, onnx.AttributeProto.SPARSE_TENSOR, onnx.AttributeProto.SPARSE_TENSORS), "Nested graphs and sparse tensors are unsupported.")
            if attr.type == onnx.AttributeProto.TENSOR:
                tensors.append(attr.t)
            if attr.type == onnx.AttributeProto.TENSORS:
                tensors.extend(attr.tensors)
    for tensor in tensors:
        require(tensor.data_location != onnx.TensorProto.EXTERNAL and not tensor.external_data, "External model tensor files are unsupported.")
        require(tensor.data_type in (1, 6, 7, 9) and math.prod(tensor.dims) <= MAX_TENSOR, "An initializer exceeds the numeric tensor limits.")
    onnx.checker.check_model(model, full_check=True)
    model = onnx.shape_inference.infer_shapes(model, check_type=True, strict_mode=True, data_prop=True)
    all_info = {x.name: x for x in list(model.graph.input) + list(model.graph.value_info) + list(model.graph.output)}
    initialized = {x.name for x in graph.initializer}
    require(len(graph.input) == 1 and graph.input[0].name == request["input"] and not (initialized & {request["input"]}), "Exactly one image input is required.")
    require({x.name for x in graph.output} == ({request["grid"]} if tiny else {request[x] for x in ("boxes", "scores", "classes")}), "Model output names differ from the admitted backend contract.")
    total = 0
    for name, value in all_info.items():
        _, count = tensor_count(value)
        total += count
    require(total <= MAX_TOTAL, "The inferred graph exceeds the aggregate tensor memory limit.")
    for node in graph.node:
        require(all(name in all_info or name in initialized for name in node.output if name), "Every intermediate tensor needs a statically inferred shape.")
    shape, _ = tensor_count(all_info[request["input"]])
    require(len(shape) == 4 and shape[:2] == [1, 3] and 1 <= shape[2] <= 1024 and 1 <= shape[3] <= 1024 and all_info[request["input"]].type.tensor_type.elem_type == 1, "Input must be float32 [1,3,H,W], with H and W at most 1024.")
    if tiny:
        require(shape == [1, 3, 416, 416] and tensor_count(all_info[request["grid"]])[0] == [1, 125, 13, 13], "The pinned Tiny YOLOv2 tensor shapes differ.")
        return shape, model.SerializeToString()
    boxes, _ = tensor_count(all_info[request["boxes"]])
    scores, _ = tensor_count(all_info[request["scores"]])
    classes, _ = tensor_count(all_info[request["classes"]])
    require(len(boxes) == 2 and 1 <= boxes[0] <= 2000 and boxes[1] == 4 and scores == classes == [boxes[0]], "Outputs must be boxes [N,4], scores [N], classes [N], with fixed N <= 2000; rotated boxes are unsupported.")
    require(all_info[request["boxes"]].type.tensor_type.elem_type == all_info[request["scores"]].type.tensor_type.elem_type == 1 and all_info[request["classes"]].type.tensor_type.elem_type == 7, "Boxes and scores must be float32; classes must be int64.")
    return shape, data


def decode_tiny(grid, threshold):
    """Pinned VOC anchors; bounded 845 candidates, class-aware IoU suppression."""
    import numpy as np
    require(grid.dtype == np.dtype("float32") and grid.shape == (1, 125, 13, 13) and np.isfinite(grid).all(), "Invalid Tiny YOLOv2 output tensor.")
    anchors = [(1.08, 1.19), (3.42, 4.41), (6.63, 11.38), (9.42, 5.11), (16.62, 10.52)]
    candidates = []
    sigmoid = lambda x: 1 / (1 + math.exp(-max(-80, min(80, float(x)))))
    for cy in range(13):
        for cx in range(13):
            for anchor, (aw, ah) in enumerate(anchors):
                row = grid[0, anchor*25:(anchor+1)*25, cy, cx]
                logits = row[5:].astype(np.float64)
                logits -= logits.max()
                probabilities = np.exp(logits)
                probabilities /= probabilities.sum()
                label = int(probabilities.argmax())
                score = sigmoid(row[4]) * float(probabilities[label])
                if score < threshold:
                    continue
                x, y = (cx + sigmoid(row[0])) / 13, (cy + sigmoid(row[1])) / 13
                w, h = math.exp(max(-80, min(80, float(row[2])))) * aw / 13, math.exp(max(-80, min(80, float(row[3])))) * ah / 13
                box = [max(0., x-w/2), max(0., y-h/2), min(1., x+w/2), min(1., y+h/2)]
                if box[0] < box[2] and box[1] < box[3]:
                    candidates.append((box, score, label))
    candidates.sort(key=lambda x: -x[1])
    selected = []
    for candidate in candidates:
        box, _, label = candidate
        duplicate = False
        for other, _, other_label in selected:
            if label != other_label:
                continue
            intersection = max(0., min(box[2], other[2])-max(box[0], other[0])) * max(0., min(box[3], other[3])-max(box[1], other[1]))
            union = (box[2]-box[0])*(box[3]-box[1]) + (other[2]-other[0])*(other[3]-other[1]) - intersection
            if union > 0 and intersection/union > 0.5:
                duplicate = True
                break
        if not duplicate:
            selected.append(candidate)
            if len(selected) == 200:
                break
    return (np.array([x[0] for x in selected], dtype=np.float32).reshape((-1, 4)), np.array([x[1] for x in selected], dtype=np.float32), np.array([x[2] for x in selected], dtype=np.int64))


def geographic_box(box, bounds):
    x0, y0, x1, y1 = [float(x) for x in box]
    require(all(math.isfinite(x) for x in (x0, y0, x1, y1)) and 0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1, "Model output contains an invalid normalized box.")
    w, s, e, n = bounds
    return [w + x0 * (e - w), n - y1 * (n - s), w + x1 * (e - w), n - y0 * (n - s)]


def detect(request, contract_sha):
    validate_request(request)
    import numpy as np
    import onnxruntime as ort
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = 2048 * 2048
    model_data = bounded_file(request["model"], MAX_MODEL)
    shape, runtime_model = validate_model(model_data, request)
    source = bounded_file(request["source"], MAX_IMAGE)
    with Image.open(io.BytesIO(source)) as image:
        require(image.format == "PNG" and 0 < image.width <= 2048 and 0 < image.height <= 2048 and getattr(image, "n_frames", 1) == 1, "Select one PNG preview, at most 2048×2048 pixels.")
        require(image.getexif().get(274, 1) == 1, "Oriented EXIF previews are unsupported.")
        require(image.mode in ("RGB", "RGBA", "L"), "Unsupported preview pixel format.")
        # Transparency represents unavailable source pixels, not black objects.
        if image.mode == "RGBA":
            require(image.getchannel("A").getextrema() == (255, 255), "This preview contains transparent/no-data pixels; select a complete preview.")
        width, height = image.size
        rgb = image.convert("RGB").resize((shape[3], shape[2]), Image.Resampling.BILINEAR)
        pixels = np.asarray(rgb, dtype=np.float32).transpose((2, 0, 1))[None]
        if request["backend"] == "onnx-cpu":
            pixels = pixels / 255.0
    event("processing", "Running the selected local model on CPU")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.enable_cpu_mem_arena = False
    options.enable_mem_pattern = False
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.log_severity_level = 3
    started = time.monotonic()
    session = ort.InferenceSession(runtime_model, sess_options=options, providers=["CPUExecutionProvider"])
    session.disable_fallback()
    require(session.get_providers() == ["CPUExecutionProvider"], "The requested CPU provider was not selected.")
    if request["backend"] == "onnx-tinyyolov2-voc-cpu":
        grid = session.run([request["grid"]], {request["input"]: pixels})[0]
        boxes, scores, classes = decode_tiny(grid, request["threshold"])
    else:
        boxes, scores, classes = session.run([request[x] for x in ("boxes", "scores", "classes")], {request["input"]: pixels})
    require(boxes.dtype == scores.dtype == np.dtype("float32") and classes.dtype == np.dtype("int64"), "The runtime returned unsupported output types.")
    require(boxes.shape == (len(scores), 4) and scores.shape == classes.shape and scores.ndim == 1 and len(scores) <= 2000, "Runtime tensor shapes differ from the admitted contract.")
    require(np.isfinite(scores).all() and ((scores >= 0) & (scores <= 1)).all(), "Model scores are outside [0,1] or non-finite.")
    require(((classes >= 0) & (classes < len(request["labels"]))).all(), "The model returned an undeclared class ID.")
    # Validate every output, including boxes below threshold: malformed tensors
    # must not be silently interpreted as empty detections.
    mapped = [geographic_box(box, request["bounds"]) for box in boxes]
    selected = sorted((i for i, score in enumerate(scores) if score >= request["threshold"]), key=lambda i: (-float(scores[i]), i))[:200]
    source_sha = hashlib.sha256(source).hexdigest()
    now = time.time() - 978307200  # Swift Foundation epoch, retained by mission JSON.
    proposals = []
    transform = "RGB full-image bilinear stretch " + ("0..255; TinyYOLOv2 VOC anchors; class NMS IoU .5" if request["backend"] == "onnx-tinyyolov2-voc-cpu" else "/255; NCHW") + "; upper-left normalized xyxy to EPSG:4326 preview edges"
    for index in selected:
        bounds = mapped[index]
        label = request["labels"][int(classes[index])]
        item = dict(id=str(uuid.uuid4()), title=label, category=label, coordinate=dict(latitude=(bounds[1] + bounds[3]) / 2, longitude=(bounds[0] + bounds[2]) / 2), bounds=bounds, evidenceID=request["evidenceID"], state="Unreviewed", method="ONNX CPU proposal · " + Path(request["model"]).name[:200], modelScore=float(scores[index]), modelName=Path(request["model"]).name[:200], inputSHA256=source_sha, inputTransform=transform, createdAt=now, note="Unreviewed local model output. Model score is not calibrated probability; geographic accuracy follows the source preview.", modelSHA256=request["modelSHA256"], modelContractSHA256=contract_sha, modelBackend=request["backend"], imageBox=[float(x) for x in boxes[index]], imagePixelBox=[float(boxes[index][0])*width, float(boxes[index][1])*height, float(boxes[index][2])*width, float(boxes[index][3])*height])
        observed = request.get("observedAt")
        if number(observed):
            item["observedAt"] = observed
        proposals.append(item)
    return dict(version=1, state="completed", backend=request["backend"], runtimeVersion=ort.__version__, providers=session.get_providers(), missionID=request["missionID"], evidenceID=request["evidenceID"], sourceSHA256=source_sha, modelSHA256=request["modelSHA256"], runtimeModelSHA256=hashlib.sha256(runtime_model).hexdigest(), modelNormalization="Freeze pinned legacy batch to one; remove initializer graph inputs" if request["backend"] == "onnx-tinyyolov2-voc-cpu" else "none", contractSHA256=contract_sha, sourcePixels=[width, height], inputShape=shape, sourceBounds=request["bounds"], transform=transform, threshold=request["threshold"], outputRows=845 if request["backend"] == "onnx-tinyyolov2-voc-cpu" else len(scores), proposalCount=len(proposals), proposals=proposals, inferenceSeconds=time.monotonic()-started, peakRSSBytes=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * (1 if sys.platform == "darwin" else 1024), accuracyValidated=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["render"])
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    # Hard ceiling complements static admission, wall timeout and owned process cancellation.
    resource.setrlimit(resource.RLIMIT_CPU, (120, 125))
    if sys.platform != "darwin":
        resource.setrlimit(resource.RLIMIT_AS, (4 * 1024**3, 4 * 1024**3))
    signal.alarm(180)
    output = Path(args.output)
    require(not output.exists() and not output.is_symlink(), "Output must be a new owned job directory.")
    event("validating", "Checking the local model contract and source preview")
    request, _ = load_json(args.input)
    contract, contract_data = load_json(request.get("contractPath", ""), 64 * 1024)
    model = contract.get("model")
    require(isinstance(model, str) and model == Path(model).name and model not in ("", ".", ".."), "The contract model must be a filename beside the contract.")
    contract["model"] = str(Path(request["contractPath"]).resolve().parent / model)
    for field in ("source", "bounds", "threshold", "missionID", "evidenceID", "observedAt"):
        if field in request:
            contract[field] = request[field]
    result = detect(contract, hashlib.sha256(contract_data).hexdigest())
    event("exporting", "Saving unreviewed proposals and model provenance")
    output.mkdir(mode=0o700)
    with (output / "model-contract.json").open("xb") as handle:
        handle.write(contract_data)
        handle.flush()
        os.fsync(handle.fileno())
    result["modelContractFile"] = "model-contract.json"
    preview_data = bounded_file(contract["source"], MAX_IMAGE)
    require(hashlib.sha256(preview_data).hexdigest() == result["sourceSHA256"], "The source preview changed while it was analyzed. Retry with a stable source.")
    with (output / "preview.png").open("xb") as handle:
        handle.write(preview_data)
        handle.flush()
        os.fsync(handle.fileno())
    result["reviewPreview"] = "preview.png"
    temporary = output / "result.tmp"
    encoded = json.dumps(result, allow_nan=False, ensure_ascii=False).encode("utf-8")
    require(len(encoded) <= 1024 * 1024, "The proposal result exceeds 1 MiB.")
    with temporary.open("xb") as handle:
        handle.write(encoded)
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(output / "result.json")
    event("completed", "Local CPU analysis finished; proposals need analyst review", 1.0)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Local path/model errors are useful diagnostics; bounded by WorkerJob.
        event("failed", str(error)[:500])
        sys.exit(1)
