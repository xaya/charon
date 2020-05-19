#!/usr/bin/env python3

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
Tests how Charon handles XMPP server restarts.  Since this test needs
the XMPP server to be restarted in the middle, it has to be run manually
(and the restart needs to be done manually).
"""

import testcase

import waitforchange

import logging
import threading
import time

from jsonrpclib import ProtocolError


class Methods (waitforchange.Methods):

  def echo (self, val):
    return val


class UpdateSpammer (threading.Thread):
  """
  Thread that "spams" notification updates on our Charon server.
  It is run while the XMPP server is restarted, to make sure that Charon
  still handles the disconnect and reconnect gracefully even while things
  are going on as they might in a real-world situation.
  """

  def __init__ (self, backend):
    super ().__init__ ()
    self.log = logging.getLogger ("RpcSpammer")
    self.backend = backend
    self.lock = threading.Lock ()
    self.shouldStop = False

  def run (self):
    cnt = 0
    while True:
      with self.lock:
        if self.shouldStop:
          self.log.info ("Spammed %d state updates" % cnt)
          return

        self.backend.update ("ignored %d" % cnt)
        cnt += 1

      # Without holding the lock, sleep a little time to give other parts
      # time to process.
      time.sleep (0.01)

  def stop (self):
    with self.lock:
      self.shouldStop = True
    self.join ()

  def __enter__ (self):
    self.start ()
    return self

  def __exit__ (self, exc, value, traceback):
    self.stop ()


class RpcSpammer (threading.Thread):
  """
  Thread that "spams" RPC calls on the Charon client.
  """

  def __init__ (self, test, client):
    super ().__init__ ()
    self.test = test
    self.log = logging.getLogger ("RpcSpammer")
    self.client = client
    self.lock = threading.Lock ()
    self.shouldStop = False

  def run (self):
    cnt = 0
    rpc = self.client.createRpc ()
    while True:
      with self.lock:
        if self.shouldStop:
          self.log.info ("Spammed %d RPC calls" % cnt)
          return

        cur = "iteration %d" % cnt
        try:
          self.test.assertEqual (rpc.echo (cur), cur)
        except ProtocolError as exc:
          # While the server is disconnected, we will get timeout errors.
          pass
        cnt += 1

      # Without holding the lock, sleep a little time to give other parts
      # time to process.
      time.sleep (0.01)

  def stop (self):
    with self.lock:
      self.shouldStop = True
    self.join ()

  def __enter__ (self):
    self.start ()
    return self

  def __exit__ (self, exc, value, traceback):
    self.stop ()


def runTest (t, backend, c, nonce):
  """
  Runs basic tests that the Charon connection is up and working.  This sends
  a normal RPC and also triggers a waitforchange update.
  """

  waiterRpc = c.createRpc ()
  w = waitforchange.Waiter (waiterRpc.waitforchange, "")

  t.assertEqual (c.rpc.echo (nonce), nonce)

  w.assertRunning ()
  backend.update (nonce)
  t.assertEqual (w.wait (), nonce)


with Methods () as backend, \
     testcase.Fixture (["echo"], waitforchange=True) as t, \
     t.runClient () as c, \
     t.runServer (backend):

  t.mainLogger.info ("Initial Charon test...")
  runTest (t, backend, c, "foo")

  with UpdateSpammer (backend), \
       RpcSpammer (t, c):
    input ("Restart the XMPP server and press Enter to continue...\n")

  # Let the dust settle a bit after stopping the spammer threads.
  time.sleep (0.1)

  t.mainLogger.info ("Testing Charon after the restart...")
  runTest (t, backend, c, "bar")
