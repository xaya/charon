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

#include "rpcserver.hpp"

#include <jsonrpccpp/common/errors.h>

#include <glog/logging.h>

#include <sstream>

namespace charon
{

ForwardingRpcServer::ForwardingRpcServer (const std::string& url)
  : http(url), target(http)
{}

Json::Value
ForwardingRpcServer::HandleMethod (const std::string& method,
                                   const Json::Value& params)
{
  VLOG (1) << "Attempted forwarding call to " << method;
  VLOG (2) << "Parameters: " << params;

  if (methods.count (method) == 0)
    {
      std::ostringstream msg;
      msg << "method not found or not allowed: " << method;
      throw Error (jsonrpc::Errors::ERROR_RPC_METHOD_NOT_FOUND, msg.str ());
    }

  return target.CallMethod (method, params);
}

} // namespace charon
