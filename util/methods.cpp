/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>

namespace charon
{

namespace
{

DEFINE_string (methods, "", "Comma-separated list of supported RPC methods");
DEFINE_string (methods_exclude, "",
               "Comma-separated list of methods to exclude");

/**
 * Parses a comma-separated string into pieces.
 */
std::set<std::string>
ParseCommaSeparated (const std::string& lst)
{
  if (lst.empty ())
    return {};

  std::set<std::string> res;
  std::istringstream in(lst);
  while (in.good ())
    {
      std::string cur;
      std::getline (in, cur, ',');
      res.insert (cur);
    }

  return res;
}

} // anonymous namespace

std::set<std::string>
GetSelectedMethods ()
{
  const auto methods = ParseCommaSeparated (FLAGS_methods);
  const auto excluded = ParseCommaSeparated (FLAGS_methods_exclude);

  std::set<std::string> diff;
  std::set_difference (methods.begin (), methods.end (),
                       excluded.begin (), excluded.end (),
                       std::inserter (diff, diff.begin ()));

  return diff;
}

} // namespace charon
