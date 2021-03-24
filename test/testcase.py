#   Charon - a transport system for GSP data
#   Copyright (C) 2019-2021  Autonomous Worlds Ltd
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
Basic framework for integration tests of Charon.
"""

import charonbin
import rpcserver

from contextlib import contextmanager
import logging
import os
import os.path
import random
import re
import shutil
import sys

from jsonrpclib import ProtocolError


BASE_DIR = "/tmp"
DIR_PREFIX = "charontest"


class Fixture (object):
  """
  Context manager that sets up a basic Charon test environment.  It creates
  a temporary data directory for logs, sets up logging itself, and provides
  functionality to easily run a charon-client and/or charon-server based
  on the environment and predefined test accounts.
  """

  def __init__ (self, methods, waitforchange=False):
    self.methods = methods
    self.waitforchange = waitforchange

  def __enter__ (self):
    randomSuffix = "%08x" % random.getrandbits (32)
    self.basedir = os.path.join (BASE_DIR, "%s_%s" % (DIR_PREFIX, randomSuffix))
    shutil.rmtree (self.basedir, ignore_errors=True)
    os.mkdir (self.basedir)

    logfile = os.path.join (self.basedir, "test.log")
    logHandler = logging.FileHandler (logfile)
    logFmt = "%(asctime)s %(name)s (%(levelname)s): %(message)s"
    logHandler.setFormatter (logging.Formatter (logFmt))

    rootLogger = logging.getLogger ()
    rootLogger.setLevel (logging.INFO)
    rootLogger.addHandler (logHandler)

    self.log = logging.getLogger ("charontest")

    mainHandler = logging.StreamHandler (sys.stderr)
    mainHandler.setFormatter (logging.Formatter ("%(message)s"))

    self.mainLogger = logging.getLogger ("main")
    self.mainLogger.addHandler (mainHandler)
    self.mainLogger.info ("Base directory for integration test: %s"
                            % self.basedir)

    self.cfg = self.getServerConfig ()
    self.nextPort = random.randint (1024, 30000)
    self.log.info ("Using ports starting from %d" % self.nextPort)

    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = ".."
    self.bindir = os.path.join (top_builddir, "util")
    self.log.info ("Using binaries from %s" % self.bindir)

    return self

  def __exit__ (self, exc, value, traceback):
    if exc:
      self.mainLogger.error ("Test failed")
      self.log.info ("Not cleaning up base directory %s" % self.basedir)
    else:
      self.mainLogger.info ("Test succeeded")
      shutil.rmtree (self.basedir, ignore_errors=True)

    logging.shutdown ()

  def runClient (self, methods=None, extraArgs=[]):
    """
    Returns a context manager for running a Charon client in our environment.
    """

    binary = os.path.join (self.bindir, "charon-client")
    port = self.getNextPort ()

    if methods is None:
      methods = self.methods

    if self.waitforchange:
      extraArgs.append ("--waitforchange")

    acc = self.cfg["accounts"]
    res = charonbin.Client (self.basedir, binary, port, methods,
                            self.getAccountJid (acc[0]),
                            self.getAccountJid (acc[1]),
                            acc[1][1],
                            extraArgs)
    res.cafile = self.getRootCA ()
    return res

  @contextmanager
  def runServer (self, obj, methods=None, extraArgs=[]):
    """
    Returns a context manager for running a Charon server in our environment.
    It will start a fresh JSON-RPC server as backend, exposing all the members
    of obj as methods.
    """

    binary = os.path.join (self.bindir, "charon-server")
    port = self.getNextPort ()
    backend = "http://localhost:%d" % port

    if methods is None:
      methods = self.methods

    if self.waitforchange:
      extraArgs.append ("--waitforchange")

    acc = self.cfg["accounts"]
    srv = charonbin.Server (self.basedir, binary, methods, backend,
                            self.getAccountJid (acc[0]),
                            acc[0][1], self.cfg["pubsub"],
                            extraArgs)
    srv.cafile = self.getRootCA ()
    with rpcserver.Server (("localhost", port), obj), srv:
      yield

  def assertEqual (self, a, b):
    """
    Asserts that two values are equal, logging them if not.
    """

    if a == b:
      return

    self.log.error ("The value of:\n%s\n\nis not equal to:\n%s" % (a, b))
    raise AssertionError ("%s != %s" % (a, b))

  def expectRpcError (self, msgRegExp, method, *args, **kwargs):
    """
    Calls the method object with the given arguments, and expects that
    an RPC error is raised matching the message.
    """

    try:
      method (*args, **kwargs)
      self.log.error ("Expected RPC error with message %s" % msgRegExp)
      raise AssertionError ("expected RPC error was not raised")
    except ProtocolError as exc:
      self.log.info ("Caught expected RPC error: %s" % exc)
      m = exc.args[0][1]
      msgPattern = re.compile (msgRegExp)
      assert msgPattern.match (m)

  def getNextPort (self):
    """
    Returns the next port to use from our port range.
    """

    self.nextPort += 1
    return self.nextPort - 1

  def getAccountJid (self, acc):
    """
    Returns the JID of the given test account.
    """

    return "%s@%s" % (acc[0], self.cfg["server"])

  def getRootCA (self):
    """
    Returns the full path of the root CA file, based on our server
    config in self.cfg.
    """

    top_srcdir = os.getenv ("top_srcdir")
    if top_srcdir is None:
      top_srcdir = ".."

    return os.path.join (top_srcdir, "data", self.cfg["cafile"])

  def getServerConfig (self):
    """
    Returns the XMPP server, pubsub service and test accounts to use
    for the integration test.  This can either be the local environment
    or the production chat.xaya.io server with real XID mainnet accounts
    (see src/testutils.cpp for more details).

    By default, the local environment is used.  To run tests against the
    production server, set the CHARON_TEST_SERVER environment variable
    to "chat.xaya.io".
    """

    srv = os.getenv ("CHARON_TEST_SERVER", default="localhost")
    self.mainLogger.info ("Using %s as test server" % srv)

    if srv == "localhost":
      return {
        "server": "localhost",
        "pubsub": "pubsub.localhost",
        "cafile": "testenv.pem",
        "accounts": [
          ("xmpptest1", "password"),
          ("xmpptest2", "password"),
        ],
      }

    if srv == "chat.xaya.io":
      return {
        "server": "chat.xaya.io",
        "pubsub": "pubsub.chat.xaya.io",
        "cafile": "letsencrypt.pem",
        "accounts": [
          ("xmpptest1", "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiB"
                        "WL302N4kdLZYygJVBVx3vYsDNUx8xBbw27WA=="),
          ("xmpptest2", "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0Jxc"
                        "FfQmDbbF4C4OpqSlTpx8OG6xtFjCUMLh/AGA=="),
        ],
      }

    raise AssertionError ("invalid test server: %s" % srv)
