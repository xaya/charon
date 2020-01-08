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

#ifndef CHARON_RPCWAITER_HPP
#define CHARON_RPCWAITER_HPP

#include "waiterthread.hpp"

#include <json/json.h>
#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>

#include <mutex>
#include <string>

namespace charon
{

/**
 * Implementation of the UpdateWaiter interface that simply calls a long-polling
 * RPC method on a given server.  Note that each instance only supports one
 * concurrent WaitForUpdate call.
 */
class RpcUpdateWaiter : public UpdateWaiter
{

private:

  /** The name of the method to call.  */
  const std::string method;

  /** The argument list to pass to the method.  */
  Json::Value params;

  /**
   * Mutex used to synchronise access to our RPC client.  Note that we use
   * this mostly to enforce that no concurrent calls are made, rather than
   * actually allow them.
   */
  std::mutex mut;

  /** HTTP connector for the backend target.  */
  jsonrpc::HttpClient http;

  /** The RPC client we forward calls to.  */
  jsonrpc::Client target;

  /**
   * Sets a short timeout on the HTTP client, so that we can test what
   * happens if a timeout occurs in unit testing.
   */
  void ShortTimeout ();

  friend class RpcUpdateWaiterTests;

public:

  /**
   * Constructs a new instance with the given RPC backend and method name.
   * This must specify an "always block" argument that will be passed as
   * only positional argument to each call.
   */
  explicit RpcUpdateWaiter (const std::string& url, const std::string& m,
                            const Json::Value& alwaysBlock);

  bool WaitForUpdate (Json::Value& newState) override;

};

} // namespace charon

#endif // CHARON_RPCWAITER_HPP
