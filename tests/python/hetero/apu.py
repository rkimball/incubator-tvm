import tvm
import numpy as np
import os
import wget
import onnx
from tvm import relay, tir, autotvm
from tvm.relay import transform

#        a  b  a  b
#         \/    \/
#         add  add
#          \   /
#           \ /
#           mul
#           /  \
#       c  / c  |
#        \/   \/
#        mul  mul
#         \   /
#          \ /
#          add

# [[1620. 1782. 1944.]
#  [2106. 2268. 2430.]]


def get_annotated_model(cpu_ctx, dev_ctx):
    a = relay.var("a", shape=(2, 3))
    b = relay.var("b", shape=(2, 3))
    c = relay.var("c", shape=(2, 3))
    add1 = relay.add(a, b)
    add2 = relay.add(a, b)
    mul1 = relay.annotation.on_device(relay.multiply(add1, add2), dev_ctx)
    mul2 = relay.annotation.on_device(relay.multiply(mul1, c), dev_ctx)
    mul3 = relay.annotation.on_device(relay.multiply(mul1, c), dev_ctx)
    add3 = relay.add(mul2, mul3)
    func = relay.Function([a, b, c], add3)

    mod = tvm.IRModule()
    mod["main"] = func

    # This pass will apply the on_device annotations from graph creation and insert
    # device_copy ops, splitting the graph into subgraphs to be run on the specified
    # devices.
    mod = relay.transform.RewriteAnnotatedOps(cpu_ctx.device_type)(mod)
    return mod


def test_local_gpu_cpu():
    gpu_target = "vulkan"
    cpu_ctx = tvm.context("cpu")
    dev_ctx = tvm.context(gpu_target)
    mod = get_annotated_model(cpu_ctx, dev_ctx)
    target = {"cpu": "llvm", gpu_target: gpu_target}

    A = np.array([[1, 2, 3], [4, 5, 6]]).astype("float32")
    B = np.array([[8, 7, 6], [5, 4, 3]]).astype("float32")
    C = np.array([[10, 11, 12], [13, 14, 15]]).astype("float32")

    print(mod)
    exe = relay.vm.compile(mod, target)
    ctx = [cpu_ctx, dev_ctx]
    vm = tvm.runtime.vm.VirtualMachine(exe, ctx)
    result = vm.invoke("main", A, B, C)

    print(result)


def test_onnx_resnet50():
    model_path = os.path.join("../../models", "resnet50_v1.onnx")
    url = "https://zenodo.org/record/2592612/files/resnet50_v1.onnx"
    iname = "input_tensor:0"
    ishape = (1, 3, 224, 224)
    dtype = "float32"

    if not os.path.exists(model_path):
        wget.download(url, out=model_path)

    shape_dict = {iname: ishape}
    dtype_dict = {iname: dtype}
    onnx_model = onnx.load(model_path)
    # Import into Relay
    mod, params = relay.frontend.from_onnx(onnx_model, shape_dict)

    print(mod)

    # return mod, params, shape_dict, dtype_dict


if __name__ == "__main__":
    # test_local_gpu_cpu()
    test_onnx_resnet50()
