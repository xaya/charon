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

#ifndef CHARON_RPCSERVER_HPP
#define CHARON_RPCSERVER_HPP

#include <json/json.h>
#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/common/exception.h>

#include <unordered_set>
#include <string>

namespace charon
{

/**
 * Interface for an RPC server that is able to handle method calls and reply
 * to them by returning a JSON result value or throwing an exception in case
 * of errors.  An implementation of this class is used by the Charon server
 * to answer incoming IQ requests.
 */
class RpcServer
{

public:

  /**
   * Exception type to be thrown to signal errors that should be returned
   * for JSON-RPC.
   */
  using Error = jsonrpc::JsonRpcException;

  RpcServer () = default;
  virtual ~RpcServer () = default;

  /**
   * Answers a call to the given method with the given params.  Should return
   * the JSON result on success, and throw an instance of Error in case
   * an error occurs.
   */
  virtual Json::Value HandleMethod (const std::string& method,
                                    const Json::Value& params) = 0;

};

/**
 * Implementation of RpcServer that just forwards calls to a certain list
 * of "allowed" methods to another JSON-RPC endpoint, and answers all others
 * with "method does not exist".
 */
class ForwardingRpcServer : public RpcServer
{

private:

  /** The list of allowed methods.  */
  std::unordered_set<std::string> methods;

  /** HTTP connector for the backend target.  */
  jsonrpc::HttpClient http;

  /** The RPC client we forward calls to.  */
  jsonrpc::Client target;

public:

  /**
   * Constructs a new instance with no allowed methods (for now) and the
   * given RPC endpoint.
   */
  explicit ForwardingRpcServer (const std::string& url);

  /**
   * Allows the given method.
   */
  void
  AllowMethod (const std::string& method)
  {
    methods.insert (method);
  }

  Json::Value HandleMethod (const std::string& method,
                            const Json::Value& params) override;

};

} // namespace charon

#endif // CHARON_RPCSERVER_HPP
