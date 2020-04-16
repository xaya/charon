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
#include <glog/logging.h>

#include <json/json.h>

#include <algorithm>
#include <fstream>
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
DEFINE_string (methods_json_spec, "",
               "If specified, load methods from the given JSON file");

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

/**
 * Tries to parse methods from the libjson-rpc-cpp stubgenerator JSON
 * file.
 */
std::set<std::string>
GetJsonMethods ()
{
  if (FLAGS_methods_json_spec.empty ())
    return {};

  std::ifstream in(FLAGS_methods_json_spec);
  CHECK (in) << "Failed to open JSON spec file " << FLAGS_methods_json_spec;
  LOG (INFO) << "Loading JSON specification file " << FLAGS_methods_json_spec;

  Json::Value spec;
  in >> spec;
  CHECK (spec.isArray ()) << "Invalid JSON specification: " << spec;

  std::set<std::string> res;
  for (const auto& entry : spec)
    {
      CHECK (entry.isObject ()) << "Invalid spec entry: " << entry;
      const std::string name = entry["name"].asString ();
      if (entry.isMember ("returns"))
        {
          LOG (INFO) << "Using method " << name << " from JSON spec";
          res.insert (name);
        }
      else
        LOG (INFO) << "Ignoring notification " << name;
    }

  return res;
}

} // anonymous namespace

std::set<std::string>
GetSelectedMethods ()
{
  const auto methods = ParseCommaSeparated (FLAGS_methods);
  const auto fromJson = GetJsonMethods ();
  const auto excluded = ParseCommaSeparated (FLAGS_methods_exclude);

  std::set<std::string> allMethods;
  std::set_union (methods.begin (), methods.end (),
                  fromJson.begin (), fromJson.end (),
                  std::inserter (allMethods, allMethods.begin ()));

  std::set<std::string> diff;
  std::set_difference (allMethods.begin (), allMethods.end (),
                       excluded.begin (), excluded.end (),
                       std::inserter (diff, diff.begin ()));

  return diff;
}

} // namespace charon
