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
"""Local based implementation of the executor using multiprocessing"""

from tvm.contrib.popen_pool import PopenPoolExecutor
from . import executor


class LocalExecutor(executor.Executor):
    """Local executor that runs workers on the same machine with multiprocessing.

    Parameters
    ----------
    timeout: float, optional
        timeout of a job. If time is out. A TimeoutError will be returned (not raised)
    """

    def __init__(self, timeout=None):
        self.timeout = timeout or executor.Executor.DEFAULT_TIMEOUT
        self.pool = PopenPoolExecutor(max_workers=2, timeout=self.timeout)

    def submit(self, func, *args, **kwargs):
        return self.pool.submit(func, *args, **kwargs)
