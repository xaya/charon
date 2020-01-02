/*
    Charon - a transport system for GSP data
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "notifications.hpp"

#include <glog/logging.h>

namespace charon
{

Json::Value
StateChangeNotification::ExtractStateId (const Json::Value& fullState) const
{
  CHECK (fullState.isString ());
  return fullState;
}

Json::Value
PendingChangeNotification::ExtractStateId (const Json::Value& fullState) const
{
  CHECK (fullState.isObject ());

  const auto& version = fullState["version"];
  CHECK (version.isUInt ());

  return version;
}

} // namespace charon
