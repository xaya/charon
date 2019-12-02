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

#include "config.h"

#include "methods.hpp"

#include "client.hpp"

#include <json/json.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace
{

DEFINE_string (server_jid, "", "Bare or full JID for the server");

DEFINE_string (client_jid, "", "Bare or full JID for the client");
DEFINE_string (password, "", "XMPP password for the client JID");

DEFINE_int32 (port, 0, "Port for the local JSON-RPC server");

DEFINE_bool (detect_server, true,
             "Whether to run server detection immediately on start");

/**
 * Local JSON-RPC server that supports stopping via notification, but otherwise
 * forwards calls to a given list of methods to a Charon client.
 */
class LocalServer : public jsonrpc::AbstractServer<LocalServer>
{

private:

  /** Charon client to forward to.  */
  charon::Client& client;

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
    result = client.ForwardMethod (proc.GetProcedureName (), params);
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run a Charon client");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_server_jid.empty ())
    {
      std::cerr << "Error: --server_jid must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_client_jid.empty ())
    {
      std::cerr << "Error: --client_jid must be set" << std::endl;
      return EXIT_FAILURE;
    }

  if (FLAGS_port == 0)
    {
      std::cerr << "Error: --port must be set" << std::endl;
      return EXIT_FAILURE;
    }

  LOG (INFO) << "Using " << FLAGS_server_jid << " as server";
  charon::Client client(FLAGS_server_jid);

  LOG (INFO) << "Connecting client to XMPP as " << FLAGS_client_jid;
  client.Connect (FLAGS_client_jid, FLAGS_password, -1);

  if (FLAGS_detect_server)
    {
      const std::string srvResource = client.GetServerResource ();
      if (srvResource.empty ())
        {
          std::cerr << "Could not detect server";
          return EXIT_FAILURE;
        }
      LOG (INFO) << "Using server resource: " << srvResource;
    }
  else
    LOG (WARNING) << "Not detecting server for now";

  LOG (INFO) << "Listening for local RPCs on port " << FLAGS_port;
  jsonrpc::HttpServer httpServer(FLAGS_port);
  httpServer.BindLocalhost ();

  LocalServer rpcServer(httpServer, client);

  const auto methods = charon::GetSelectedMethods ();
  if (methods.empty ())
    LOG (WARNING) << "No methods are selected for forwarding";
  for (const auto& m : methods)
    {
      LOG (INFO) << "Forwarding method: " << m;
      rpcServer.AddMethod (m);
    }

  LOG (INFO) << "Starting RPC server...";
  rpcServer.Run ();

  return EXIT_SUCCESS;
}
