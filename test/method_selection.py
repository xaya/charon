#!/usr/bin/env python2

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
Tests the method-selection args for the utility binaries.
"""

import testcase


class Methods:

  def echo (self, val):
    return val

  def doNotCall (self):
    raise AssertionError ("invalid forwarded call")


def test (t, methods, extraArgs):
  """
  Runs one round of testing, using the specified methods array
  and extraArgs.  The configuration should always result in the
  "echo" method being available and the "doNotCall" method not being
  available.
  """

  with t.runClient (methods=methods, extraArgs=extraArgs) as c, \
       t.runServer (backend, methods=methods, extraArgs=extraArgs):

    t.log.info ("Testing successful call forwarding...")
    t.assertEqual (c.rpc.echo ("bla"), "bla")

    t.log.info ("Unsupported method...")
    t.expectRpcError (".*METHOD_NOT_FOUND.*", c.rpc.doNotCall)


backend = Methods ()
with testcase.Fixture ([]) as t:

  t.mainLogger.info ("Just --methods...")
  test (t, ["echo"], [])

  t.mainLogger.info ("With --methods and --methods_exclude...")
  test (t, ["echo", "doNotCall"], ["--methods_exclude", "doNotCall"])
