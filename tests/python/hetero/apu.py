import tvm
import numpy as np
import tvm.ir
from tvm import relay, tir, autotvm
from tvm.relay.dataflow_pattern import TupleGetItemPattern, is_op, wildcard
from tvm.relay.op.contrib.register import get_pattern_table, register_pattern_table
from tvm.relay.op.annotation import compiler_begin, compiler_end
from tvm.relay.expr import Call, TupleGetItem, Var, Constant, Tuple
from tvm.ir import Op
from tvm.contrib import utils
import tvm.contrib.graph_runtime as runtime
import argparse

# def make_add_pattern():
#     """Create a pattern to match x + y

#        a  b  a  b
#         \/    \/
#         add  add
#          \   /
#           \ /
#           mul
#           /  \
#        c /  c |
#        \/   \/
#        mul  mul
#         \   /
#          \ /
#          add

# [[1620. 1782. 1944.]
#  [2106. 2268. 2430.]]


def get_model():
    a = relay.var("a", shape=(2, 3))
    b = relay.var("b", shape=(2, 3))
    c = relay.var("c", shape=(2, 3))
    add1 = relay.add(a, b)
    add2 = relay.add(a, b)
    mul1 = relay.multiply(add1, add2)
    mul2 = relay.multiply(mul1, c)
    mul3 = relay.multiply(mul1, c)
    add3 = relay.add(mul2, mul3)
    func = relay.Function([a, b, c], add3)

    mod = tvm.IRModule()
    mod["main"] = func
    return mod


def get_placement(expr):
    """This method is called for each Call node in the graph. Return the targeted
    compiler for each Op or "default"
    """
    target_1 = "default"
    target_2 = "cuda"
    target_ops = ["multiply"]
    placement = target_1
    if isinstance(expr, Call):
        if isinstance(expr.op, Op):
            if expr.op.name in target_ops:
                placement = target_2
    return placement


def test_local_cpu():
    mod = get_model()
    print(mod)
    target_host = "llvm"
    target = "llvm"
    context = tvm.cpu()

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target, target_host=target_host)

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)
    result = ex.evaluate()(A, B, C)
    print(result)


def test_local_cuda():
    mod = get_model()
    print(mod)
    target_host = "llvm"
    target = "cuda"
    context = tvm.context("cuda", 0)

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target, target_host=target_host)

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)
    result = ex.evaluate()(A, B, C)
    print(result)


# @tvm._ffi.register_func("relay.ext.cuda")
# def cuda_ext_compiler(ref):
#     """Create a CUDA runtime from the provided Relay expression"""

#     print("apy.py 111: &&&&&&&&&&&&&&&&&&&& in relay.ext.cuda")
#     assert isinstance(ref, tvm.relay.function.Function)

#     out_tensor_names = []
#     name = str(ref.attrs.global_symbol)

#     # print("ref.attrs", ref.attrs)
#     # if "Compiler" in ref.attrs:
#     #     ref.attrs.pop("Compiler")
#     # print("ref.attrs post pop", ref.attrs)

#     # Rewrite the function
#     # TVM_DLL Function(tvm::Array<Var> params, Expr body, Type ret_type, tvm::Array<TypeVar> ty_params,
#     #                  tvm::DictAttrs attrs = NullValue<DictAttrs>(), Span span = Span());
#     # new_ref = tvm.relay.function.Function(ref.params, ref.body, ref.ret_type, ref.type_params, ref.attrs, ref.span)
#     # attrs = ref.attrs
#     # new_attrs = dict()
#     # for key, val in ref.attrs.items():
#     #     print(key, val)
#     #     if key != "Compiler":
#     #         new_attrs[key] = val

#     # # attrs.pop("Compiler")
#     # print(new_attrs)
#     # # print("*****")
#     # new_ref = ref.with_attr(new_attrs)
#     # # print("*****")
#     # # new_ref = ref

#     mod = tvm.IRModule()
#     mod["main"] = ref
#     mod = relay.transform.ExternalFunctionToInternal()(mod)
#     print("module 1#######################", mod)

#     # print("cuda ext module", mod)
#     # pass_context = tvm.get_global_func("transform.GetCurrentPassContext")()
#     # lib = relay.build(mod, target="cuda")
#     # print(lib)
#     # return lib


def test_local_cuda_cpu():
    mod = get_model()
    # print(mod)
    mod = relay.transform.AnnotateCompiler(get_placement)(mod)
    mod = relay.transform.MergeCompilerRegions()(mod)
    mod = relay.transform.PartitionGraph()(mod)
    print(mod)
    target_host = "llvm"
    target = "llvm"
    # target = {"llvm":"llvm", "cuda":"cuda"}

    context = tvm.context("cuda", 0)

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target, target_host=target_host)

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)
    result = ex.evaluate()(A, B, C)
    print(result)


def test_local_vulkan():
    mod = get_model()
    print(mod)
    target_host = "llvm"
    target = "vulkan"
    context = tvm.vulkan()

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target, target_host=target_host)

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)
    result = ex.evaluate()(A, B, C)
    print(result)


