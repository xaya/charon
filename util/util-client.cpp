/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "util-client.hpp"

#include "client.hpp"

#include <json/json.h>
#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace charon
{
namespace
{

/**
 * Local JSON-RPC server that supports stopping via notification, but otherwise
 * forwards calls to a given list of methods to a Charon client.
 */
class LocalServer : public jsonrpc::AbstractServer<LocalServer>
{

private:

  /** Charon client to forward to.  */
  charon::Client& client;

  /**
   * Waiter methods that are supported, with the corresponding notification
   * "type" string that should be passed to Client::WaitForChange.
   */
  std::unordered_map<std::string, std::string> notifications;

  /** Mutex for stopping.  */
  std::mutex mut;

  /** Condition variable to wake up the main thread when stopped.  */
  std::condition_variable cv;

  /** Set to true when we should stop running.  */
  bool shouldStop;

  /**
   * Handler method for the stop notification.
   */
  void
  stop (const Json::Value& params)
  {
    std::lock_guard<std::mutex> lock(mut);
    shouldStop = true;
    cv.notify_all ();
  }

  /**
   * Dummy handler method for the forwarded methods.  It will never be called
   * since those calls are intercepted in HandleMethodCall anyway.  We just
   * need something to pass for bindAndAddMethod.
   */
  void
  neverCalled (const Json::Value& params, Json::Value& result)
  {
    LOG (FATAL) << "method call not intercepted";
  }

public:

  explicit LocalServer (jsonrpc::AbstractServerConnector& conn,
                        charon::Client& c)
    : jsonrpc::AbstractServer<LocalServer>(conn, jsonrpc::JSONRPC_SERVER_V2),
      client(c)
  {
    jsonrpc::Procedure stopProc("stop", jsonrpc::PARAMS_BY_POSITION, nullptr);
    bindAndAddNotification (stopProc, &LocalServer::stop);
  }

  ~LocalServer ()
  {
    StopListening ();
  }

  LocalServer () = delete;
  LocalServer (const LocalServer&) = delete;
  void operator= (const LocalServer&) = delete;

  /**
   * Adds a method with the given name (and any parameters) to the list of
   * methods that will be forwarded.
   */
  void
  AddMethod (const std::string& method)
  {
    jsonrpc::Procedure proc(method, jsonrpc::PARAMS_BY_POSITION,
                            jsonrpc::JSON_OBJECT, nullptr);
    bindAndAddMethod (proc, &LocalServer::neverCalled);
  }

  /**
   * Enables a notification waiter method with the given RPC name.
   */
  void
  AddNotification (const std::string& method, const charon::NotificationType& n)
  {
    const auto res = notifications.emplace (method, n.GetType ());
    CHECK (res.second) << "Duplicate notification method: " << method;
    AddMethod (method);
  }

  /**
   * Listens on the server until the stop notification is sent.
   */
  void
  Run ()
  {
    shouldStop = false;
    StartListening ();

    {
      std::unique_lock<std::mutex> lock(mut);
      while (!shouldStop)
        cv.wait (lock);
    }

    StopListening ();
  }

  void
  HandleMethodCall (jsonrpc::Procedure& proc, const Json::Value& params,
                    Json::Value& result) override
  {
    const std::string method = proc.GetProcedureName ();

    const auto mit = notifications.find (method);
    if (mit != notifications.end ())
      {
        if (!params.isArray () || params.size () != 1)
          throw jsonrpc::JsonRpcException (
              jsonrpc::Errors::ERROR_RPC_INVALID_PARAMS,
              "wait method expects a single positional argument");

        result = client.WaitForChange (mit->second, params[0]);
        return;
      }

    result = client.ForwardMethod (method, params);
  }

};

} // anonymous namespace

/**
 * Internal implementation details for the UtilClient.
 */
class UtilClient::Impl
{

private:

  /** The client JID (used for logging).  */
  const std::string clientJid;

  /** The underlying Charon client.  */
  charon::Client client;

  /** The HTTP server connector for the local RPC server.  */
  jsonrpc::HttpServer httpServer;

  /** The local RPC server.  */
  LocalServer rpcServer;

  friend class UtilClient;

public:

  explicit Impl (const std::string& serverJid,
                 const std::string& backendVersion,
                 const std::string& cJid, const std::string& password,
                 const int port)
    : clientJid(cJid),
      client(serverJid, backendVersion, clientJid, password),
      httpServer(port),
      rpcServer(httpServer, client)
  {}

};

UtilClient::UtilClient (const std::string& serverJid,
                        const std::string& backendVersion,
                        const std::string& clientJid,
                        const std::string& password,
                        const int port)
{
  CHECK (!serverJid.empty ());
  CHECK (!clientJid.empty ());
  CHECK_GT (port, 0);

  LOG (INFO) << "Using " << serverJid << " as server";
  LOG (INFO) << "Requiring backend version " << backendVersion;
  LOG (INFO) << "Listening for local RPCs on port " << port;

  impl = std::make_unique<Impl> (serverJid, backendVersion,
                                 clientJid, password,
                                 port);
}

UtilClient::~UtilClient () = default;

void
UtilClient::AddMethods (const std::set<std::string>& methods)
{
  if (methods.empty ())
    LOG (WARNING) << "No methods are selected for forwarding";
  for (const auto& m : methods)
    {
      LOG (INFO) << "Forwarding method: " << m;
      impl->rpcServer.AddMethod (m);
    }
}

void
UtilClient::EnableWaitForChange ()
{
  auto n = std::make_unique<charon::StateChangeNotification> ();
  impl->rpcServer.AddNotification ("waitforchange", *n);
  impl->client.AddNotification (std::move (n));
}

void
UtilClient::EnableWaitForPendingChange ()
{
  auto n = std::make_unique<charon::PendingChangeNotification> ();
  impl->rpcServer.AddNotification ("waitforpendingchange", *n);
  impl->client.AddNotification (std::move (n));
}

void
UtilClient::Run (const bool detectServer)
{
  LOG (INFO) << "Connecting client to XMPP as " << impl->clientJid;
  impl->client.Connect ();

  if (detectServer)
    {
      const std::string srvResource = impl->client.GetServerResource ();
      if (srvResource.empty ())
        throw std::runtime_error ("Could not detect server");
      LOG (INFO) << "Using server resource: " << srvResource;
    }
  else
    LOG (WARNING) << "Not detecting server for now";

  LOG (INFO) << "Starting RPC server...";
  impl->rpcServer.Run ();
}

} // namespace charon
