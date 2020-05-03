#   Charon - a transport system for GSP data
#   Copyright (C) 2019-2020  Autonomous Worlds Ltd
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""
Logic for implementing test RPC servers.
"""

from jsonrpclib.SimpleJSONRPCServer import SimpleJSONRPCServer

import logging
import socketserver
import threading


# Polling interval for shutdown in serve_forever.
SHUTDOWN_POLLING_INTERVAL = 0.1


class Server (socketserver.ThreadingMixIn, SimpleJSONRPCServer, object):
  """
  JSON-RPC server for use in tests.  It forwards calls to the members of
  a given object.  This is also a context manager, which takes care of starting
  and stopping the server in a background thread.
  """

  def __init__ (self, addr, obj):
    """
    Constructs the server instance, which will listen at the given address
    ((host, port) tuple) and call methods from obj.
    """

    super (Server, self).__init__ (addr, logRequests=False)

    self.log = logging.getLogger ("rpcserver")
    self.log.info ("Setting up test RPC server at %s:%d..." % addr)

    self.methods = []
    for nm in dir (obj):
      fcn = getattr (obj, nm)
      if not callable (fcn):
        continue
      self.log.info ("RPC method: %s" % nm)
      self.register_function (fcn, nm)

    self.loop = None

  def __enter__ (self):
    assert self.loop is None
    self.log.info ("Starting RPC serving loop...")
    def run ():
      self.serve_forever (SHUTDOWN_POLLING_INTERVAL)
    self.loop = threading.Thread (target=run)
    self.loop.start ()
    return self

  def __exit__ (self, exc, value, traceback):
    assert self.loop is not None
    self.log.info ("Stopping RPC serving loop...")
    self.shutdown ()
    self.loop.join ()
    self.loop = None