def test_local():
    mod = get_model()
    print(mod)

    # mod = relay.transform.AnnotateCompiler(get_placement)(mod)
    # mod = relay.transform.MergeCompilerRegions()(mod)
    # mod = relay.transform.PartitionGraph()(mod)
    # print(mod)

    # setup remote execution
    device = "4900hs"
    host = "tracker"
    port = 9191

    # target_host = "llvm -mtriple=x86_64-linux-win32"

    # this is for running on windows
    # target = "vulkan -mtriple=x86_64-pc-win32"
    # target_host = "llvm -mtriple=x86_64-linux-win32"
    target_host = "llvm"

    if args.remote:
        remote = autotvm.measure.request_remote(device, host, port, timeout=1000)
        build_type = "cpu"
        if build_type == "cpu":
            context = remote.cpu(0)
            target = "llvm -mcpu=znver2"
        elif build_type == "vulkan":
            context = remote.vulkan(0)
            target = "vulkan"
        elif build_type == "hetero":
            # context = ???
            target = "llvm -mcpu=znver2"
        else:
            print("Must specify build_type")
            sys.exit(-1)
    else:
        target = {"llvm": "llvm -mcpu=znver2", "vulkan": "vulkan"}

    target = "llvm"
    context = tvm.cpu()

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target, target_host=target_host)

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    module = runtime.GraphModule(lib["default"](context))
    # print(module.get_source())
    module.set_input(0, A)
    module.set_input(1, B)
    module.set_input(2, C)

    module.run()
    result = module.get_output(0)

    # ctx = tvm.context("llvm", 0)
    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)
    result = ex.evaluate()(A, B, C)
    print(result)


def test_remote():
    mod = get_model()
    print(mod)

    # mod = relay.transform.AnnotateCompiler(get_placement)(mod)
    # mod = relay.transform.MergeCompilerRegions()(mod)
    # mod = relay.transform.PartitionGraph()(mod)
    # print(mod)

    # setup remote execution
    device = "4900hs"
    host = "tracker"
    port = 9191

    # target_host = "llvm -mtriple=x86_64-linux-win32"

    # this is for running on windows
    # target = "vulkan -mtriple=x86_64-pc-win32"
    # target_host = "llvm -mtriple=x86_64-linux-win32"
    target_host = "llvm"

    if args.remote:
        remote = autotvm.measure.request_remote(device, host, port, timeout=1000)
        build_type = "cpu"
        if build_type == "cpu":
            context = remote.cpu(0)
            target = "llvm -mcpu=znver2"
        elif build_type == "vulkan":
            context = remote.vulkan(0)
            target = "vulkan"
        elif build_type == "hetero":
            # context = ???
            target = "llvm -mcpu=znver2"
        else:
            print("Must specify build_type")
            sys.exit(-1)
    else:
        target = {"llvm": "llvm -mcpu=znver2", "vulkan": "vulkan"}

    # target = {"llvm":"llvm -mcpu=znver2", "vulkan":"vulkan"}
    # target[cpu_context] = "llvm -mcpu=znver2" # windows llvm
    # target[vulkan_context] = "vulkan"

    target = "vulkan"
    context = tvm.vulkan()

    with tvm.transform.PassContext(opt_level=3):
        lib = relay.build(mod, target=target)

    # temp = utils.tempdir()
    # lib.export_library(temp.relpath("graphlib.tar"))
    # # lib.get_source()
    # remote.upload(temp.relpath("graphlib.tar"))
    # rlib = remote.load_module("graphlib.tar")

    A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), context)
    B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), context)
    C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), context)

    module = runtime.GraphModule(lib["default"](context))
    # print(module.get_source())
    module.set_input(0, A)
    module.set_input(1, B)
    module.set_input(2, C)

    module.run()
    result = module.get_output(0)

    # ctx = tvm.context("llvm", 0)
    ex = tvm.relay.create_executor(mod=mod, ctx=context, target=target)

    result = ex.evaluate()(A, B, C)

    print(result)


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("--remote", action="store_true", help="Use remote RPC.")
    args = parser.parse_args()

    # parser.add_argument("--tune", action="store_true",
    #                     help="If set, run tuning on the model.")
    # parser.add_argument("--ntrial", type=int, default=2000,
    #                     help="Number of autoTVM trials (used when passing --tune).")
    # parser.add_argument("--start-layer", type=int, default=0,
    #                     help="Start tuning from layer N (default 0).")
    # parser.add_argument("--log-dir", type=str, default="../logs_v1000/",
    #                     help="Dir of autoTVM logs.")
    # parser.add_argument("--apply-log", action="store_true",
    #                     help="If set, apply autotuning log.")
    # parser.add_argument("--check-on-image", action="store_true",
    #                     help="If set, runs the network against a cat image to check output (only works for classifiation workloads).")
    # parser.add_argument("--debug", action="store_true",
    #                     help="If set, obtains per-layer breakdown of execution time.")
    # parser.add_argument("--get-tar", action="store_true",
    #                     help="If set, generates locally a tarball.")
    # parser.add_argument("--network", type=str, default="amd_sinet_onnx",
    #                     help="Model to evaluate.")
    # parser.add_argument("--layout", type=str, choices=["NCHW", "NHWC"], default="NCHW",
    #                     help="Layout to transpose operators to.")
    # parser.add_argument("--via-onnx", action="store_true", help="If set, import via ONNX")
    # parser.add_argument("--target", type=str, choices=["llvm -mcpu=znver1",
    #                                                    "llvm -mcpu=znver2",
    #                                                    "vulkan"],
    #                     default="llvm -mcpu=znver1", help="TVM target string.")
    # parser.add_argument("--target-os", type=str, choices=["windows", "linux"],
    #                     default="linux", help="TVM target operating system.")
    # parser.add_argument("--remote", action="store_true",
    #                     help="Program target over RPC (requires valid tracker params).")
    # parser.add_argument("--rpc-host", type=str, default='tracker',
    #                     help="RPC tracker host name.")
    # parser.add_argument("--rpc-port", type=int, default=9191,
    #                     help="RPC tracker port.")
    # parser.add_argument("--rpc-device", type=str, default="v1000", choices=["4900hs", "v1000"],
    #                     help="Device string used to register target to the tracker.")

    # test_local_vulkan()
    # test_local_cpu()
    # test_local_cuda()
    test_local_cuda_cpu()
    # test_remote()
