#!/usr/bin/env python3

#   Charon - a transport system for GSP data
#   Copyright (C) 2020  Autonomous Worlds Ltd
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
Tests relaying of notifications (waitforchange) through the Charon
binary utils.
"""

import testcase

import threading
import time


class Methods:
  """
  The RPC backend, which supports waitforchange calls.  The caller will
  be blocked until we explicitly specify a new state.

  This is also a context manager.  On exit, we stop waiting and wake up
  all possibly still waiting threads.
  """

  def __init__ (self):
    self.cv = threading.Condition ()
    self.state = ""
    self.waitEnabled = False

  def __enter__ (self):
    with self.cv:
      self.waitEnabled = True
    return self

  def __exit__ (self, exc, value, traceback):
    with self.cv:
      self.waitEnabled = False
      self.cv.notifyAll ()

  def waitforchange (self, known):
    assert known == ""
    with self.cv:
      if self.waitEnabled:
        self.cv.wait ()
      return self.state

  def update (self, newState):
    with self.cv:
      self.state = newState
      self.cv.notifyAll ()


class Waiter:
  """
  Async call to a waitforchange method, which allows waiting for its result
  and checking that the result is as expected.
  """

  def __init__ (self, fcn, *args):
    def task ():
      self.result = fcn (*args)

    self.thread = threading.Thread (target=task)
    self.thread.start ()

  def assertRunning (self):
    time.sleep (0.1)
    assert self.thread.isAlive ()

  def wait (self):
    self.thread.join ()
    return self.result


if __name__ == "__main__":
  with Methods () as backend, \
       testcase.Fixture ([], waitforchange=True) as t, \
       t.runClient () as c:

    with t.runServer (backend):
      t.mainLogger.info ("Testing waitforchange update...")

      w = Waiter (c.rpc.waitforchange, "")

      # Give the client time to finish selecting the server.  This has only
      # been triggered by the first call above.
      time.sleep (1)

      w.assertRunning ()
      backend.update ("first")
      t.assertEqual (w.wait (), "first")

      w = Waiter (c.rpc.waitforchange, "other")
      t.assertEqual (w.wait (), "first")

      w = Waiter (c.rpc.waitforchange, "")
      w.assertRunning ()
      backend.update ("second")
      t.assertEqual (w.wait (), "second")

    t.mainLogger.info ("Testing server reselection...")
    with t.runServer (backend):
      w = Waiter (c.rpc.waitforchange, "")
      time.sleep (1)

      w.assertRunning ()
      backend.update ("third")
      t.assertEqual (w.wait (), "third")
