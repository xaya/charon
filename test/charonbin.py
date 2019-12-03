#   Charon - a transport system for GSP data
#   Copyright (C) 2019  Autonomous Worlds Ltd
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
Python code to run charon-client and charon-server processes in a controlled
fashion for integration testing.
"""

import jsonrpclib
import logging
import os
import subprocess
import time


# Sleep time before we consider charon-server and charon-client connected.
# Especially since charon-server does not have an RPC interface by itself,
# it would not be easy to properly implement a "wait until started" function.
STARTUP_SLEEP = 1


class Client ():
  """
  A context manager that runs a charon-client process under the hood while
  the context is active.  It tries to clean the process up afterwards.
  """

  def __init__ (self, basedir, binary, port, methods,
                serverJid, clientJid, password):
    """
    Constructs the manager, which will run the charon-client binary located
    at the given path, setting its log directory and JSON-RPC port as provided.
    """

    self.log = logging.getLogger ("charon-client")

    self.basedir = basedir
    self.binary = binary
    self.port = port
    self.methods = methods
    self.serverJid = serverJid
    self.clientJid = clientJid
    self.password = password

    self.rpcurl = "http://localhost:%d" % port
    self.proc = None

  def __enter__ (self):
    assert self.proc is None

    self.log.info ("Starting new charon-client process...")

    args = [self.binary]
    args.append ("--nodetect_server")
    args.extend (["--port", "%d" % self.port])
    args.extend (["--client_jid", self.clientJid])
    args.extend (["--server_jid", self.serverJid])
    args.extend (["--password", self.password])
    args.extend (["--methods", ",".join (self.methods)])

    envVars = dict (os.environ)
    envVars["GLOG_log_dir"] = self.basedir

    self.proc = subprocess.Popen (args, env=envVars)
    self.rpc = self.createRpc ()

    time.sleep (STARTUP_SLEEP)

    return self

  def __exit__ (self, exc, value, traceback):
    assert self.proc is not None

    self.log.info ("Stopping charon-client process...")
    self.rpc._notify.stop ()
    self.proc.wait ()
    self.proc = None

  def createRpc (self):
    """
    Returns a fresh JSON-RPC client connection to the process' local server.
    """

    return jsonrpclib.Server (self.rpcurl)


class Server ():
  """
  A context manager that runs a charon-server process under the hood while
  the context is active.  It tries to clean the process up afterwards.
  """

  def __init__ (self, basedir, binary, methods, backendRpcUrl,
                serverJid, password):
    """
    Constructs the manager, which will run the charon-server binary located
    at the given path, setting its log directory and other variables as given.
    """

    self.log = logging.getLogger ("charon-server")

    self.basedir = basedir
    self.binary = binary
    self.methods = methods
    self.backendRpcUrl = backendRpcUrl
    self.serverJid = serverJid
    self.password = password

    self.proc = None

  def __enter__ (self):
    assert self.proc is None

    self.log.info ("Starting new charon-server process...")

    args = [self.binary]
    args.extend (["--backend_rpc_url", self.backendRpcUrl])
    args.extend (["--server_jid", self.serverJid])
    args.extend (["--password", self.password])
    args.extend (["--methods", ",".join (self.methods)])

    envVars = dict (os.environ)
    envVars["GLOG_log_dir"] = self.basedir

    self.proc = subprocess.Popen (args, env=envVars)

    time.sleep (STARTUP_SLEEP)

    return self

  def __exit__ (self, exc, value, traceback):
    assert self.proc is not None

    self.log.info ("Stopping charon-server process...")
    self.proc.terminate ()
    self.proc.wait ()
    self.proc = None
