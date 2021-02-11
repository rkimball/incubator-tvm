import multiprocessing
import tvm
import numpy as np
import os
import time
from tvm import relay, tir, autotvm
from tvm.relay import transform
from tvm.relay.expr import Call, TupleGetItem, Var, Constant, Tuple
from tvm.ir import Op
from tvm.runtime import vm

import deckhand
m = deckhand.load_model("tvm:mlp")
m.tune()

# def get_model():
#     #        a  b  a  b
#     #         \/    \/
#     #         add  add
#     #          \   /
#     #           \ /
#     #           mul
#     #           /  \
#     #       c  / c  |
#     #        \/   \/
#     #        mul  mul
#     #         \   /
#     #          \ /
#     #          add
#     a = relay.var("a", shape=(2, 3))
#     b = relay.var("b", shape=(2, 3))
#     c = relay.var("c", shape=(2, 3))
#     add1 = relay.add(a, b)
#     add2 = relay.add(a, b)
#     mul1 = relay.multiply(add1, add2)
#     mul2 = relay.multiply(mul1, c)
#     mul3 = relay.multiply(mul1, c)
#     add3 = relay.add(mul2, mul3)
#     func = relay.Function([a, b, c], add3)

#     mod = tvm.IRModule()
#     mod["main"] = func
#     return mod

# multiprocessing.set_start_method("spawn", force=True)

# mod = get_model()


# exe = relay.vm.compile(mod, "llvm")
# ctx = tvm.context("cpu")

# vm = vm.VirtualMachine(exe, ctx)


# A = np.array([[1, 2, 3], [4, 5, 6]]).astype("float32")
# B = np.array([[8, 7, 6], [5, 4, 3]]).astype("float32")
# C = np.array([[10, 11, 12], [13, 14, 15]]).astype("float32")

# result = vm.invoke("main", A, B, C)



# measure_option = autotvm.measure_option(
#     builder=autotvm.LocalBuilder(),
#     runner=autotvm.LocalRunner(
#         number=tuning_config.runner_number,
#         repeat=tuning_config.runner_repeat,
#         min_repeat_ms=tuning_config.runner_min_repeat_ms,
#     ),
# )

# vm.tune()
# # m.tune()
