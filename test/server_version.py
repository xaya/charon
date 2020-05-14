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
Tests selection of the right server if two versions are provided.
"""

import testcase


class Methods:

  def __init__ (self, val):
    self.returnValue = val

  def test (self):
    return self.returnValue


if __name__ == "__main__":
  right = Methods ("right")
  left = Methods ("left")

  with testcase.Fixture (["test"]) as t, \
       t.runServer (right, extraArgs=["--backend_version", "right"]), \
       t.runServer (left, extraArgs=["--backend_version", "left"]), \
       t.runClient (extraArgs=["--backend_version", "right"]) as cr, \
       t.runClient (extraArgs=["--backend_version", "left"]) as cl:
    t.assertEqual (cr.rpc.test (), "right")
    t.assertEqual (cl.rpc.test (), "left")
