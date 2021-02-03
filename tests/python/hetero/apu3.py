# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
"""Unit tests for heterogeneous compilation and execution."""
import json
import numpy as np

import tvm
from tvm import relay
from tvm.contrib import graph_runtime
from tvm.relay.expr_functor import ExprMutator
from tvm.relay import transform
import tvm.testing


def check_vm_runtime(target, ref_res, device, func, params, opt_level):
    with tvm.transform.PassContext(opt_level=opt_level):
        mod = tvm.IRModule()
        mod["main"] = func
        print(mod)
        exe = relay.vm.compile(mod, target)
        ctx = [tvm.cpu(0), tvm.context(device)]
        vm = tvm.runtime.vm.VirtualMachine(exe, ctx)
        res = vm.invoke("main", **params)
        # print(ref_res)
        print(res)
        tvm.testing.assert_allclose(res.asnumpy(), ref_res, rtol=1e-5, atol=1e-5)


def run_opt_pass(expr, passes):
    passes = passes if isinstance(passes, list) else [passes]
    mod = tvm.IRModule.from_expr(expr)
    seq = tvm.transform.Sequential(passes)
    with tvm.transform.PassContext(opt_level=3):
        mod = seq(mod)
    return mod["main"]


def annotated(cpu_ctx, dev_ctx):
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


def run_fusible_network(dev, tgt):
    R""" The network is as following:
               x     y
                \   /
                 add
                /   \
             sqrt   log
                \   /
              subtract
                  |
                 exp
    """
    x_data = np.random.rand(1, 10).astype("float32")
    y_data = np.random.rand(10, 10).astype("float32")
    # tmp_add = x_data + y_data
    # tmp_sqrt = np.sqrt(tmp_add)
    # tmp_log = np.log(tmp_add)
    # tmp_sub = np.subtract(tmp_sqrt, tmp_log)
    # ref_res = np.exp(tmp_sub)

    target = {"cpu": "llvm", dev: tgt}
    cpu_ctx = tvm.context("cpu")
    dev_ctx = tvm.context(dev)

    func = annotated(cpu_ctx, dev_ctx)
    opt_level = 2

    with tvm.transform.PassContext(opt_level=opt_level):
        mod = tvm.IRModule()
        mod["main"] = func
        print(mod)
        print(target)
        exe = relay.vm.compile(mod, target)
        ctx = [tvm.cpu(0), tvm.context(dev)]
        vm = tvm.runtime.vm.VirtualMachine(exe, ctx)
        res = vm.invoke("main", x_data, y_data)
        # print(ref_res)
        print(res)
        # tvm.testing.assert_allclose(res.asnumpy(), ref_res, rtol=1e-5, atol=1e-5)


if __name__ == "__main__":
    dev = "cuda"
    tgt = "cuda"
    run_fusible_network(dev, tgt)
