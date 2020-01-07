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

#include "rpcwaiter.hpp"

#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>

namespace charon
{

RpcUpdateWaiter::RpcUpdateWaiter (const std::string& url, const std::string& m,
                                  const Json::Value& alwaysBlock)
  : method(m), params(Json::arrayValue),
    http(url), target(http)
{
  params.append (alwaysBlock);
}

bool
RpcUpdateWaiter::WaitForUpdate (Json::Value& newState)
{
  VLOG (1) << "Calling backend waiter RPC " << method << "...";

  std::unique_lock<std::mutex> lock(mut, std::try_to_lock);
  CHECK (lock.owns_lock ()) << "Concurrent calls to WaitForUpdate";

  try
    {
      newState = target.CallMethod (method, params);
      return true;
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      LOG (WARNING) << "Long-polling call returned error: " << exc.what ();
      return false;
    }
}

void
RpcUpdateWaiter::ShortTimeout ()
{
  http.SetTimeout (50);
}

} // namespace charon
