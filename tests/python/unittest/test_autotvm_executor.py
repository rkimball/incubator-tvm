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
"""Test local executor"""
import pytest
import time

from tvm.autotvm.measure import LocalExecutor, executor
from tvm.testing import timeout_job, fast, slow


def test_local_measure_async():
    ex = LocalExecutor()
    f1 = ex.submit(slow, 9999999)
    f2 = ex.submit(fast, 9999999)
    t1 = 0
    t2 = 0
    while True:
        if t1 == 0 and f1.done():
            t1 = time.time()
        if t2 == 0 and f2.done():
            t2 = time.time()
        if t1 != 0 and t2 != 0:
            break
    assert t2 < t1, "Expected fast async job to finish first!"
    assert f1.result() == f2.result()


def test_timeout():
    timeout = 0.5

    ex = LocalExecutor(timeout=timeout)

    f1 = ex.submit(timeout_job, timeout)
    while not f1.done():
        pass
    with pytest.raises(TimeoutError):
        f1.result()


if __name__ == "__main__":
    test_local_measure_async()
    test_timeout()
