# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# 'License'); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# 'AS IS' BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
import os
import numpy as np

import tvm
import tvm.script
from tvm import te
from tvm import relay

def get_args():
    import argparse
    parser = argparse.ArgumentParser(description='Set test arguments')
    #parser.add_argument('-b', '--build', action="store_true", help='Whether to try to compile the test case; default is to lower only without compilation.')
    # parser.add_argument('-e', '--evaluate', action="store_true", help='Whether to evaluate the kernel and schedule.')
    # parser.add_argument('-v', '--verify', action="store_false", help='Whether to verify numerical results of evaluation.')
    # parser.add_argument('-B', '--batch_size', type=int, help='Batch size to use in batched gemm computation')
    parser.add_argument('-t', '--tensor_rank', type=int, default=3, choices=[3, 5], help='Rank of the input tensor')
    # parser.add_argument('-N', type=int, help='Size of N for matrix B (KxN)')
    # parser.add_argument('-K', type=int, help='Size of reduction axis K')
    # parser.add_argument('-r', '--relay', action="store_true", help='Use relay for testing')
    args = parser.parse_args()

    return args

args = get_args()

def compute(shape):
    X = te.placeholder(shape, name="X", dtype="float32")
    Y = te.compute(shape, lambda i, j, k: X[i, j, k] + 1, name="Compute_Y")
    return X, Y

def schedule(X, Y):
    s = te.create_schedule(Y.op)
    Xt = s.cache_read(X, "global", [Y])

    # copy to texture stage
    x, y, c = s[Xt].op.axis
    s[Xt].bind(x, te.thread_axis("blockIdx.x"))
    s[Xt].bind(y, te.thread_axis("threadIdx.x"))
    #s[Xt].vectorize(c)

    # the compute stage
    x, y, c = s[Y].op.axis
    xo, yo, xi, yi = s[Y].tile(x, y, 4, 4)
    s[Y].bind(xo, te.thread_axis("blockIdx.x"))
    s[Y].bind(yo, te.thread_axis("threadIdx.x"))
    #s[Y].vectorize(c)
    return s

def compute5d(shape):
    X = te.placeholder(shape, name="X", dtype="float32")
    Y = te.compute(shape, lambda i, j, k, l, m: X[i, j, k, l, m] + 1, name="Compute_Y")
    return X, Y

def schedule5d(X, Y):
    s = te.create_schedule(Y.op)
    Xt = s.cache_read(X, "global", [Y])

    # copy to texture stage
    a, b, c, d, e = s[Xt].op.axis
    #ab = s[Xt].fuse(a, b)
    #cd = s[Xt].fuse(c, d)
    bcd = s[Xt].fuse(b, c, d)
    s[Xt].bind(a, te.thread_axis("blockIdx.x"))
    s[Xt].bind(bcd, te.thread_axis("threadIdx.x"))
    #s[Xt].vectorize(e)

    # the compute stage
    a, b, c, d, e = s[Y].op.axis
    #ab = s[Y].fuse(a, b)
    #cd = s[Y].fuse(c, d)
    bcd = s[Y].fuse(b, c, d)
    #xo, yo, xi, yi = s[Y].tile(ab, cd, 4, 4)
    xo, yo, xi, yi = s[Y].tile(a, bcd, 4, 4)
    s[Y].bind(xo, te.thread_axis("blockIdx.x"))
    s[Y].bind(yo, te.thread_axis("threadIdx.x"))
    #s[Y].vectorize(e)
    return s

def test_texture(target="llvm", target_host="llvm"):
    if args.tensor_rank == 3:
        shape =(32, 32, 4)
        X, Y = compute(shape)
        s = schedule(X, Y)
    elif args.tensor_rank == 5:
        shape =(32, 2, 4, 4, 4)
        X, Y = compute5d(shape)
        s = schedule5d(X, Y)

    result = tvm.driver.lower(s, [X, Y])
    mod = tvm.driver.form_irmodule(s, [X, Y], "main", None)
    optimize = tvm.transform.Sequential([tvm.tir.transform.VisualizeGraph("tir_vis_output.pdf")])
    mod = optimize(mod)
    print("tvm.lower:\n", result)



if __name__ == "__main__":
    test_texture()
