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
"""RPC Tracker, tracks and distributes the TVM RPC resources.

This folder implemements the tracker server logic.

Note
----
Tracker is a TCP based rest api with the following protocol:
- Initial handshake to the peer
  - RPC_TRACKER_MAGIC
- Normal message: [size(int32), json-data]
- Each message is initiated by the client, and the tracker replies with a json.

List of available APIs:

- PING: check if tracker is alive
  - input: [TrackerCode.PING]
  - return: TrackerCode.SUCCESS
- PUT: report resource to tracker
  - input: [TrackerCode.PUT, [port, match-key]]
  - return: TrackerCode.SUCCESS
  - note: match-key is a randomly generated identify the resource during connection.
- REQUEST: request a new resource from tracker
  - input: [TrackerCode.REQUEST, [key, user, priority]]
  - return: [TrackerCode.SUCCESS, [url, port, match-key]]
"""
# pylint: disable=invalid-name

import tvm._ffi
from tvm.runtime import Object
from . import _ffi_api

@tvm._ffi.register_object("rpc.RPCTracker")
class Tracker(Object):
    def __init__(self, host, port=9190, port_end=9199, silent=False):
        self.__init_handle_by_constructor__(_ffi_api.RPCTracker, host, port, port_end, silent)
