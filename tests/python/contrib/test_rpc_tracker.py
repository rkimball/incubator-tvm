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
import tvm
from tvm import te
import logging
import numpy as np
import time
import multiprocessing
from tvm import rpc


def check_server_drop():
    """test when server drops"""
    try:
        from tvm.rpc import tracker, proxy, base
        from tvm.rpc.base import TrackerCode

        @tvm.register_func("rpc.test2.addone")
        def addone(x):
            return x + 1

        def _put(tclient, value):
            print("********************************************************************")
            base.sendjson(tclient._sock, value)
            rc = base.recvjson(tclient._sock)
            print("rc", rc)

        tserver = tracker.Tracker("localhost", 8888)
        tproxy = proxy.Proxy("localhost", 8881, tracker_addr=("localhost", tserver.port))
        tclient = rpc.connect_tracker("localhost", tserver.port)

        server0 = rpc.Server(
            "localhost", port=9099, tracker_addr=("localhost", tserver.port), key="abc"
        )
        server1 = rpc.Server(
            "localhost", port=9099, tracker_addr=("localhost", tserver.port), key="xyz"
        )
        server2 = rpc.Server("localhost", tproxy.port, is_proxy=True, key="xyz")
        server3 = rpc.Server("localhost", tproxy.port, is_proxy=True, key="xyz1")

        # Fault tolerence to un-handled requested value
        _put(tclient, [TrackerCode.REQUEST, "abc", "", 1])
        _put(tclient, [TrackerCode.REQUEST, "xyz1", "", 1])

        # Fault tolerence to stale worker value
        _put(tclient, [TrackerCode.PUT, "xyz", (server1.port, "abc")])
        _put(tclient, [TrackerCode.PUT, "xyz", (server1.port, "abcxxx")])
        _put(tclient, [TrackerCode.PUT, "xyz", (tproxy.port, "abcxxx11")])

        # Fault tolerence server timeout
        def check_timeout(timeout, sleeptime):
            def myfunc(remote):
                time.sleep(sleeptime)
                f1 = remote.get_function("rpc.test2.addone")
                assert f1(10) == 11

            try:
                tclient.request_and_run("xyz", myfunc, session_timeout=timeout)
            except RuntimeError:
                pass
            print(tclient.text_summary())
            try:
                remote = tclient.request("xyz", priority=0, session_timeout=timeout)
                remote2 = tclient.request("xyz", session_timeout=timeout)
                time.sleep(sleeptime)
                f1 = remote.get_function("rpc.test2.addone")
                assert f1(10) == 11
                f1 = remote2.get_function("rpc.test2.addone")
                assert f1(10) == 11

            except tvm.error.TVMError as e:
                pass
            remote3 = tclient.request("abc")
            f1 = remote3.get_function("rpc.test2.addone")
            remote3 = tclient.request("xyz1")
            f1 = remote3.get_function("rpc.test2.addone")
            assert f1(10) == 11

        print("\n\n\n\n************* 1\n\n\n")
        check_timeout(0.01, 0.1)
        print("\n\n\n\n************* 2\n\n\n")
        check_timeout(2, 0)
        print("\n\n\n\n************* 3\n\n\n")
        tserver.terminate()
        print("\n\n\n\n************* 4\n\n\n")
        server0.terminate()
        print("\n\n\n\n************* 5\n\n\n")
        server1.terminate()
        print("\n\n\n\n************* 6\n\n\n")
        server2.terminate()
        print("\n\n\n\n************* 7\n\n\n")
        server3.terminate()
        print("\n\n\n\n************* 8\n\n\n")
        tproxy.terminate()
        print("\n\n\n\n************* 9\n\n\n")
    except ImportError:
        print("Skip because tornado is not available")


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    check_server_drop()
