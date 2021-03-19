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

import heapq
import logging
import socket
import threading
import multiprocessing
from tvm.contrib.popen_pool import PopenPoolExecutor
import errno
import struct
import json
import tvm._ffi
from tvm.runtime import Object

from .._ffi.base import py_str
from . import base
from .base import RPC_TRACKER_MAGIC, TrackerCode

# import tvm.runtime.rpc
from . import _ffi_api

logger = logging.getLogger("RPCTracker")

# @tvm._ffi.register_object("Tracker")
# class Tracker(Object):

#     def __init__(self, addr, port, port_end, silent):
#         pass


#     # def __getitem__(self, idx):
#     #     return getitem_helper(self, _ffi_api.ArrayGetItem, len(self), idx)

#     # def __len__(self):
#     #     return _ffi_api.ArraySize(self)


@tvm._ffi.register_object("rpc.RPCTracker")
class Tracker(Object):
    def __init__(self, host, port=9190, port_end=9199, silent=False):
        self.__init_handle_by_constructor__(
                    _ffi_api.RPCTracker,
                    host, port, port_end, silent
                )
# class Tracker(object):
#     """Start RPC tracker on a seperate process.

#     Python implementation based on multi-processing.

#     Parameters
#     ----------
#     host : str
#         The host url of the server.

#     port : int
#         The TCP port to be bind to

#     port_end : int, optional
#         The end TCP port to search

#     silent: bool, optional
#         Whether run in silent mode
#     """

#     def __init__(self, host, port=9190, port_end=9199, silent=False):
#         if silent:
#             logger.setLevel(logging.WARN)

#         # sock = socket.socket(base.get_addr_family((host, port)), socket.SOCK_STREAM)
#         self.port = None
#         # self.stop_key = base.random_key("tracker")
#         # for my_port in range(port, port_end):
#         #     try:
#         #         sock.bind((host, my_port))
#         #         self.port = my_port
#         #         break
#         #     except socket.error as sock_err:
#         #         if sock_err.errno in [98, 48]:
#         #             continue
#         #         raise sock_err
#         # if not self.port:
#         #     raise ValueError("cannot bind to any port in [%d, %d)" % (port, port_end))
#         # logger.info("bind to %s:%d", host, self.port)
#         # sock.listen(1)
#         print("calling tracker start")
#         # self.tracker = tvm.runtime.RPCTracker(host, port, port_end, silent)
#         # self.port = RPCTrackerStart(host, port, port_end, silent)
#         # rc = _ffi_api.RPCTrackerStart(self, host, port, port_end, silent)
#         # print("called tracker start, port =", self.port)
#         # self.proc = multiprocessing.Process(target=_tracker_server, args=(sock, self.stop_key))
#         # self.pool = PopenPoolExecutor(max_workers=2)
#         # self.pool.submit(_tracker_server, sock, self.stop_key)
#         # self.proc.start()
#         self.host = host
#         # close the socket on this process
#         # sock.close()

#     def _stop_tracker(self):
#         print("tracker.py _stop_tracker()")
#         # _ffi_api.RPCTrackerStop(self)
#         # RPCTrackerStop()
#         # sock = socket.socket(base.get_addr_family((self.host, self.port)), socket.SOCK_STREAM)
#         # sock.connect((self.host, self.port))
#         # sock.sendall(struct.pack("<i", base.RPC_TRACKER_MAGIC))
#         # magic = struct.unpack("<i", base.recvall(sock, 4))[0]
#         # assert magic == base.RPC_TRACKER_MAGIC
#         # base.sendjson(sock, [TrackerCode.STOP, self.stop_key])
#         # assert base.recvjson(sock) == TrackerCode.SUCCESS
#         # sock.close()

#     def terminate(self):
#         """Terminate the server process"""
#         print("tracker.py terminate()")
#         # _ffi_api.RPCTrackerTerminate(self)
#         # RPCTrackerTerminate()
#         # if self.proc:
#         #     if self.proc.is_alive():
#         #         self._stop_tracker()
#         #         self.proc.join(1)
#         #     if self.proc.is_alive():
#         #         logger.info("Terminating Tracker Server...")
#         #         self.proc.terminate()
#         #     self.proc = None

#     def __del__(self):
#         self.terminate()
