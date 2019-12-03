#!/usr/bin/env python2

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
Tests basic (non-blocking) RPC calls through Charon.
"""

import testcase


class Methods:

  methods = ["echo", "error"]

  def echo (self, val):
    return val

  def error (self, msg):
    raise RuntimeError (msg)

  def doNotCall (self):
    raise AssertionError ("invalid forwarded call")


backend = Methods ()
with testcase.Fixture (backend.methods) as t, \
     t.runClient () as c:

  with t.runServer (backend):
    t.mainLogger.info ("Testing successful call forwarding...")
    t.assertEqual (c.rpc.echo ("bla"), "bla")
    t.expectRpcError (".*my error.*", c.rpc.error, "my error")

    t.mainLogger.info ("Invalid method call...")
    t.expectRpcError (".*METHOD_NOT_FOUND.*", c.rpc.doNotCall)

  t.mainLogger.info ("Testing server reselection...")
  with t.runServer (backend):
    t.assertEqual (c.rpc.echo ("success"), "success")
