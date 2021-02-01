import tvm
import numpy as np
import tvm.ir
from tvm import relay, tir, autotvm
from tvm.relay import transform
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
#       c  / c  |
#        \/   \/
#        mul  mul
#         \   /
#          \ /
#          add

# [[1620. 1782. 1944.]
#  [2106. 2268. 2430.]]


def run_opt_pass(expr, passes):
    passes = passes if isinstance(passes, list) else [passes]
    mod = tvm.IRModule.from_expr(expr)
    seq = tvm.transform.Sequential(passes)
    with tvm.transform.PassContext(opt_level=3):
        mod = seq(mod)
    return mod["main"]


def get_annotated_model(cpu_ctx, dev_ctx):
    # a = relay.var("a", shape=(2, 3))
    # b = relay.var("b", shape=(2, 3))
    # c = relay.var("c", shape=(2, 3))
    # add1 = relay.annotation.on_device(relay.add(a, b), cpu_ctx)
    # add2 = relay.annotation.on_device(relay.add(a, b), cpu_ctx)
    # mul1 = relay.annotation.on_device(relay.multiply(add1, add2), dev_ctx)
    # mul2 = relay.annotation.on_device(relay.multiply(mul1, mul1), dev_ctx)
    # mul3 = relay.annotation.on_device(relay.multiply(mul1, mul1), dev_ctx)
    # add3 = relay.annotation.on_device(relay.add(mul2, mul3), cpu_ctx)
    # func = relay.Function([a, b, c], add3)

    # func = run_opt_pass(func, transform.RewriteAnnotatedOps(dev_ctx.device_type))
    x = relay.var("x", shape=(1, 10))
    y = relay.var("y", shape=(10, 10))
    add = relay.add(x, y)
    sqrt = relay.sqrt(add)
    log = relay.log(add)
    subtract = relay.subtract(sqrt, log)
    exp = relay.exp(subtract)
    _exp = relay.annotation.on_device(exp, cpu_ctx)

    func = relay.Function([x, y], _exp)
    func = run_opt_pass(func, transform.RewriteAnnotatedOps(dev_ctx.device_type))
    return func


def test_local_cuda_cpu():
    cpu_ctx = tvm.context("cpu")
    dev_ctx = tvm.context("cuda")
    func = get_annotated_model(cpu_ctx, dev_ctx)
    target = {"cpu": "llvm", "cuda": "cuda"}

    x_data = np.random.rand(1, 10).astype("float32")
    y_data = np.random.rand(10, 10).astype("float32")
    params = {"x": x_data, "y": y_data}


    # A = tvm.nd.array(np.array([[1, 2, 3], [4, 5, 6]], dtype="float32"), cpu_ctx)
    # B = tvm.nd.array(np.array([[8, 7, 6], [5, 4, 3]], dtype="float32"), cpu_ctx)
    # C = tvm.nd.array(np.array([[10, 11, 12], [13, 14, 15]], dtype="float32"), cpu_ctx)

    # ex = tvm.relay.create_executor(kind = "vm", mod=mod, ctx=cpu_ctx, target=target)
    # # print(ex.executable.lib.get_source())
    # result = ex.evaluate()(A, B, C)

    # with tvm.transform.PassContext(opt_level=2):
    #     exe = relay.vm.compile(mod, target)
    #     ctx = [cpu_ctx, dev_ctx]
    #     vm = tvm.runtime.vm.VirtualMachine(exe, ctx)
    #     res = vm.invoke("main", **params)
    #     print(res)
    #     # tvm.testing.assert_allclose(res.asnumpy(), ref_res, rtol=1e-5, atol=1e-5)


    mod = tvm.IRModule()
    mod["main"] = func
    print(mod)
    print(target)
    exe = relay.vm.compile(mod, target)
    ctx = [cpu_ctx, dev_ctx]
    vm = tvm.runtime.vm.VirtualMachine(exe, ctx)
    result = vm.invoke("main", **params)

    print(result)


if __name__ == "__main__":
    test_local_cuda_cpu()
