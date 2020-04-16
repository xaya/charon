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

# Accounts on chat.xaya.io (XID mainnet) for use with testing.
# See src/testutils.cpp for more details.
TEST_ACCOUNTS = [
  ("xmpptest1", "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4k"
                "dLZYygJVBVx3vYsDNUx8xBbw27WA=="),
  ("xmpptest2", "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF"
                "4C4OpqSlTpx8OG6xtFjCUMLh/AGA=="),
]
XMPP_SERVER = "chat.xaya.io"
PUBSUB = "pubsub.chat.xaya.io"


class Fixture (object):
  """
  Context manager that sets up a basic Charon test environment.  It creates
  a temporary data directory for logs, sets up logging itself, and provides
  functionality to easily run a charon-client and/or charon-server based
  on the environment and predefined test accounts.
  """

  def __init__ (self, methods):
    self.methods = methods

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

    return charonbin.Client (self.basedir, binary, port, methods,
                             self.getAccountJid (TEST_ACCOUNTS[0]),
                             self.getAccountJid (TEST_ACCOUNTS[1]),
                             TEST_ACCOUNTS[1][1],
                             extraArgs)

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

    with rpcserver.Server (("localhost", port), obj), \
         charonbin.Server (self.basedir, binary, methods, backend,
                           self.getAccountJid (TEST_ACCOUNTS[0]),
                           TEST_ACCOUNTS[0][1], PUBSUB,
                           extraArgs):
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

    return "%s@%s" % (acc[0], XMPP_SERVER)
