"""Generated input-dependent CPU fixture. This is not a trained detector."""
import hashlib
import json
from pathlib import Path
import sys
import uuid
import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from PIL import Image


def make_fixture(directory, *, score=None, label=0, box=None, mode="RGB"):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    image = helper.make_tensor_value_info("image", TensorProto.FLOAT, [1, 3, 8, 8])
    boxes = helper.make_tensor_value_info("boxes", TensorProto.FLOAT, [1, 4])
    scores = helper.make_tensor_value_info("scores", TensorProto.FLOAT, [1])
    classes = helper.make_tensor_value_info("classes", TensorProto.INT64, [1])
    arrays = [numpy_helper.from_array(np.array([box or [.1, .2, .6, .7]], dtype=np.float32), "bbox"), numpy_helper.from_array(np.array([label], dtype=np.int64), "classid")]
    nodes = [helper.make_node("Identity", ["bbox"], ["boxes"]), helper.make_node("Identity", ["classid"], ["classes"])]
    if score is None:
        arrays.append(numpy_helper.from_array(np.array([0], dtype=np.int64), "axis"))
        nodes += [helper.make_node("ReduceMean", ["image"], ["mean"], keepdims=0), helper.make_node("Unsqueeze", ["mean", "axis"], ["scores"])]
    else:
        arrays.append(numpy_helper.from_array(np.array([score], dtype=np.float32), "score"))
        nodes.append(helper.make_node("Identity", ["score"], ["scores"]))
    graph = helper.make_graph(nodes, "GENERATED_QA_INPUT_MEAN_NOT_TRAINED", [image], [boxes, scores, classes], arrays)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)], ir_version=10)
    model.doc_string = "Generated integration fixture; no detector accuracy claim."
    data = model.SerializeToString()
    model_path = directory / "GENERATED_QA.onnx"
    model_path.write_bytes(data)
    source = directory / "GENERATED_QA.png"
    Image.new(mode, (40, 20), (255, 255, 255, 255) if mode == "RGBA" else (255, 255, 255)).save(source)
    contract = dict(version=1, backend="onnx-cpu", model=model_path.name, modelSHA256=hashlib.sha256(data).hexdigest(), boxFormat="xyxy-normalized-upper-left", preprocessing="rgb-stretch-bilinear-0to1", input="image", boxes="boxes", scores="scores", classes="classes", labels=["GENERATED_QA object"])
    contract_path = directory / "GENERATED_QA.contract.json"
    contract_path.write_text(json.dumps(contract), encoding="utf-8")
    request = dict(contractPath=str(contract_path.resolve()), source=str(source.resolve()), bounds=[10, 20, 12, 24], threshold=.5, missionID=str(uuid.uuid4()), evidenceID=str(uuid.uuid4()))
    request_path = directory / "request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    return contract, request, request_path


if __name__ == "__main__":
    _, _, path = make_fixture(sys.argv[1])
    print(path)
