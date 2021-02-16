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

import logging
import multiprocessing
import time
import tvm
from tvm import te
from test_autotvm_common import DummyRunner, bad_matmul, get_sample_task
from tvm import autotvm
from tvm.autotvm.measure.measure import MeasureErrorNo, MeasureResult

multiprocessing.set_start_method("spawn", force=True)

task, _ = get_sample_task()

measure_option = autotvm.measure_option(builder=autotvm.LocalBuilder(), runner=DummyRunner())

logging.info("%s", task.config_space)

for tuner_class in [
    autotvm.tuner.RandomTuner,
    autotvm.tuner.GridSearchTuner,
    autotvm.tuner.GATuner,
    autotvm.tuner.XGBTuner,
]:
    tuner = tuner_class(task)
    tuner.tune(n_trial=10, measure_option=measure_option)
    # assert tuner.best_flops > 1
