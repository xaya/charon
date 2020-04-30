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

#include "config.h"

#include "methods.hpp"

#include "notifications.hpp"
#include "rpcserver.hpp"
#include "rpcwaiter.hpp"
#include "server.hpp"
#include "waiterthread.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace
{

DEFINE_string (backend_rpc_url, "",
               "URL at which the backend JSON-RPC interface is available");
DEFINE_string (backend_version, "",
               "A string identifying the version of the backend provided");

DEFINE_string (server_jid, "", "Bare or full JID for the server");
DEFINE_string (password, "", "XMPP password for the server JID");
DEFINE_int32 (priority, 0, "Priority for the XMPP connection");

DEFINE_string (pubsub_service, "", "The pubsub service to use on the server");

DEFINE_bool (waitforchange, false, "If true, enable waitforchange updates");
DEFINE_bool (waitforpendingchange, false,
             "If true, enable waitforpendingchange updates");

/**
 * Constructs a WaiterThread instance for the given notification type, using
 * the given RPC method as long-polling backend call.
 */
template <typename Notification>
  std::unique_ptr<charon::WaiterThread>
  NewWaiter (const std::string& method)
{
  auto n = std::make_unique<Notification> ();
  auto w = std::make_unique<charon::RpcUpdateWaiter> (
      FLAGS_backend_rpc_url, method, n->AlwaysBlockId ());

  return std::make_unique<charon::WaiterThread> (std::move (n), std::move (w));
}

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run a Charon server");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_backend_rpc_url.empty ())
    {
      std::cerr << "Error: --backend_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_server_jid.empty ())
    {
      std::cerr << "Error: --server_jid must be set" << std::endl;
      return EXIT_FAILURE;
    }

  charon::ForwardingRpcServer backend(FLAGS_backend_rpc_url);
  LOG (INFO)
      << "Forwarding calls to JSON-RPC server at " << FLAGS_backend_rpc_url;
  LOG (INFO) << "Reporting backend version " << FLAGS_backend_version;

  const auto methods = charon::GetSelectedMethods ();
  if (methods.empty ())
    LOG (WARNING) << "No methods are selected for forwarding";
  for (const auto& m : methods)
    {
      LOG (INFO) << "Allowing method: " << m;
      backend.AllowMethod (m);
    }

  LOG (INFO) << "Connecting server to XMPP as " << FLAGS_server_jid;
  charon::Server srv(FLAGS_backend_version, backend);
  srv.Connect (FLAGS_server_jid, FLAGS_password, FLAGS_priority);

  if (FLAGS_pubsub_service.empty ())
    {
      if (FLAGS_waitforchange || FLAGS_waitforpendingchange)
        {
          std::cerr
              << "Error: Notifications are enabled"
              << " but no pubsub service is defined"
              << std::endl;
          return EXIT_FAILURE;
        }
    }
  else
    srv.AddPubSub (FLAGS_pubsub_service);

  if (FLAGS_waitforchange)
    srv.AddNotification (NewWaiter<charon::StateChangeNotification> (
        "waitforchange"));
  if (FLAGS_waitforpendingchange)
    srv.AddNotification (NewWaiter<charon::PendingChangeNotification> (
        "waitforpendingchange"));

  while (true)
    std::this_thread::sleep_for (std::chrono::seconds (1));

  return EXIT_SUCCESS;
}
