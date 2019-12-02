/*
    Charon - a transport system for GSP data
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "methods.hpp"

#include <gflags/gflags.h>

#include <iostream>
#include <sstream>

namespace charon
{

namespace
{

DEFINE_string (methods, "", "Comma-separated list of supported RPC methods");

} // anonymous namespace

std::set<std::string>
GetSelectedMethods ()
{
  if (FLAGS_methods.empty ())
    return {};

  std::set<std::string> res;
  std::istringstream methodsIn(FLAGS_methods);
  while (methodsIn.good ())
    {
      std::string method;
      std::getline (methodsIn, method, ',');
      res.insert (method);
    }

  return res;
}

} // namespace charon
