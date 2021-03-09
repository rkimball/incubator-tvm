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

import multiprocessing
import tvm
from test_autotvm_common import DummyRunner, get_sample_task
from tvm import autotvm

multiprocessing.set_start_method("spawn", force=True)


def do_test():
    task, _ = get_sample_task()

    measure_option = autotvm.measure_option(builder=autotvm.LocalBuilder(), runner=DummyRunner())

    tuner = autotvm.tuner.RandomTuner(task)
    tuner.tune(n_trial=10, measure_option=measure_option)


# if __name__ == '__main__':
do_test()
